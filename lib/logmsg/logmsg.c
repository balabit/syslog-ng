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

#include "logmsg/logmsg.h"
#include "str-utils.h"
#include "str-repr/encode.h"
#include "messages.h"
#include "logpipe.h"
#include "timeutils/cache.h"
#include "timeutils/misc.h"
#include "logmsg/nvtable.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-single.h"
#include "template/templates.h"
#include "tls-support.h"
#include "compat/string.h"
#include "rcptid.h"
#include "template/macros.h"
#include "host-id.h"
#include "ack-tracker/ack_tracker.h"
#include "apphook.h"

#include <glib/gprintf.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <syslog.h>

/*
 * Reference/ACK counting for LogMessage structures
 *
 * Each LogMessage structure is allocated when received by a LogSource
 * instance, and then freed once all destinations finish with it.  Since a
 * LogMessage is processed by different threads, reference counting must be
 * atomic.
 *
 * A similar counter is used to track when a given message is considered to
 * be delivered.  In case flow-control is in use, the number of
 * to-be-expected ACKs are counted in an atomic variable.
 *
 * Code is written in a way, that it is explicitly mentioned whether a
 * function expects a reference (called "consuming" the ref), or whether it
 * gets a 'borrowed' reference (more common).  Also, a function that returns
 * a LogMessage instance generally returns a reference too.
 *
 * Because of these rules, quite a number of refs/unrefs are being made
 * during the processing of a message, even though they wouldn't cause the
 * structure to be freed.  Considering that ref/unref operations are
 * expensive atomic operations, these have a considerable overhead.
 *
 * The solution we employ in this module, is that we try to exploit the
 * fact, that each thread is usually working on a single LogMessage
 * instance, and once it is done with it, it fetches the next one, and so
 * on.  In the LogReader/LogWriter cases, this usage pattern is quite
 * normal.
 *
 * Also, a quite similar usage pattern can be applied to the ACK counter
 * (the one which tracks how much flow-controlled destinations need to
 * confirm the deliver of the message).
 *
 * The solution implemented here is outlined below (and the same rules are
 * used for ACKs and REFs):
 *
 *    - ACKs and REFs are put into the same 32 bit integer value, one half
 *      is used for the ref counter, the other is used for the ACK counter
 *      (see the macros LOGMSG_REFCACHE_ below)
 *
 *    - The processing of a given message in a given thread is enclosed by
 *      calls to log_msg_refcache_start() / log_msg_refcache_stop()
 *
 *    - The calls to these functions is optional, if they are not called,
 *      then normal atomic reference counting is performed
 *
 *    - When refcache_start() is called, the atomic reference count is not
 *      changed by log_msg_ref()/unref(). Rather, a per-thread variable is
 *      used to count the _difference_ to the "would-be" value of the ref counter.
 *
 *    - When refcache_stop() is called, the atomic reference counter is
 *      updated as a single atomic operation. This means, that if a thread
 *      calls refcache_start() early enough, then the ref/unref operations
 *      performed by the given thread can be completely atomic-op free.
 *
 *    - There's one catch: if the producer thread (e.g. LogReader), doesn't
 *      add references to a consumer thread (e.g.  LogWriter), and the
 *      consumer doesn't use the refcache infrastructure, then the counters
 *      could become negative (simply because the reader's real number of
 *      references is not represented in the refcounter).  This is solved by
 *      adding a large-enough number (so called BIAS) to the ref counter in
 *      refcache_start(), which ensures that all possible writers will see a
 *      positive value.  This is then subtracted in refcache_stop() the
 *      same way as the other references.
 *
 * Since we use the same atomic variable to store two things, updating that
 * counter becomes somewhat more complicated, therefore a g_atomic_int_add()
 * doesn't suffice.  We're using a CAS loop (compare-and-exchange) to do our
 * stuff, but that shouldn't have that much of an overhead.
 */

TLS_BLOCK_START
{
  /* message that is being processed by the current thread. Its ack/ref changes are cached */
  LogMessage *logmsg_current;
  /* whether the consumer is flow-controlled, (the producer always is) */
  gboolean logmsg_cached_ack_needed;

  /* has to be signed as these can become negative */
  /* number of cached refs by the current thread */
  gint logmsg_cached_refs;
  /* number of cached acks by the current thread */
  gint logmsg_cached_acks;
  /* abort flag in the current thread for acks */
  gboolean logmsg_cached_abort;
  /* suspend flag in the current thread for acks */
  gboolean logmsg_cached_suspend;
}
TLS_BLOCK_END;

#define logmsg_current              __tls_deref(logmsg_current)
#define logmsg_cached_refs          __tls_deref(logmsg_cached_refs)
#define logmsg_cached_acks          __tls_deref(logmsg_cached_acks)
#define logmsg_cached_ack_needed    __tls_deref(logmsg_cached_ack_needed)
#define logmsg_cached_abort         __tls_deref(logmsg_cached_abort)
#define logmsg_cached_suspend       __tls_deref(logmsg_cached_suspend)

#define LOGMSG_REFCACHE_SUSPEND_SHIFT                 31 /* number of bits to shift to get the SUSPEND flag */
#define LOGMSG_REFCACHE_SUSPEND_MASK          0x80000000 /* bit mask to extract the SUSPEND flag */
#define LOGMSG_REFCACHE_ABORT_SHIFT                   30 /* number of bits to shift to get the ABORT flag */
#define LOGMSG_REFCACHE_ABORT_MASK            0x40000000 /* bit mask to extract the ABORT flag */
#define LOGMSG_REFCACHE_ACK_SHIFT                     15 /* number of bits to shift to get the ACK counter */
#define LOGMSG_REFCACHE_ACK_MASK              0x3FFF8000 /* bit mask to extract the ACK counter */
#define LOGMSG_REFCACHE_REF_SHIFT                      0 /* number of bits to shift to get the REF counter */
#define LOGMSG_REFCACHE_REF_MASK              0x00007FFF /* bit mask to extract the ACK counter */
#define LOGMSG_REFCACHE_BIAS                  0x00002000 /* the BIAS we add to the ref counter in refcache_start */

#define LOGMSG_REFCACHE_REF_TO_VALUE(x)    (((x) << LOGMSG_REFCACHE_REF_SHIFT)   & LOGMSG_REFCACHE_REF_MASK)
#define LOGMSG_REFCACHE_ACK_TO_VALUE(x)    (((x) << LOGMSG_REFCACHE_ACK_SHIFT)   & LOGMSG_REFCACHE_ACK_MASK)
#define LOGMSG_REFCACHE_ABORT_TO_VALUE(x)  (((x) << LOGMSG_REFCACHE_ABORT_SHIFT) & LOGMSG_REFCACHE_ABORT_MASK)
#define LOGMSG_REFCACHE_SUSPEND_TO_VALUE(x)  (((x) << LOGMSG_REFCACHE_SUSPEND_SHIFT) & LOGMSG_REFCACHE_SUSPEND_MASK)

#define LOGMSG_REFCACHE_VALUE_TO_REF(x)    (((x) & LOGMSG_REFCACHE_REF_MASK)   >> LOGMSG_REFCACHE_REF_SHIFT)
#define LOGMSG_REFCACHE_VALUE_TO_ACK(x)    (((x) & LOGMSG_REFCACHE_ACK_MASK)   >> LOGMSG_REFCACHE_ACK_SHIFT)
#define LOGMSG_REFCACHE_VALUE_TO_ABORT(x)  (((x) & LOGMSG_REFCACHE_ABORT_MASK) >> LOGMSG_REFCACHE_ABORT_SHIFT)
#define LOGMSG_REFCACHE_VALUE_TO_SUSPEND(x)  (((x) & LOGMSG_REFCACHE_SUSPEND_MASK) >> LOGMSG_REFCACHE_SUSPEND_SHIFT)

/**********************************************************************
 * LogMessage
 **********************************************************************/

static inline gboolean
log_msg_chk_flag(const LogMessage *self, gint32 flag)
{
  return self->flags & flag;
}

static inline void
log_msg_set_flag(LogMessage *self, gint32 flag)
{
  self->flags |= flag;
}

static inline void
log_msg_set_host_id(LogMessage *msg)
{
  msg->host_id = host_id_get();
}

/* the index matches the value id */
const gchar *builtin_value_names[] =
{
  "HOST",
  "HOST_FROM",
  "MESSAGE",
  "PROGRAM",
  "PID",
  "MSGID",
  "SOURCE",
  "LEGACY_MSGHDR",
  "__RESERVED_LM_V_MAX",
  NULL,
};

