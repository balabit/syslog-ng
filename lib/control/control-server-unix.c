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

#include "control-server.h"
#include "messages.h"

#include "gsocket.h"

#include <iv.h>

typedef struct _ControlServerUnix
{
  ControlServer super;
  gint control_socket;
  struct iv_fd control_listen;
} ControlServerUnix;

typedef struct _ControlConnectionUnix
{
  ControlConnection super;
  struct iv_fd control_io;
  gint fd;
} ControlConnectionUnix;

gint
control_connection_unix_write(ControlConnection *s, gpointer buffer, gsize size)
{
  ControlConnectionUnix *self = (ControlConnectionUnix *)s;
  return write(self->control_io.fd, buffer, size);
}

gint
control_connection_unix_read(ControlConnection *s, gpointer buffer, gsize size)
{
  ControlConnectionUnix *self = (ControlConnectionUnix *)s;
  return read(self->control_io.fd, buffer, size);
}

static void
control_connection_unix_start_watches(ControlConnection *s)
{
  ControlConnectionUnix *self = (ControlConnectionUnix *)s;
  IV_FD_INIT(&self->control_io);
  self->control_io.cookie = self;
  self->control_io.fd = self->fd;
  iv_fd_register(&self->control_io);

  control_connection_update_watches(s);
}

static void
control_connection_unix_stop_watches(ControlConnection *s)
{
  ControlConnectionUnix *self = (ControlConnectionUnix *)s;
  iv_fd_unregister(&self->control_io);
}

static gboolean
_pending_output(ControlConnection *s)
{
  g_mutex_lock(s->response_batches_lock);
  gboolean result = (s->output_buffer && s->output_buffer->len > s->pos) ||
                    !g_queue_is_empty(s->response_batches);
  g_mutex_unlock(s->response_batches_lock);
  return result;
}

static void
control_connection_unix_update_watches(ControlConnection *s)
{
  ControlConnectionUnix *self = (ControlConnectionUnix *)s;

  if (s->waiting_for_output)
    {
      iv_fd_set_handler_out(&self->control_io, NULL);
      iv_fd_set_handler_in(&self->control_io, NULL);
    }
  else if (_pending_output(s))
    {
      iv_fd_set_handler_out(&self->control_io, s->handle_output);
      iv_fd_set_handler_in(&self->control_io, NULL);
    }
  else
    {
      iv_fd_set_handler_out(&self->control_io, NULL);
      iv_fd_set_handler_in(&self->control_io, s->handle_input);
    }
}

void
control_connection_unix_free(ControlConnection *s)
{
  ControlConnectionUnix *self = (ControlConnectionUnix *)s;
  close(self->control_io.fd);
}

ControlConnection *
control_connection_new(ControlServer *server, gint sock)
{
  ControlConnectionUnix *self = g_new0(ControlConnectionUnix, 1);

  control_connection_init_instance(&self->super, server);
  self->fd = sock;
  self->super.free_fn = control_connection_unix_free;
  self->super.read = control_connection_unix_read;
  self->super.write = control_connection_unix_write;
  self->super.events.start_watches = control_connection_unix_start_watches;
  self->super.events.update_watches = control_connection_unix_update_watches;
  self->super.events.stop_watches = control_connection_unix_stop_watches;

  return &self->super;
}

static void
control_socket_accept(void *cookie)
{
  ControlServerUnix *self = (ControlServerUnix *)cookie;
  gint conn_socket;
  GSockAddr *peer_addr;
  GIOStatus status;
  ControlConnection *cc;

  if (self->control_socket == -1)
    return;
  status = g_accept(self->control_socket, &conn_socket, &peer_addr);
  if (status != G_IO_STATUS_NORMAL)
    {
      msg_error("Error accepting control socket connection",
                evt_tag_error("error"));
      goto error;
    }


  cc = control_connection_new(&self->super, conn_socket);

  /* NOTE: with the call below, the reference to the control connection (cc)
   * will be taken over by the various I/O callbacks, Those will return to
   * us (ControlServer) via the control_server_connection_closed() function,
   * which in turn will properly dispose the ControlConnection instance.
   */

  control_connection_start_watches(cc);
  g_sockaddr_unref(peer_addr);
error:
  ;
}

void
control_server_start(ControlServer *s)
{
  ControlServerUnix *self = (ControlServerUnix *)s;
  GSockAddr *saddr;

  saddr = g_sockaddr_unix_new(self->super.control_socket_name);
  self->control_socket = socket(PF_UNIX, SOCK_STREAM, 0);
  if (self->control_socket == -1)
    {
      msg_error("Error opening control socket, external controls will not be available",
                evt_tag_str("socket", self->super.control_socket_name));
      return;
    }
  if (g_bind(self->control_socket, saddr) != G_IO_STATUS_NORMAL)
    {
      msg_error("Error opening control socket, bind() failed",
                evt_tag_str("socket", self->super.control_socket_name),
                evt_tag_error("error"));
      goto error;
    }
  if (listen(self->control_socket, 255) < 0)
    {
      msg_error("Error opening control socket, listen() failed",
                evt_tag_str("socket", self->super.control_socket_name),
                evt_tag_error("error"));
      goto error;
    }

  self->control_listen.fd = self->control_socket;
  self->control_listen.cookie = self;
  iv_fd_register(&self->control_listen);
  iv_fd_set_handler_in(&self->control_listen, control_socket_accept);

  g_sockaddr_unref(saddr);
  return;
error:
  if (self->control_socket != -1)
    {
      close(self->control_socket);
      self->control_socket = -1;
    }
  g_sockaddr_unref(saddr);
  return;
}

void
control_server_unix_free(ControlServer *s)
{
  ControlServerUnix *self = (ControlServerUnix *)s;
  if (iv_fd_registered(&self->control_listen))
    {
      iv_fd_unregister(&self->control_listen);
    }
  if (self->control_socket != -1)
    {
      close(self->control_socket);
    }
}

ControlServer *
control_server_new(const gchar *path)
{
  ControlServerUnix *self = g_new(ControlServerUnix, 1);

  control_server_init_instance(&self->super, path);
  IV_FD_INIT(&self->control_listen);
  self->super.free_fn = control_server_unix_free;
  return &self->super;
}
