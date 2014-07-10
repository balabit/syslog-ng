/*
 * Copyright (c) 2013 BalaBit IT Ltd, Budapest, Hungary
 * Copyright (c) 2013 Gergely Nagy <algernon@balabit.hu>
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

#include "logthrdestdrv.h"

void
log_threaded_dest_driver_suspend(LogThrDestDriver *self)
{
  iv_validate_now();
  self->timer_reopen.expires  = iv_now;
  self->timer_reopen.expires.tv_sec += self->time_reopen;
  iv_timer_register(&self->timer_reopen);
}

static void
log_threaded_dest_driver_message_became_available_in_the_queue(gpointer user_data)
{
  LogThrDestDriver *self = (LogThrDestDriver *) user_data;
  iv_event_post(&self->wake_up_event);
}

static void
log_threaded_dest_driver_wake_up(gpointer data)
{
  LogThrDestDriver *self = (LogThrDestDriver *)data;
  if (!iv_task_registered(&self->do_work))
    {
      iv_task_register(&self->do_work);
    }
}

static void
log_threaded_dest_driver_stop_watches(LogThrDestDriver* self)
{
  if (iv_task_registered(&self->do_work))
  {
    iv_task_unregister(&self->do_work);
  }
  if (iv_timer_registered(&self->timer_reopen))
  {
    iv_timer_unregister(&self->timer_reopen);
  }
  if (iv_timer_registered(&self->timer_throttle))
  {
    iv_timer_unregister(&self->timer_throttle);
  }
}

static void
log_threaded_dest_driver_shutdown(gpointer data)
{
  LogThrDestDriver *self = (LogThrDestDriver *)data;
  log_threaded_dest_driver_stop_watches(self);
  iv_quit();
}


static void
log_threaded_dest_driver_do_work(gpointer data)
{
  LogThrDestDriver *self = (LogThrDestDriver *)data;
  gint timeout_msec = 0;
  log_threaded_dest_driver_stop_watches(self);
  if (log_queue_check_items(self->queue, &timeout_msec,
                                        log_threaded_dest_driver_message_became_available_in_the_queue,
                                        self, NULL))
    {
      if (!self->worker.insert(self))
        {
          if (self->worker.disconnect)
            self->worker.disconnect(self);
          log_queue_reset_parallel_push(self->queue);
          log_threaded_dest_driver_suspend(self);
         }
      else
        {
          iv_task_register(&self->do_work);
        }
    }
  else if (timeout_msec != 0)
    {
      log_queue_reset_parallel_push(self->queue);
      iv_validate_now();
      self->timer_throttle.expires = iv_now;
      timespec_add_msec(&self->timer_throttle.expires, timeout_msec);
      iv_timer_register(&self->timer_throttle);
    }
}

static void
log_threaded_dest_driver_init_watches(LogThrDestDriver* self)
{
  IV_EVENT_INIT(&self->wake_up_event);
  self->wake_up_event.cookie = self;
  self->wake_up_event.handler = log_threaded_dest_driver_wake_up;
  iv_event_register(&self->wake_up_event);

  IV_EVENT_INIT(&self->shutdown_event);
  self->shutdown_event.cookie = self;
  self->shutdown_event.handler = log_threaded_dest_driver_shutdown;
  iv_event_register(&self->shutdown_event);

  IV_TIMER_INIT(&self->timer_reopen);
  self->timer_reopen.cookie = self;
  self->timer_reopen.handler = log_threaded_dest_driver_do_work;

  IV_TIMER_INIT(&self->timer_throttle);
  self->timer_throttle.cookie = self;
  self->timer_throttle.handler = log_threaded_dest_driver_do_work;

  IV_TASK_INIT(&self->do_work);
  self->do_work.cookie = self;
  self->do_work.handler = log_threaded_dest_driver_do_work;
}

static void
log_threaded_dest_driver_start_watches(LogThrDestDriver* self)
{
  iv_task_register(&self->do_work);
}

static void
log_threaded_dest_driver_worker_thread_main(gpointer arg)
{
  LogThrDestDriver *self = (LogThrDestDriver *)arg;

  iv_init();

  msg_debug("Worker thread started",
            evt_tag_str("driver", self->super.super.id),
            NULL);

  if (self->worker.thread_init)
    self->worker.thread_init(self);
  log_threaded_dest_driver_init_watches(self);

  log_threaded_dest_driver_start_watches(self);
  iv_main();

  if (self->worker.disconnect)
    self->worker.disconnect(self);

  if (self->worker.thread_deinit)
    self->worker.thread_deinit(self);

  msg_debug("Worker thread finished",
            evt_tag_str("driver", self->super.super.id),
            NULL);
  iv_deinit();
}

static void
log_threaded_dest_driver_stop_thread(gpointer s)
{
  LogThrDestDriver *self = (LogThrDestDriver *) s;

  iv_event_post(&self->shutdown_event);
}

static void
log_threaded_dest_driver_start_thread(LogThrDestDriver *self)
{
  main_loop_create_worker_thread(log_threaded_dest_driver_worker_thread_main,
                                 log_threaded_dest_driver_stop_thread,
                                 self, &self->worker_options);
}


gboolean
log_threaded_dest_driver_start(LogPipe *s)
{
  LogThrDestDriver *self = (LogThrDestDriver *)s;
  GlobalConfig *cfg = log_pipe_get_config(s);

  if (cfg)
    self->time_reopen = cfg->time_reopen;

  self->queue = log_dest_driver_acquire_queue(&self->super,
                                              self->format.persist_name(self));

  if (self->queue == NULL)
    {
      return FALSE;
    }

  stats_lock();
  stats_register_counter(0, self->stats_source | SCS_DESTINATION, self->super.super.id,
                         self->format.stats_instance(self),
                         SC_TYPE_STORED, &self->stored_messages);
  stats_register_counter(0, self->stats_source | SCS_DESTINATION, self->super.super.id,
                         self->format.stats_instance(self),
                         SC_TYPE_DROPPED, &self->dropped_messages);
  stats_unlock();

  log_queue_set_counters(self->queue, self->stored_messages,
                         self->dropped_messages);

  log_threaded_dest_driver_start_thread(self);

  return TRUE;
}

gboolean
log_threaded_dest_driver_deinit_method(LogPipe *s)
{
  LogThrDestDriver *self = (LogThrDestDriver *)s;

  log_queue_reset_parallel_push(self->queue);

  log_queue_set_counters(self->queue, NULL, NULL);

  stats_lock();
  stats_unregister_counter(self->stats_source | SCS_DESTINATION, self->super.super.id,
                           self->format.stats_instance(self),
                           SC_TYPE_STORED, &self->stored_messages);
  stats_unregister_counter(self->stats_source | SCS_DESTINATION, self->super.super.id,
                           self->format.stats_instance(self),
                           SC_TYPE_DROPPED, &self->dropped_messages);
  stats_unlock();

  if (!log_dest_driver_deinit_method(s))
    return FALSE;

  return TRUE;
}

void
log_threaded_dest_driver_free(LogPipe *s)
{
  LogThrDestDriver *self = (LogThrDestDriver *)s;

  if (self->queue)
    log_queue_unref(self->queue);

  log_dest_driver_free((LogPipe *)self);
}

static void
log_threaded_dest_driver_queue(LogPipe *s, LogMessage *msg,
                               const LogPathOptions *path_options,
                               gpointer user_data)
{
  LogThrDestDriver *self = (LogThrDestDriver *)s;
  LogPathOptions local_options;

  if (!path_options->flow_control_requested)
    path_options = log_msg_break_ack(msg, path_options, &local_options);

  if (self->queue_method)
    self->queue_method(self);

  log_msg_add_ack(msg, path_options);
  log_queue_push_tail(self->queue, log_msg_ref(msg), path_options);

  log_dest_driver_queue_method(s, msg, path_options, user_data);
}

void
log_threaded_dest_driver_init_instance(LogThrDestDriver *self, GlobalConfig *cfg)
{
  log_dest_driver_init_instance(&self->super, cfg);

  self->worker_options.is_output_thread = TRUE;

  self->super.super.super.init = log_threaded_dest_driver_start;
  self->super.super.super.deinit = log_threaded_dest_driver_deinit_method;
  self->super.super.super.queue = log_threaded_dest_driver_queue;
  self->super.super.super.free_fn = log_threaded_dest_driver_free;
}