static NVHandle match_handles[256];
NVRegistry *logmsg_registry;
const char logmsg_sd_prefix[] = ".SDATA.";
const gint logmsg_sd_prefix_len = sizeof(logmsg_sd_prefix) - 1;
gint logmsg_queue_node_max = 1;
/* statistics */
static StatsCounterItem *count_msg_clones;
static StatsCounterItem *count_payload_reallocs;
static StatsCounterItem *count_sdata_updates;
static StatsCounterItem *count_allocated_bytes;
static GStaticPrivate priv_macro_value = G_STATIC_PRIVATE_INIT;

void
log_msg_write_protect(LogMessage *self)
{
  self->protected = TRUE;
}

LogMessage *
log_msg_make_writable(LogMessage **pself, const LogPathOptions *path_options)
{
  if (log_msg_is_write_protected(*pself))
    {
      LogMessage *new;

      new = log_msg_clone_cow(*pself, path_options);
      log_msg_unref(*pself);
      *pself = new;
    }
  return *pself;
}


static void
log_msg_update_sdata_slow(LogMessage *self, NVHandle handle, const gchar *name, gssize name_len)
{
  guint16 alloc_sdata;
  guint16 prefix_and_block_len;
  gint i;
  const gchar *dot;

  /* this was a structured data element, insert a ref to the sdata array */

  stats_counter_inc(count_sdata_updates);
  if (self->num_sdata == 255)
    {
      msg_error("syslog-ng only supports 255 SD elements right now, just drop an email to the mailing list that it was not enough with your use-case so we can increase it");
      return;
    }

  if (self->alloc_sdata <= self->num_sdata)
    {
      alloc_sdata = MAX(self->num_sdata + 1, STRICT_ROUND_TO_NEXT_EIGHT(self->num_sdata));
      if (alloc_sdata > 255)
        alloc_sdata = 255;
    }
  else
    alloc_sdata = self->alloc_sdata;

  if (log_msg_chk_flag(self, LF_STATE_OWN_SDATA) && self->sdata)
    {
      if (self->alloc_sdata < alloc_sdata)
        {
          self->sdata = g_realloc(self->sdata, alloc_sdata * sizeof(self->sdata[0]));
          memset(&self->sdata[self->alloc_sdata], 0, (alloc_sdata - self->alloc_sdata) * sizeof(self->sdata[0]));
        }
    }
  else
    {
      NVHandle *sdata;

      sdata = g_malloc(alloc_sdata * sizeof(self->sdata[0]));
      if (self->num_sdata)
        memcpy(sdata, self->sdata, self->num_sdata * sizeof(self->sdata[0]));
      memset(&sdata[self->num_sdata], 0, sizeof(self->sdata[0]) * (self->alloc_sdata - self->num_sdata));
      self->sdata = sdata;
      log_msg_set_flag(self, LF_STATE_OWN_SDATA);
    }
  guint16 old_alloc_sdata = self->alloc_sdata;
  self->alloc_sdata = alloc_sdata;
  if (self->sdata)
    {
      self->allocated_bytes += ((self->alloc_sdata - old_alloc_sdata) * sizeof(self->sdata[0]));
      stats_counter_add(count_allocated_bytes, (self->alloc_sdata - old_alloc_sdata) * sizeof(self->sdata[0]));
    }
  /* ok, we have our own SDATA array now which has at least one free slot */

  if (!self->initial_parse)
    {
      dot = memrchr(name, '.', name_len);
      prefix_and_block_len = dot - name;

      for (i = self->num_sdata - 1; i >= 0; i--)
        {
          gssize sdata_name_len;
          const gchar *sdata_name;

          sdata_name_len = 0;
          sdata_name = log_msg_get_value_name(self->sdata[i], &sdata_name_len);
          if (sdata_name_len > prefix_and_block_len &&
              strncmp(sdata_name, name, prefix_and_block_len) == 0)
            {
              /* ok we have found the last SDATA entry that has the same block */
              break;
            }
        }
      i++;
    }
  else
    i = -1;

  if (i >= 0)
    {
      memmove(&self->sdata[i+1], &self->sdata[i], (self->num_sdata - i) * sizeof(self->sdata[0]));
      self->sdata[i] = handle;
    }
  else
    {
      self->sdata[self->num_sdata] = handle;
    }
  self->num_sdata++;
}

static inline void
log_msg_update_sdata(LogMessage *self, NVHandle handle, const gchar *name, gssize name_len)
{
  guint8 flags;

  flags = nv_registry_get_handle_flags(logmsg_registry, handle);
  if (G_UNLIKELY(flags & LM_VF_SDATA))
    log_msg_update_sdata_slow(self, handle, name, name_len);
}

NVHandle
log_msg_get_value_handle(const gchar *value_name)
{
  NVHandle handle;

  handle = nv_registry_alloc_handle(logmsg_registry, value_name);

  /* check if name starts with sd_prefix and has at least one additional character */
  if (strncmp(value_name, logmsg_sd_prefix, logmsg_sd_prefix_len) == 0 && value_name[6])
    {
      nv_registry_set_handle_flags(logmsg_registry, handle, LM_VF_SDATA);
    }

  return handle;
}

gboolean
log_msg_is_value_name_valid(const gchar *value)
{
  if (strncmp(value, logmsg_sd_prefix, logmsg_sd_prefix_len) == 0)
    {
      const gchar *dot;
      int dot_found = 0;

      dot = strchr(value, '.');
      while (dot && strlen(dot) > 1)
        {
          dot_found++;
          dot++;
          dot = strchr(dot, '.');
        }
      return (dot_found >= 3);
    }
  else
    return TRUE;
}


static void
__free_macro_value(void *val)
{
  g_string_free((GString *) val, TRUE);
}

const gchar *
log_msg_get_macro_value(const LogMessage *self, gint id, gssize *value_len)
{
  GString *value;

  value = g_static_private_get(&priv_macro_value);
  if (!value)
    {
      value = g_string_sized_new(256);
      g_static_private_set(&priv_macro_value, value, __free_macro_value);
    }
  g_string_truncate(value, 0);

  log_macro_expand_simple(value, id, self);
  if (value_len)
    *value_len = value->len;
  return value->str;
}

gboolean
log_msg_is_handle_macro(NVHandle handle)
{
  guint16 flags;

  flags = nv_registry_get_handle_flags(logmsg_registry, handle);
  return !!(flags & LM_VF_MACRO);
}

gboolean
log_msg_is_handle_sdata(NVHandle handle)
{
  guint16 flags;

  flags = nv_registry_get_handle_flags(logmsg_registry, handle);
  return !!(flags & LM_VF_SDATA);
}

gboolean
log_msg_is_handle_match(NVHandle handle)
{
  g_assert(match_handles[0] && match_handles[255] && match_handles[0] < match_handles[255]);

  /* NOTE: match_handles are allocated sequentially in log_msg_registry_init(),
   * so this simple & fast check is enough */
  return (match_handles[0] <= handle && handle <= match_handles[255]);
}

static void
log_msg_init_queue_node(LogMessage *msg, LogMessageQueueNode *node, const LogPathOptions *path_options)
{
  INIT_IV_LIST_HEAD(&node->list);
  node->ack_needed = path_options->ack_needed;
  node->flow_control_requested = path_options->flow_control_requested;
  node->msg = log_msg_ref(msg);
  log_msg_write_protect(msg);
}

/*
 * Allocates a new LogMessageQueueNode instance to be enqueued in a
 * LogQueue.
 *
 * NOTE: Assumed to be running in the source thread, and that the same
 * LogMessage instance is only put into queue from the same thread (e.g.
 * the related fields are _NOT_ locked).
 */
LogMessageQueueNode *
log_msg_alloc_queue_node(LogMessage *msg, const LogPathOptions *path_options)
{
  LogMessageQueueNode *node;

  if (msg->cur_node < msg->num_nodes)
    {
      node = &msg->nodes[msg->cur_node++];
      node->embedded = TRUE;
    }
  else
    {
      gint nodes = (volatile gint) logmsg_queue_node_max;

      /* this is a racy update, but it doesn't really hurt if we lose an
       * update or if we continue with a smaller value in parallel threads
       * for some time yet, since the smaller number only means that we
       * pre-allocate somewhat less LogMsgQueueNodes in the message
       * structure, but will be fine regardless (if we'd overflow the
       * pre-allocated space, we start allocating nodes dynamically from
       * heap.
       */
      if (nodes < 32 && nodes <= msg->num_nodes)
        logmsg_queue_node_max = msg->num_nodes + 1;
      node = g_slice_new(LogMessageQueueNode);
      node->embedded = FALSE;
    }
  log_msg_init_queue_node(msg, node, path_options);
  return node;
}

