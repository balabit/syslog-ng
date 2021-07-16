/*
 * Copyright (c) 2002-2014 Balabit
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

#include "control-server.h"
#include "control-commands.h"
#include "messages.h"
#include "str-utils.h"
#include "secret-storage/secret-storage.h"
#include "thread-utils.h"

#include <string.h>
#include <errno.h>

#include <iv.h>
#include <iv_event.h>

typedef struct _ThreadedCommandRunner
{
  ControlConnection *connection;
  GString *command;
  gpointer user_data;

  GThread *thread;
  struct
  {
    GMutex *state_lock;
    gboolean cancelled;
    gboolean finished;
  } real_thread;
  ControlConnectionCommand func;
  struct iv_event thread_finished;
} ThreadedCommandRunner;

static ThreadedCommandRunner *
_thread_command_runner_new(ControlConnection *cc, GString *cmd, gpointer user_data)
{
  ThreadedCommandRunner *self = g_new0(ThreadedCommandRunner, 1);
  self->connection = control_connection_ref(cc);
  self->command = g_string_new(cmd->str);
  self->user_data = user_data;
  self->real_thread.state_lock = g_mutex_new();

  return self;
}

static void
_thread_command_runner_free(ThreadedCommandRunner *self)
{
  g_mutex_free(self->real_thread.state_lock);
  g_string_free(self->command, TRUE);
  control_connection_unref(self->connection);
  g_free(self);
}

static void
_on_thread_finished(gpointer user_data)
{
  ThreadedCommandRunner *self = (ThreadedCommandRunner *) user_data;
  g_thread_join(self->thread);
  iv_event_unregister(&self->thread_finished);
  ControlServer *server = self->connection->server;
  server->worker_threads = g_list_remove(server->worker_threads, self);
  _thread_command_runner_free(self);
}

static void
_thread(gpointer user_data)
{
  ThreadedCommandRunner *self = (ThreadedCommandRunner *)user_data;
  GString *response = self->func(self->connection, self->command, self->user_data);
  if (response)
    control_connection_send_reply(self->connection, response);
  g_mutex_lock(self->real_thread.state_lock);
  {
    self->real_thread.finished = TRUE;
    if (!self->real_thread.cancelled)
      iv_event_post(&self->thread_finished);
  }
  g_mutex_unlock(self->real_thread.state_lock);
}

static void
_thread_command_runner_sync_run(ThreadedCommandRunner *self, ControlConnectionCommand func)
{
  msg_warning("Cannot start a separated thread - ControlServer is not running",
              evt_tag_str("command", self->command->str));

  GString *response = func(self->connection, self->command, self->user_data);
  if (response)
    control_connection_send_reply(self->connection, response);

  _thread_command_runner_free(self);
}

static void
_thread_command_runner_run(ThreadedCommandRunner *self, ControlConnectionCommand func)
{
  IV_EVENT_INIT(&self->thread_finished);
  self->thread_finished.handler = _on_thread_finished;
  self->thread_finished.cookie = self;
  self->func = func;

  if (!main_loop_is_control_server_running(main_loop_get_instance()))
    {
      _thread_command_runner_sync_run(self, func);
      return;
    }

  iv_event_register(&self->thread_finished);

  self->thread = g_thread_new(self->command->str, (GThreadFunc) _thread, self);
  ControlServer *server = self->connection->server;
  server->worker_threads = g_list_append(server->worker_threads, self);
}

void
control_connection_start_as_thread(ControlConnection *self, ControlConnectionCommand cmd_cb,
                                   GString *command, gpointer user_data)
{
  ThreadedCommandRunner *runner = _thread_command_runner_new(self, command, user_data);
  _thread_command_runner_run(runner, cmd_cb);
}

static void
_delete_thread_command_runner(gpointer data)
{
  ThreadedCommandRunner *self = (ThreadedCommandRunner *) data;
  gboolean has_to_free = FALSE;

  g_mutex_lock(self->real_thread.state_lock);
  {
    self->real_thread.cancelled = TRUE;
    if (!self->real_thread.finished)
      {
        has_to_free = TRUE;
      }
  }
  g_mutex_unlock(self->real_thread.state_lock);

  if (has_to_free)
    {
      g_thread_join(self->thread);
      _thread_command_runner_free(self);
    }
}

void
control_server_cancel_workers(ControlServer *self)
{
  if (self->worker_threads)
    {
      self->cancelled = TRUE; // racy, but it's okay
      msg_warning("Cancelling control server worker threads");
      g_list_free_full(self->worker_threads, _delete_thread_command_runner);
      msg_warning("Control server worker threads has been cancelled.");
      self->worker_threads = NULL;
    }
}

void
control_server_connection_closed(ControlServer *self, ControlConnection *cc)
{
  control_connection_stop_watches(cc);
  control_connection_unref(cc);
}

void
control_server_init_instance(ControlServer *self, const gchar *path)
{
  self->control_socket_name = g_strdup(path);
  self->worker_threads = NULL;
  self->cancelled = FALSE;
}

void
control_server_free(ControlServer *self)
{
  // it's not racy as the iv_main() is executed by this thread
  // posted events are not executed as iv_quit() is already called (before control_server_free)
  // but it is possible that some ThreadedCommandRunner::thread are still running
  if (self->worker_threads)
    g_list_free_full(self->worker_threads, _delete_thread_command_runner);

  if (self->free_fn)
    {
      self->free_fn(self);
    }
  g_free(self->control_socket_name);
  g_free(self);
}

static void
_g_string_destroy(gpointer user_data)
{
  GString *str = (GString *) user_data;
  g_string_free(str, TRUE);
}

static void
_control_connection_free(ControlConnection *self)
{
  if (self->free_fn)
    {
      self->free_fn(self);
    }
  /*
   * when write() fails, control_connection_closed() is called,
   * which then calls this free func and in this case output_buffer is not NULL
   * */
  if (self->output_buffer)
    g_string_free(self->output_buffer, TRUE);
  g_string_free(self->input_buffer, TRUE);
  g_queue_free_full(self->response_batches, _g_string_destroy);
  g_mutex_free(self->response_batches_lock);
  iv_event_unregister(&self->evt_response_added);
  g_free(self);
}

