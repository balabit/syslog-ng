/*
 * Copyright (c) 2013, 2014 Balabit
 * Copyright (c) 2013, 2014 Gergely Nagy <algernon@balabit.hu>
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

#include "stats/stats-cluster-logpipe.h"
#include "logthrdestdrv.h"
#include "seqnum.h"
#include "scratch-buffers.h"
#include "timeutils/misc.h"

#define MAX_RETRIES_ON_ERROR_DEFAULT 3
#define MAX_RETRIES_BEFORE_SUSPEND_DEFAULT 3

static void _init_stats_key(LogThreadedDestDriver *self, StatsClusterKey *sc_key);

const gchar *
log_threaded_result_to_str(LogThreadedResult self)
{
  g_assert(self <= LTR_MAX);

  static const gchar *as_str[] = { "DROP",
                                   "ERROR",
                                   "EXPLICIT_ACK_MGMT",
                                   "SUCCESS",
                                   "QUEUED",
                                   "NOT_CONNECTED",
                                   "RETRY",
                                   "MAX"
                                 };

  return as_str[self];
}

/* LogThreadedDestWorker */

void
log_threaded_dest_driver_set_batch_lines(LogDriver *s, gint batch_lines)
{
  LogThreadedDestDriver *self = (LogThreadedDestDriver *) s;

  self->batch_lines = batch_lines;
}

void
log_threaded_dest_driver_set_batch_timeout(LogDriver *s, gint batch_timeout)
{
  LogThreadedDestDriver *self = (LogThreadedDestDriver *) s;

  self->batch_timeout = batch_timeout;
}

void
log_threaded_dest_driver_set_time_reopen(LogDriver *s, time_t time_reopen)
{
  LogThreadedDestDriver *self = (LogThreadedDestDriver *) s;

  self->time_reopen = time_reopen;
}

/* this should be used in combination with LTR_EXPLICIT_ACK_MGMT to actually confirm message delivery. */
void
log_threaded_dest_worker_ack_messages(LogThreadedDestWorker *self, gint batch_size)
{
  log_queue_ack_backlog(self->queue, batch_size);
  stats_counter_add(self->owner->written_messages, batch_size);
  self->retries_on_error_counter = 0;
  self->batch_size -= batch_size;
}

void
log_threaded_dest_worker_drop_messages(LogThreadedDestWorker *self, gint batch_size)
{
  log_queue_ack_backlog(self->queue, batch_size);
  stats_counter_add(self->owner->dropped_messages, batch_size);
  self->retries_on_error_counter = 0;
  self->batch_size -= batch_size;
}

void
log_threaded_dest_worker_rewind_messages(LogThreadedDestWorker *self, gint batch_size)
{
  log_queue_rewind_backlog(self->queue, batch_size);
  self->rewound_batch_size = self->batch_size;
  self->batch_size -= batch_size;
}

static gchar *
_format_queue_persist_name(LogThreadedDestWorker *self)
{
  if (self->worker_index == 0)
    {
      /* the first worker uses the legacy persist name, e.g.  to be able to
       * recover the queue previously used.  */
      return g_strdup(log_pipe_get_persist_name((LogPipe *)self->owner));
    }
  else
    {
      return g_strdup_printf("%s.%d.queue", log_pipe_get_persist_name((LogPipe *)self->owner), self->worker_index);
    }
}


static gboolean
_should_flush_now(LogThreadedDestWorker *self)
{
  struct timespec now;
  glong diff;

  if (self->owner->batch_timeout <= 0 ||
      self->owner->batch_lines <= 1 ||
      !self->enable_batching)
    return TRUE;

  iv_validate_now();
  now = iv_now;
  diff = timespec_diff_msec(&now, &self->last_flush_time);

  return (diff >= self->owner->batch_timeout);
}

static void
_stop_watches(LogThreadedDestWorker *self)
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
  if (iv_timer_registered(&self->timer_flush))
    {
      iv_timer_unregister(&self->timer_flush);
    }
}

/* NOTE: runs in the worker thread in response to a wakeup event being
 * posted, which happens if a new element is added to our queue while we
 * were sleeping */
static void
_wakeup_event_callback(gpointer data)
{
  LogThreadedDestWorker *self = (LogThreadedDestWorker *) data;

  if (!iv_task_registered(&self->do_work))
    {
      iv_task_register(&self->do_work);
    }
}

