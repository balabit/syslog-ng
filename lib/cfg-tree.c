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

#include "cfg-tree.h"
#include "logmpx.h"
#include "logpipe.h"

#include <string.h>

static void _log_expr_node_free(LogExprNode *self);

LogExprNode *
log_expr_node_ref(LogExprNode *self)
{
  g_assert(!self || g_atomic_counter_get(&self->ref_cnt) > 0);

  if (self)
    {
      g_atomic_counter_inc(&self->ref_cnt);
    }
  return self;
}

LogExprNode *
log_expr_node_unref(LogExprNode *self)
{
  g_assert(!self || g_atomic_counter_get(&self->ref_cnt));

  if (self && (g_atomic_counter_dec_and_test(&self->ref_cnt)))
    {
      _log_expr_node_free(self);
      return NULL;
    }
  return self;
}

/*
 * Return the textual representation of a node content type.
 */
const gchar *
log_expr_node_get_content_name(gint content)
{
  switch (content)
    {
    case ENC_PIPE:
      return "log";
    case ENC_SOURCE:
      return "source";
    case ENC_FILTER:
      return "filter";
    case ENC_PARSER:
      return "parser";
    case ENC_REWRITE:
      return "rewrite";
    case ENC_DESTINATION:
      return "destination";
    default:
      g_assert_not_reached();
      break;
    }
}

/*
 * Return the textual representation of a node layout.
 */
const gchar *
log_expr_node_get_layout_name(gint layout)
{
  switch (layout)
    {
    case ENL_SINGLE:
      return "single";
    case ENL_REFERENCE:
      return "reference";
    case ENL_SEQUENCE:
      return "sequence";
    case ENL_JUNCTION:
      return "junction";
    default:
      g_assert_not_reached();
      break;
    }
}

/* return the top-most rule that matches content. This is used to
 * query the enclosing rule for a given LogExprNode. It is looking up
 * the top-most node, so that we use the name of the enclosing block
 * that the user specified. In-line defined, and thus anonymous
 * expressions are automatically named. In that case this function
 * will return a node matching @content but without an actual name.
 */
LogExprNode *
log_expr_node_get_container_rule(LogExprNode *self, gint content)
{
  LogExprNode *node, *result = NULL;

  node = self->parent;
  while (node)
    {
      if (node->content == content)
        {
          result = node;
        }
      node = node->parent;
    }
  return result;
}

/**
 * log_expr_node_append:
 * @a: first LogExprNode
 * @b: second LogExprNode
 *
 * This function appends @b to @a in a linked list using the ep_next field
 * in LogExprNode.
 **/
void
log_expr_node_append(LogExprNode *a, LogExprNode *b)
{
  a->next = b;
}

LogExprNode *
log_expr_node_append_tail(LogExprNode *a, LogExprNode *b)
{
  if (a)
    {
      LogExprNode *p = a;
      while (p->next)
        p = p->next;
      log_expr_node_append(p, b);
      return a;
    }
  return b;
}

/*
 * Format the location information for a LogExprNode. For nodes that
 * have no location information, the parent is considered, this way
 * always returning something close to the location that defined the
 * node.
 */
const gchar *
log_expr_node_format_location(LogExprNode *self, gchar *buf, gsize buf_len)
{
  LogExprNode *node = self;

  while (node)
    {
      if (node->line || node->column)
        {
          g_snprintf(buf, buf_len, "%s:%d:%d", self->filename ? : "#buffer", node->line, node->column);
          break;
        }
      node = node->parent;
    }
  if (!node)
    strncpy(buf, "#unknown", buf_len);
  return buf;
}

EVTTAG *
log_expr_node_location_tag(LogExprNode *self)
{
  gchar buf[128];

  return evt_tag_str("location", log_expr_node_format_location(self, buf, sizeof(buf)));
}

/*
 * Set the list of children of a LogExprNode. It automatically updates
 * the children's "parent" pointers so that the tree can be traversed
 * upwards too.
 */
void
log_expr_node_set_children(LogExprNode *self, LogExprNode *children)
{
  LogExprNode *ep;

  /* we don't currently support setting the children list multiple
   * times. no inherent reason, just the proper free function would
   * need to be written, until then this assert would reveal the case
   * quite fast.
   */

  g_assert(self->children == NULL);

  for (ep = children; ep; ep = ep->next)
    ep->parent = self;

  self->children = children;
}

/*
 * Set the object associated with a node. The "object" member is used
 * to store the LogPipe instance associated with ENL_SINGLE/ENC_PIPE
 * nodes.
 */
void
log_expr_node_set_object(LogExprNode *self, gpointer object, GDestroyNotify destroy)
{
  self->object = object;
  self->object_destroy = destroy;
}

/*
 * The aux object is the secondary object associated with a node, it
 * is mostly unused, except for nodes storing source and destination
 * statements, in which case this contains a reference to the last
 * item of the compiled sequence (for sources) or the first item of
 * the compiled sequence (destinations).
 *
 * This mechanism is used to avoid having to clone source/destination
 * pipes, which operation they don't support.
 */
void
log_expr_node_set_aux(LogExprNode *self, gpointer aux, GDestroyNotify destroy)
{
  self->aux = aux;
  self->aux_destroy = destroy;
}

/**
 * log_expr_node_new:
 * @layout: layout of the children (ENL_*)
 * @content: what kind of expression this node stores (ENC_*)
 * @name: name of this rule (optional)
 * @children: child nodes
 * @flags: a combination of LC_* flags as specified by the administrator
 * @yylloc: the lexer location in the configuration file.
 *
 * This function constructs a LogExprNode object which encapsulates a
 * log expression in the configuration. See the note in cfg-tree.h for
 * more information about LogExprNode objects and log expressions.
 **/
