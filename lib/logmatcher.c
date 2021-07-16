/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "logmatcher.h"
#include "messages.h"
#include "cfg.h"
#include "str-utils.h"
#include "scratch-buffers.h"
#include "compat/string.h"
#include "compat/pcre.h"

static gboolean
_shall_set_values_indirectly(NVHandle value_handle)
{
  return value_handle != LM_V_NONE &&
         !log_msg_is_handle_macro(value_handle) &&
         !log_msg_is_handle_match(value_handle);
}

static void
log_matcher_store_pattern(LogMatcher *self, const gchar *pattern)
{
  g_free(self->pattern);
  self->pattern = g_strdup(pattern);
}

static void
log_matcher_free_method(LogMatcher *self)
{
  g_free(self->pattern);
}

static void
log_matcher_init(LogMatcher *self, const LogMatcherOptions *options)
{
  self->ref_cnt = 1;
  self->flags = options->flags;
  self->free_fn = log_matcher_free_method;
}

typedef struct _LogMatcherString
{
  LogMatcher super;
  gint pattern_len;
} LogMatcherString;

static gboolean
log_matcher_string_compile(LogMatcher *s, const gchar *pattern, GError **error)
{
  LogMatcherString *self = (LogMatcherString *) s;

  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
  log_matcher_store_pattern(s, pattern);

  self->pattern_len = strlen(pattern);
  return TRUE;
}

static const gchar *
log_matcher_string_match_string(LogMatcherString *self, const gchar *value, gsize value_len)
{
  const gchar *result = NULL;
  gboolean match = FALSE;
  const gchar *pattern = self->super.pattern;

  if (self->pattern_len > value_len)
    return NULL;
  if (G_LIKELY((self->super.flags & (LMF_SUBSTRING + LMF_PREFIX)) == 0))
    {
      if (self->super.flags & LMF_ICASE)
        match = strncasecmp(value, pattern, value_len) == 0;
      else
        match = strncmp(value, pattern, value_len) == 0;
    }
  else if (self->super.flags & LMF_PREFIX)
    {
      if (self->super.flags & LMF_ICASE)
        match = strncasecmp(value, pattern, MIN(value_len, self->pattern_len)) == 0;
      else
        match = strncmp(value, pattern, MIN(value_len, self->pattern_len)) == 0;
    }
  else if (self->super.flags & LMF_SUBSTRING)
    {
      if (self->super.flags & LMF_ICASE)
        {
          gchar *buf;
          gchar *res;

          APPEND_ZERO(buf, value, value_len);
          res = strcasestr(buf, pattern);
          if (res)
            result = value + (res - buf);
        }
      else
        {
          result = g_strstr_len(value, value_len, pattern);
        }
    }

  if (match && !result)
    result = value;
  return result;
}

static gboolean
log_matcher_string_match(LogMatcher *s, LogMessage *msg, gint value_handle, const gchar *value, gssize value_len)
{
  LogMatcherString *self = (LogMatcherString *) s;

  return log_matcher_string_match_string(self, value, value_len) != NULL;
}

static gchar *
log_matcher_string_replace(LogMatcher *s, LogMessage *msg, gint value_handle, const gchar *value, gssize value_len,
                           LogTemplate *replacement, gssize *new_length)
{
  LogMatcherString *self = (LogMatcherString *) s;
  GString *new_value = NULL;
  gsize current_ofs = 0;
  gboolean first_round = TRUE;

  if (value_len < 0)
    value_len = strlen(value);

  const gchar *match;

  do
    {
      if (current_ofs == value_len)
        break;

      match = log_matcher_string_match_string(self, value + current_ofs, value_len - current_ofs);

      if (match != NULL)
        {
          /* start_ofs & end_ofs are relative to the original string */
          gsize start_ofs = match - value;
          gsize end_ofs = start_ofs + self->pattern_len;

          if (start_ofs == end_ofs && !first_round)
            {
              start_ofs++;
              end_ofs++;
            }

          if ((s->flags & LMF_STORE_MATCHES))
            log_msg_clear_matches(msg);

          if (!new_value)
            new_value = g_string_sized_new(value_len);

          g_string_append_len(new_value, value + current_ofs, start_ofs - current_ofs);
          log_template_append_format(replacement, msg, &DEFAULT_TEMPLATE_EVAL_OPTIONS, new_value);
          current_ofs = end_ofs;

          if ((self->super.flags & LMF_GLOBAL) == 0)
            {
              g_string_append_len(new_value, value + current_ofs, value_len - current_ofs);
              break;
            }
        }
      else
        {
          if (new_value)
            {
              /* no more matches, append the end of the string */
              g_string_append_len(new_value, value + current_ofs, value_len - current_ofs);
            }
        }
      first_round = FALSE;
    }
  while (match && (self->super.flags & LMF_GLOBAL));

  if (new_value)
    {
      if (new_length)
        *new_length = new_value->len;
      return g_string_free(new_value, FALSE);
    }
  return NULL;
}