/* NOTE: runs in the worker thread in response to the shutdown event being
 * posted.  The shutdown event is initiated by the mainloop when the
 * configuration is deinited */
static void
_shutdown_event_callback(gpointer data)
{
  LogThreadedDestWorker *self = (LogThreadedDestWorker *) data;

  log_queue_reset_parallel_push(self->queue);
  _stop_watches(self);
  iv_quit();
}

/* NOTE: runs in the worker thread */
static void
_suspend(LogThreadedDestWorker *self)
{
  self->suspended = TRUE;
}

/* NOTE: runs in the worker thread */
static void
_connect(LogThreadedDestWorker *self)
{
  if (!log_threaded_dest_worker_connect(self))
    {
      msg_debug("Error establishing connection to server",
                evt_tag_str("driver", self->owner->super.super.id),
                evt_tag_int("worker_index", self->worker_index),
                log_expr_node_location_tag(self->owner->super.super.super.expr_node));
      _suspend(self);
    }
}

/* NOTE: runs in the worker thread */
static void
_disconnect(LogThreadedDestWorker *self)
{
  log_threaded_dest_worker_disconnect(self);
}

/* NOTE: runs in the worker thread */
static void
_disconnect_and_suspend(LogThreadedDestWorker *self)
{
  _disconnect(self);
  _suspend(self);
}

/* Accepts the current batch including the current message by acking it back
 * to the source.
 *
 * NOTE: runs in the worker thread */
static void
_accept_batch(LogThreadedDestWorker *self)
{
  log_threaded_dest_worker_ack_messages(self, self->batch_size);
}

/* NOTE: runs in the worker thread */
static void
_drop_batch(LogThreadedDestWorker *self)
{
  log_threaded_dest_worker_drop_messages(self, self->batch_size);
}

/* NOTE: runs in the worker thread */
static void
_rewind_batch(LogThreadedDestWorker *self)
{
  log_threaded_dest_worker_rewind_messages(self, self->batch_size);
}

static void
_process_result_drop(LogThreadedDestWorker *self)
{
  msg_error("Message(s) dropped while sending message to destination",
            evt_tag_str("driver", self->owner->super.super.id),
            evt_tag_int("worker_index", self->worker_index),
            evt_tag_int("time_reopen", self->time_reopen),
            evt_tag_int("batch_size", self->batch_size));

  _drop_batch(self);
  _disconnect_and_suspend(self);
}

static void
_process_result_error(LogThreadedDestWorker *self)
{
  self->retries_on_error_counter++;

  if (self->retries_on_error_counter >= self->owner->retries_on_error_max)
    {
      msg_error("Multiple failures while sending message(s) to destination, message(s) dropped",
                evt_tag_str("driver", self->owner->super.super.id),
                log_expr_node_location_tag(self->owner->super.super.super.expr_node),
                evt_tag_int("worker_index", self->worker_index),
                evt_tag_int("retries", self->retries_on_error_counter),
                evt_tag_int("batch_size", self->batch_size));

      _drop_batch(self);
    }
  else
    {
      msg_error("Error occurred while trying to send a message, trying again",
                evt_tag_str("driver", self->owner->super.super.id),
                log_expr_node_location_tag(self->owner->super.super.super.expr_node),
                evt_tag_int("worker_index", self->worker_index),
                evt_tag_int("retries", self->retries_on_error_counter),
                evt_tag_int("time_reopen", self->time_reopen),
                evt_tag_int("batch_size", self->batch_size));
      _rewind_batch(self);
      _disconnect_and_suspend(self);
    }
}

static void
_process_result_not_connected(LogThreadedDestWorker *self)
{
  msg_info("Server disconnected while preparing messages for sending, trying again",
           evt_tag_str("driver", self->owner->super.super.id),
           log_expr_node_location_tag(self->owner->super.super.super.expr_node),
           evt_tag_int("worker_index", self->worker_index),
           evt_tag_int("time_reopen", self->time_reopen),
           evt_tag_int("batch_size", self->batch_size));
  self->retries_counter = 0;
  _rewind_batch(self);
  _disconnect_and_suspend(self);
}

static void
_process_result_success(LogThreadedDestWorker *self)
{
  _accept_batch(self);
}

static void
_process_result_queued(LogThreadedDestWorker *self)
{
  self->enable_batching = TRUE;
}

static void
_process_result_retry(LogThreadedDestWorker *self)
{
  self->retries_counter++;
  if (self->retries_counter >= self->owner->retries_max)
    _process_result_not_connected(self);
  else
    _rewind_batch(self);
}

