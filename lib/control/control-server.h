/*
 * Copyright (c) 2002-2013 Balabit
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
#ifndef CONTROL_SERVER_H
#define CONTROL_SERVER_H

#include "syslog-ng.h"
#include "control.h"
#include "atomic.h"
#include <iv_event.h>
#include <stdio.h>

#define MAX_CONTROL_LINE_LENGTH 4096

struct _ControlConnection
{
  GAtomicCounter ref_cnt;
  GQueue *response_batches;
  GMutex *response_batches_lock;
  struct iv_event evt_response_added;
  gboolean waiting_for_output;
  gboolean watches_are_running;
  GString *input_buffer;
  GString *output_buffer;
  gsize pos;
  ControlServer *server;
  int (*read)(ControlConnection *self, gpointer buffer, gsize size);
  int (*write)(ControlConnection *self, gpointer buffer, gsize size);
  void (*handle_input)(gpointer s);
  void (*handle_output)(gpointer s);
  void (*free_fn)(ControlConnection *self);
  struct
  {
    void (*start_watches)(ControlConnection *self);
    void (*update_watches)(ControlConnection *self);
    void (*stop_watches)(ControlConnection *self);
  } events;

};

struct _ControlServer
{
  GList *worker_threads;
  gboolean cancelled;
  gchar *control_socket_name;
  void (*free_fn)(ControlServer *self);
};

void control_server_cancel_workers(ControlServer *self);
void control_server_connection_closed(ControlServer *self, ControlConnection *cc);
void control_server_start(ControlServer *self);
void control_server_free(ControlServer *self);
void control_server_init_instance(ControlServer *self, const gchar *path);
ControlServer *control_server_new(const gchar *path);

typedef GString *(*ControlConnectionCommand)(ControlConnection *cc, GString *command, gpointer user_data);
void control_connection_start_as_thread(ControlConnection *self, ControlConnectionCommand cmd_cb,
                                        GString *command, gpointer user_data);

void control_connection_send_reply(ControlConnection *self, GString *reply);
void control_connection_send_batched_reply(ControlConnection *self, GString *reply);
void control_connection_send_close_batch(ControlConnection *self);
void control_connection_start_watches(ControlConnection *self);
void control_connection_update_watches(ControlConnection *self);
void control_connection_stop_watches(ControlConnection *self);
void control_connection_init_instance(ControlConnection *self, ControlServer *server);

ControlConnection *control_connection_ref(ControlConnection *self);
void control_connection_unref(ControlConnection *self);

#endif