LogExprNode *
log_expr_node_new(gint layout, gint content, const gchar *name, LogExprNode *children, guint32 flags, CFG_LTYPE *yylloc)
{
  LogExprNode *self = g_new0(LogExprNode, 1);

  g_atomic_counter_set(&self->ref_cnt, 1);

  self->layout = layout;
  self->content = content;
  self->name = g_strdup(name);
  log_expr_node_set_children(self, children);
  self->flags = flags;
  if (yylloc)
    {
      self->filename = g_strdup(yylloc->level->name);
      self->line = yylloc->first_line;
      self->column = yylloc->first_column;
    }
  return self;
}

/**
 * log_expr_node_free:
 * @self: LogExprNode instance
 *
 * This function frees the LogExprNode object encapsulating a log
 * expression node pointed to by @self.
 **/
static void
_log_expr_node_free(LogExprNode *self)
{
  LogExprNode *next, *p;

  for (p = self->children ; p; p = next)
    {
      next = p->next;
      log_expr_node_unref(p);
    }
  if (self->object && self->object_destroy)
    self->object_destroy(self->object);
  if (self->aux && self->aux_destroy)
    self->aux_destroy(self->aux);
  g_free(self->name);
  g_free(self->filename);
  g_free(self);
}

LogExprNode *
log_expr_node_new_pipe(LogPipe *pipe, CFG_LTYPE *yylloc)
{
  LogExprNode *node = log_expr_node_new(ENL_SINGLE, ENC_PIPE, NULL, NULL, 0, yylloc);

  log_expr_node_set_object(node, pipe, (GDestroyNotify) log_pipe_unref);
  return node;
}

LogExprNode *
log_expr_node_new_source(const gchar *name, LogExprNode *children, CFG_LTYPE *yylloc)
{
  return log_expr_node_new(ENL_SEQUENCE, ENC_SOURCE, name, children, 0, yylloc);
}

LogExprNode *
log_expr_node_new_source_reference(const gchar *name, CFG_LTYPE *yylloc)
{
  return log_expr_node_new(ENL_REFERENCE, ENC_SOURCE, name, NULL, 0, yylloc);
}

LogExprNode *
log_expr_node_new_destination(const gchar *name, LogExprNode *children, CFG_LTYPE *yylloc)
{
  return log_expr_node_new(ENL_SEQUENCE, ENC_DESTINATION, name, children, 0, yylloc);
}

LogExprNode *
log_expr_node_new_destination_reference(const gchar *name, CFG_LTYPE *yylloc)
{
  return log_expr_node_new(ENL_REFERENCE, ENC_DESTINATION, name, NULL, 0, yylloc);
}

LogExprNode *
log_expr_node_new_filter(const gchar *name, LogExprNode *child, CFG_LTYPE *yylloc)
{
  return log_expr_node_new(ENL_SEQUENCE, ENC_FILTER, name, child, 0, yylloc);
}

LogExprNode *
log_expr_node_new_filter_reference(const gchar *name, CFG_LTYPE *yylloc)
{
  return log_expr_node_new(ENL_REFERENCE, ENC_FILTER, name, NULL, 0, yylloc);
}

LogExprNode *
log_expr_node_new_parser(const gchar *name, LogExprNode *children, CFG_LTYPE *yylloc)
{
  return log_expr_node_new(ENL_SEQUENCE, ENC_PARSER, name, children, 0, yylloc);
}

LogExprNode *
log_expr_node_new_parser_reference(const gchar *name, CFG_LTYPE *yylloc)
{
  return log_expr_node_new(ENL_REFERENCE, ENC_PARSER, name, NULL, 0, yylloc);
}

LogExprNode *
log_expr_node_new_rewrite(const gchar *name, LogExprNode *children, CFG_LTYPE *yylloc)
{
  return log_expr_node_new(ENL_SEQUENCE, ENC_REWRITE, name, children, 0, yylloc);
}

LogExprNode *
log_expr_node_new_rewrite_reference(const gchar *name, CFG_LTYPE *yylloc)
{
  return log_expr_node_new(ENL_REFERENCE, ENC_REWRITE, name, NULL, 0, yylloc);
}

LogExprNode *
log_expr_node_new_log(LogExprNode *children, guint32 flags, CFG_LTYPE *yylloc)
{
  return log_expr_node_new(ENL_SEQUENCE, ENC_PIPE, NULL, children, flags, yylloc);
}

LogExprNode *
log_expr_node_new_sequence(LogExprNode *children, CFG_LTYPE *yylloc)
{
  return log_expr_node_new(ENL_SEQUENCE, ENC_PIPE, NULL, children, 0, yylloc);
}

LogExprNode *
log_expr_node_new_junction(LogExprNode *children, CFG_LTYPE *yylloc)
{
  return log_expr_node_new(ENL_JUNCTION, ENC_PIPE, NULL, children, 0, yylloc);
}

/****************************************************************************
 * Functions related to conditional nodes
 *
 * These are higher-level functions that map if-elif-else structure to
 * LogExprNode instances.  Rather than using a higher level data struct
 * which then generates LogExprNode instances, we represent/manipulate the
 * if-elif-else structure right within LogExprNode.
 *
 * A conditional node is simply a junction with two children:
 *
 *     1) the first child is the "TRUE" branch, with the filter expression
 *     attached and the final flag
 *
 *     2) the second child is the "FALSE" branch, possibly empty, but also
 *     the final flag.
 *
 * Basically this is the equivalent to:
 *
 *     junction {
 *       channel {
 *         filter { EXPRESSION; };
 *         flags(final);
 *       };
 *       channel {
 *         flags(final);
 *       };
 *     };
 *
 * When parsing an if block, we generate both children immediately, with the
 * empty 2nd channel, and then if an elif or else comes, the FALSE branch
 * gets replaced.
 *
 * The series of if-elif-else sequences is represented by its first
 * LogExprNode (e.g.  the first if).  When we need to add an else or elif,
 * we would have to locate the last dangling if statement based on this
 * first LogExprNode.  The alternative would have been to store the last if
 * statement in a variable, however that becomes pretty complicated if we
 * need to handle nesting.
 *
 ****************************************************************************/

