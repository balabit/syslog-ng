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

#include "logreader.h"
#include "mainloop-call.h"
#include "ack-tracker/ack_tracker.h"
#include "ack-tracker/ack_tracker_factory.h"

static void log_reader_io_handle_in(gpointer s);
static gboolean log_reader_fetch_log(LogReader *self);
static void log_reader_update_watches(LogReader *self);

/*****************************************************************************
 * LogReader setters
 *****************************************************************************/


void
log_reader_set_peer_addr(LogReader *s, GSockAddr *peer_addr)
{
  LogReader *self = (LogReader *) s;

  g_sockaddr_unref(self->peer_addr);
  self->peer_addr = g_sockaddr_ref(peer_addr);
}

void
log_reader_set_local_addr(LogReader *s, GSockAddr *local_addr)
{
  LogReader *self = (LogReader *) s;

  g_sockaddr_unref(self->local_addr);
  self->local_addr = g_sockaddr_ref(local_addr);
}

void
log_reader_set_immediate_check(LogReader *s)
{
  LogReader *self = (LogReader *) s;

  self->immediate_check = TRUE;
}

void
log_reader_set_options(LogReader *s, LogPipe *control, LogReaderOptions *options,
                       const gchar *stats_id, const gchar *stats_instance)
{
  LogReader *self = (LogReader *) s;

  /* log_reader_reopen() needs to be called prior to set_options.  This is
   * an ugly hack, but at least it is more explicitly than what used to be
   * here, which silently ignored if self->proto was NULL.
   */

  g_assert(self->proto != NULL);

  log_source_set_options(&self->super, &options->super, stats_id, stats_instance,
                         (options->flags & LR_THREADED), control->expr_node);
  AckTrackerFactory *factory = log_proto_server_get_ack_tracker_factory(self->proto);
  log_source_set_ack_tracker_factory(&self->super, ack_tracker_factory_ref(factory));

  log_pipe_unref(self->control);
  self->control = log_pipe_ref(control);

  self->options = options;
  log_proto_server_set_options(self->proto, &self->options->proto_options.super);
}

void
log_reader_set_name(LogReader *self, const gchar *name)
{
  log_source_set_name(&self->super, name);
}

void
log_reader_disable_bookmark_saving(LogReader *s)
{
  log_source_disable_bookmark_saving(&s->super);
}

/****************************************************************************
 * Watches: the poll_events instance and the idle timer
 ***************************************************************************/

static void
log_reader_idle_timeout(void *cookie)
{
  LogReader *self = (LogReader *) cookie;

  g_assert(!self->io_job.working);
  msg_notice("Source timeout has elapsed, closing connection",
             evt_tag_int("fd", log_proto_server_get_fd(self->proto)));

  log_pipe_notify(self->control, NC_CLOSE, self);
}

static void
log_reader_stop_idle_timer(LogReader *self)
{
  if (iv_timer_registered(&self->idle_timer))
    iv_timer_unregister(&self->idle_timer);
}

static void
log_reader_start_watches(LogReader *self)
{
  g_assert(!self->watches_running);
  if (self->poll_events)
    poll_events_start_watches(self->poll_events);
  self->watches_running = TRUE;
  log_reader_update_watches(self);
}

static void
log_reader_stop_watches(LogReader *self)
{
  g_assert(self->watches_running);
  if (self->poll_events)
    poll_events_stop_watches(self->poll_events);
  log_reader_stop_idle_timer(self);

  self->watches_running = FALSE;
}

static void
log_reader_disable_watches(LogReader *self)
{
  g_assert(self->watches_running);
  if (self->poll_events)
    poll_events_suspend_watches(self->poll_events);
  log_reader_stop_idle_timer(self);
}

/****************************************************************************
 * Suspend/wakeup
 ****************************************************************************/

static void
log_reader_suspend_until_awoken(LogReader *self)
{
  self->immediate_check = FALSE;
  log_reader_disable_watches(self);
  self->suspended = TRUE;
}

static void
log_reader_force_check_in_next_poll(LogReader *self)
{
  self->immediate_check = FALSE;
  log_reader_disable_watches(self);
  self->suspended = FALSE;

  if (!iv_task_registered(&self->restart_task))
    {
      iv_task_register(&self->restart_task);
    }
}

static void
log_reader_wakeup_triggered(gpointer s)
{
  LogReader *self = (LogReader *) s;

  if (!self->io_job.working && self->suspended)
    {
      /* NOTE: by the time working is set to FALSE we're over an
       * update_watches call.  So it is called either here (when
       * work_finished has done its work) or from work_finished above. The
       * two are not racing as both run in the main thread
       */
      log_reader_update_watches(self);
    }
}