void
control_connection_send_batched_reply(ControlConnection *self, GString *reply)
{
  g_mutex_lock(self->response_batches_lock);
  g_queue_push_tail(self->response_batches, reply);
  g_mutex_unlock(self->response_batches_lock);

  self->waiting_for_output = FALSE;

  iv_event_post(&self->evt_response_added);
}

void
control_connection_send_close_batch(ControlConnection *self)
{
  g_mutex_lock(self->response_batches_lock);
  GString *last = (GString *) g_queue_peek_tail(self->response_batches);
  if (last)
    {
      if (last->str[last->len - 1] != '\n')
        g_string_append_c(last, '\n');
      g_string_append(last, ".\n");
      g_mutex_unlock(self->response_batches_lock);
    }
  else
    {
      g_mutex_unlock(self->response_batches_lock);
      control_connection_send_batched_reply(self, g_string_new("\n.\n"));
    }
}

void
control_connection_send_reply(ControlConnection *self, GString *reply)
{
  control_connection_send_batched_reply(self, reply);
  control_connection_send_close_batch(self);
}

static void
control_connection_io_output(gpointer s)
{
  ControlConnection *self = (ControlConnection *) s;
  gint rc;

  if (!self->output_buffer)
    {
      g_mutex_lock(self->response_batches_lock);
      self->output_buffer = g_queue_pop_head(self->response_batches);
      g_mutex_unlock(self->response_batches_lock);
      self->pos = 0;
    }

  g_assert(self->output_buffer);
  rc = self->write(self, self->output_buffer->str + self->pos, self->output_buffer->len - self->pos);
  if (rc < 0)
    {
      if (errno != EAGAIN)
        {
          msg_error("Error writing control channel",
                    evt_tag_error("error"));
          control_server_connection_closed(self->server, self);
          return;
        }
    }
  else
    {
      self->pos += rc;
      if (self->pos == self->output_buffer->len)
        {
          g_string_free(self->output_buffer, TRUE);
          g_mutex_lock(self->response_batches_lock);
          self->output_buffer = g_queue_pop_head(self->response_batches);
          g_mutex_unlock(self->response_batches_lock);
          self->pos = 0;
        }
    }
  control_connection_update_watches(self);
}

void
control_connection_wait_for_output(ControlConnection *self)
{
  g_mutex_lock(self->response_batches_lock);
  if (g_queue_is_empty(self->response_batches) && !self->output_buffer)
    self->waiting_for_output = TRUE;
  g_mutex_unlock(self->response_batches_lock);
  control_connection_update_watches(self);
}