LogMatcher *
log_matcher_string_new(const LogMatcherOptions *options)
{
  LogMatcherString *self = g_new0(LogMatcherString, 1);

  log_matcher_init(&self->super, options);
  self->super.compile = log_matcher_string_compile;
  self->super.match = log_matcher_string_match;
  self->super.replace = log_matcher_string_replace;

  return &self->super;
}

typedef struct _LogMatcherGlob
{
  LogMatcher super;
  GPatternSpec *pattern;
} LogMatcherGlob;

static gboolean
log_matcher_glob_compile(LogMatcher *s, const gchar *pattern, GError **error)
{
  LogMatcherGlob *self = (LogMatcherGlob *)s;

  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
  log_matcher_store_pattern(s, pattern);

  self->pattern = g_pattern_spec_new(pattern);
  return TRUE;
}

/* GPattern only works with utf8 strings, if the input is not utf8, we risk
 * a crash
 */
static gboolean
log_matcher_glob_match(LogMatcher *s, LogMessage *msg, gint value_handle, const gchar *value, gssize value_len)
{
  LogMatcherGlob *self =  (LogMatcherGlob *) s;

  if (G_LIKELY((msg->flags & LF_UTF8) || g_utf8_validate(value, value_len, NULL)))
    {
      static gboolean warned = FALSE;
      gchar *buf;

      if (G_UNLIKELY(!warned && (msg->flags & LF_UTF8) == 0))
        {
          msg_warning("Input is valid utf8, but the log message is not tagged as such, this performs worse than enabling validate-utf8 flag on input",
                      evt_tag_printf("value", "%.*s", (gint) value_len, value));
          warned = TRUE;
        }
      APPEND_ZERO(buf, value, value_len);
      return g_pattern_match(self->pattern, value_len, buf, NULL);
    }
  else
    {
      msg_warning("Input is not valid utf8, glob match requires utf8 input, thus it never matches in this case",
                  evt_tag_printf("value", "%.*s", (gint) value_len, value));
    }
  return FALSE;
}

static void
log_matcher_glob_free(LogMatcher *s)
{
  LogMatcherGlob *self = (LogMatcherGlob *)s;
  g_pattern_spec_free(self->pattern);
  log_matcher_free_method(s);
}

LogMatcher *
log_matcher_glob_new(const LogMatcherOptions *options)
{
  LogMatcherGlob *self = g_new0(LogMatcherGlob, 1);

  log_matcher_init(&self->super, options);
  self->super.compile = log_matcher_glob_compile;
  self->super.match = log_matcher_glob_match;
  self->super.replace = NULL;
  self->super.free_fn = log_matcher_glob_free;

  return &self->super;
}

/* libpcre support */

typedef struct _LogMatcherPcreRe
{
  LogMatcher super;
  pcre *pattern;
  pcre_extra *extra;
  gint match_options;
  gchar *prefix;
  gint prefix_len;
} LogMatcherPcreRe;