/* NOTE: may be running in the destination's thread, thus proper locking must be used */
static void
log_reader_wakeup(LogSource *s)
{
  LogReader *self = (LogReader *) s;

  /*
   * We might get called even after this LogReader has been
   * deinitialized, in which case we must not do anything (since the
   * iv_event triggered here is not registered).
   *
   * This happens when log_writer_deinit() flushes its output queue
   * after the reader which produced the message has already been
   * deinited. Since init/deinit calls are made in the main thread, no
   * locking is needed.
   *
   */

  if (self->super.super.flags & PIF_INITIALIZED)
    iv_event_post(&self->schedule_wakeup);
}

/****************************************************************************
 * Open/close/reopen
 ***************************************************************************/

static void
log_reader_apply_proto_and_poll_events(LogReader *self, LogProtoServer *proto, PollEvents *poll_events)
{
  if (self->proto)
    log_proto_server_free(self->proto);
  if (self->poll_events)
    poll_events_free(self->poll_events);

  self->proto = proto;

  if (self->proto)
    log_proto_server_set_wakeup_cb(self->proto, (LogProtoServerWakeupFunc) log_reader_wakeup, self);

  self->poll_events = poll_events;
}


/* run in the main thread in reaction to a log_reader_reopen to change
 * the source LogProtoServer instance. It needs to be ran in the main
 * thread as it reregisters the watches associated with the main
 * thread. */
void
log_reader_close_proto_deferred(gpointer s)
{
  LogReader *self = (LogReader *) s;

  if (self->io_job.working)
    {
      self->pending_close = TRUE;
      return;
    }

  log_reader_stop_watches(self);
  log_reader_apply_proto_and_poll_events(self, NULL, NULL);
  log_reader_start_watches(self);
}

void
log_reader_close_proto(LogReader *self)
{
  g_assert(self->watches_running);
  main_loop_call((MainLoopTaskFunc) log_reader_close_proto_deferred, self, TRUE);

  if (!main_loop_is_main_thread())
    {
      g_static_mutex_lock(&self->pending_close_lock);
      while (self->pending_close)
        {
          g_cond_wait(self->pending_close_cond, g_static_mutex_get_mutex(&self->pending_close_lock));
        }
      g_static_mutex_unlock(&self->pending_close_lock);
    }
}

void
log_reader_open(LogReader *self, LogProtoServer *proto, PollEvents *poll_events)
{
  g_assert(!self->watches_running);
  poll_events_set_callback(poll_events, log_reader_io_handle_in, self);

  log_reader_apply_proto_and_poll_events(self, proto, poll_events);
}

static gboolean
log_reader_is_opened(LogReader *self)
{
  return self->proto && self->poll_events;
}

/*****************************************************************************
 * Set watches state so we are polling the event(s) that comes next.
 *****************************************************************************/

static void
log_reader_update_watches(LogReader *self)
{
  GIOCondition cond;
  gint idle_timeout = -1;

  main_loop_assert_main_thread();
  g_assert(self->watches_running);

  log_reader_disable_watches(self);

  if (!log_reader_is_opened(self))
    return;

  gboolean free_to_send = log_source_free_to_send(&self->super);
  if (!free_to_send)
    {
      log_reader_suspend_until_awoken(self);
      return;
    }

  LogProtoPrepareAction prepare_action = log_proto_server_prepare(self->proto, &cond, &idle_timeout);

  if (idle_timeout > 0)
    {
      iv_validate_now();

      self->idle_timer.expires = iv_now;
      self->idle_timer.expires.tv_sec += idle_timeout;

      iv_timer_register(&self->idle_timer);
    }

  if (self->immediate_check)
    {
      log_reader_force_check_in_next_poll(self);
      return;
    }

  switch (prepare_action)
    {
    case LPPA_POLL_IO:
      poll_events_update_watches(self->poll_events, cond);
      break;
    case LPPA_FORCE_SCHEDULE_FETCH:
      log_reader_force_check_in_next_poll(self);
      break;
    case LPPA_SUSPEND:
      log_reader_suspend_until_awoken(self);
      break;
    default:
      g_assert_not_reached();
      break;
    }
}

/*****************************************************************************
 * Glue into MainLoopIOWorker
 *****************************************************************************/