LogMessageQueueNode *
log_msg_alloc_dynamic_queue_node(LogMessage *msg, const LogPathOptions *path_options)
{
  LogMessageQueueNode *node;
  node = g_slice_new(LogMessageQueueNode);
  node->embedded = FALSE;
  log_msg_init_queue_node(msg, node, path_options);
  return node;
}

void
log_msg_free_queue_node(LogMessageQueueNode *node)
{
  if (!node->embedded)
    g_slice_free(LogMessageQueueNode, node);
}

static gboolean
_log_name_value_updates(LogMessage *self)
{
  /* we don't log name value updates for internal messages that are
   * initialized at this point, as that may generate an endless recursion.
   * log_msg_new_internal() calling log_msg_set_value(), which in turn
   * generates an internal message, again calling log_msg_set_value()
   */
  return (self->flags & LF_INTERNAL) == 0;
}

static inline gboolean
_value_invalidates_legacy_header(NVHandle handle)
{
  return handle == LM_V_PROGRAM || handle == LM_V_PID;
}

void
log_msg_set_value(LogMessage *self, NVHandle handle, const gchar *value, gssize value_len)
{
  const gchar *name;
  gssize name_len;
  gboolean new_entry = FALSE;

  g_assert(!log_msg_is_write_protected(self));

  if (handle == LM_V_NONE)
    return;

  name_len = 0;
  name = log_msg_get_value_name(handle, &name_len);

  if (_log_name_value_updates(self))
    {
      msg_trace("Setting value",
                evt_tag_str("name", name),
                evt_tag_printf("value", "%.*s", (gint) value_len, value),
                evt_tag_printf("msg", "%p", self));
    }

  if (value_len < 0)
    value_len = strlen(value);

  if (!log_msg_chk_flag(self, LF_STATE_OWN_PAYLOAD))
    {
      self->payload = nv_table_clone(self->payload, name_len + value_len + 2);
      log_msg_set_flag(self, LF_STATE_OWN_PAYLOAD);
      self->allocated_bytes += self->payload->size;
      stats_counter_add(count_allocated_bytes, self->payload->size);
    }

  /* we need a loop here as a single realloc may not be enough. Might help
   * if we pass how much bytes we need though. */

  while (!nv_table_add_value(self->payload, handle, name, name_len, value, value_len, &new_entry))
    {
      /* error allocating string in payload, reallocate */
      guint32 old_size = self->payload->size;
      if (!nv_table_realloc(self->payload, &self->payload))
        {
          /* can't grow the payload, it has reached the maximum size */
          msg_info("Cannot store value for this log message, maximum size has been reached",
                   evt_tag_int("maximum_payload", NV_TABLE_MAX_BYTES),
                   evt_tag_str("name", name),
                   evt_tag_printf("value", "%.32s%s", value, value_len > 32 ? "..." : ""));
          break;
        }
      guint32 new_size = self->payload->size;
      self->allocated_bytes += (new_size - old_size);
      stats_counter_add(count_allocated_bytes, new_size-old_size);
      stats_counter_inc(count_payload_reallocs);
    }

  if (new_entry)
    log_msg_update_sdata(self, handle, name, name_len);

  if (_value_invalidates_legacy_header(handle))
    log_msg_unset_value(self, LM_V_LEGACY_MSGHDR);
}

void
log_msg_unset_value(LogMessage *self, NVHandle handle)
{
  while (!nv_table_unset_value(self->payload, handle))
    {
      /* error allocating string in payload, reallocate */
      guint32 old_size = self->payload->size;
      if (!nv_table_realloc(self->payload, &self->payload))
        {
          /* can't grow the payload, it has reached the maximum size */
          const gchar *name = log_msg_get_value_name(handle, NULL);
          msg_info("Cannot unset value for this log message, maximum size has been reached",
                   evt_tag_int("maximum_payload", NV_TABLE_MAX_BYTES),
                   evt_tag_str("name", name));
          break;
        }
      guint32 new_size = self->payload->size;
      self->allocated_bytes += (new_size - old_size);
      stats_counter_add(count_allocated_bytes, new_size-old_size);
      stats_counter_inc(count_payload_reallocs);
    }

  if (_value_invalidates_legacy_header(handle))
    log_msg_unset_value(self, LM_V_LEGACY_MSGHDR);
}

void
log_msg_unset_value_by_name(LogMessage *self, const gchar *name)
{
  log_msg_unset_value(self, log_msg_get_value_handle(name));
}

void
log_msg_set_value_indirect(LogMessage *self, NVHandle handle, NVHandle ref_handle, guint8 type, guint16 ofs,
                           guint16 len)
{
  const gchar *name;
  gssize name_len;
  gboolean new_entry = FALSE;

  g_assert(!log_msg_is_write_protected(self));

  if (handle == LM_V_NONE)
    return;

  g_assert(handle >= LM_V_MAX);

  name_len = 0;
  name = log_msg_get_value_name(handle, &name_len);

  if (_log_name_value_updates(self))
    {
      msg_trace("Setting indirect value",
                evt_tag_printf("msg", "%p", self),
                evt_tag_str("name", name),
                evt_tag_int("ref_handle", ref_handle),
                evt_tag_int("ofs", ofs),
                evt_tag_int("len", len));
    }

  if (!log_msg_chk_flag(self, LF_STATE_OWN_PAYLOAD))
    {
      self->payload = nv_table_clone(self->payload, name_len + 1);
      log_msg_set_flag(self, LF_STATE_OWN_PAYLOAD);
    }

  NVReferencedSlice referenced_slice =
  {
    .handle = ref_handle,
    .ofs = ofs,
    .len = len,
    .type = type
  };

  while (!nv_table_add_value_indirect(self->payload, handle, name, name_len, &referenced_slice, &new_entry))
    {
      /* error allocating string in payload, reallocate */
      if (!nv_table_realloc(self->payload, &self->payload))
        {
          /* error growing the payload, skip without storing the value */
          msg_info("Cannot store referenced value for this log message, maximum size has been reached",
                   evt_tag_str("name", name),
                   evt_tag_str("ref-name", log_msg_get_value_name(ref_handle, NULL)));
          break;
        }
      stats_counter_inc(count_payload_reallocs);
    }

  if (new_entry)
    log_msg_update_sdata(self, handle, name, name_len);
}

gboolean
log_msg_values_foreach(const LogMessage *self, NVTableForeachFunc func, gpointer user_data)
{
  return nv_table_foreach(self->payload, logmsg_registry, func, user_data);
}

void
log_msg_set_match(LogMessage *self, gint index_, const gchar *value, gssize value_len)
{
  g_assert(index_ < 256);

  if (index_ >= self->num_matches)
    self->num_matches = index_ + 1;
  log_msg_set_value(self, match_handles[index_], value, value_len);
}

void
log_msg_set_match_indirect(LogMessage *self, gint index_, NVHandle ref_handle, guint8 type, guint16 ofs, guint16 len)
{
  g_assert(index_ < 256);

  log_msg_set_value_indirect(self, match_handles[index_], ref_handle, type, ofs, len);
}

void
log_msg_clear_matches(LogMessage *self)
{
  gint i;

  for (i = 0; i < self->num_matches; i++)
    {
      log_msg_set_value(self, match_handles[i], "", 0);
    }
  self->num_matches = 0;
}

#if GLIB_SIZEOF_LONG != GLIB_SIZEOF_VOID_P
#error "The tags bit array assumes that long is the same size as the pointer"
#endif

#if GLIB_SIZEOF_LONG == 8
#define LOGMSG_TAGS_NDX_SHIFT 6
#define LOGMSG_TAGS_NDX_MASK  0x3F
#define LOGMSG_TAGS_BITS      64
#elif GLIB_SIZEOF_LONG == 4
#define LOGMSG_TAGS_NDX_SHIFT 5
#define LOGMSG_TAGS_NDX_MASK  0x1F
#define LOGMSG_TAGS_BITS      32
#else
#error "Unsupported word length, only 32 or 64 bit platforms are supported"
#endif