static void
_process_result(LogThreadedDestWorker *self, gint result)
{
  switch (result)
    {
    case LTR_DROP:
      _process_result_drop(self);
      break;

    case LTR_ERROR:
      _process_result_error(self);
      break;

    case LTR_NOT_CONNECTED:
      _process_result_not_connected(self);
      break;

    case LTR_EXPLICIT_ACK_MGMT:
      /* we require the instance to use explicit calls to ack_messages/rewind_messages */
      break;

    case LTR_SUCCESS:
      _process_result_success(self);
      break;

    case LTR_QUEUED:
      _process_result_queued(self);
      break;

    case LTR_RETRY:
      _process_result_retry(self);
      break;

    default:
      break;
    }

}

static void
_perform_flush(LogThreadedDestWorker *self)
{
  /* NOTE: earlier we had a condition on only calling flush() if batch_size
   * is non-zero.  This was removed, as the language bindings that were done
   * _before_ the batching support in LogThreadedDestDriver relies on
   * flush() being called always, even if LTR_SUCCESS is
   * returned, in which case batch_size is already zero at this point.
   */
  if (!self->suspended)
    {
      msg_trace("Flushing batch",
                evt_tag_str("driver", self->owner->super.super.id),
                evt_tag_int("worker_index", self->worker_index),
                evt_tag_int("batch_size", self->batch_size));

      LogThreadedResult result = log_threaded_dest_worker_flush(self, LTF_FLUSH_NORMAL);
      _process_result(self, result);
    }

  iv_invalidate_now();
}

/* NOTE: runs in the worker thread, whenever items on our queue are
 * available. It iterates all elements on the queue, however will terminate
 * if the mainloop requests that we exit. */
static void
_perform_inserts(LogThreadedDestWorker *self)
{
  LogMessage *msg;
  LogThreadedResult result;
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;

  if (self->batch_size == 0)
    {
      /* first message in the batch sets the last_flush_time, so we
       * won't expedite the flush even if the previous one was a long
       * time ago */

      iv_validate_now();
      self->last_flush_time = iv_now;
    }

  while (G_LIKELY(!self->owner->under_termination) &&
         !self->suspended &&
         (msg = log_queue_pop_head(self->queue, &path_options)) != NULL)
    {
      msg_set_context(msg);
      log_msg_refcache_start_consumer(msg, &path_options);

      self->batch_size++;
      ScratchBuffersMarker mark;
      scratch_buffers_mark(&mark);

      result = log_threaded_dest_worker_insert(self, msg);
      scratch_buffers_reclaim_marked(mark);

      _process_result(self, result);

      if (self->enable_batching && self->batch_size >= self->owner->batch_lines)
        _perform_flush(self);

      log_msg_unref(msg);
      msg_set_context(NULL);
      log_msg_refcache_stop();

      if (self->rewound_batch_size)
        {
          self->rewound_batch_size--;
          if (self->rewound_batch_size == 0)
            break;
        }

      iv_invalidate_now();
    }
  self->rewound_batch_size = 0;
}

/* this callback is invoked by LogQueue and is registered using
 * log_queue_check_items().  This only gets registered if at that point
 * we've decided to wait for the queue, e.g.  the work_task is not running.
 *
 * This callback is invoked from the source thread, e.g.  it is not safe to
 * do anything, but ensure that our thread is woken up in response.
 */
static void
_message_became_available_callback(gpointer user_data)
{
  LogThreadedDestWorker *self = (LogThreadedDestWorker *) user_data;

  if (!self->owner->under_termination)
    iv_event_post(&self->wake_up_event);
}

static void
_schedule_restart_on_suspend_timeout(LogThreadedDestWorker *self)
{
  iv_validate_now();
  self->timer_reopen.expires  = iv_now;
  self->timer_reopen.expires.tv_sec += self->time_reopen;
  iv_timer_register(&self->timer_reopen);
}

static void
_schedule_restart_on_batch_timeout(LogThreadedDestWorker *self)
{
  self->timer_flush.expires = self->last_flush_time;
  timespec_add_msec(&self->timer_flush.expires, self->owner->batch_timeout);
  iv_timer_register(&self->timer_flush);
}

static void
_schedule_restart(LogThreadedDestWorker *self)
{
  if (self->suspended)
    _schedule_restart_on_suspend_timeout(self);
  else
    iv_task_register(&self->do_work);
}

