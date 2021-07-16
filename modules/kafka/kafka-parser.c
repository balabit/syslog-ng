/*
 * Copyright (c) 2014 Pierre-Yves Ritschard <pyr@spootnik.org>
 * Copyright (c) 2019 Balabit
 * Copyright (c) 2019 Balazs Scheidler
 *
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

#include "cfg-parser.h"
#include "kafka-grammar.h"

extern int kafka_debug;
int kafka_parse(CfgLexer *lexer, LogDriver **instance, gpointer arg);

static CfgLexerKeyword kafka_keywords[] =
{
  { "topic",          KW_TOPIC },
  { "fallback_topic", KW_FALLBACK_TOPIC},

  /* https://github.com/edenhill/librdkafka/blob/master/CONFIGURATION.md */
  { "config",         KW_CONFIG },
  { "flush_timeout_on_shutdown", KW_FLUSH_TIMEOUT_ON_SHUTDOWN },
  { "flush_timeout_on_reload",   KW_FLUSH_TIMEOUT_ON_RELOAD },

  { "key",            KW_KEY },
  { "message",        KW_MESSAGE },
  { "sync_send",      KW_SYNC_SEND},
  { "bootstrap_servers", KW_BOOTSTRAP_SERVERS },
  { "workers",        KW_WORKERS },
  { "poll_timeout",   KW_POLL_TIMEOUT },
  { "kafka_c",        KW_KAFKA },   /* compatibility with incubator naming */
  { NULL }
};

CfgParser kafka_parser =
{
#if SYSLOG_NG_ENABLE_DEBUG
  .debug_flag = &kafka_debug,
#endif
  .name = "kafka",
  .keywords = kafka_keywords,
  .parse = (gint (*)(CfgLexer *, gpointer *, gpointer)) kafka_parse,
  .cleanup = (void (*)(gpointer)) log_pipe_unref,
};

CFG_PARSER_IMPLEMENT_LEXER_BINDING(kafka_, KAFKA_, LogDriver **)