static LogExprNode *
log_expr_node_conditional_get_true_branch(LogExprNode *node)
{
  g_assert(node->layout == ENL_JUNCTION);

  LogExprNode *branches = node->children;

  g_assert(branches != NULL);
  g_assert(branches->next != NULL);
  g_assert(branches->next->next == NULL);

  /* first child */
  return branches;
}

static LogExprNode *
log_expr_node_conditional_get_false_branch(LogExprNode *node)
{
  g_assert(node->layout == ENL_JUNCTION);

  LogExprNode *branches = node->children;
  g_assert(branches != NULL);
  g_assert(branches->next != NULL);
  g_assert(branches->next->next == NULL);

  /* second child */
  return branches->next;
}

static gboolean
log_expr_node_conditional_is_branch_empty(LogExprNode *node)
{
  return node->children == NULL;
}

/* this function locates the last dangling if, based on the very first if
 * statement in a series of ifs at the same level */
static LogExprNode *
_locate_last_conditional_along_nested_else_blocks(LogExprNode *head)
{
  while (1)
    {
      g_assert(log_expr_node_conditional_get_true_branch(head) != NULL);
      g_assert(log_expr_node_conditional_get_false_branch(head) != NULL);

      LogExprNode *false_branch = log_expr_node_conditional_get_false_branch(head);

      /* check if this is the last else */
      if (log_expr_node_conditional_is_branch_empty(false_branch))
        return head;
      head = false_branch->children;
    }
  g_assert_not_reached();
}

/* change the FALSE branch (e.g. the else case) of the last dangling if, specified by the head element */
void
log_expr_node_conditional_set_false_branch_of_the_last_if(LogExprNode *conditional_head_node, LogExprNode *false_expr)
{
  LogExprNode *conditional_node = _locate_last_conditional_along_nested_else_blocks(conditional_head_node);
  LogExprNode *branches = conditional_node->children;

  /* a conditional branch always have two children (see the constructor
   * below), the first one is the "true" branch and the second one is the
   * "false" branch, as they are constructed as final log channels with
   * filter statement in the first one as the "if" expression.  */

  /* assert that we only have two children */
  g_assert(branches != NULL);
  g_assert(branches->next != NULL);
  g_assert(branches->next->next == NULL);


  /* construct the new false branch */
  LogExprNode *false_branch = log_expr_node_new_log(
                                false_expr,
                                log_expr_node_lookup_flag("final"),
                                NULL
                              );

  /* unlink and free the old one */
  LogExprNode *old_false_branch = branches->next;
  branches->next = false_branch;
  false_branch->parent = conditional_node;
  log_expr_node_unref(old_false_branch);
}

/*
 */
LogExprNode *
log_expr_node_new_conditional_with_filter(LogExprNode *filter_pipe, LogExprNode *true_expr, CFG_LTYPE *yylloc)
{
  LogExprNode *filter_node = log_expr_node_new_filter(NULL, filter_pipe, NULL);

  /*
   *  channel {
   *    filter { EXPRESSION };
   *    true_expr;
   *    flags(final);
   *  };
   */
  LogExprNode *true_branch = log_expr_node_new_log(
                               log_expr_node_append_tail(
                                 filter_node,
                                 log_expr_node_new_log(true_expr, LC_DROP_UNMATCHED, NULL)
                               ),
                               LC_FINAL,
                               NULL
                             );

  /*
   *  channel {
   *    flags(final);
   *  };
   *
   * NOTE: the false branch may be modified later, once an else or elif is
   * encountered in the configuration, see
   * log_expr_node_conditional_set_false_branch_of_the_last_if() function
   * above.
   */
  LogExprNode *false_branch = log_expr_node_new_log(
                                NULL,
                                LC_FINAL,
                                NULL
                              );
  return log_expr_node_new_junction(
           log_expr_node_append_tail(true_branch, false_branch),
           yylloc
         );
}

LogExprNode *
log_expr_node_new_conditional_with_block(LogExprNode *block, CFG_LTYPE *yylloc)
{
  /*
   *  channel {
   *    filtering_and_parsing_expr;
   *    flags(final);
   *  };
   */
  LogExprNode *true_branch = log_expr_node_new_log(
                               block,
                               LC_FINAL,
                               NULL
                             );

  /*
   *  channel {
   *    flags(final);
   *  };
   *
   * NOTE: the false branch may be modified later, once an else or elif is
   * encountered in the configuration, see
   * log_expr_node_conditional_set_false_branch_of_the_last_if() function
   * above.
   */
  LogExprNode *false_branch = log_expr_node_new_log(
                                NULL,
                                LC_FINAL,
                                NULL
                              );
  return log_expr_node_new_junction(
           log_expr_node_append_tail(true_branch, false_branch),
           yylloc
         );
}

/****************************************************************************/

gint
log_expr_node_lookup_flag(const gchar *flag)
{
  if (strcmp(flag, "catch-all") == 0 || strcmp(flag, "catchall") == 0)
    return LC_CATCHALL;
  else if (strcmp(flag, "fallback") == 0)
    return LC_FALLBACK;
  else if (strcmp(flag, "final") == 0)
    return LC_FINAL;
  else if (strcmp(flag, "flow-control") == 0)
    return LC_FLOW_CONTROL;
  else if (strcmp(flag, "drop-unmatched") == 0)
    return LC_DROP_UNMATCHED;
  msg_error("Unknown log statement flag", evt_tag_str("flag", flag));
  return 0;
}

