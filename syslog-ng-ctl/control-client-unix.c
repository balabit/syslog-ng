/*
 * Copyright (c) 2002-2013 Balabit
 * Copyright (c) 1998-2013 Balázs Scheidler
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

#include "control-client.h"
#include "gsocket.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

struct _ControlClient
{
  gchar *path;
  gint control_socket;
};

ControlClient *
control_client_new(const gchar *name)
{
  ControlClient *self = g_new(ControlClient, 1);

  self->path = g_strdup(name);
  self->control_socket = -1;

  return self;
}

gboolean
control_client_connect(ControlClient *self)
{
  GSockAddr *saddr = NULL;

  if (self->control_socket != -1)
    {
      return TRUE;
    }

  saddr = g_sockaddr_unix_new(self->path);
  self->control_socket = socket(PF_UNIX, SOCK_STREAM, 0);

  if (self->control_socket == -1)
    {
      fprintf(stderr, "Error opening control socket, socket='%s', error='%s'\n", self->path, strerror(errno));
      goto error;
    }

  if (g_connect(self->control_socket, saddr) != G_IO_STATUS_NORMAL)
    {
      fprintf(stderr, "Error connecting control socket, socket='%s', error='%s'\n", self->path, strerror(errno));
      close(self->control_socket);
      self->control_socket = -1;
      goto error;
    }
error:
  g_sockaddr_unref(saddr);
  return (self->control_socket != -1);
}

gint
control_client_send_command(ControlClient *self, const gchar *cmd)
{
  return write(self->control_socket, cmd, strlen(cmd));
}

#define BUFF_LEN 8192

gint
control_client_read_reply(ControlClient *self, response_handle response_handler, gpointer user_data)
{
  gint retval = 0;
  gssize len = 0;
  gchar buff[BUFF_LEN];
  gboolean received_all = FALSE;

  while (!received_all)
    {
      if ((len = read(self->control_socket, buff, BUFF_LEN - 1)) < 0)
        {
          fprintf(stderr, "Error reading control socket, error='%s'\n", strerror(errno));
          return 1;
        }

      if (len == 0)
        {
          fprintf(stderr, "EOF occurred while reading control socket\n");
          return 1;
        }

      GString *reply = g_string_new_len(buff, len);
      if (reply->str[reply->len - 1] == '\n' &&
          reply->str[reply->len - 2] == '.' &&
          reply->str[reply->len - 3] == '\n')
        {
          g_string_truncate(reply, reply->len - 2);
          received_all = TRUE;
        }
      retval = response_handler(reply, user_data);
      g_string_free(reply, TRUE);
      if (retval != 0)
        break;
    }

  return retval;
}

void
control_client_free(ControlClient *self)
{
  if (self->control_socket != -1)
    {
      shutdown(self->control_socket, SHUT_RDWR);
      close(self->control_socket);
    }
  g_free(self->path);
  g_free(self);
}