static void
_schedule_restart_on_next_flush(LogThreadedDestWorker *self)
{
  if (self->suspended)
    _schedule_restart_on_suspend_timeout(self);
  else if (!_should_flush_now(self))
    _schedule_restart_on_batch_timeout(self);
  else
    iv_task_register(&self->do_work);
}

static void
_schedule_restart_on_throttle_timeout(LogThreadedDestWorker *self, gint timeout_msec)
{
  iv_validate_now();
  self->timer_throttle.expires = iv_now;
  timespec_add_msec(&self->timer_throttle.expires, timeout_msec);
  iv_timer_register(&self->timer_throttle);
}

static void
_perform_work(gpointer data)
{
  LogThreadedDestWorker *self = (LogThreadedDestWorker *) data;
  gint timeout_msec = 0;

  self->suspended = FALSE;
  main_loop_worker_run_gc();
  _stop_watches(self);

  if (!self->connected)
    {
      /* try to connect and come back if successful, would be suspended otherwise. */
      _connect(self);
      _schedule_restart(self);
    }
  else if (log_queue_check_items(self->queue, &timeout_msec,
                                 _message_became_available_callback,
                                 self, NULL))
    {

      msg_trace("Message(s) available in queue, starting inserts",
                evt_tag_str("driver", self->owner->super.super.id),
                evt_tag_int("worker_index", self->worker_index));

      /* Something is in the queue, buffer them up and flush (if needed) */
      _perform_inserts(self);
      if (_should_flush_now(self))
        _perform_flush(self);
      _schedule_restart(self);
    }
  else if (self->batch_size > 0)
    {
      /* nothing in the queue, but there are pending elements in the buffer
       * (e.g.  batch size != 0).  perform a round of flushing.  We might
       * get back here, as the flush() routine doesn't have to flush
       * everything.  We are awoken either by the
       * _message_became_available_callback() or if the next flush time has
       * arrived.  */
      msg_trace("Queue empty, flushing previously buffered data",
                evt_tag_str("driver", self->owner->super.super.id),
                evt_tag_int("worker_index", self->worker_index));

      if (_should_flush_now(self))
        _perform_flush(self);
      _schedule_restart_on_next_flush(self);
    }
  else if (timeout_msec != 0)
    {
      /* We probably have some items in the queue, but timeout_msec is set,
       * indicating a throttle being active.
       * _message_became_available_callback() is not set up in this case.
       * we need to wake up after timeout_msec time.
       *
       * We are processing throttle delays _after_ we finished flushing, as
       * items in the queue were already accepted by throttling, so they can
       * be flushed.
       */
      msg_trace("Delaying output due to throttling",
                evt_tag_int("timeout_msec", timeout_msec),
                evt_tag_str("driver", self->owner->super.super.id),
                evt_tag_int("worker_index", self->worker_index));

      _schedule_restart_on_throttle_timeout(self, timeout_msec);

    }
  else
    {
      /* NOTE: at this point we are not doing anything but keep the
       * parallel_push callback alive, which will call
       * _message_became_available_callback(), which in turn wakes us up by
       * posting an event which causes this function to be run again
       *
       * NOTE/2: the parallel_push callback may need to be cancelled if we
       * need to exit.  That happens in the shutdown_event_callback(), or
       * here in this very function, as log_queue_check_items() will cancel
       * outstanding parallel push callbacks automatically.
       */
    }
}

void
log_threaded_dest_worker_wakeup_when_suspended(LogThreadedDestWorker *self)
{
  if (self->suspended)
    _perform_work(self);
}

static void
_flush_timer_cb(gpointer data)
{
  LogThreadedDestWorker *self = (LogThreadedDestWorker *) data;
  msg_trace("Flush timer expired",
            evt_tag_str("driver", self->owner->super.super.id),
            evt_tag_int("worker_index", self->worker_index),
            evt_tag_int("batch_size", self->batch_size));
  _perform_work(data);
}

/* these are events of the _worker_ thread and are not registered to the
 * actual main thread.  We basically run our workload in the handler of the
 * do_work task, which might be invoked in a number of ways.
 *
 * Basic states:
 *   1) disconnected state: _perform_work() will try to connect periodically
 *      using the suspend() mechanism, which uses a timer to get up periodically.
 *
 *   2) once connected:
 *      - if messages are already on the queue: flush them
 *
 *      - if no messages are on the queue: schedule
 *        _message_became_available_callback() to be called by the LogQueue.
 *
 *      - if there's an error, disconnect go back to the #1 state above.
 *
 */