LogPipe *
cfg_tree_new_pipe(CfgTree *self, LogExprNode *related_expr)
{
  LogPipe *pipe = log_pipe_new(self->cfg);
  pipe->expr_node = related_expr;
  g_ptr_array_add(self->initialized_pipes, pipe);
  log_pipe_add_info(pipe, "cfg_tree_pipe");
  return pipe;
}

LogMultiplexer *
cfg_tree_new_mpx(CfgTree *self, LogExprNode *related_expr)
{
  LogMultiplexer *pipe = log_multiplexer_new(self->cfg);
  pipe->super.expr_node = related_expr;
  g_ptr_array_add(self->initialized_pipes, pipe);
  return pipe;
}

static gchar *
_format_anon_rule_name(CfgTree *self, gint content)
{
  return g_strdup_printf("#anon-%s%d", log_expr_node_get_content_name(content), self->anon_counters[content]++);
}

/*
 * Return the name of the rule that contains a LogExprNode. Generates
 * one automatically for anonymous log expressions.
 *
 * NOTE: this returns an allocated string, the caller must free that.
 */
gchar *
cfg_tree_get_rule_name(CfgTree *self, gint content, LogExprNode *node)
{
  gchar *rule_name;

  if (node)
    {
      LogExprNode *rule = log_expr_node_get_container_rule(node, content);
      if (!rule->name)
        rule->name = _format_anon_rule_name(self, content);
      rule_name = g_strdup(rule->name);
    }
  else
    {
      rule_name = _format_anon_rule_name(self, content);
    }

  return rule_name;
}

/*
 * Return a unique the name associated with a LogExprNode. It is of
 * the format <rule>#<seqid>.
 *
 * NOTE: this returns an allocated string, the caller must free that.
 */
gchar *
cfg_tree_get_child_id(CfgTree *self, gint content, LogExprNode *node)
{
  gchar *rule_name = cfg_tree_get_rule_name(self, content, node);
  gint cur_child_id;
  gchar *res;

  if (node)
    {
      LogExprNode *rule = log_expr_node_get_container_rule(node, content);
      cur_child_id = rule->child_id++;
    }
  else
    cur_child_id = 0;
  res = g_strdup_printf("%s#%d", rule_name, cur_child_id);
  g_free(rule_name);
  return res;
}

/* hash foreach function to add all source objects to catch-all rules */
static void
cfg_tree_add_all_sources(gpointer key, gpointer value, gpointer user_data)
{
  gpointer *args = (gpointer *) user_data;
  LogExprNode *referring_rule = args[1];
  LogExprNode *rule = (LogExprNode *) value;

  if (rule->content != ENC_SOURCE)
    return;

  /* prepend a source reference */
  referring_rule->children = log_expr_node_append_tail(log_expr_node_new_source_reference(rule->name, NULL),
                                                       referring_rule->children);
}

static gboolean
cfg_tree_compile_node(CfgTree *self, LogExprNode *node,
                      LogPipe **outer_pipe_head, LogPipe **outer_pipe_tail);

static gboolean
cfg_tree_compile_single(CfgTree *self, LogExprNode *node,
                        LogPipe **outer_pipe_head, LogPipe **outer_pipe_tail)
{
  LogPipe *pipe;

  g_assert(node->content == ENC_PIPE);

  /* LC_XXX flags are currently only implemented for sequences, ensure that the grammar enforces this. */
  g_assert(node->flags == 0);

  pipe = node->object;

  if ((pipe->flags & PIF_INLINED) == 0)
    {
      /* first reference to the pipe uses the same instance, further ones will get cloned */
      pipe->flags |= PIF_INLINED;
      /* The pipe object is borrowed, so the reference counter must be increased. */
      log_pipe_ref(pipe);
    }
  else
    {
      /* ok, we are using this pipe again, it needs to be cloned */
      pipe = log_pipe_clone(pipe);
      if (!pipe)
        {
          msg_error("Error cloning pipe into its reference point, probably the element in question is not meant to be used in this situation",
                    log_expr_node_location_tag(node));
          goto error;
        }
      pipe->flags |= PIF_INLINED;
    }
  g_ptr_array_add(self->initialized_pipes, pipe);
  pipe->expr_node = node;

  if ((pipe->flags & PIF_SOURCE) == 0)
    *outer_pipe_head = pipe;
  *outer_pipe_tail = pipe;
  return TRUE;

error:
  return FALSE;
}

