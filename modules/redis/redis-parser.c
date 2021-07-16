/*
 * Copyright (c) 2013 Balabit
 * Copyright (c) 2013 Tihamer Petrovics <tihameri@gmail.com>
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

#include "redis.h"
#include "cfg-parser.h"
#include "redis-grammar.h"

extern int redis_debug;
int redis_parse(CfgLexer *lexer, LogDriver **instance, gpointer arg);

static CfgLexerKeyword redis_keywords[] =
{
  { "redis",    KW_REDIS },
  { "command",  KW_COMMAND },
  { "host",     KW_HOST },
  { "port",     KW_PORT },
  { "auth",     KW_AUTH },
  { "timeout",  KW_TIMEOUT },
  { "workers", KW_WORKERS },
  { NULL }
};

CfgParser redis_parser =
{
#if SYSLOG_NG_ENABLE_DEBUG
  .debug_flag = &redis_debug,
#endif
  .name = "redis",
  .keywords = redis_keywords,
  .parse = (int (*)(CfgLexer *lexer, gpointer *instance, gpointer)) redis_parse,
  .cleanup = (void (*)(gpointer)) log_pipe_unref,
};

CFG_PARSER_IMPLEMENT_LEXER_BINDING(redis_, REDIS_, LogDriver **)