static void
_init_watches(LogThreadedDestWorker *self)
{
  IV_EVENT_INIT(&self->wake_up_event);
  self->wake_up_event.cookie = self;
  self->wake_up_event.handler = _wakeup_event_callback;

  IV_EVENT_INIT(&self->shutdown_event);
  self->shutdown_event.cookie = self;
  self->shutdown_event.handler = _shutdown_event_callback;

  IV_TIMER_INIT(&self->timer_reopen);
  self->timer_reopen.cookie = self;
  self->timer_reopen.handler = _perform_work;

  IV_TIMER_INIT(&self->timer_throttle);
  self->timer_throttle.cookie = self;
  self->timer_throttle.handler = _perform_work;

  IV_TIMER_INIT(&self->timer_flush);
  self->timer_flush.cookie = self;
  self->timer_flush.handler = _flush_timer_cb;

  IV_TASK_INIT(&self->do_work);
  self->do_work.cookie = self;
  self->do_work.handler = _perform_work;
}

static void
_signal_startup_finished(LogThreadedDestWorker *self, gboolean thread_failure)
{
  g_mutex_lock(self->owner->lock);
  self->startup_finished = TRUE;
  self->startup_failure |= thread_failure;
  g_cond_signal(self->started_up);
  g_mutex_unlock(self->owner->lock);
}

static void
_signal_startup_success(LogThreadedDestWorker *self)
{
  _signal_startup_finished(self, FALSE);
}

static void
_signal_startup_failure(LogThreadedDestWorker *self)
{
  _signal_startup_finished(self, TRUE);
}

static void
_wait_for_startup_finished(LogThreadedDestWorker *self)
{
  g_mutex_lock(self->owner->lock);
  while (!self->startup_finished)
    g_cond_wait(self->started_up, self->owner->lock);
  g_mutex_unlock(self->owner->lock);
}

static void
_register_worker_stats(LogThreadedDestWorker *self)
{
  StatsClusterKey sc_key;

  stats_lock();
  _init_stats_key(self->owner, &sc_key);
  log_queue_register_stats_counters(self->queue, 0, &sc_key);
  stats_unlock();
}

static void
_unregister_worker_stats(LogThreadedDestWorker *self)
{
  StatsClusterKey sc_key;

  stats_lock();
  _init_stats_key(self->owner, &sc_key);
  log_queue_unregister_stats_counters(self->queue, &sc_key);
  stats_unlock();
}

static void
_perform_final_flush(LogThreadedDestWorker *self)
{
  GlobalConfig *cfg = log_pipe_get_config(&self->owner->super.super.super);
  LogThreadedResult result;
  LogThreadedFlushMode mode = LTF_FLUSH_NORMAL;

  if (!cfg_is_shutting_down(cfg))
    mode = LTF_FLUSH_EXPEDITE;

  result = log_threaded_dest_worker_flush(self, mode);
  _process_result(self, result);
  log_queue_rewind_backlog_all(self->queue);
}

static void
_worker_thread(gpointer arg)
{
  LogThreadedDestWorker *self = (LogThreadedDestWorker *) arg;

  iv_init();

  msg_debug("Dedicated worker thread started",
            evt_tag_int("worker_index", self->worker_index),
            evt_tag_str("driver", self->owner->super.super.id),
            log_expr_node_location_tag(self->owner->super.super.super.expr_node));

  iv_event_register(&self->wake_up_event);
  iv_event_register(&self->shutdown_event);

  if (!log_threaded_dest_worker_thread_init(self))
    goto error;

  _signal_startup_success(self);

  /* if we have anything on the backlog, that was a partial, potentially
   * not-flushed batch.  Rewind it, so we start with that */

  log_queue_rewind_backlog_all(self->queue);

  _schedule_restart(self);
  iv_main();

  _perform_final_flush(self);

  _disconnect(self);

  log_threaded_dest_worker_thread_deinit(self);

  msg_debug("Dedicated worker thread finished",
            evt_tag_int("worker_index", self->worker_index),
            evt_tag_str("driver", self->owner->super.super.id),
            log_expr_node_location_tag(self->owner->super.super.super.expr_node));

  goto ok;

error:
  _signal_startup_failure(self);
ok:
  iv_event_unregister(&self->wake_up_event);
  iv_event_unregister(&self->shutdown_event);
  iv_deinit();
}