static gboolean
cfg_tree_compile_reference(CfgTree *self, LogExprNode *node,
                           LogPipe **outer_pipe_head, LogPipe **outer_pipe_tail)
{
  LogExprNode *referenced_node;

  /* LC_XXX flags are currently only implemented for sequences, ensure that the grammar enforces this. */
  g_assert(node->flags == 0);

  if (!node->object)
    {
      referenced_node = cfg_tree_get_object(self, node->content, node->name);
    }
  else
    referenced_node = node->object;

  if (!referenced_node)
    {
      msg_error("Error resolving reference",
                evt_tag_str("content", log_expr_node_get_content_name(node->content)),
                evt_tag_str("name", node->name),
                log_expr_node_location_tag(node));
      goto error;
    }

  switch (referenced_node->content)
    {
    case ENC_SOURCE:
    {
      LogMultiplexer *mpx;
      LogPipe *sub_pipe_head = NULL, *sub_pipe_tail = NULL;
      LogPipe *attach_pipe = NULL;

      if (!referenced_node->aux)
        {
          if (!cfg_tree_compile_node(self, referenced_node, &sub_pipe_head, &sub_pipe_tail))
            goto error;
          log_expr_node_set_aux(referenced_node, log_pipe_ref(sub_pipe_tail), (GDestroyNotify) log_pipe_unref);
        }
      else
        {
          sub_pipe_tail = referenced_node->aux;
        }

      attach_pipe = cfg_tree_new_pipe(self, node);

      if (sub_pipe_tail)
        {
          /* when the source is empty, we'll get a NULL tail in
           * sub_pipe_tail.  We handle that by simply not connecting
           * anything to the attachment point */

          if (!sub_pipe_tail->pipe_next)
            {
              mpx = cfg_tree_new_mpx(self, referenced_node);
              log_pipe_add_info(&mpx->super, "mpx(source)");
              log_pipe_append(sub_pipe_tail, &mpx->super);
            }
          else
            {
              mpx = (LogMultiplexer *) sub_pipe_tail->pipe_next;
            }
          log_multiplexer_add_next_hop(mpx, attach_pipe);
        }
      *outer_pipe_head = NULL;
      *outer_pipe_tail = attach_pipe;
      break;
    }
    case ENC_DESTINATION:
    {
      LogMultiplexer *mpx;
      LogPipe *sub_pipe_head = NULL, *sub_pipe_tail = NULL;

      if (!referenced_node->aux)
        {
          if (!cfg_tree_compile_node(self, referenced_node, &sub_pipe_head, &sub_pipe_tail))
            goto error;
          log_expr_node_set_aux(referenced_node, log_pipe_ref(sub_pipe_head), (GDestroyNotify) log_pipe_unref);
        }
      else
        {
          sub_pipe_head = referenced_node->aux;
        }

      /* We need a new LogMultiplexer instance for two reasons:

         1) we need to link something into the sequence, all
         reference based destination invocations need a separate
         LogPipe

         2) we have to fork downwards to the destination, it may
         change the message but we need the original one towards
         our next chain
      */

      mpx = cfg_tree_new_mpx(self, node);
      log_pipe_add_info(&mpx->super, "mpx(destination-reference)");

      if (sub_pipe_head)
        {
          /* when the destination is empty */
          log_multiplexer_add_next_hop(mpx, sub_pipe_head);
        }
      *outer_pipe_head = &mpx->super;
      *outer_pipe_tail = NULL;
      break;
    }
    default:
      return cfg_tree_compile_node(self, referenced_node, outer_pipe_head, outer_pipe_tail);
    }
  return TRUE;

error:
  return FALSE;
}

static void
cfg_tree_propagate_expr_node_properties_to_pipe(LogExprNode *node, LogPipe *pipe)
{
  if (node->flags & LC_FALLBACK)
    pipe->flags |= PIF_BRANCH_FALLBACK;

  if (node->flags & LC_FINAL)
    pipe->flags |= PIF_BRANCH_FINAL;

  if (node->flags & LC_FLOW_CONTROL)
    pipe->flags |= PIF_HARD_FLOW_CONTROL;

  if (node->flags & LC_DROP_UNMATCHED)
    pipe->flags |= PIF_DROP_UNMATCHED;

  if (!pipe->expr_node)
    pipe->expr_node = node;
}

/**
 * cfg_tree_compile_sequence:
 *
 * Construct the sequential part of LogPipe pipeline as specified by
 * the user. The sequential part is where no branches exist, pipes are
 * merely linked to each other. This is in contrast with a "junction"
 * where the processing is forked into different branches. Junctions
 * are built using cfg_tree_compile_junction() above.
 *
 * The configuration is parsed into a series of LogExprNode
 * elements, each giving a reference to a source, filter, parser,
 * rewrite and destination. This function connects these so that their
 * log_pipe_queue() method will dispatch the message correctly (which
 * in turn boils down to setting the LogPipe->next member).
 *
 * The tree like structure is created using LogMultiplexer instances,
 * pipes are connected back with a simple LogPipe instance that only
 * forwards messages.
 *
 * The next member pointer is not holding a reference, but can be
 * assumed to be kept alive as long as the configuration is running.
 *
 * Parameters:
 * @self: the CfgTree instance
 * @rule: the series of LogExprNode instances encapsulates as a LogExprNode
 * @outer_pipe_tail: the last LogPipe to be used to chain further elements to this sequence
 * @cfg: GlobalConfig instance
 * @toplevel: whether this rule is a top-level one.
 **/
