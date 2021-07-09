/*
 * Copyright (c) 2021 Balabit
 * Copyright (c) 2021 Xiaoyu Qiu
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "regexp-parser.h"
#include "parser/parser-expr.h"
#include "scratch-buffers.h"
#include "string-list.h"

#include <string.h>
#include <stdio.h>

typedef struct _RegexpParser
{
  LogParser super;
  gchar *prefix;
  GList *patterns;
  LogMatcherOptions matcher_options;
  GList *matchers;
} RegexpParser;

LogMatcherOptions *
regexp_parser_get_matcher_options(LogParser *s)
{
  RegexpParser *self = (RegexpParser *) s;

  return &self->matcher_options;
}

void
regexp_parser_set_prefix(LogParser *s, const gchar *prefix)
{
  RegexpParser *self = (RegexpParser *) s;

  g_free(self->prefix);
  if (prefix)
    self->prefix = g_strdup(prefix);
  else
    self->prefix = NULL;
}

void
regexp_parser_set_patterns(LogParser *s, GList *patterns)
{
  RegexpParser *self = (RegexpParser *) s;

  string_list_free(self->patterns);
  self->patterns = patterns;
}

gboolean
regexp_parser_compile(LogParser *s)
{
  RegexpParser *self = (RegexpParser *) s;

  log_matcher_options_init(&self->matcher_options);
  gboolean result = TRUE;

  for (GList *item = self->patterns; item; item = item->next)
    {
      self->matchers = g_list_prepend(self->matchers, log_matcher_new(&self->matcher_options));
      log_matcher_pcre_set_prefix((LogMatcher *)self->matchers->data, self->prefix);

      if(!log_matcher_compile((LogMatcher *)self->matchers->data, item->data, NULL))
        {
          result = FALSE;
          break;
        }
    }

  if (result)
    self->matchers = g_list_reverse(self->matchers);
  else
    g_list_free_full(self->matchers, (GDestroyNotify) log_matcher_unref);

  return result;
}

static gboolean
regexp_parser_process(LogParser *s, LogMessage **pmsg, const LogPathOptions *path_options, const gchar *input,
                      gsize input_len)
{
  RegexpParser *self = (RegexpParser *) s;

  log_msg_make_writable(pmsg, path_options);
  msg_trace("regexp-parser message processing started",
            evt_tag_str ("input", input),
            evt_tag_str ("prefix", self->prefix),
            evt_tag_printf("msg", "%p", *pmsg));

  gboolean result = FALSE;
  for (GList *item = self->matchers; item; item = item->next)
    {
      msg_trace("regexp-parser message processing for",
                evt_tag_str ("input", input),
                evt_tag_str ("pattern", ((LogMatcher *)item->data)->pattern));

      if (G_LIKELY(!self->super.template))
        {
          if (log_matcher_match((LogMatcher *)item->data, *pmsg, LM_V_MESSAGE, input, input_len))
            result = TRUE;
        }
      else
        {
          if (log_matcher_match((LogMatcher *)item->data, *pmsg, LM_V_NONE, input, input_len))
            result = TRUE;
        }
    }

  return result;
}

static void
regexp_parser_free(LogPipe *s)
{
  RegexpParser *self = (RegexpParser *) s;

  g_free(self->prefix);
  string_list_free(self->patterns);
  log_parser_free_method(s);
  g_list_free_full(self->matchers, (GDestroyNotify) log_matcher_unref);

  log_matcher_options_destroy(&self->matcher_options);
}

static LogPipe *
regexp_parser_clone(LogPipe *s)
{
  RegexpParser *self = (RegexpParser *)s;
  RegexpParser *cloned;

  cloned = (RegexpParser *)regexp_parser_new(s->cfg);
  regexp_parser_set_prefix(&cloned->super, self->prefix);
  regexp_parser_set_patterns(&cloned->super, string_list_clone(self->patterns));
  log_parser_set_template(&cloned->super, log_template_ref(self->super.template));

  for (GList *item = self->matchers; item; item = item->next)
    cloned->matchers = g_list_append(cloned->matchers, log_matcher_ref((LogMatcher *)item->data));

  return &cloned->super.super;
}


LogParser *
regexp_parser_new(GlobalConfig *cfg)
{
  RegexpParser *self = g_new0(RegexpParser, 1);

  log_parser_init_instance(&self->super, cfg);
  self->super.super.free_fn = regexp_parser_free;
  self->super.super.clone = regexp_parser_clone;
  self->super.process = regexp_parser_process;

  log_matcher_options_defaults(&self->matcher_options);
  self->matcher_options.flags |= LMF_STORE_MATCHES;
  self->patterns = NULL;

  return &self->super;
}