static gboolean
_compile_pcre_regexp(LogMatcherPcreRe *self, const gchar *re, GError **error)
{
  gint rc;
  const gchar *errptr;
  gint erroffset;
  gint flags = 0;

  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  if (self->super.flags & LMF_ICASE)
    flags |= PCRE_CASELESS;

  if (self->super.flags & LMF_NEWLINE)
    {
      if (!PCRE_NEWLINE_ANYCRLF)
        msg_warning("syslog-ng was compiled against an old PCRE which doesn't support the 'newline' flag");
      flags |= PCRE_NEWLINE_ANYCRLF;
    }
  if (self->super.flags & LMF_UTF8)
    {
      gint support;
      flags |= PCRE_UTF8 | PCRE_NO_UTF8_CHECK;
      self->match_options |= PCRE_NO_UTF8_CHECK;

      pcre_config(PCRE_CONFIG_UTF8, &support);
      if (!support)
        {
          g_set_error(error, LOG_TEMPLATE_ERROR, 0, "PCRE library is compiled without UTF8 support and utf8 flag was present");
          return FALSE;
        }

      pcre_config(PCRE_CONFIG_UNICODE_PROPERTIES, &support);
      if (!support)
        {
          g_set_error(error, LOG_TEMPLATE_ERROR, 0,
                      "PCRE library is compiled without UTF8 properties support and utf8 flag was present");
          return FALSE;
        }
    }
  if (self->super.flags & LMF_DUPNAMES)
    {
      if (!PCRE_DUPNAMES)
        msg_warning("syslog-ng was compiled against an old PCRE which doesn't support the 'dupnames' flag");
      flags |= PCRE_DUPNAMES;
    }

  /* compile the regexp */
  self->pattern = pcre_compile2(re, flags, &rc, &errptr, &erroffset, NULL);
  if (!self->pattern)
    {
      g_set_error(error, LOG_TEMPLATE_ERROR, 0, "Failed to compile PCRE expression >>>%s<<< `%s' at character %d",
                  re, errptr, erroffset);
      return FALSE;
    }
  return TRUE;
}

static gboolean
_study_pcre_regexp(LogMatcherPcreRe *self, const gchar *re, GError **error)
{
  const gchar *errptr;
  gint options = 0;

  if ((self->super.flags & LMF_DISABLE_JIT) == 0)
    options |= PCRE_STUDY_JIT_COMPILE;

  /* optimize regexp */
  self->extra = pcre_study(self->pattern, options, &errptr);
  if (errptr != NULL)
    {
      g_set_error(error, LOG_TEMPLATE_ERROR, 0, "Failed to optimize regular expression >>>%s<<< `%s'",
                  re, errptr);
      return FALSE;
    }
  return TRUE;
}

static gboolean
log_matcher_pcre_re_compile(LogMatcher *s, const gchar *re, GError **error)
{
  LogMatcherPcreRe *self = (LogMatcherPcreRe *) s;

  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
  log_matcher_store_pattern(s, re);

  if (!_compile_pcre_regexp(self, re, error))
    return FALSE;

  if (!_study_pcre_regexp(self, re, error))
    return FALSE;

  return TRUE;
}

static void
log_matcher_pcre_re_feed_backrefs(LogMatcher *s, LogMessage *msg, gint value_handle, int *matches, gint match_num,
                                  const gchar *value)
{
  gint i;
  gboolean indirect = _shall_set_values_indirectly(value_handle);

  for (i = 0; i < (RE_MAX_MATCHES) && i < match_num; i++)
    {
      gint begin_index = matches[2 * i];
      gint end_index = matches[2 * i + 1];

      if (begin_index < 0 || end_index < 0)
        continue;

      if (indirect)
        {
          log_msg_set_match_indirect(msg, i, value_handle, 0, begin_index, end_index - begin_index);
        }
      else
        {
          log_msg_set_match(msg, i, &value[begin_index], end_index - begin_index);
        }
    }
}

static inline void
log_matcher_pcre_re_feed_value_by_name(LogMatcherPcreRe *s, LogMessage *msg, GString *formatted_name, gchar *tabptr,
                                       const gchar *value, gint begin_index, gint end_index)
{
  if(s->prefix != NULL)
    {
      if (formatted_name->len > 0)
        g_string_truncate(formatted_name, s->prefix_len);
      else
        g_string_assign(formatted_name, s->prefix);
      g_string_append(formatted_name, tabptr + 2);

      log_msg_set_value_by_name(msg, formatted_name->str, value + begin_index, end_index - begin_index);
    }
  else
    {
      log_msg_set_value_by_name(msg, tabptr + 2, value + begin_index, end_index - begin_index);
    }
}