static void
log_reader_work_perform(void *s, GIOCondition cond)
{
  LogReader *self = (LogReader *) s;

  self->notify_code = log_reader_fetch_log(self);
}

static void
log_reader_work_finished(void *s)
{
  LogReader *self = (LogReader *) s;

  if (self->pending_close)
    {
      /* pending proto is only set in the main thread, so no need to
       * lock it before coming here. After we're syncing with the
       * log_writer_reopen() call, quite possibly coming from a
       * non-main thread. */

      g_static_mutex_lock(&self->pending_close_lock);

      log_reader_apply_proto_and_poll_events(self, NULL, NULL);
      self->pending_close = FALSE;

      g_cond_signal(self->pending_close_cond);
      g_static_mutex_unlock(&self->pending_close_lock);
    }

  if (self->notify_code)
    {
      gint notify_code = self->notify_code;

      self->notify_code = 0;
      log_pipe_notify(self->control, notify_code, self);
    }
  if (self->super.super.flags & PIF_INITIALIZED)
    {
      /* reenable polling the source assuming that we're still in
       * business (e.g. the reader hasn't been uninitialized) */

      if (self->realloc_window_after_fetch)
        {
          self->realloc_window_after_fetch = FALSE;
          log_source_dynamic_window_realloc(s);
        }
      log_proto_server_reset_error(self->proto);
      log_reader_update_watches(self);
    }
}

/*****************************************************************************
 * Input processing, the main function of LogReader
 *****************************************************************************/

static void
_add_aux_nvpair(const gchar *name, const gchar *value, gsize value_len, gpointer user_data)
{
  LogMessage *msg = (LogMessage *) user_data;

  log_msg_set_value_by_name(msg, name, value, value_len);;
}

static inline gint
log_reader_process_handshake(LogReader *self)
{
  LogProtoStatus status = log_proto_server_handshake(self->proto);

  switch (status)
    {
    case LPS_EOF:
    case LPS_ERROR:
      return status == LPS_ERROR ? NC_READ_ERROR : NC_CLOSE;
    case LPS_SUCCESS:
      break;
    case LPS_AGAIN:
      break;
    default:
      g_assert_not_reached();
      break;
    }
  return 0;
}

static gboolean
log_reader_handle_line(LogReader *self, const guchar *line, gint length, LogTransportAuxData *aux)
{
  LogMessage *m;

  msg_debug("Incoming log entry",
            evt_tag_printf("line", "%.*s", length, line));
  /* use the current time to get the time zone offset */
  m = log_msg_new((gchar *) line, length,
                  &self->options->parse_options);

  if (aux)
    {
      log_msg_set_saddr(m, aux->peer_addr ? : self->peer_addr);
      log_msg_set_daddr(m, aux->local_addr ? : self->local_addr);
      m->proto = aux->proto;
    }
  log_msg_refcache_start_producer(m);

  log_transport_aux_data_foreach(aux, _add_aux_nvpair, m);

  log_source_post(&self->super, m);
  log_msg_refcache_stop();
  return log_source_free_to_send(&self->super);
}

/* returns: notify_code (NC_XXXX) or 0 for success */
static gint
log_reader_fetch_log(LogReader *self)
{
  gint msg_count = 0;
  gboolean may_read = TRUE;
  LogTransportAuxData aux_storage, *aux = &aux_storage;

  if ((self->options->flags & LR_IGNORE_AUX_DATA))
    aux = NULL;

  log_transport_aux_data_init(aux);
  if (log_proto_server_handshake_in_progress(self->proto))
    {
      return log_reader_process_handshake(self);
    }

  /* NOTE: this loop is here to decrease the load on the main loop, we try
   * to fetch a couple of messages in a single run (but only up to
   * fetch_limit).
   */
  while (msg_count < self->options->fetch_limit && !main_loop_worker_job_quit())
    {
      Bookmark *bookmark;
      const guchar *msg;
      gsize msg_len;
      LogProtoStatus status;

      msg = NULL;

      /* NOTE: may_read is used to implement multi-read checking. It
       * is initialized to TRUE to indicate that the protocol is
       * allowed to issue a read(). If multi-read is disallowed in the
       * protocol, it resets may_read to FALSE after the first read was issued.
       */

      log_transport_aux_data_reinit(aux);
      bookmark = ack_tracker_request_bookmark(self->super.ack_tracker);
      status = log_proto_server_fetch(self->proto, &msg, &msg_len, &may_read, aux, bookmark);
      switch (status)
        {
        case LPS_EOF:
          log_transport_aux_data_destroy(aux);
          return NC_CLOSE;
        case LPS_ERROR:
          log_transport_aux_data_destroy(aux);
          return NC_READ_ERROR;
        case LPS_SUCCESS:
          break;
        case LPS_AGAIN:
          break;
        default:
          g_assert_not_reached();
          break;
        }

      if (!msg)
        {
          /* no more messages for now */
          break;
        }
      if (msg_len > 0 || (self->options->flags & LR_EMPTY_LINES))
        {
          msg_count++;

          if (!log_reader_handle_line(self, msg, msg_len, aux))
            {
              /* window is full, don't generate further messages */
              break;
            }
        }
    }
  log_transport_aux_data_destroy(aux);

  if (msg_count == self->options->fetch_limit)
    self->immediate_check = TRUE;
  return 0;
}

