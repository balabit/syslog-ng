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
#include "cfg-parser.h"
#include "regexp-parser-grammar.h"

extern int regexp_parser_debug;

int regexp_parser_parse(CfgLexer *lexer, LogParser **instance, gpointer arg);

static CfgLexerKeyword regexp_parser_keywords[] =
{
  {"regexp_parser", KW_REGEXP_PARSER},
  {"prefix", KW_PREFIX},
  {"patterns", KW_PATTERNS},
  {NULL}
};

CfgParser regexp_parser_parser =
{
#if SYSLOG_NG_ENABLE_DEBUG
  .debug_flag = &regexp_parser_debug,
#endif
  .name = "regexp-parser",
  .keywords = regexp_parser_keywords,
  .parse = (gint (*)(CfgLexer *, gpointer *, gpointer)) regexp_parser_parse,
  .cleanup = (void (*)(gpointer)) log_pipe_unref,
};

CFG_PARSER_IMPLEMENT_LEXER_BINDING(regexp_parser_, REGEXP_PARSER_, LogParser **)