static gboolean
_acquire_worker_queue(LogThreadedDestWorker *self)
{
  gchar *queue_persist_name = _format_queue_persist_name(self);
  self->queue = log_dest_driver_acquire_queue(&self->owner->super, queue_persist_name);
  g_free(queue_persist_name);

  if (!self->queue)
    return FALSE;

  log_queue_set_use_backlog(self->queue, TRUE);

  return TRUE;
}

gboolean
log_threaded_dest_worker_init_method(LogThreadedDestWorker *self)
{
  if (self->time_reopen == -1)
    self->time_reopen = self->owner->time_reopen;

  _register_worker_stats(self);

  return TRUE;
}

void
log_threaded_dest_worker_deinit_method(LogThreadedDestWorker *self)
{
  _unregister_worker_stats(self);
}

void
log_threaded_dest_worker_free_method(LogThreadedDestWorker *self)
{
  g_cond_free(self->started_up);
}

void
log_threaded_dest_worker_init_instance(LogThreadedDestWorker *self, LogThreadedDestDriver *owner, gint worker_index)
{
  self->worker_index = worker_index;
  self->thread_init = log_threaded_dest_worker_init_method;
  self->thread_deinit = log_threaded_dest_worker_deinit_method;
  self->free_fn = log_threaded_dest_worker_free_method;
  self->owner = owner;
  self->time_reopen = -1;
  self->started_up = g_cond_new();
  _init_watches(self);
}

void
log_threaded_dest_worker_free(LogThreadedDestWorker *self)
{
  if (self->free_fn)
    self->free_fn(self);
  g_free(self);
}

/* LogThreadedDestDriver */

void
log_threaded_dest_driver_set_num_workers(LogDriver *s, gint num_workers)
{
  LogThreadedDestDriver *self = (LogThreadedDestDriver *) s;

  self->num_workers = num_workers;
}

/* compatibility bridge between LogThreadedDestWorker */

static gboolean
_compat_thread_init(LogThreadedDestWorker *self)
{
  if (!log_threaded_dest_worker_init_method(self))
    return FALSE;

  /* NOTE: driver level thread_init() didn't have a gboolean return */
  if (self->owner->worker.thread_init)
    self->owner->worker.thread_init(self->owner);
  return TRUE;
}

static void
_compat_thread_deinit(LogThreadedDestWorker *self)
{
  if (self->owner->worker.thread_deinit)
    self->owner->worker.thread_deinit(self->owner);
  log_threaded_dest_worker_deinit_method(self);
}

static gboolean
_compat_connect(LogThreadedDestWorker *self)
{
  if (self->owner->worker.connect)
    return self->owner->worker.connect(self->owner);
  return TRUE;
}

static void
_compat_disconnect(LogThreadedDestWorker *self)
{
  if (self->owner->worker.disconnect)
    self->owner->worker.disconnect(self->owner);
}

static LogThreadedResult
_compat_insert(LogThreadedDestWorker *self, LogMessage *msg)
{
  return self->owner->worker.insert(self->owner, msg);
}

static LogThreadedResult
_compat_flush(LogThreadedDestWorker *self, LogThreadedFlushMode mode)
{
  if (self->owner->worker.flush)
    return self->owner->worker.flush(self->owner);
  return LTR_SUCCESS;
}

static void
_init_worker_compat_layer(LogThreadedDestWorker *self)
{
  self->thread_init = _compat_thread_init;
  self->thread_deinit = _compat_thread_deinit;
  self->connect = _compat_connect;
  self->disconnect = _compat_disconnect;
  self->insert = _compat_insert;
  self->flush = _compat_flush;
}

static gboolean
_is_worker_compat_mode(LogThreadedDestDriver *self)
{
  return !self->worker.construct;
}

/* temporary function until proper LogThreadedDestWorker allocation logic is
 * created.  Right now it is just using a singleton within the driver */
static LogThreadedDestWorker *
_construct_worker(LogThreadedDestDriver *self, gint worker_index)
{
  if (_is_worker_compat_mode(self))
    {
      /* kick in the compat layer, this case self->worker.instance is the
       * single worker we have and all Worker related state is in the
       * (derived) Driver class. */

      return &self->worker.instance;
    }
  return self->worker.construct(self, worker_index);
}