static gboolean
cfg_tree_compile_sequence(CfgTree *self, LogExprNode *node,
                          LogPipe **outer_pipe_head, LogPipe **outer_pipe_tail)
{
  LogExprNode *ep;
  LogPipe
  *first_pipe,   /* the head of the constructed pipeline */
  *last_pipe;    /* the current tail of the constructed pipeline */
  LogPipe *source_join_pipe = NULL;
  gboolean node_properties_propagated = FALSE;

  if ((node->flags & LC_CATCHALL) != 0)
    {
      /* the catch-all resolution code clears this flag */

      msg_error("Error in configuration, catch-all flag can only be specified for top-level log statements");
      goto error;
    }

  /* the loop below creates a sequence of LogPipe instances which
   * essentially execute the user configuration once it is
   * started.
   *
   * The input of this is a log expression, denoted by a tree of
   * LogExprNode structures, built by the parser. We are storing the
   * sequence as a linked list, pipes are linked with their "next"
   * field.
   *
   * The head of this list is pointed to by @first_pipe, the current
   * end is known as @last_pipe.
   *
   * In case the sequence starts with a source LogPipe (PIF_SOURCE
   * flag), the head of the list is _not_ tracked, in that case
   * first_pipe is NULL.
   *
   */

  first_pipe = last_pipe = NULL;

  for (ep = node->children; ep; ep = ep->next)
    {
      LogPipe *sub_pipe_head = NULL, *sub_pipe_tail = NULL;

      if (!cfg_tree_compile_node(self, ep, &sub_pipe_head, &sub_pipe_tail))
        goto error;

      /* add pipe to the current pipe_line, e.g. after last_pipe, update last_pipe & first_pipe */
      if (sub_pipe_head)
        {
          if (!node_properties_propagated)
            {
              cfg_tree_propagate_expr_node_properties_to_pipe(node, sub_pipe_head);
              node_properties_propagated = TRUE;
            }
          if (!first_pipe && !last_pipe)
            {
              /* we only remember the first pipe in case we're not in
               * source mode. In source mode, only last_pipe is set */

              first_pipe = sub_pipe_head;
            }

          if (last_pipe)
            {
              g_assert(last_pipe->pipe_next == NULL);
              log_pipe_append(last_pipe, sub_pipe_head);
            }

          if (sub_pipe_tail)
            {
              last_pipe = sub_pipe_tail;
            }
          else
            {
              last_pipe = sub_pipe_head;
              /* look for the final pipe */
              while (last_pipe->pipe_next)
                {
                  last_pipe = last_pipe->pipe_next;
                }
            }
          sub_pipe_head = NULL;
        }
      else if (sub_pipe_tail)
        {
          /* source pipe */

          if (first_pipe)
            {
              msg_error("Error compiling sequence, source-pipe follows a non-source one, please list source references/definitions first",
                        log_expr_node_location_tag(ep));
              goto error;
            }

          if (!source_join_pipe)
            {
              source_join_pipe = last_pipe = cfg_tree_new_pipe(self, node);
            }
          log_pipe_append(sub_pipe_tail, source_join_pipe);
        }
    }



  /* NOTE: if flow control is enabled, then we either need to have an
   * embedded log statement (in which case first_pipe is set, as we're not
   * starting with sources), OR we created a source_join_pipe already.
   *
   */

  g_assert(((node->flags & LC_FLOW_CONTROL) && (first_pipe || source_join_pipe)) ||
           !(node->flags & LC_FLOW_CONTROL));

  if (!first_pipe && !last_pipe)
    {
      /* this is an empty sequence, insert a do-nothing LogPipe */
      first_pipe = last_pipe = cfg_tree_new_pipe(self, node);
    }

  if (!node_properties_propagated)
    {
      /* we never encountered anything that would produce a head_pipe, e.g.
       * this sequence only contains a source and nothing else.  In that
       * case, apply node flags to the last pipe.  It should be picked up
       * when LogMultiplexer iterates over the branch in
       * log_multiplexer_init() as long as last_pipe is linked into the
       * pipe_next list and is not forked off at a LogMultiplexer.
       * */

      cfg_tree_propagate_expr_node_properties_to_pipe(node, last_pipe);
      node_properties_propagated = TRUE;
    }

  if (node->content == ENC_DESTINATION)
    {
      /* We want to leave pipe-next available to use for
         destinations. Config graph uses pipe-next to pass messages forward
         in a sequence layout. But pipe-next might be overridden (for
         example network destination with LogWriter) hence disjointing
         the config graph.

         This patch links destinations in T form, instead of single link.

         * (endpoint of sequence)
         |
         V
         * (multiplexer) -(next-hop)-> destination -(pipe-next*)-> logwriter
         |
         (pipe-next)
         |
         V
         * (rest of the sequence)
         That way destinations are free to use the pipe-next*
      */

      last_pipe = first_pipe;
    }

  *outer_pipe_tail = last_pipe;
  *outer_pipe_head = first_pipe;
  return TRUE;
error:

  /* we don't need to free anything, everything we allocated is recorded in
   * @self, thus will be freed whenever cfg_tree_free is called */

  return FALSE;
}

/**
 * cfg_tree_compile_junction():
 *
 * This function builds a junction within the configuration. A
 * junction is where processing is forked into several branches, each
 * doing its own business, and then the end of each branch is
 * collected at the end so that further processing can be done on the
 * combined output of each log branch.
 *
 *       /-- branch --\
 *      /              \
 *  ---+---- branch ----+---
 *      \              /
 *       \-- branch --/
 **/
static gboolean
cfg_tree_compile_junction(CfgTree *self,
                          LogExprNode *node,
                          LogPipe **outer_pipe_head, LogPipe **outer_pipe_tail)
{
  LogExprNode *ep;
  LogPipe *join_pipe = NULL;    /* the pipe where parallel branches are joined in a junction */
  LogMultiplexer *fork_mpx = NULL;

  /* LC_XXX flags are currently only implemented for sequences, ensure that the grammar enforces this. */
  g_assert(node->flags == 0);

  for (ep = node->children; ep; ep = ep->next)
    {
      LogPipe *sub_pipe_head = NULL, *sub_pipe_tail = NULL;
      gboolean is_first_branch = (ep == node->children);

      if (!cfg_tree_compile_node(self, ep, &sub_pipe_head, &sub_pipe_tail))
        goto error;

      if (sub_pipe_head)
        {
          /* ep is an intermediate LogPipe or a destination, we have to fork */

          if (!is_first_branch && !fork_mpx)
            {
              msg_error("Error compiling junction, source and non-source branches are mixed",
                        log_expr_node_location_tag(ep));
              goto error;
            }
          if (!fork_mpx)
            {
              fork_mpx = cfg_tree_new_mpx(self, node);
              log_pipe_add_info(&fork_mpx->super, "mpx(junction)");
              *outer_pipe_head = &fork_mpx->super;
            }
          log_multiplexer_add_next_hop(fork_mpx, sub_pipe_head);
        }
      else
        {
          /* ep is a "source" LogPipe (cause no sub_pipe_head returned by compile_node). */

          if (fork_mpx)
            {
              msg_error("Error compiling junction, source and non-source branches are mixed",
                        log_expr_node_location_tag(ep));
              goto error;
            }
        }

      if (sub_pipe_tail && outer_pipe_tail)
        {
          if (!join_pipe)
            {
              join_pipe = cfg_tree_new_pipe(self, node);
            }
          log_pipe_append(sub_pipe_tail, join_pipe);

        }
    }

  if (outer_pipe_tail)
    *outer_pipe_tail = join_pipe;
  return TRUE;
error:

  /* we don't need to free anything, everything we allocated is recorded in
   * @self, thus will be freed whenever cfg_tree_free is called */

  return FALSE;
}

