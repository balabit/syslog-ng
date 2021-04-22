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

#ifndef CFGTREE_H_INCLUDED
#define CFGTREE_H_INCLUDED

#include "syslog-ng.h"
#include "template/templates.h"
#include "cfg-lexer.h"
#include "messages.h"
#include "atomic.h"

const gchar *log_expr_node_get_content_name(gint content);

#define LC_CATCHALL       1
#define LC_FALLBACK       2
#define LC_FINAL          4
#define LC_FLOW_CONTROL   8
#define LC_DROP_UNMATCHED 16

enum
{
  /* expr node content type */
  ENC_SOURCE,
  ENC_DESTINATION,
  ENC_FILTER,
  ENC_PARSER,
  ENC_REWRITE,
  ENC_MAX,
  /* */
  ENC_PIPE,

  /* expr node layouts type */
  ENL_SINGLE,
  ENL_REFERENCE,
  ENL_SEQUENCE,
  ENL_JUNCTION,
};


typedef struct _LogExprNode LogExprNode;

/**
 * Log Expressions
 * ===============
 *
 * Everything except a few things are parsed from the configuration as
 * a log expression. The few exceptions are: templates, global options and blocks.
 *
 * Sources, destinations, filters, parsers, rewrite rules and global
 * log statements are log expressions.
 *
 * Log expressions describe a graph, which is then traversed by
 * messages received by syslog-ng. The graph used to be a tree
 * (e.g. no cycles), but this limitation was lifted in syslog-ng 3.4,
 * when the concept of log expression was introduced.
 *
 * Log expression is a part of the graph, the larger graph is created
 * by connecting these parts as dictated by the configuration.
 *
 * Each log expression is represented using a tree of LogExprNode
 * elements. Each node in this tree defines the layout how its children
 * are to be connected:
 *   - simple element: holds a single LogPipe, no children
 *   - reference:      used to reference log expressions defined elsewhere, no children
 *   - sequence:       holds a sequence of LogExprNodes
 *   - junction:       holds a junction
 *
 * Sometimes syslog-ng needs to know what kind of object the user
 * originally defined, this is stored in the "content" member.
 *
 *   ENC_PIPE: content is a single LogPipe instance (in the "object" member)
 *   ENC_SOURCE: content is a source log expression node (source statement or one defined inline)
 *   ENC_DESTINATION: content is a destination node
 *   ENC_FILTER: content is a filter node
 *   ENC_PARSER: content is a parser node
 *   ENC_REWRITE: content is a rewrite node
 */
struct _LogExprNode
{
  GAtomicCounter ref_cnt;
  gint16 layout;
  gint16 content;

  guint32 flags;

  /* name of the rule for named rules and name of the named rule for references */
  gchar *name;
  /* parent node */
  LogExprNode *parent;
  /* list of children */
  LogExprNode *children;
  /* next sibling */
  LogExprNode *next;

  gpointer object;
  GDestroyNotify object_destroy;

  /* used during construction in case a rule specific object needs to be created. */
  gpointer aux;
  GDestroyNotify aux_destroy;
  gchar *filename;
  gint line, column;
  gint child_id;
};

gint log_expr_node_lookup_flag(const gchar *flag);

LogExprNode *log_expr_node_append_tail(LogExprNode *a, LogExprNode *b);
void log_expr_node_set_object(LogExprNode *self, gpointer object, GDestroyNotify destroy);
const gchar *log_expr_node_format_location(LogExprNode *self, gchar *buf, gsize buf_len);
EVTTAG *log_expr_node_location_tag(LogExprNode *self);

LogExprNode *log_expr_node_new(gint layout, gint content, const gchar *name, LogExprNode *children, guint32 flags,
                               CFG_LTYPE *yylloc);

LogExprNode *log_expr_node_ref(LogExprNode *self);
LogExprNode *log_expr_node_unref(LogExprNode *self);

LogExprNode *log_expr_node_new_pipe(LogPipe *pipe, CFG_LTYPE *yylloc);
LogExprNode *log_expr_node_new_source(const gchar *name, LogExprNode *children, CFG_LTYPE *yylloc);
LogExprNode *log_expr_node_new_source_reference(const gchar *name, CFG_LTYPE *yylloc);
LogExprNode *log_expr_node_new_destination(const gchar *name, LogExprNode *children, CFG_LTYPE *yylloc);
LogExprNode *log_expr_node_new_destination_reference(const gchar *name, CFG_LTYPE *yylloc);
LogExprNode *log_expr_node_new_filter(const gchar *name, LogExprNode *node, CFG_LTYPE *yylloc);
LogExprNode *log_expr_node_new_filter_reference(const gchar *name, CFG_LTYPE *yylloc);
LogExprNode *log_expr_node_new_parser(const gchar *name, LogExprNode *children, CFG_LTYPE *yylloc);
LogExprNode *log_expr_node_new_parser_reference(const gchar *name, CFG_LTYPE *yylloc);
LogExprNode *log_expr_node_new_rewrite(const gchar *name, LogExprNode *children, CFG_LTYPE *yylloc);
LogExprNode *log_expr_node_new_rewrite_reference(const gchar *name, CFG_LTYPE *yylloc);
LogExprNode *log_expr_node_new_log(LogExprNode *children, guint32 flags, CFG_LTYPE *yylloc);
LogExprNode *log_expr_node_new_sequence(LogExprNode *children, CFG_LTYPE *yylloc);
LogExprNode *log_expr_node_new_junction(LogExprNode *children, CFG_LTYPE *yylloc);
void log_expr_node_conditional_set_false_branch_of_the_last_if(LogExprNode *conditional_node, LogExprNode *false_expr);
LogExprNode *log_expr_node_new_conditional_with_filter(LogExprNode *filter_pipe, LogExprNode *true_expr,
                                                       CFG_LTYPE *yylloc);
LogExprNode *log_expr_node_new_conditional_with_block(LogExprNode *block, CFG_LTYPE *yylloc);

typedef struct _CfgTree
{
  GlobalConfig *cfg;
  GPtrArray *initialized_pipes;
  gint anon_counters[ENC_MAX];
  /* hash of predefined source/filter/rewrite/parser/destination objects */
  GHashTable *objects;
  /* list of top-level rules */
  GPtrArray *rules;
  GHashTable *templates;
  gboolean compiled;
  gboolean check_zero_source_count;
} CfgTree;

gboolean cfg_tree_add_object(CfgTree *self, LogExprNode *rule);
LogExprNode *cfg_tree_get_object(CfgTree *self, gint type, const gchar *name);
GList *cfg_tree_get_objects(CfgTree *self);

gboolean cfg_tree_add_template(CfgTree *self, LogTemplate *template);
LogTemplate *cfg_tree_lookup_template(CfgTree *self, const gchar *name);
LogTemplate *cfg_tree_check_inline_template(CfgTree *self, const gchar *template_or_name, GError **error);

gchar *cfg_tree_get_rule_name(CfgTree *self, gint content, LogExprNode *node);
gchar *cfg_tree_get_child_id(CfgTree *self, gint content, LogExprNode *node);

gboolean cfg_tree_start(CfgTree *self);
gboolean cfg_tree_stop(CfgTree *self);
gboolean cfg_tree_on_inited(CfgTree *self);

void cfg_tree_init_instance(CfgTree *self, GlobalConfig *cfg);
void cfg_tree_free_instance(CfgTree *self);


#endif