static void
_request_worker_exit(gpointer s)
{
  LogThreadedDestWorker *self = (LogThreadedDestWorker *) s;

  msg_debug("Shutting down dedicated worker thread",
            evt_tag_int("worker_index", self->worker_index),
            evt_tag_str("driver", self->owner->super.super.id),
            log_expr_node_location_tag(self->owner->super.super.super.expr_node));
  self->owner->under_termination = TRUE;
  iv_event_post(&self->shutdown_event);
}

static gboolean
_start_worker_thread(LogThreadedDestWorker *self)
{
  msg_debug("Starting dedicated worker thread",
            evt_tag_int("worker_index", self->worker_index),
            evt_tag_str("driver", self->owner->super.super.id),
            log_expr_node_location_tag(self->owner->super.super.super.expr_node));

  main_loop_create_worker_thread(_worker_thread,
                                 _request_worker_exit,
                                 self, &self->owner->worker_options);
  _wait_for_startup_finished(self);
  return !self->startup_failure;
}

void
log_threaded_dest_driver_set_max_retries_on_error(LogDriver *s, gint max_retries)
{
  LogThreadedDestDriver *self = (LogThreadedDestDriver *)s;

  self->retries_on_error_max = max_retries;
}

LogThreadedDestWorker *
_lookup_worker(LogThreadedDestDriver *self, LogMessage *msg)
{
  gint worker_index = self->last_worker % self->num_workers;
  self->last_worker++;

  /* here would come the lookup mechanism that maps msg -> worker that doesn't exist yet. */
  return self->workers[worker_index];
}

/* the feeding side of the driver, runs in the source thread and puts an
 * incoming message to the associated queue.
 */
static void
log_threaded_dest_driver_queue(LogPipe *s, LogMessage *msg,
                               const LogPathOptions *path_options)
{
  LogThreadedDestDriver *self = (LogThreadedDestDriver *)s;
  LogThreadedDestWorker *dw = _lookup_worker(self, msg);
  LogPathOptions local_options;

  if (!path_options->flow_control_requested)
    path_options = log_msg_break_ack(msg, path_options, &local_options);

  log_msg_add_ack(msg, path_options);
  log_queue_push_tail(dw->queue, log_msg_ref(msg), path_options);

  stats_counter_inc(self->processed_messages);

  log_dest_driver_queue_method(s, msg, path_options);
}

static void
_init_stats_key(LogThreadedDestDriver *self, StatsClusterKey *sc_key)
{
  stats_cluster_logpipe_key_set(sc_key, self->stats_source | SCS_DESTINATION,
                                self->super.super.id,
                                self->format_stats_instance(self));
}

static void
_register_stats(LogThreadedDestDriver *self)
{
  stats_lock();
  {
    StatsClusterKey sc_key;

    _init_stats_key(self, &sc_key);
    stats_register_counter(0, &sc_key, SC_TYPE_DROPPED, &self->dropped_messages);
    stats_register_counter(0, &sc_key, SC_TYPE_PROCESSED, &self->processed_messages);
    stats_register_counter(0, &sc_key, SC_TYPE_WRITTEN, &self->written_messages);
  }
  stats_unlock();
}

static void
_unregister_stats(LogThreadedDestDriver *self)
{
  stats_lock();
  {
    StatsClusterKey sc_key;

    _init_stats_key(self, &sc_key);
    stats_unregister_counter(&sc_key, SC_TYPE_DROPPED, &self->dropped_messages);
    stats_unregister_counter(&sc_key, SC_TYPE_PROCESSED, &self->processed_messages);
    stats_unregister_counter(&sc_key, SC_TYPE_WRITTEN, &self->written_messages);
  }
  stats_unlock();
}

static gchar *
_format_seqnum_persist_name(LogThreadedDestDriver *self)
{
  static gchar seqnum_persist_name[256];

  g_snprintf(seqnum_persist_name, sizeof(seqnum_persist_name), "%s.seqnum", log_pipe_get_persist_name((LogPipe *)self));
  return seqnum_persist_name;
}

static gboolean
_create_workers(LogThreadedDestDriver *self)
{
  /* free previous workers array if set to cope with num_workers change */
  g_free(self->workers);
  self->workers = g_new0(LogThreadedDestWorker *, self->num_workers);

  for (self->created_workers = 0; self->created_workers < self->num_workers; self->created_workers++)
    {
      LogThreadedDestWorker *dw = _construct_worker(self, self->created_workers);

      self->workers[self->created_workers] = dw;
      if (!_acquire_worker_queue(dw))
        return FALSE;
    }

  return TRUE;
}