/*
 * cfg_tree_compile_node:
 *
 * This function takes care of compiling a LogExprNode.
 *
 */
gboolean
cfg_tree_compile_node(CfgTree *self, LogExprNode *node,
                      LogPipe **outer_pipe_head, LogPipe **outer_pipe_tail)
{
  gboolean result = FALSE;
  static gint indent = -1;

  if (trace_flag)
    {
      gchar buf[128];
      gchar compile_message[256];

      indent++;
      g_snprintf(compile_message, sizeof(compile_message),
                 "%-*sCompiling %s %s [%s] at [%s]",
                 indent * 2, "",
                 node->name ? : "#unnamed",
                 log_expr_node_get_layout_name(node->layout),
                 log_expr_node_get_content_name(node->content),
                 log_expr_node_format_location(node, buf, sizeof(buf)));
      msg_send_formatted_message(EVT_PRI_DEBUG, compile_message);
    }

  switch (node->layout)
    {
    case ENL_SINGLE:
      result = cfg_tree_compile_single(self, node, outer_pipe_head, outer_pipe_tail);
      break;
    case ENL_REFERENCE:
      result = cfg_tree_compile_reference(self, node, outer_pipe_head, outer_pipe_tail);
      break;
    case ENL_SEQUENCE:
      result = cfg_tree_compile_sequence(self, node, outer_pipe_head, outer_pipe_tail);
      break;
    case ENL_JUNCTION:
      result = cfg_tree_compile_junction(self, node, outer_pipe_head, outer_pipe_tail);
      break;
    default:
      g_assert_not_reached();
    }

  indent--;
  return result;
}

gboolean
cfg_tree_compile_rule(CfgTree *self, LogExprNode *rule)
{
  LogPipe *sub_pipe_head = NULL, *sub_pipe_tail = NULL;

  return cfg_tree_compile_node(self, rule, &sub_pipe_head, &sub_pipe_tail);
}

static gboolean
cfg_tree_objects_equal(gconstpointer v1, gconstpointer v2)
{
  LogExprNode *r1 = (LogExprNode *) v1;
  LogExprNode *r2 = (LogExprNode *) v2;

  if (r1->content != r2->content)
    return FALSE;

  /* we assume that only rules with a name are hashed */

  return strcmp(r1->name, r2->name) == 0;
}

static guint
cfg_tree_objects_hash(gconstpointer v)
{
  LogExprNode *r = (LogExprNode *) v;

  /* we assume that only rules with a name are hashed */
  return r->content + g_str_hash(r->name);
}

gboolean
cfg_tree_add_object(CfgTree *self, LogExprNode *rule)
{
  gboolean res = TRUE;

  if (rule->name)
    {
      /* only named rules can be stored as objects to be referenced later */

      /* check if already present */
      res = (g_hash_table_lookup(self->objects, rule) == NULL);

      /* key is the same as the object */
      g_hash_table_replace(self->objects, rule, rule);
    }
  else
    {
      /* unnamed rules are simply put in the rules array */
      g_ptr_array_add(self->rules, rule);
    }

  return res;
}

LogExprNode *
cfg_tree_get_object(CfgTree *self, gint content, const gchar *name)
{
  LogExprNode lookup_node;

  memset(&lookup_node, 0, sizeof(lookup_node));
  lookup_node.content = content;
  lookup_node.name = (gchar *) name;

  return g_hash_table_lookup(self->objects, &lookup_node);
}

GList *
cfg_tree_get_objects(CfgTree *self)
{
  return g_hash_table_get_values(self->objects);
}

gboolean
cfg_tree_add_template(CfgTree *self, LogTemplate *template)
{
  gboolean res = (g_hash_table_lookup(self->templates, template->name) == NULL);
  g_hash_table_replace(self->templates, template->name, template);
  return res;
}

LogTemplate *
cfg_tree_lookup_template(CfgTree *self, const gchar *name)
{
  if (name)
    return log_template_ref(g_hash_table_lookup(self->templates, name));
  return NULL;
}

LogTemplate *
cfg_tree_check_inline_template(CfgTree *self, const gchar *template_or_name, GError **error)
{
  LogTemplate *template = cfg_tree_lookup_template(self, template_or_name);

  if (template == NULL)
    {
      template = log_template_new(self->cfg, NULL);
      if (!log_template_compile(template, template_or_name, error))
        {
          log_template_unref(template);
          return NULL;
        }
      template->def_inline = TRUE;
    }
  return template;
}

gboolean
_count_sources(LogExprNode *node, gpointer user_data)
{
  gint *counter = (gint *)user_data;

  if (node->content == ENC_SOURCE)
    ++(*counter);

  return TRUE;
}