static void
log_matcher_pcre_re_feed_named_substrings(LogMatcher *s, LogMessage *msg, int *matches, const gchar *value)
{
  gchar *name_table = NULL;
  gint i = 0;
  gint namecount = 0;
  gint name_entry_size = 0;
  LogMatcherPcreRe *self = (LogMatcherPcreRe *) s;

  pcre_fullinfo(self->pattern, self->extra, PCRE_INFO_NAMECOUNT, &namecount);
  if (namecount > 0)
    {
      gchar *tabptr;
      /* Before we can access the substrings, we must extract the table for
         translating names to numbers, and the size of each entry in the table.
       */
      pcre_fullinfo(self->pattern, self->extra, PCRE_INFO_NAMETABLE, &name_table);
      pcre_fullinfo(self->pattern, self->extra, PCRE_INFO_NAMEENTRYSIZE, &name_entry_size);
      /* Now we can scan the table and, for each entry, print the number, the name,
         and the substring itself.
       */
      GString *formatted_name = scratch_buffers_alloc();
      tabptr = name_table;
      for (i = 0; i < namecount; i++, tabptr += name_entry_size)
        {
          int n = (tabptr[0] << 8) | tabptr[1];
          gint begin_index = matches[2 * n];
          gint end_index = matches[2 * n + 1];

          if (begin_index < 0 || end_index < 0)
            continue;

          log_matcher_pcre_re_feed_value_by_name(self, msg, formatted_name, tabptr, value, begin_index, end_index);
        }
    }
}

static gboolean
log_matcher_pcre_re_match(LogMatcher *s, LogMessage *msg, gint value_handle, const gchar *value, gssize value_len)
{
  LogMatcherPcreRe *self = (LogMatcherPcreRe *) s;
  gint *matches;
  gsize matches_size;
  gint num_matches;
  gint rc;

  if (value_len == -1)
    value_len = strlen(value);

  if (pcre_fullinfo(self->pattern, self->extra, PCRE_INFO_CAPTURECOUNT, &num_matches) < 0)
    g_assert_not_reached();
  if (num_matches > RE_MAX_MATCHES)
    num_matches = RE_MAX_MATCHES;

  matches_size = 3 * (num_matches + 1);
  matches = g_alloca(matches_size * sizeof(gint));

  rc = pcre_exec(self->pattern, self->extra,
                 value, value_len, 0, self->match_options, matches, matches_size);
  if (rc < 0)
    {
      switch (rc)
        {
        case PCRE_ERROR_NOMATCH:
          break;

        default:
          /* Handle other special cases */
          msg_error("Error while matching regexp",
                    evt_tag_int("error_code", rc));
          break;
        }
      return FALSE;
    }
  if (rc == 0)
    {
      msg_error("Error while storing matching substrings");
    }
  else
    {
      if ((s->flags & LMF_STORE_MATCHES))
        {
          log_matcher_pcre_re_feed_backrefs(s, msg, value_handle, matches, rc, value);
          log_matcher_pcre_re_feed_named_substrings(s, msg, matches, value);
        }
    }
  return TRUE;
}