static inline void
log_msg_tags_foreach_item(const LogMessage *self, gint base, gulong item, LogMessageTagsForeachFunc callback,
                          gpointer user_data)
{
  gint i;

  for (i = 0; i < LOGMSG_TAGS_BITS; i++)
    {
      if (G_LIKELY(!item))
        return;
      if (item & 1)
        {
          LogTagId id = (LogTagId) base + i;

          callback(self, id, log_tags_get_by_id(id), user_data);
        }
      item >>= 1;
    }
}


void
log_msg_tags_foreach(const LogMessage *self, LogMessageTagsForeachFunc callback, gpointer user_data)
{
  guint i;

  if (self->num_tags == 0)
    {
      log_msg_tags_foreach_item(self, 0, (gulong) self->tags, callback, user_data);
    }
  else
    {
      for (i = 0; i != self->num_tags; ++i)
        {
          log_msg_tags_foreach_item(self, i * LOGMSG_TAGS_BITS, self->tags[i], callback, user_data);
        }
    }
}


static inline void
log_msg_set_bit(gulong *tags, gint index_, gboolean value)
{
  if (value)
    tags[index_ >> LOGMSG_TAGS_NDX_SHIFT] |= ((gulong) (1UL << (index_ & LOGMSG_TAGS_NDX_MASK)));
  else
    tags[index_ >> LOGMSG_TAGS_NDX_SHIFT] &= ~((gulong) (1UL << (index_ & LOGMSG_TAGS_NDX_MASK)));
}

static inline gboolean
log_msg_get_bit(gulong *tags, gint index_)
{
  return !!(tags[index_ >> LOGMSG_TAGS_NDX_SHIFT] & ((gulong) (1UL << (index_ & LOGMSG_TAGS_NDX_MASK))));
}

void
log_msg_set_tag_by_id_onoff(LogMessage *self, LogTagId id, gboolean on)
{
  gulong *old_tags;
  gint old_num_tags;
  gboolean inline_tags;

  g_assert(!log_msg_is_write_protected(self));
  if (!log_msg_chk_flag(self, LF_STATE_OWN_TAGS) && self->num_tags)
    {
      self->tags = g_memdup(self->tags, sizeof(self->tags[0]) * self->num_tags);
    }
  log_msg_set_flag(self, LF_STATE_OWN_TAGS);

  /* if num_tags is 0, it means that we use inline storage of tags */
  inline_tags = self->num_tags == 0;
  if (inline_tags && id < LOGMSG_TAGS_BITS)
    {
      /* store this tag inline */
      log_msg_set_bit((gulong *) &self->tags, id, on);
    }
  else
    {
      /* we can't put this tag inline, either because it is too large, or we don't have the inline space any more */

      if ((self->num_tags * LOGMSG_TAGS_BITS) <= id)
        {
          if (G_UNLIKELY(8159 < id))
            {
              msg_error("Maximum number of tags reached");
              return;
            }
          old_num_tags = self->num_tags;
          self->num_tags = (id / LOGMSG_TAGS_BITS) + 1;

          old_tags = self->tags;
          if (old_num_tags)
            self->tags = g_realloc(self->tags, sizeof(self->tags[0]) * self->num_tags);
          else
            self->tags = g_malloc(sizeof(self->tags[0]) * self->num_tags);
          memset(&self->tags[old_num_tags], 0, (self->num_tags - old_num_tags) * sizeof(self->tags[0]));

          if (inline_tags)
            self->tags[0] = (gulong) old_tags;
        }

      log_msg_set_bit(self->tags, id, on);
    }
  if (on)
    {
      log_tags_inc_counter(id);
    }
  else
    {
      log_tags_dec_counter(id);
    }
}

void
log_msg_set_tag_by_id(LogMessage *self, LogTagId id)
{
  log_msg_set_tag_by_id_onoff(self, id, TRUE);
}

void
log_msg_set_tag_by_name(LogMessage *self, const gchar *name)
{
  log_msg_set_tag_by_id_onoff(self, log_tags_get_by_name(name), TRUE);
}

void
log_msg_clear_tag_by_id(LogMessage *self, LogTagId id)
{
  log_msg_set_tag_by_id_onoff(self, id, FALSE);
}

void
log_msg_clear_tag_by_name(LogMessage *self, const gchar *name)
{
  log_msg_set_tag_by_id_onoff(self, log_tags_get_by_name(name), FALSE);
}

gboolean
log_msg_is_tag_by_id(LogMessage *self, LogTagId id)
{
  if (G_UNLIKELY(8159 < id))
    {
      msg_error("Invalid tag", evt_tag_int("id", (gint) id));
      return FALSE;
    }
  if (self->num_tags == 0 && id < LOGMSG_TAGS_BITS)
    return log_msg_get_bit((gulong *) &self->tags, id);
  else if (id < self->num_tags * LOGMSG_TAGS_BITS)
    return log_msg_get_bit(self->tags, id);
  else
    return FALSE;
}

gboolean
log_msg_is_tag_by_name(LogMessage *self, const gchar *name)
{
  return log_msg_is_tag_by_id(self, log_tags_get_by_name(name));
}

/* structured data elements */

static void
log_msg_sdata_append_key_escaped(GString *result, const gchar *sstr, gssize len)
{
  /* The specification does not have any way to escape keys.
   * The goal is to create syntactically valid structured data fields. */
  const guchar *ustr = (const guchar *) sstr;

  for (gssize i = 0; i < len; i++)
    {
      if (!isascii(ustr[i]) || ustr[i] == '=' || ustr[i] == ' '
          || ustr[i] == '[' || ustr[i] == ']' || ustr[i] == '"')
        {
          gchar hex_code[4];
          g_sprintf(hex_code, "%%%02X", ustr[i]);
          g_string_append(result, hex_code);
        }
      else
        g_string_append_c(result, ustr[i]);
    }
}

static void
log_msg_sdata_append_escaped(GString *result, const gchar *sstr, gssize len)
{
  gint i;
  const guchar *ustr = (const guchar *) sstr;

  for (i = 0; i < len; i++)
    {
      if (ustr[i] == '"' || ustr[i] == '\\' || ustr[i] == ']')
        {
          g_string_append_c(result, '\\');
          g_string_append_c(result, ustr[i]);
        }
      else
        g_string_append_c(result, ustr[i]);
    }
}

static const gchar *
find_sd_id_in_key(gint sd_id_len, const gchar *sdata_elem, const gchar *sdata_name,
                  gint sdata_name_len)
{
  if (sd_id_len)
    {
      const gchar *sd_id_sep = sdata_elem + sd_id_len;
      if (sd_id_sep - sdata_name == sdata_name_len)
        {
          /* Standalone sdata e.g. [[UserData.updatelist@18372.4]] */
          sd_id_sep = NULL;
        }
      else
        {
          g_assert((sd_id_sep - sdata_name < sdata_name_len) && *sd_id_sep == '.');
        }

      return sd_id_sep;
    }

  /* The SD-ID has two format, one with enterpriseId:
   * SD-ID : <key>@<enterprise-id>[.<sub-id>]*.<sd-name> = <sd-value>
   * Both <enterprise-id> and <sub-id> must be a number.
   */
  const gchar *at = memchr(sdata_name + logmsg_sd_prefix_len, '@', sdata_name_len - logmsg_sd_prefix_len);
  if (at)
    {
      const gint entreprise_id_len = strspn(at + 1, ".1234567890");
      return at + entreprise_id_len;
    }


  return memrchr(sdata_elem, '.', sdata_name_len - 7);
}

