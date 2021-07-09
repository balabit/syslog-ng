/*
 * Copyright (c) 2002-2010 Balabit
 * Copyright (c) 1998-2010 Balázs Scheidler
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

#ifndef LOGMATCHER_H_INCLUDED
#define LOGMATCHER_H_INCLUDED

#include "logmsg/logmsg.h"
#include "template/templates.h"

#define LOG_MATCHER_ERROR log_template_error_quark()

GQuark log_matcher_error_quark(void);

enum
{
  /* use global search/replace */
  LMF_GLOBAL = 0x0001,
  LMF_ICASE  = 0x0002,
  LMF_MATCH_ONLY = 0x0004,

  /* POSIX + PCRE common flags */
  LMF_NEWLINE= 0x0008,
  LMF_UTF8   = 0x0010,
  LMF_STORE_MATCHES = 0x0020,
  LMF_DISABLE_JIT = 0x0040,

  /* string flags */
  LMF_SUBSTRING = 0x0080,
  LMF_PREFIX = 0x0100,

  /*  advanced LIBPCRE flags */
  LMF_DUPNAMES = 0x00080000,
};

typedef struct _LogMatcherOptions
{
  gint flags;
  gchar *type;
} LogMatcherOptions;

typedef struct _LogMatcher LogMatcher;

struct _LogMatcher
{
  gint ref_cnt;
  gint flags;
  gchar *pattern;
  gboolean (*compile)(LogMatcher *s, const gchar *re, GError **error);
  /* value_len can be -1 to indicate unknown length */
  gboolean (*match)(LogMatcher *s, LogMessage *msg, gint value_handle, const gchar *value, gssize value_len);
  /* value_len can be -1 to indicate unknown length, new_length can be returned as -1 to indicate unknown length */
  gchar *(*replace)(LogMatcher *s, LogMessage *msg, gint value_handle, const gchar *value, gssize value_len,
                    LogTemplate *replacement, gssize *new_length);
  void (*free_fn)(LogMatcher *s);
};

static inline gboolean
log_matcher_compile(LogMatcher *s, const gchar *re, GError **error)
{
  return s->compile(s, re, error);
}

static inline gboolean
log_matcher_match(LogMatcher *s, LogMessage *msg, gint value_handle, const gchar *value, gssize value_len)
{
  return s->match(s, msg, value_handle, value, value_len);
}

static inline gchar *
log_matcher_replace(LogMatcher *s, LogMessage *msg, gint value_handle, const gchar *value, gssize value_len,
                    LogTemplate *replacement, gssize *new_length)
{
  if (s->replace)
    return s->replace(s, msg, value_handle, value, value_len, replacement, new_length);
  return NULL;
}

static inline void
log_matcher_set_flags(LogMatcher *s, gint flags)
{
  s->flags = flags;
}

static inline gboolean
log_matcher_is_replace_supported(LogMatcher *s)
{
  return s->replace != NULL;
}

LogMatcher *log_matcher_pcre_re_new(const LogMatcherOptions *options);
LogMatcher *log_matcher_string_new(const LogMatcherOptions *options);
LogMatcher *log_matcher_glob_new(const LogMatcherOptions *options);

LogMatcher *log_matcher_new(const LogMatcherOptions *options);
LogMatcher *log_matcher_ref(LogMatcher *s);
void log_matcher_unref(LogMatcher *s);


gboolean log_matcher_options_set_type(LogMatcherOptions *options, const gchar *type);
gboolean log_matcher_options_process_flag(LogMatcherOptions *self, const gchar *flag);
void log_matcher_options_defaults(LogMatcherOptions *options);
void log_matcher_options_init(LogMatcherOptions *options);
void log_matcher_options_destroy(LogMatcherOptions *options);

void log_matcher_pcre_set_prefix(LogMatcher *s, const gchar *prefix);

#endif