static gchar *
log_matcher_pcre_re_replace(LogMatcher *s, LogMessage *msg, gint value_handle, const gchar *value, gssize value_len,
                            LogTemplate *replacement, gssize *new_length)
{
  LogMatcherPcreRe *self = (LogMatcherPcreRe *) s;
  GString *new_value = NULL;
  gint *matches;
  gsize matches_size;
  gint num_matches;
  gint rc;
  gint start_offset, last_offset;
  gint options;
  gboolean last_match_was_empty;

  if (pcre_fullinfo(self->pattern, self->extra, PCRE_INFO_CAPTURECOUNT, &num_matches) < 0)
    g_assert_not_reached();
  if (num_matches > RE_MAX_MATCHES)
    num_matches = RE_MAX_MATCHES;

  matches_size = 3 * (num_matches + 1);
  matches = g_alloca(matches_size * sizeof(gint));

  /* we need zero initialized offsets for the last match as the
   * algorithm tries uses that as the base position */

  matches[0] = matches[1] = matches[2] = 0;

  if (value_len == -1)
    value_len = strlen(value);

  last_offset = start_offset = 0;
  last_match_was_empty = FALSE;
  do
    {
      /* loop over the string, replacing one occurrence at a time. */

      /* NOTE: zero length matches need special care, as we could spin
       * forever otherwise (since the current position wouldn't be
       * advanced).
       *
       * A zero-length match can be as simple as "a*" which will be
       * returned unless PCRE_NOTEMPTY is specified.
       *
       * By supporting zero-length matches, we basically make it
       * possible to insert replacement between each incoming
       * character.
       *
       * For example:
       *     pattern: a*
       *     replacement: #
       *     input: message
       *     result: #m#e#s#s#a#g#e#
       *
       * This mimics Perl behaviour.
       */

      if (last_match_was_empty)
        {
          /* Otherwise, arrange to run another match at the same point
           * to see if a non-empty match can be found.
           */

          options = PCRE_NOTEMPTY | PCRE_ANCHORED;
        }
      else
        {
          options = 0;
        }

      rc = pcre_exec(self->pattern, self->extra,
                     value, value_len,
                     start_offset, (self->match_options | options), matches, matches_size);
      if (rc < 0 && rc != PCRE_ERROR_NOMATCH)
        {
          msg_error("Error while matching regexp",
                    evt_tag_int("error_code", rc));
          break;
        }
      else if (rc < 0)
        {
          if ((options & PCRE_NOTEMPTY) == 0)
            {
              /* we didn't match, even when we permitted to match the
               * empty string. Nothing to find here, bail out */
              break;
            }

          /* we didn't match, quite possibly because the empty match
           * was not permitted. Skip one character in order to avoid
           * infinite loop over the same zero-length match. */

          start_offset = start_offset + 1;
          /* FIXME: handle complex sequences like utf8 and newline characters */
          last_match_was_empty = FALSE;
          continue;
        }
      else
        {
          /* if the output array was too small, truncate the number of
             captures to RE_MAX_MATCHES */

          if (rc == 0)
            rc = matches_size / 3;

          log_matcher_pcre_re_feed_backrefs(s, msg, value_handle, matches, rc, value);
          log_matcher_pcre_re_feed_named_substrings(s, msg, matches, value);

          if (!new_value)
            new_value = g_string_sized_new(value_len);
          /* append non-matching portion */
          g_string_append_len(new_value, &value[last_offset], matches[0] - last_offset);
          /* replacement */
          log_template_append_format(replacement, msg, &DEFAULT_TEMPLATE_EVAL_OPTIONS, new_value);

          last_match_was_empty = (matches[0] == matches[1]);
          start_offset = last_offset = matches[1];
        }
    }
  while (self->super.flags & LMF_GLOBAL && start_offset < value_len);

  if (new_value)
    {
      /* append the last literal */
      g_string_append_len(new_value, &value[last_offset], value_len - last_offset);
      if (new_length)
        *new_length = new_value->len;
      return g_string_free(new_value, FALSE);
    }
  return NULL;
}

static void
log_matcher_pcre_re_free(LogMatcher *s)
{
  LogMatcherPcreRe *self = (LogMatcherPcreRe *) s;
  pcre_free_study(self->extra);
  pcre_free(self->pattern);
  log_matcher_free_method(s);
}

LogMatcher *
log_matcher_pcre_re_new(const LogMatcherOptions *options)
{
  LogMatcherPcreRe *self = g_new0(LogMatcherPcreRe, 1);
  self->prefix = NULL;
  self->prefix_len = 0;

  log_matcher_init(&self->super, options);
  self->super.compile = log_matcher_pcre_re_compile;
  self->super.match = log_matcher_pcre_re_match;
  self->super.replace = log_matcher_pcre_re_replace;
  self->super.free_fn = log_matcher_pcre_re_free;

  return &self->super;
}

void
log_matcher_pcre_set_prefix(LogMatcher *s, const gchar *prefix)
{
  LogMatcherPcreRe *self = (LogMatcherPcreRe *) s;

  g_free(self->prefix);
  if (prefix)
    {
      self->prefix = g_strdup(prefix);
      self->prefix_len = strlen(prefix);
    }
  else
    {
      self->prefix = NULL;
      self->prefix_len = 0;
    }
}