void
log_msg_append_format_sdata(const LogMessage *self, GString *result,  guint32 seq_num)
{
  const gchar *value;
  const gchar *sdata_name, *sdata_elem, *sdata_param, *cur_elem = NULL, *dot;
  gssize sdata_name_len, sdata_elem_len, sdata_param_len, cur_elem_len = 0, len;
  gint i;
  static NVHandle meta_seqid = 0;
  gssize seqid_length;
  gboolean has_seq_num = FALSE;
  const gchar *seqid;

  if (!meta_seqid)
    meta_seqid = log_msg_get_value_handle(".SDATA.meta.sequenceId");

  seqid = log_msg_get_value(self, meta_seqid, &seqid_length);
  APPEND_ZERO(seqid, seqid, seqid_length);
  has_seq_num = !!seqid[0];

  for (i = 0; i < self->num_sdata; i++)
    {
      NVHandle handle = self->sdata[i];
      guint16 handle_flags;
      gint sd_id_len;

      sdata_name_len = 0;
      sdata_name = log_msg_get_value_name(handle, &sdata_name_len);
      handle_flags = nv_registry_get_handle_flags(logmsg_registry, handle);
      value = log_msg_get_value_if_set(self, handle, &len);
      if (!value)
        continue;

      g_assert(handle_flags & LM_VF_SDATA);

      /* sdata_name always begins with .SDATA. */
      g_assert(sdata_name_len > 6);

      sdata_elem = sdata_name + 7;
      sd_id_len = (handle_flags >> 8);

      dot = find_sd_id_in_key(sd_id_len, sdata_elem, sdata_name, sdata_name_len);

      if (G_LIKELY(dot))
        {
          if (*dot == '.' || *dot == '@')
            {
              sdata_elem_len = dot - sdata_elem;

              sdata_param = dot + 1;
              sdata_param_len = sdata_name_len - (dot + 1 - sdata_name);
            }
          else
            {
              sdata_elem_len = dot + 1 - sdata_elem;

              sdata_param = dot + 1;
              sdata_param_len = sdata_name_len - (dot + 1 - sdata_name);
            }
        }
      else
        {
          sdata_elem_len = sdata_name_len - 7;
          if (sdata_elem_len == 0)
            {
              sdata_elem = "none";
              sdata_elem_len = 4;
            }
          sdata_param = "";
          sdata_param_len = 0;
        }
      if (!cur_elem || sdata_elem_len != cur_elem_len || strncmp(cur_elem, sdata_elem, sdata_elem_len) != 0)
        {
          if (cur_elem)
            {
              /* close the previous block */
              g_string_append_c(result, ']');
            }

          /* the current SD block has changed, emit a start */
          g_string_append_c(result, '[');
          log_msg_sdata_append_key_escaped(result, sdata_elem, sdata_elem_len);

          /* update cur_elem */
          cur_elem = sdata_elem;
          cur_elem_len = sdata_elem_len;
        }
      /* if message hasn't sequenceId and the cur_elem is the meta block Append the sequenceId for the result
         if seq_num isn't 0 */
      if (!has_seq_num && seq_num!=0 && strncmp(sdata_elem, "meta.", 5) == 0)
        {
          gchar sequence_id[16];
          g_snprintf(sequence_id, sizeof(sequence_id), "%d", seq_num);
          g_string_append_c(result, ' ');
          g_string_append_len(result, "sequenceId=\"", 12);
          g_string_append_len(result, sequence_id, strlen(sequence_id));
          g_string_append_c(result, '"');
          has_seq_num = TRUE;
        }
      if (sdata_param_len)
        {
          if (value)
            {
              g_string_append_c(result, ' ');
              log_msg_sdata_append_key_escaped(result, sdata_param, sdata_param_len);
              g_string_append(result, "=\"");
              log_msg_sdata_append_escaped(result, value, len);
              g_string_append_c(result, '"');
            }
        }
    }
  if (cur_elem)
    {
      g_string_append_c(result, ']');
    }
  /*
    There was no meta block and if sequenceId must be added (seq_num!=0)
    create the whole meta block with sequenceId
  */
  if (!has_seq_num && seq_num!=0)
    {
      gchar sequence_id[16];
      g_snprintf(sequence_id, sizeof(sequence_id), "%d", seq_num);
      g_string_append_c(result, '[');
      g_string_append_len(result, "meta sequenceId=\"", 17);
      g_string_append_len(result, sequence_id, strlen(sequence_id));
      g_string_append_len(result, "\"]", 2);
    }
}

void
log_msg_format_sdata(const LogMessage *self, GString *result,  guint32 seq_num)
{
  g_string_truncate(result, 0);
  log_msg_append_format_sdata(self, result, seq_num);
}

gboolean
log_msg_append_tags_callback(const LogMessage *self, LogTagId tag_id, const gchar *name, gpointer user_data)
{
  GString *result = (GString *) ((gpointer *) user_data)[0];
  gint original_length = GPOINTER_TO_UINT(((gpointer *) user_data)[1]);

  g_assert(result);

  if (result->len > original_length)
    g_string_append_c(result, ',');

  str_repr_encode_append(result, name, -1, ",");
  return TRUE;
}

void
log_msg_print_tags(const LogMessage *self, GString *result)
{
  gpointer args[] = { result, GUINT_TO_POINTER(result->len) };

  log_msg_tags_foreach(self, log_msg_append_tags_callback, args);
}

void
log_msg_set_saddr(LogMessage *self, GSockAddr *saddr)
{
  log_msg_set_saddr_ref(self, g_sockaddr_ref(saddr));
}

void
log_msg_set_saddr_ref(LogMessage *self, GSockAddr *saddr)
{
  if (log_msg_chk_flag(self, LF_STATE_OWN_SADDR))
    g_sockaddr_unref(self->saddr);
  self->saddr = saddr;
  self->flags |= LF_STATE_OWN_SADDR;
}

void
log_msg_set_daddr(LogMessage *self, GSockAddr *daddr)
{
  log_msg_set_daddr_ref(self, g_sockaddr_ref(daddr));
}

void
log_msg_set_daddr_ref(LogMessage *self, GSockAddr *daddr)
{
  if (log_msg_chk_flag(self,  LF_STATE_OWN_DADDR))
    g_sockaddr_unref(self->daddr);
  self->daddr = daddr;
  self->flags |= LF_STATE_OWN_DADDR;
}

/**
 * log_msg_init:
 * @self: LogMessage instance
 * @saddr: sender address
 *
 * This function initializes a LogMessage instance without allocating it
 * first. It is used internally by the log_msg_new function.
 **/
static void
log_msg_init(LogMessage *self)
{
  GTimeVal tv;

  /* ref is set to 1, ack is set to 0 */
  self->ack_and_ref_and_abort_and_suspended = LOGMSG_REFCACHE_REF_TO_VALUE(1);
  cached_g_current_time(&tv);
  self->timestamps[LM_TS_RECVD].ut_sec = tv.tv_sec;
  self->timestamps[LM_TS_RECVD].ut_usec = tv.tv_usec;
  self->timestamps[LM_TS_RECVD].ut_gmtoff = get_local_timezone_ofs(self->timestamps[LM_TS_RECVD].ut_sec);
  self->timestamps[LM_TS_STAMP] = self->timestamps[LM_TS_RECVD];
  unix_time_unset(&self->timestamps[LM_TS_PROCESSED]);

  self->sdata = NULL;
  self->saddr = NULL;
  self->daddr = NULL;

  self->original = NULL;
  self->flags |= LF_STATE_OWN_MASK;
  self->pri = LOG_USER | LOG_NOTICE;

  self->rcptid = rcptid_generate_id();
  log_msg_set_host_id(self);
}

void
log_msg_clear(LogMessage *self)
{
  if(log_msg_chk_flag(self, LF_STATE_OWN_PAYLOAD))
    nv_table_unref(self->payload);
  self->payload = nv_table_new(LM_V_MAX, 16, 256);

  if (log_msg_chk_flag(self, LF_STATE_OWN_TAGS) && self->tags)
    {
      gboolean inline_tags = self->num_tags == 0;

      if (inline_tags)
        self->tags = NULL;
      else
        memset(self->tags, 0, self->num_tags * sizeof(self->tags[0]));
    }
  else
    {
      self->tags = NULL;
      self->num_tags = 0;
    }

  self->num_matches = 0;
  if (!log_msg_chk_flag(self, LF_STATE_OWN_SDATA))
    {
      self->sdata = NULL;
      self->alloc_sdata = 0;
    }
  self->num_sdata = 0;

  if (log_msg_chk_flag(self, LF_STATE_OWN_SADDR))
    g_sockaddr_unref(self->saddr);
  self->saddr = NULL;
  if (log_msg_chk_flag(self, LF_STATE_OWN_DADDR))
    g_sockaddr_unref(self->daddr);
  self->daddr = NULL;

  /* clear "local", "utf8", "internal", "mark" and similar flags, we start afresh */
  self->flags = LF_STATE_OWN_MASK;
}