static void
log_reader_io_handle_in(gpointer s)
{
  LogReader *self = (LogReader *) s;

  log_reader_disable_watches(self);
  if ((self->options->flags & LR_THREADED))
    {
      main_loop_io_worker_job_submit(&self->io_job, G_IO_IN);
    }
  else
    {
      /* Checking main_loop_io_worker_job_quit() helps to speed up the
       * reload process.  If reload/shutdown is requested we shouldn't do
       * anything here, outstanding messages will be processed by the new
       * configuration.
       *
       * Our current understanding is that it doesn't prevent race
       * conditions of any kind.
       */
      if (!main_loop_worker_job_quit())
        {
          log_reader_work_perform(s, G_IO_IN);
          log_reader_work_finished(s);
        }
    }
}

/*****************************************************************************
 * LogReader->LogPipe interface implementation
 *****************************************************************************/

static gboolean
log_reader_init(LogPipe *s)
{
  LogReader *self = (LogReader *) s;

  if (!log_source_init(s))
    return FALSE;

  log_proto_server_set_ack_tracker(self->proto, self->super.ack_tracker);


  if (!log_proto_server_validate_options(self->proto))
    return FALSE;

  if (!self->options->parse_options.format_handler)
    {
      msg_error("Unknown format plugin specified",
                evt_tag_str("format", self->options->parse_options.format));
      return FALSE;
    }

  iv_event_register(&self->schedule_wakeup);

  log_reader_start_watches(self);

  return TRUE;
}

static gboolean
log_reader_deinit(LogPipe *s)
{
  LogReader *self = (LogReader *) s;

  main_loop_assert_main_thread();

  iv_event_unregister(&self->schedule_wakeup);
  if (iv_task_registered(&self->restart_task))
    iv_task_unregister(&self->restart_task);

  log_reader_stop_watches(self);

  if (!log_source_deinit(s))
    return FALSE;

  return TRUE;
}


static void
log_reader_init_watches(LogReader *self)
{
  IV_TASK_INIT(&self->restart_task);
  self->restart_task.cookie = self;
  self->restart_task.handler = log_reader_io_handle_in;

  IV_EVENT_INIT(&self->schedule_wakeup);
  self->schedule_wakeup.cookie = self;
  self->schedule_wakeup.handler = log_reader_wakeup_triggered;

  IV_TIMER_INIT(&self->idle_timer);
  self->idle_timer.cookie = self;
  self->idle_timer.handler = log_reader_idle_timeout;

  main_loop_io_worker_job_init(&self->io_job);
  self->io_job.user_data = self;
  self->io_job.work = (void (*)(void *, GIOCondition)) log_reader_work_perform;
  self->io_job.completion = (void (*)(void *)) log_reader_work_finished;
  self->io_job.engage = (void (*)(void *)) log_pipe_ref;
  self->io_job.release = (void (*)(void *)) log_pipe_unref;
}

static void
log_reader_free(LogPipe *s)
{
  LogReader *self = (LogReader *) s;

  if (self->proto)
    {
      log_proto_server_free(self->proto);
      self->proto = NULL;
    }
  if (self->poll_events)
    poll_events_free(self->poll_events);

  log_pipe_unref(self->control);
  g_sockaddr_unref(self->peer_addr);
  g_sockaddr_unref(self->local_addr);
  g_static_mutex_free(&self->pending_close_lock);
  g_cond_free(self->pending_close_cond);
  log_source_free(s);
}

static void
_schedule_dynamic_window_realloc(LogSource *s)
{
  LogReader *self = (LogReader *)s;

  msg_trace("LogReader::dynamic_window_realloc called");

  if (self->io_job.working)
    {
      self->realloc_window_after_fetch = TRUE;
      return;
    }

  log_source_dynamic_window_realloc(s);
}


