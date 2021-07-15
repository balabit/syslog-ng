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

#ifndef LOGREADER_H_INCLUDED
#define LOGREADER_H_INCLUDED

#include "logsource.h"
#include "logproto/logproto-server.h"
#include "poll-events.h"
#include "mainloop-io-worker.h"
#include <iv_event.h>

/* flags */
#define LR_KERNEL          0x0002
#define LR_EMPTY_LINES     0x0004
#define LR_IGNORE_AUX_DATA 0x0008
#define LR_THREADED        0x0040

/* options */

typedef struct _LogReaderOptions
{
  gboolean initialized;
  LogSourceOptions super;
  MsgFormatOptions parse_options;
  LogProtoServerOptionsStorage proto_options;
  guint32 flags;
  gint fetch_limit;
  const gchar *group_name;
  gboolean check_hostname;
} LogReaderOptions;

typedef struct _LogReader LogReader;

struct _LogReader
{
  LogSource super;
  LogProtoServer *proto;
  gboolean immediate_check;
  LogPipe *control;
  LogReaderOptions *options;
  PollEvents *poll_events;
  GSockAddr *peer_addr;
  GSockAddr *local_addr;

  /* NOTE: these used to be LogReaderWatch members, which were merged into
   * LogReader with the multi-thread refactorization */

  struct iv_task restart_task;
  struct iv_event schedule_wakeup;
  MainLoopIOWorkerJob io_job;
  gboolean watches_running:1, suspended:1, realloc_window_after_fetch:1;
  gint notify_code;


  /* proto & poll_events pending to be applied. As long as the previous
   * processing is being done, we can't replace these in self->proto and
   * self->poll_events, they get applied to the production ones as soon as
   * the previous work is finished */
  gboolean pending_close;
  GCond *pending_close_cond;
  GStaticMutex pending_close_lock;

  struct iv_timer idle_timer;
};

void log_reader_set_options(LogReader *s, LogPipe *control, LogReaderOptions *options, const gchar *stats_id,
                            const gchar *stats_instance);
void log_reader_set_follow_filename(LogReader *self, const gchar *follow_filename);
void log_reader_set_name(LogReader *s, const gchar *name);
void log_reader_set_peer_addr(LogReader *s, GSockAddr *peer_addr);
void log_reader_set_local_addr(LogReader *s, GSockAddr *local_addr);
void log_reader_set_immediate_check(LogReader *s);
void log_reader_disable_bookmark_saving(LogReader *s);
void log_reader_open(LogReader *s, LogProtoServer *proto, PollEvents *poll_events);
void log_reader_close_proto(LogReader *s);
LogReader *log_reader_new(GlobalConfig *cfg);

void log_reader_options_defaults(LogReaderOptions *options);
void log_reader_options_init(LogReaderOptions *options, GlobalConfig *cfg, const gchar *group_name);
void log_reader_options_destroy(LogReaderOptions *options);
void log_reader_options_set_tags(LogReaderOptions *options, GList *tags);
gboolean log_reader_options_process_flag(LogReaderOptions *options, const gchar *flag);

#endif