static inline LogMessage *
log_msg_alloc(gsize payload_size)
{
  LogMessage *msg;
  gsize payload_space = payload_size ? nv_table_get_alloc_size(LM_V_MAX, 16, payload_size) : 0;
  gsize alloc_size, payload_ofs = 0;

  /* NOTE: logmsg_node_max is updated from parallel threads without locking. */
  gint nodes = (volatile gint) logmsg_queue_node_max;

  alloc_size = sizeof(LogMessage) + sizeof(LogMessageQueueNode) * nodes;
  /* align to 8 boundary */
  if (payload_size)
    {
      alloc_size = (alloc_size + 7) & ~7;
      payload_ofs = alloc_size;
      alloc_size += payload_space;
    }
  msg = g_malloc(alloc_size);

  memset(msg, 0, sizeof(LogMessage));

  if (payload_size)
    msg->payload = nv_table_init_borrowed(((gchar *) msg) + payload_ofs, payload_space, LM_V_MAX);

  msg->num_nodes = nodes;
  msg->allocated_bytes = alloc_size + payload_space;
  stats_counter_add(count_allocated_bytes, msg->allocated_bytes);
  return msg;
}

static gboolean
_merge_value(NVHandle handle, const gchar *name, const gchar *value, gssize value_len, gpointer user_data)
{
  LogMessage *msg = (LogMessage *) user_data;

  if (!nv_table_is_value_set(msg->payload, handle))
    log_msg_set_value(msg, handle, value, value_len);
  return FALSE;
}

void
log_msg_merge_context(LogMessage *self, LogMessage **context, gsize context_len)
{
  gint i;

  for (i = context_len - 1; i >= 0; i--)
    {
      LogMessage *msg_to_be_merged = context[i];

      log_msg_values_foreach(msg_to_be_merged, _merge_value, self);
    }
}

static void
log_msg_clone_ack(LogMessage *msg, AckType ack_type)
{
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;

  g_assert(msg->original);
  path_options.ack_needed = TRUE;
  log_msg_ack(msg->original, &path_options, ack_type);
}

/*
 * log_msg_clone_cow:
 *
 * Clone a copy-on-write (cow) copy of a log message.
 */
LogMessage *
log_msg_clone_cow(LogMessage *msg, const LogPathOptions *path_options)
{
  LogMessage *self = log_msg_alloc(0);
  gsize allocated_bytes = self->allocated_bytes;

  stats_counter_inc(count_msg_clones);
  log_msg_write_protect(msg);

  memcpy(self, msg, sizeof(*msg));
  msg->allocated_bytes = allocated_bytes;

  msg_trace("Message was cloned",
            evt_tag_printf("original_msg", "%p", msg),
            evt_tag_printf("new_msg", "%p", self));

  /* every field _must_ be initialized explicitly if its direct
   * copying would cause problems (like copying a pointer by value) */

  /* reference the original message */
  self->original = log_msg_ref(msg);
  self->ack_and_ref_and_abort_and_suspended = LOGMSG_REFCACHE_REF_TO_VALUE(1) + LOGMSG_REFCACHE_ACK_TO_VALUE(
                                                0) + LOGMSG_REFCACHE_ABORT_TO_VALUE(0);
  self->cur_node = 0;
  self->protected = FALSE;

  log_msg_add_ack(self, path_options);
  if (!path_options->ack_needed)
    {
      self->ack_func  = NULL;
    }
  else
    {
      self->ack_func = log_msg_clone_ack;
    }

  self->flags &= ~LF_STATE_MASK;

  if (self->num_tags == 0)
    self->flags |= LF_STATE_OWN_TAGS;
  return self;
}

static gsize
_determine_payload_size(gint length, MsgFormatOptions *parse_options)
{
  gsize payload_size;

  if ((parse_options->flags & LP_STORE_RAW_MESSAGE))
    payload_size = length * 4;
  else
    payload_size = length * 2;

  return MAX(payload_size, 256);
}

/**
 * log_msg_new:
 * @msg: message to parse
 * @length: length of @msg
 * @flags: parse flags (LP_*)
 *
 * This function allocates, parses and returns a new LogMessage instance.
 **/
LogMessage *
log_msg_new(const gchar *msg, gint length,
            MsgFormatOptions *parse_options)
{
  LogMessage *self = log_msg_alloc(_determine_payload_size(length, parse_options));

  log_msg_init(self);

  msg_trace("Initial message parsing follows");
  msg_format_parse(parse_options, self, (guchar *) msg, length);
  return self;
}

LogMessage *
log_msg_new_empty(void)
{
  LogMessage *self = log_msg_alloc(256);

  log_msg_init(self);
  return self;
}

/* This function creates a new log message that should be considered local */
LogMessage *
log_msg_new_local(void)
{
  LogMessage *self = log_msg_new_empty();

  self->flags |= LF_LOCAL;
  return self;
}


/**
 * log_msg_new_internal:
 * @prio: message priority (LOG_*)
 * @msg: message text
 * @flags: parse flags (LP_*)
 *
 * This function creates a new log message for messages originating
 * internally to syslog-ng
 **/
LogMessage *
log_msg_new_internal(gint prio, const gchar *msg)
{
  gchar buf[32];
  LogMessage *self;

  g_snprintf(buf, sizeof(buf), "%d", (int) getpid());
  self = log_msg_new_local();
  self->flags |= LF_INTERNAL;
  self->initial_parse = TRUE;
  log_msg_set_value(self, LM_V_PROGRAM, "syslog-ng", 9);
  log_msg_set_value(self, LM_V_PID, buf, -1);
  log_msg_set_value(self, LM_V_MESSAGE, msg, -1);
  self->initial_parse = FALSE;
  self->pri = prio;

  return self;
}

/**
 * log_msg_new_mark:
 *
 * This function returns a new MARK message. MARK messages have the LF_MARK
 * flag set.
 **/
LogMessage *
log_msg_new_mark(void)
{
  LogMessage *self = log_msg_new_local();

  log_msg_set_value(self, LM_V_MESSAGE, "-- MARK --", 10);
  self->pri = LOG_SYSLOG | LOG_INFO;
  self->flags |= LF_MARK | LF_INTERNAL;
  return self;
}

/**
 * log_msg_free:
 * @self: LogMessage instance
 *
 * Frees a LogMessage instance.
 **/
static void
log_msg_free(LogMessage *self)
{
  if (log_msg_chk_flag(self, LF_STATE_OWN_PAYLOAD) && self->payload)
    nv_table_unref(self->payload);
  if (log_msg_chk_flag(self, LF_STATE_OWN_TAGS) && self->tags && self->num_tags > 0)
    g_free(self->tags);

  if (log_msg_chk_flag(self, LF_STATE_OWN_SDATA) && self->sdata)
    g_free(self->sdata);
  if (log_msg_chk_flag(self, LF_STATE_OWN_SADDR))
    g_sockaddr_unref(self->saddr);
  if (log_msg_chk_flag(self, LF_STATE_OWN_DADDR))
    g_sockaddr_unref(self->daddr);

  if (self->original)
    log_msg_unref(self->original);

  stats_counter_sub(count_allocated_bytes, self->allocated_bytes);

  g_free(self);
}

/**
 * log_msg_drop:
 * @msg: LogMessage instance
 * @path_options: path specific options
 *
 * This function is called whenever a destination driver feels that it is
 * unable to process this message. It acks and unrefs the message.
 **/
void
log_msg_drop(LogMessage *msg, const LogPathOptions *path_options, AckType ack_type)
{
  log_msg_ack(msg, path_options, ack_type);
  log_msg_unref(msg);
}

static AckType
_ack_and_ref_and_abort_and_suspend_to_acktype(gint value)
{
  AckType type = AT_PROCESSED;

  if (IS_SUSPENDFLAG_ON(LOGMSG_REFCACHE_VALUE_TO_SUSPEND(value)))
    type = AT_SUSPENDED;
  else if (IS_ABORTFLAG_ON(LOGMSG_REFCACHE_VALUE_TO_ABORT(value)))
    type = AT_ABORTED;

  return type;
}


/***************************************************************************************
 * In order to read & understand this code, reading the comment on the top
 * of this file about ref/ack handling is strongly recommended.
 ***************************************************************************************/