LogReader *
log_reader_new(GlobalConfig *cfg)
{
  LogReader *self = g_new0(LogReader, 1);

  log_source_init_instance(&self->super, cfg);
  self->super.super.init = log_reader_init;
  self->super.super.deinit = log_reader_deinit;
  self->super.super.free_fn = log_reader_free;
  self->super.wakeup = log_reader_wakeup;
  self->super.schedule_dynamic_window_realloc = _schedule_dynamic_window_realloc;
  self->immediate_check = FALSE;
  log_reader_init_watches(self);
  g_static_mutex_init(&self->pending_close_lock);
  self->pending_close_cond = g_cond_new();
  return self;
}

/****************************************************************************
 * LogReaderOptions defaults/init/destroy
 ***************************************************************************/

void
log_reader_options_defaults(LogReaderOptions *options)
{
  log_source_options_defaults(&options->super);
  log_proto_server_options_defaults(&options->proto_options.super);
  msg_format_options_defaults(&options->parse_options);
  options->fetch_limit = 10;
}

/*
 * NOTE: _init needs to be idempotent when called multiple times w/o invoking _destroy
 *
 * Rationale:
 *   - init is called from driver init (e.g. affile_sd_init),
 *   - destroy is called from driver free method (e.g. affile_sd_free, NOT affile_sd_deinit)
 *
 * The reason:
 *   - when initializing the reloaded configuration fails for some reason,
 *     we have to fall back to the old configuration, thus we cannot dump
 *     the information stored in the Options structure at deinit time, but
 *     have to recover it when the old configuration is initialized.
 *
 * For the reasons above, init and destroy behave the following way:
 *
 *   - init is idempotent, it can be called multiple times without leaking
 *     memory, and without loss of information
 *   - destroy is only called once, when the options are indeed to be destroyed
 *
 * Also important to note is that when init is called multiple times, the
 * GlobalConfig reference is the same, this means that it is enough to
 * remember whether init was called already and return w/o doing anything in
 * that case, which is actually how idempotency is implemented here.
 */
void
log_reader_options_init(LogReaderOptions *options, GlobalConfig *cfg, const gchar *group_name)
{
  if (options->initialized)
    return;

  log_source_options_init(&options->super, cfg, group_name);
  log_proto_server_options_init(&options->proto_options.super, cfg);
  msg_format_options_init(&options->parse_options, cfg);

  if (options->check_hostname == -1)
    options->check_hostname = cfg->check_hostname;
  if (options->check_hostname)
    {
      options->parse_options.flags |= LP_CHECK_HOSTNAME;
    }
  if (!options->super.keep_timestamp)
    {
      options->parse_options.flags |= LP_NO_PARSE_DATE;
    }
  if (options->parse_options.default_pri == 0xFFFF)
    {
      if (options->flags & LR_KERNEL)
        options->parse_options.default_pri = LOG_KERN | LOG_NOTICE;
      else
        options->parse_options.default_pri = LOG_USER | LOG_NOTICE;
    }
  if (options->proto_options.super.encoding)
    options->parse_options.flags |= LP_ASSUME_UTF8;
  if (cfg->threaded)
    options->flags |= LR_THREADED;
  options->initialized = TRUE;
}

void
log_reader_options_destroy(LogReaderOptions *options)
{
  log_source_options_destroy(&options->super);
  log_proto_server_options_destroy(&options->proto_options.super);
  msg_format_options_destroy(&options->parse_options);
  options->initialized = FALSE;
}

CfgFlagHandler log_reader_flag_handlers[] =
{
  /* NOTE: underscores are automatically converted to dashes */

  /* LogReaderOptions */
  { "kernel",                     CFH_SET, offsetof(LogReaderOptions, flags),               LR_KERNEL },
  { "empty-lines",                CFH_SET, offsetof(LogReaderOptions, flags),               LR_EMPTY_LINES },
  { "threaded",                   CFH_SET, offsetof(LogReaderOptions, flags),               LR_THREADED },
  { "ignore-aux-data",            CFH_SET, offsetof(LogReaderOptions, flags),               LR_IGNORE_AUX_DATA },
  { NULL },
};

gboolean
log_reader_options_process_flag(LogReaderOptions *options, const gchar *flag)
{
  if (!msg_format_options_process_flag(&options->parse_options, flag))
    return cfg_process_flag(log_reader_flag_handlers, options, flag);
  return TRUE;
}