typedef LogMatcher *(*LogMatcherConstructFunc)(const LogMatcherOptions *options);

struct
{
  const gchar *name;
  LogMatcherConstructFunc construct;
} matcher_types[] =
{
  { "pcre", log_matcher_pcre_re_new },
  { "string", log_matcher_string_new },
  { "glob", log_matcher_glob_new },
  { NULL, NULL },
};

static LogMatcherConstructFunc
log_matcher_lookup_construct(const gchar *type)
{
  gint i;

  for (i = 0; matcher_types[i].name; i++)
    {
      if (strcmp(matcher_types[i].name, type) == 0)
        return matcher_types[i].construct;
    }
  return NULL;
}

LogMatcher *
log_matcher_new(const LogMatcherOptions *options)
{
  LogMatcherConstructFunc construct;

  construct = log_matcher_lookup_construct(options->type);
  return construct(options);
}

LogMatcher *
log_matcher_ref(LogMatcher *s)
{
  s->ref_cnt++;
  return s;
}

void
log_matcher_unref(LogMatcher *s)
{
  if (--s->ref_cnt == 0)
    {
      if (s->free_fn)
        s->free_fn(s);
      g_free(s);
    }
}

gboolean
log_matcher_options_set_type(LogMatcherOptions *options, const gchar *type)
{
  LogMatcherConstructFunc construct;

  if (strcmp(type, "posix") == 0)
    {
      msg_warning_once("WARNING: syslog-ng dropped support for POSIX regexp implementations in " VERSION_3_14
                       " in favour of PCRE, which should be upward compatible. All 'posix' regexps are "
                       "automatically switched to 'pcre'. Please ensure that your regexps work with PCRE and "
                       "specify type('pcre') explicitly or increase @version to remove this warning");
      type = "pcre";
    }

  construct = log_matcher_lookup_construct(type);
  if (!construct)
    return FALSE;

  if (options->type)
    g_free(options->type);
  options->type = g_strdup(type);
  return TRUE;
}

CfgFlagHandler log_matcher_flag_handlers[] =
{
  /* NOTE: underscores are automatically converted to dashes */

  { "global",          CFH_SET, offsetof(LogMatcherOptions, flags), LMF_GLOBAL        },
  { "icase",           CFH_SET, offsetof(LogMatcherOptions, flags), LMF_ICASE         },
  { "ignore-case",     CFH_SET, offsetof(LogMatcherOptions, flags), LMF_ICASE         },
  { "newline",         CFH_SET, offsetof(LogMatcherOptions, flags), LMF_NEWLINE       },
  { "unicode",         CFH_SET, offsetof(LogMatcherOptions, flags), LMF_UTF8          },
  { "utf8",            CFH_SET, offsetof(LogMatcherOptions, flags), LMF_UTF8          },
  { "store-matches",   CFH_SET, offsetof(LogMatcherOptions, flags), LMF_STORE_MATCHES },
  { "substring",       CFH_SET, offsetof(LogMatcherOptions, flags), LMF_SUBSTRING     },
  { "prefix",          CFH_SET, offsetof(LogMatcherOptions, flags), LMF_PREFIX        },
  { "disable-jit",     CFH_SET, offsetof(LogMatcherOptions, flags), LMF_DISABLE_JIT   },
  { "dupnames",        CFH_SET, offsetof(LogMatcherOptions, flags), LMF_DUPNAMES      },

  { NULL },
};

gboolean
log_matcher_options_process_flag(LogMatcherOptions *self, const gchar *flag)
{
  return cfg_process_flag(log_matcher_flag_handlers, self, flag);
}

void
log_matcher_options_defaults(LogMatcherOptions *options)
{
  options->flags = 0;
  options->type = NULL;
}

void
log_matcher_options_init(LogMatcherOptions *options)
{
  if (!options->type)
    {
      const gchar *default_matcher = "pcre";

      if (!log_matcher_options_set_type(options, default_matcher))
        g_assert_not_reached();
    }
}

void
log_matcher_options_destroy(LogMatcherOptions *options)
{
  if (options->type)
    g_free(options->type);
}

GQuark
log_matcher_error_quark(void)
{
  return g_quark_from_static_string("log-matcher-error-quark");
}