static void
_assert_on_persist_name(LogThreadedDestDriver *self)
{
  // Make sure that generate_persist_name virtual function still returns
  // the same value as provided during the init phase. Because several
  // functionality of the LogThreadedDestDriver depends on it.
  g_assert_cmpstr(self->persist_name_for_assertion, ==, log_pipe_get_persist_name((LogPipe *)self));
}

gboolean
log_threaded_dest_driver_init_method(LogPipe *s, const gchar *persist_name)
{
  LogThreadedDestDriver *self = (LogThreadedDestDriver *)s;
  GlobalConfig *cfg = log_pipe_get_config(s);

  self->persist_name_for_assertion = g_strdup(persist_name);

  if (!log_dest_driver_init_method(&self->super.super.super))
    return FALSE;

  self->under_termination = FALSE;

  if (self->time_reopen == -1)
    self->time_reopen = cfg->time_reopen;

  self->shared_seq_num = GPOINTER_TO_INT(cfg_persist_config_fetch(cfg,
                                         _format_seqnum_persist_name(self)));
  if (!self->shared_seq_num)
    init_sequence_number(&self->shared_seq_num);

  _register_stats(self);

  if (!_create_workers(self))
    return FALSE;

  return TRUE;
}

gboolean
_log_threaded_dest_driver_init_method(LogPipe *s)
{
  const gchar *persist_name = log_pipe_get_persist_name(s);
  return log_threaded_dest_driver_init_method(s, persist_name);
}


/* This method is only used when a LogThreadedDestDriver is directly used
 * without overriding its on_config_inited method.  If there's an overridden
 * method, the caller is responsible for explicitly calling _start_workers() at
 * the end of on_config_inited(). */
gboolean
log_threaded_dest_driver_start_workers(LogPipe *s)
{
  LogThreadedDestDriver *self = (LogThreadedDestDriver *) s;

  _assert_on_persist_name(self);

  for (gint worker_index = 0; worker_index < self->num_workers; worker_index++)
    {
      if (!_start_worker_thread(self->workers[worker_index]))
        return FALSE;
    }
  return TRUE;
}

gboolean
log_threaded_dest_driver_deinit_method(LogPipe *s)
{
  LogThreadedDestDriver *self = (LogThreadedDestDriver *)s;

  _assert_on_persist_name(self);

  /* NOTE: workers are shut down by the time we get here, through the
   * request_exit mechanism of main loop worker threads */

  cfg_persist_config_add(log_pipe_get_config(s),
                         _format_seqnum_persist_name(self),
                         GINT_TO_POINTER(self->shared_seq_num), NULL, FALSE);

  g_free(self->persist_name_for_assertion);

  _unregister_stats(self);

  if (!_is_worker_compat_mode(self))
    {
      for (int i = 0; i < self->created_workers; i++)
        log_threaded_dest_worker_free(self->workers[i]);
    }

  return log_dest_driver_deinit_method(s);
}


void
log_threaded_dest_driver_free(LogPipe *s)
{
  LogThreadedDestDriver *self = (LogThreadedDestDriver *)s;

  log_threaded_dest_worker_free_method(&self->worker.instance);
  g_mutex_free(self->lock);
  g_free(self->workers);
  log_dest_driver_free((LogPipe *)self);
}

void
log_threaded_dest_driver_init_instance(LogThreadedDestDriver *self, GlobalConfig *cfg)
{
  log_dest_driver_init_instance(&self->super, cfg);

  self->worker_options.is_output_thread = TRUE;

  self->super.super.super.init = _log_threaded_dest_driver_init_method;
  self->super.super.super.deinit = log_threaded_dest_driver_deinit_method;
  self->super.super.super.queue = log_threaded_dest_driver_queue;
  self->super.super.super.free_fn = log_threaded_dest_driver_free;
  self->super.super.super.on_config_inited = log_threaded_dest_driver_start_workers;
  self->time_reopen = -1;
  self->batch_lines = -1;
  self->batch_timeout = -1;
  self->num_workers = 1;
  self->last_worker = 0;

  self->retries_on_error_max = MAX_RETRIES_ON_ERROR_DEFAULT;
  self->retries_max = MAX_RETRIES_BEFORE_SUSPEND_DEFAULT;
  self->lock = g_mutex_new();
  log_threaded_dest_worker_init_instance(&self->worker.instance, self, 0);
  _init_worker_compat_layer(&self->worker.instance);
}