/* Function to update the combined ACK (with the abort flag) and REF counter. */
static inline gint
log_msg_update_ack_and_ref_and_abort_and_suspended(LogMessage *self, gint add_ref, gint add_ack, gint add_abort,
                                                   gint add_suspend)
{
  gint old_value, new_value;
  do
    {
      new_value = old_value = (volatile gint) self->ack_and_ref_and_abort_and_suspended;
      new_value = (new_value & ~LOGMSG_REFCACHE_REF_MASK)   + LOGMSG_REFCACHE_REF_TO_VALUE(  (LOGMSG_REFCACHE_VALUE_TO_REF(
                    old_value)   + add_ref));
      new_value = (new_value & ~LOGMSG_REFCACHE_ACK_MASK)   + LOGMSG_REFCACHE_ACK_TO_VALUE(  (LOGMSG_REFCACHE_VALUE_TO_ACK(
                    old_value)   + add_ack));
      new_value = (new_value & ~LOGMSG_REFCACHE_ABORT_MASK) + LOGMSG_REFCACHE_ABORT_TO_VALUE((LOGMSG_REFCACHE_VALUE_TO_ABORT(
                    old_value) | add_abort));
      new_value = (new_value & ~LOGMSG_REFCACHE_SUSPEND_MASK) + LOGMSG_REFCACHE_SUSPEND_TO_VALUE((
                    LOGMSG_REFCACHE_VALUE_TO_SUSPEND(old_value) | add_suspend));
    }
  while (!g_atomic_int_compare_and_exchange(&self->ack_and_ref_and_abort_and_suspended, old_value, new_value));

  return old_value;
}

/* Function to update the combined ACK (without abort) and REF counter. */
static inline gint
log_msg_update_ack_and_ref(LogMessage *self, gint add_ref, gint add_ack)
{
  return log_msg_update_ack_and_ref_and_abort_and_suspended(self, add_ref, add_ack, 0, 0);
}

/**
 * log_msg_ref:
 * @self: LogMessage instance
 *
 * Increment reference count of @self and return the new reference.
 **/
LogMessage *
log_msg_ref(LogMessage *self)
{
  gint old_value;

  if (G_LIKELY(logmsg_current == self))
    {
      /* fastpath, @self is the current message, ref/unref processing is
       * delayed until log_msg_refcache_stop() is called */

      logmsg_cached_refs++;
      return self;
    }

  /* slow path, refcache is not used, do the ordinary way */
  old_value = log_msg_update_ack_and_ref(self, 1, 0);
  g_assert(LOGMSG_REFCACHE_VALUE_TO_REF(old_value) >= 1);
  return self;
}

/**
 * log_msg_unref:
 * @self: LogMessage instance
 *
 * Decrement reference count and free self if the reference count becomes 0.
 **/
void
log_msg_unref(LogMessage *self)
{
  gint old_value;

  if (G_LIKELY(logmsg_current == self))
    {
      /* fastpath, @self is the current message, ref/unref processing is
       * delayed until log_msg_refcache_stop() is called */

      logmsg_cached_refs--;
      return;
    }

  old_value = log_msg_update_ack_and_ref(self, -1, 0);
  g_assert(LOGMSG_REFCACHE_VALUE_TO_REF(old_value) >= 1);

  if (LOGMSG_REFCACHE_VALUE_TO_REF(old_value) == 1)
    {
      log_msg_free(self);
    }
}

/**
 * log_msg_add_ack:
 * @m: LogMessage instance
 *
 * This function increments the number of required acknowledges.
 **/
void
log_msg_add_ack(LogMessage *self, const LogPathOptions *path_options)
{
  if (path_options->ack_needed)
    {
      if (G_LIKELY(logmsg_current == self))
        {
          /* fastpath, @self is the current message, add_ack/ack processing is
           * delayed until log_msg_refcache_stop() is called */

          logmsg_cached_acks++;
          logmsg_cached_ack_needed = TRUE;
          return;
        }
      log_msg_update_ack_and_ref(self, 0, 1);
    }
}


/**
 * log_msg_ack:
 * @msg: LogMessage instance
 * @path_options: path specific options
 * @acked: TRUE: positive ack, FALSE: negative ACK
 *
 * Indicate that the message was processed successfully and the sender can
 * queue further messages.
 **/
void
log_msg_ack(LogMessage *self, const LogPathOptions *path_options, AckType ack_type)
{
  gint old_value;

  if (path_options->ack_needed)
    {
      if (G_LIKELY(logmsg_current == self))
        {
          /* fastpath, @self is the current message, add_ack/ack processing is
           * delayed until log_msg_refcache_stop() is called */

          logmsg_cached_acks--;
          logmsg_cached_abort |= IS_ACK_ABORTED(ack_type);
          logmsg_cached_suspend |= IS_ACK_SUSPENDED(ack_type);
          return;
        }
      old_value = log_msg_update_ack_and_ref_and_abort_and_suspended(self, 0, -1, IS_ACK_ABORTED(ack_type),
                  IS_ACK_SUSPENDED(ack_type));
      if (LOGMSG_REFCACHE_VALUE_TO_ACK(old_value) == 1)
        {
          if (ack_type == AT_SUSPENDED)
            self->ack_func(self, AT_SUSPENDED);
          else if (ack_type == AT_ABORTED)
            self->ack_func(self, AT_ABORTED);
          else
            self->ack_func(self, _ack_and_ref_and_abort_and_suspend_to_acktype(old_value));
        }
    }
}

/*
 * Break out of an acknowledgement chain. The incoming message is
 * ACKed and a new path options structure is returned that can be used
 * to send to further consuming pipes.
 */
const LogPathOptions *
log_msg_break_ack(LogMessage *msg, const LogPathOptions *path_options, LogPathOptions *local_options)
{
  /* NOTE: in case the user requested flow control, we can't break the
   * ACK chain, as that would lead to early acks, that would cause
   * message loss */

  g_assert(!path_options->flow_control_requested);

  log_msg_ack(msg, path_options, AT_PROCESSED);

  *local_options = *path_options;
  local_options->ack_needed = FALSE;

  return local_options;
}


/*
 * Start caching ref/unref/ack/add-ack operations in the current thread for
 * the message specified by @self.  See the comment at the top of this file
 * for more information.
 *
 * This function is to be called by the producer thread (e.g. the one
 * that generates new messages). You should use
 * log_msg_refcache_start_consumer() in consumer threads instead.
 *
 * This function cannot be called for the same message from multiple
 * threads.
 *
 */
void
log_msg_refcache_start_producer(LogMessage *self)
{
  g_assert(logmsg_current == NULL);

  logmsg_current = self;
  /* we're the producer of said message, and thus we want to inhibit
   * freeing/acking it due to our cached refs, add a bias large enough
   * to cover any possible unrefs/acks of the consumer side */

  /* we don't need to be thread-safe here, as a producer has just created this message and no parallel access is yet possible */

  self->ack_and_ref_and_abort_and_suspended = (self->ack_and_ref_and_abort_and_suspended & ~LOGMSG_REFCACHE_REF_MASK) +
                                              LOGMSG_REFCACHE_REF_TO_VALUE((LOGMSG_REFCACHE_VALUE_TO_REF(self->ack_and_ref_and_abort_and_suspended) +
                                                  LOGMSG_REFCACHE_BIAS));
  self->ack_and_ref_and_abort_and_suspended = (self->ack_and_ref_and_abort_and_suspended & ~LOGMSG_REFCACHE_ACK_MASK) +
                                              LOGMSG_REFCACHE_ACK_TO_VALUE((LOGMSG_REFCACHE_VALUE_TO_ACK(self->ack_and_ref_and_abort_and_suspended) +
                                                  LOGMSG_REFCACHE_BIAS));

  logmsg_cached_refs = -LOGMSG_REFCACHE_BIAS;
  logmsg_cached_acks = -LOGMSG_REFCACHE_BIAS;
  logmsg_cached_abort = FALSE;
  logmsg_cached_suspend = FALSE;
  logmsg_cached_ack_needed = TRUE;
}

/*
 * Start caching ref/unref/ack/add-ack operations in the current thread for
 * the message specified by @self.  See the comment at the top of this file
 * for more information.
 *
 * This function is to be called by the consumer threads (e.g. the
 * ones that consume messages).
 *
 * This function can be called from multiple consumer threads at the
 * same time, even for the same message.
 *
 */
void
log_msg_refcache_start_consumer(LogMessage *self, const LogPathOptions *path_options)
{
  g_assert(logmsg_current == NULL);

  logmsg_current = self;
  logmsg_cached_ack_needed = path_options->ack_needed;
  logmsg_cached_refs = 0;
  logmsg_cached_acks = 0;
  logmsg_cached_abort = FALSE;
  logmsg_cached_suspend = FALSE;
}

