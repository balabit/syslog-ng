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

#ifndef REGEXP_PARSER_PARSER_H_INCLUDED
#define REGEXP_PARSER_PARSER_H_INCLUDED

#include "cfg-parser.h"
#include "parser/parser-expr-parser.h"

extern CfgParser regexp_parser_parser;

CFG_PARSER_DECLARE_LEXER_BINDING(regexp_parser_, REGEXP_PARSER_, LogParser **)

#endif