static void
control_connection_io_input(void *s)
{
  ControlConnection *self = (ControlConnection *) s;
  GString *command = NULL;
  gchar *nl;
  gint rc;
  gint orig_len;
  GList *iter;

  if (self->input_buffer->len > MAX_CONTROL_LINE_LENGTH)
    {
      /* too much data in input, drop the connection */
      msg_error("Too much data in the control socket input buffer");
      control_server_connection_closed(self->server, self);
      return;
    }

  orig_len = self->input_buffer->len;

  /* NOTE: plus one for the terminating NUL */
  g_string_set_size(self->input_buffer, self->input_buffer->len + 128 + 1);
  rc = self->read(self, self->input_buffer->str + orig_len, 128);
  if (rc < 0)
    {
      if (errno != EAGAIN)
        {
          msg_error("Error reading command on control channel, closing control channel",
                    evt_tag_error("error"));
          goto destroy_connection;
        }
      /* EAGAIN, should try again when data comes */
      control_connection_update_watches(self);
      return;
    }
  else if (rc == 0)
    {
      msg_debug("EOF on control channel, closing connection");
      goto destroy_connection;
    }
  else
    {
      self->input_buffer->len = orig_len + rc;
      self->input_buffer->str[self->input_buffer->len] = 0;
    }

  /* here we have finished reading the input, check if there's a newline somewhere */
  nl = strchr(self->input_buffer->str, '\n');
  if (nl)
    {
      command = g_string_sized_new(128);
      /* command doesn't contain NL */
      g_string_assign_len(command, self->input_buffer->str, nl - self->input_buffer->str);
      secret_storage_wipe(self->input_buffer->str, nl - self->input_buffer->str);
      /* strip NL */
      /*g_string_erase(self->input_buffer, 0, command->len + 1);*/
      g_string_truncate(self->input_buffer, 0);
    }
  else
    {
      /* no EOL in the input buffer, wait for more data */
      control_connection_update_watches(self);
      return;
    }

  iter = g_list_find_custom(get_control_command_list(), command->str,
                            (GCompareFunc)control_command_start_with_command);
  if (iter == NULL)
    {
      msg_error("Unknown command read on control channel, closing control channel",
                evt_tag_str("command", command->str));
      g_string_free(command, TRUE);
      goto destroy_connection;
    }
  ControlCommand *cmd_desc = (ControlCommand *) iter->data;

  cmd_desc->func(self, command, cmd_desc->user_data);
  control_connection_wait_for_output(self);

  secret_storage_wipe(command->str, command->len);
  g_string_free(command, TRUE);
  return;
destroy_connection:
  control_server_connection_closed(self->server, self);
}

static void
_on_evt_response_added(gpointer user_data)
{
  ControlConnection *self = (ControlConnection *) user_data;
  control_connection_update_watches(self);
}

void
control_connection_init_instance(ControlConnection *self, ControlServer *server)
{
  g_atomic_counter_set(&self->ref_cnt, 1);
  self->server = server;
  self->input_buffer = g_string_sized_new(128);
  self->handle_input = control_connection_io_input;
  self->handle_output = control_connection_io_output;
  self->response_batches = g_queue_new();
  self->response_batches_lock = g_mutex_new();

  IV_EVENT_INIT(&self->evt_response_added);
  self->evt_response_added.cookie = self;
  self->evt_response_added.handler = _on_evt_response_added;

  iv_event_register(&self->evt_response_added);

  return;
}

ControlConnection *
control_connection_ref(ControlConnection *self)
{
  g_assert(!self || g_atomic_counter_get(&self->ref_cnt) > 0);

  if (self)
    g_atomic_counter_inc(&self->ref_cnt);

  return self;
}

void
control_connection_unref(ControlConnection *self)
{
  g_assert(!self || g_atomic_counter_get(&self->ref_cnt));

  if (self && (g_atomic_counter_dec_and_test(&self->ref_cnt)))
    _control_connection_free(self);
}

void
control_connection_start_watches(ControlConnection *self)
{
  if (self->events.start_watches)
    {
      self->watches_are_running = TRUE;
      self->events.start_watches(self);
    }
}

void
control_connection_update_watches(ControlConnection *self)
{
  if (!self->watches_are_running)
    return;

  if (self->events.update_watches)
    self->events.update_watches(self);
}

void
control_connection_stop_watches(ControlConnection *self)
{
  if (self->events.stop_watches)
    {
      self->events.stop_watches(self);
      self->watches_are_running = FALSE;
    }
}