/*
 * Stop caching ref/unref/ack/add-ack operations in the current thread for
 * the message specified by the log_msg_refcache_start() function.
 *
 * See the comment at the top of this file for more information.
 */
void
log_msg_refcache_stop(void)
{
  gint old_value;
  gint current_cached_acks;
  gboolean current_cached_abort;
  gboolean current_cached_suspend;

  g_assert(logmsg_current != NULL);

  /* validate that we didn't overflow the counters:
   *
   * Both counters must be:
   *
   * - at least 1 smaller than the bias, rationale:
   *
   *      - if we are caching "bias" number of refs, it may happen
   *        that there are bias number of unrefs, potentially running
   *        in consumer threads
   *
   *      - if the potential unrefs is larger than the bias value, it may
   *        happen that the producer sets the bias (trying to avoid
   *        the freeing of the LogMessage), but still it gets freed.
   *
   * - not smaller than the "-bias" value, rationale:
   *      - if we are caching "bias" number of unrefs the same can happen
   *        as with the ref case.
   *
   */
  g_assert((logmsg_cached_acks < LOGMSG_REFCACHE_BIAS - 1) && (logmsg_cached_acks >= -LOGMSG_REFCACHE_BIAS));
  g_assert((logmsg_cached_refs < LOGMSG_REFCACHE_BIAS - 1) && (logmsg_cached_refs >= -LOGMSG_REFCACHE_BIAS));

  /*
   * We fold the differences in ack/ref counts in three stages:
   *
   *   1) we take a ref of logmsg_current, this is needed so that the
   *      message is not freed until we return from refcache_stop()
   *
   *   2) we add in all the diffs that were accumulated between
   *      refcache_start and refcache_stop. This gets us a final value of the
   *      ack counter, ref must be >= as we took a ref ourselves.
   *
   *   3) we call the ack handler if needed, this might change ref counters
   *      recursively (but not ack counters as that already atomically
   *      dropped to zero)
   *
   *   4) drop the ref we took in step 1) above
   *
   *   4) then we fold in the net ref results of the ack callback and
   *      refcache_stop() combined. This either causes the LogMessage to be
   *      freed (when we were the last), or it stays around because of other
   *      refs.
   */

  /* 1) take ref */
  log_msg_ref(logmsg_current);

  /* 2) fold in ref/ack counter diffs into the atomic value */

  current_cached_acks = logmsg_cached_acks;
  logmsg_cached_acks = 0;

  current_cached_abort = logmsg_cached_abort;
  logmsg_cached_abort = FALSE;

  current_cached_suspend = logmsg_cached_suspend;
  logmsg_cached_suspend = FALSE;

  old_value = log_msg_update_ack_and_ref_and_abort_and_suspended(logmsg_current, 0, current_cached_acks,
              current_cached_abort, current_cached_suspend);

  if ((LOGMSG_REFCACHE_VALUE_TO_ACK(old_value) == -current_cached_acks) && logmsg_cached_ack_needed)
    {
      AckType ack_type_cumulated = _ack_and_ref_and_abort_and_suspend_to_acktype(old_value);

      if (current_cached_suspend)
        ack_type_cumulated = AT_SUSPENDED;
      else if (current_cached_abort)
        ack_type_cumulated = AT_ABORTED;

      /* 3) call the ack handler */

      logmsg_current->ack_func(logmsg_current, ack_type_cumulated);

      /* the ack callback may not change the ack counters, it already
       * dropped to zero atomically, changing that again is an error */

      g_assert(logmsg_cached_acks == 0);
    }

  /* 4) drop our own ref */
  log_msg_unref(logmsg_current);

  /* 5) fold the combined result of our own ref/unref and ack handler's results */
  old_value = log_msg_update_ack_and_ref(logmsg_current, logmsg_cached_refs, 0);

  if (LOGMSG_REFCACHE_VALUE_TO_REF(old_value) == -logmsg_cached_refs)
    log_msg_free(logmsg_current);
  logmsg_cached_refs = 0;
  logmsg_current = NULL;
}

void
log_msg_registry_init(void)
{
  gint i;

  logmsg_registry = nv_registry_new(builtin_value_names, NVHANDLE_MAX_VALUE);
  nv_registry_add_alias(logmsg_registry, LM_V_MESSAGE, "MSG");
  nv_registry_add_alias(logmsg_registry, LM_V_MESSAGE, "MSGONLY");
  nv_registry_add_alias(logmsg_registry, LM_V_HOST, "FULLHOST");
  nv_registry_add_alias(logmsg_registry, LM_V_HOST_FROM, "FULLHOST_FROM");

  for (i = 0; macros[i].name; i++)
    {
      if (nv_registry_get_handle(logmsg_registry, macros[i].name) == 0)
        {
          NVHandle handle;

          handle = nv_registry_alloc_handle(logmsg_registry, macros[i].name);
          nv_registry_set_handle_flags(logmsg_registry, handle, (macros[i].id << 8) + LM_VF_MACRO);
        }
    }

  /* register $0 - $255 in order */
  for (i = 0; i < 256; i++)
    {
      gchar buf[8];

      g_snprintf(buf, sizeof(buf), "%d", i);
      match_handles[i] = nv_registry_alloc_handle(logmsg_registry, buf);
    }
}

void
log_msg_registry_deinit(void)
{
  nv_registry_free(logmsg_registry);
  logmsg_registry = NULL;
}

void
log_msg_registry_foreach(GHFunc func, gpointer user_data)
{
  nv_registry_foreach(logmsg_registry, func, user_data);
}

static void
log_msg_register_stats(void)
{
  stats_lock();
  StatsClusterKey sc_key;
  stats_cluster_logpipe_key_set(&sc_key, SCS_GLOBAL, "msg_clones", NULL );
  stats_register_counter(0, &sc_key, SC_TYPE_PROCESSED, &count_msg_clones);

  stats_cluster_logpipe_key_set(&sc_key, SCS_GLOBAL, "payload_reallocs", NULL );
  stats_register_counter(0, &sc_key, SC_TYPE_PROCESSED, &count_payload_reallocs);

  stats_cluster_logpipe_key_set(&sc_key, SCS_GLOBAL, "sdata_updates", NULL );
  stats_register_counter(0, &sc_key, SC_TYPE_PROCESSED, &count_sdata_updates);

  stats_cluster_single_key_set(&sc_key, SCS_GLOBAL, "msg_allocated_bytes", NULL);
  stats_register_counter(1, &sc_key, SC_TYPE_SINGLE_VALUE, &count_allocated_bytes);
  stats_unlock();
}

void
log_msg_global_init(void)
{
  log_msg_registry_init();

  /* NOTE: we always initialize counters as they are on stats-level(0),
   * however we need to defer that as the stats subsystem may not be
   * operational yet */
  register_application_hook(AH_RUNNING, (ApplicationHookFunc) log_msg_register_stats, NULL, AHM_RUN_ONCE);
}


const gchar *
log_msg_get_handle_name(NVHandle handle, gssize *length)
{
  return nv_registry_get_handle_name(logmsg_registry, handle, length);
}

void
log_msg_global_deinit(void)
{
  log_msg_registry_deinit();
}

gint
log_msg_lookup_time_stamp_name(const gchar *name)
{
  if (strcmp(name, "stamp") == 0)
    return LM_TS_STAMP;
  else if (strcmp(name, "recvd") == 0)
    return LM_TS_RECVD;
  return -1;
}

gssize
log_msg_get_size(LogMessage *self)
{
  if (!self)
    return 0;

  return
    sizeof(LogMessage) + // msg.static fields
    + self->alloc_sdata * sizeof(self->sdata[0]) +
    g_sockaddr_len(self->saddr) + g_sockaddr_len(self->daddr) +
    ((self->num_tags) ? sizeof(self->tags[0]) * self->num_tags : 0) +
    nv_table_get_memory_consumption(self->payload); // msg.payload (nvtable)
}

#ifdef __linux__

const gchar *
__log_msg_get_value(const LogMessage *self, NVHandle handle, gssize *value_len)
__attribute__((alias("log_msg_get_value")));

const gchar *
__log_msg_get_value_by_name(const LogMessage *self, const gchar *name, gssize *value_len)
__attribute__((alias("log_msg_get_value_by_name")));

void
__log_msg_set_value_by_name(LogMessage *self, const gchar *name, const gchar *value, gssize length)
__attribute__((alias("log_msg_set_value_by_name")));

#endif