static void
cfg_tree_foreach(LogExprNode *node, gboolean (*func)(LogExprNode *, gpointer user_data), gpointer user_data)
{
  g_assert(func != NULL);
  g_assert(node != NULL);

  if (!func(node, user_data))
    return;

  if (node->children)
    cfg_tree_foreach(node->children, func, user_data);

  if (node->next)
    cfg_tree_foreach(node->next, func, user_data);
}

static gboolean
cfg_tree_check_empty_pipelines(CfgTree *self)
{
  gint source_counter = 0;

  for (gint i = 0; i < self->rules->len; i++)
    {
      LogExprNode *rule = (LogExprNode *) g_ptr_array_index(self->rules, i);

      cfg_tree_foreach(rule, _count_sources, &source_counter);
      if (source_counter > 0)
        return TRUE;
    }

  return (source_counter > 0);
}

gboolean
cfg_tree_compile(CfgTree *self)
{
  gint i;

  /* resolve references within the configuration */
  if (self->compiled)
    return TRUE;

  for (i = 0; i < self->rules->len; i++)
    {
      LogExprNode *rule = (LogExprNode *) g_ptr_array_index(self->rules, i);

      if ((rule->flags & LC_CATCHALL))
        {
          gpointer args[] = { self, rule };

          g_hash_table_foreach(self->objects, cfg_tree_add_all_sources, args);
          rule->flags &= ~LC_CATCHALL;
        }

      if (!cfg_tree_compile_rule(self, rule))
        {
          return FALSE;
        }
    }

  if (self->check_zero_source_count && !cfg_tree_check_empty_pipelines(self))
    {
      msg_error("There are no source configured in any of the logpaths, there is nothing for syslog-ng to do");
      return FALSE;
    }

  self->compiled = TRUE;
  return TRUE;
}

static gboolean
_verify_unique_persist_names_among_pipes(const GPtrArray *initialized_pipes)
{
  GHashTable *pipe_persist_names = g_hash_table_new(g_str_hash, g_str_equal);
  gboolean result = TRUE;

  for (gint i = 0; i < initialized_pipes->len; ++i)
    {
      LogPipe *current_pipe = g_ptr_array_index(initialized_pipes, i);
      const gchar *current_pipe_name = log_pipe_get_persist_name(current_pipe);

      if (current_pipe_name != NULL)
        {
          if (g_hash_table_lookup_extended(pipe_persist_names, current_pipe_name, NULL, NULL))
            {
              msg_error("Error checking the uniqueness of the persist names, please override it "
                        "with persist-name option. Shutting down.",
                        evt_tag_str("persist_name", current_pipe_name),
                        log_pipe_location_tag(current_pipe), NULL);
              result = FALSE;
            }
          else
            {
              g_hash_table_replace(pipe_persist_names,
                                   (gpointer)current_pipe_name,
                                   (gpointer)current_pipe_name);
            }
        }
    }

  g_hash_table_destroy(pipe_persist_names);

  return result;
}

gboolean
cfg_tree_start(CfgTree *self)
{
  gint i;

  if (!cfg_tree_compile(self))
    return FALSE;

  /*
   *   As there are pipes that are dynamically created during init, these
   *   pipes must be deinited before destroying the configuration, otherwise
   *   circular references will inhibit the free of the configuration
   *   structure.
   */
  for (i = 0; i < self->initialized_pipes->len; i++)
    {
      LogPipe *pipe = g_ptr_array_index(self->initialized_pipes, i);

      if (!log_pipe_init(pipe))
        {
          msg_error("Error initializing message pipeline",
                    evt_tag_str("plugin_name", pipe->plugin_name ? pipe->plugin_name : "not a plugin"),
                    log_pipe_location_tag(pipe));
          return FALSE;
        }
    }

  return _verify_unique_persist_names_among_pipes(self->initialized_pipes);
}

gboolean
cfg_tree_stop(CfgTree *self)
{
  gboolean success = TRUE;
  gint i;

  for (i = 0; i < self->initialized_pipes->len; i++)
    {
      if (!log_pipe_deinit(g_ptr_array_index(self->initialized_pipes, i)))
        success = FALSE;
    }

  return success;
}

gboolean
cfg_tree_on_inited(CfgTree *self)
{
  gint i;

  for (i = 0; i < self->initialized_pipes->len; i++)
    {
      LogPipe *pipe = g_ptr_array_index(self->initialized_pipes, i);

      if (!log_pipe_on_config_inited(pipe))
        {
          msg_error("Error executing on_config_inited hook",
                    evt_tag_str("plugin_name", pipe->plugin_name ? pipe->plugin_name : "not a plugin"),
                    log_pipe_location_tag(pipe));
          return FALSE;
        }
    }

  return TRUE;
}

void
cfg_tree_init_instance(CfgTree *self, GlobalConfig *cfg)
{
  memset(self, 0, sizeof(*self));
  self->initialized_pipes = g_ptr_array_new();
  self->objects = g_hash_table_new_full(cfg_tree_objects_hash, cfg_tree_objects_equal, NULL,
                                        (GDestroyNotify) log_expr_node_unref);
  self->templates = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify) log_template_unref);
  self->rules = g_ptr_array_new();
  self->cfg = cfg;
  self->check_zero_source_count = TRUE;
}

void
cfg_tree_free_instance(CfgTree *self)
{
  g_ptr_array_foreach(self->initialized_pipes, (GFunc) log_pipe_unref, NULL);
  g_ptr_array_free(self->initialized_pipes, TRUE);

  g_ptr_array_foreach(self->rules, (GFunc) log_expr_node_unref, NULL);
  g_ptr_array_free(self->rules, TRUE);

  g_hash_table_destroy(self->objects);
  g_hash_table_destroy(self->templates);
  self->cfg = NULL;
}
