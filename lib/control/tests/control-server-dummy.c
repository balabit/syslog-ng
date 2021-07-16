/*
 * Copyright (c) 2018 Balabit
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
#include "control-server-dummy.h"
#include <string.h>

typedef struct _ControlConnectionDummy
{
  ControlConnection super;
  GString *output;
  GString *input;
} ControlConnectionDummy;

gint
control_connection_dummy_write(ControlConnection *s, gpointer buffer, gsize size)
{
  ControlConnectionDummy *self = (ControlConnectionDummy *)s;

  g_string_append_len(self->output, buffer, size);
  return size;
}

/* NOTE: only supports one read */
static gint
control_connection_dummy_read(ControlConnection *s, gpointer buffer, gsize size)
{
  ControlConnectionDummy *self = (ControlConnectionDummy *)s;
  strncpy(buffer, self->input->str, size);
  return MIN(self->input->len, size);
}

static void
control_connection_dummy_start_watches(ControlConnection *s)
{
  control_connection_update_watches(s);
}

static void
control_connection_dummy_stop_watches(ControlConnection *s)
{
}

static void
control_connection_dummy_update_watches(ControlConnection *s)
{
  if (s->waiting_for_output)
    g_assert_not_reached();
  else if (!g_queue_is_empty(s->response_batches) || s->output_buffer)
    s->handle_output(s);
}

static void
control_connection_dummy_free(ControlConnection *s)
{
  ControlConnectionDummy *self = (ControlConnectionDummy *)s;
  g_string_free(self->input, TRUE);
  g_string_free(self->output, TRUE);
}

void
control_connection_dummy_set_input(ControlConnection *s, const gchar *request)
{
  ControlConnectionDummy *self = (ControlConnectionDummy *)s;

  g_string_assign(self->input, request);
  g_string_append_c(self->input, '\n');
}

const gchar *
control_connection_dummy_get_output(ControlConnection *s)
{
  ControlConnectionDummy *self = (ControlConnectionDummy *)s;

  return self->output->str;
}

void
control_connection_dummy_reset_output(ControlConnection *s)
{
  ControlConnectionDummy *self = (ControlConnectionDummy *)s;

  g_string_assign(self->output, "");
  s->waiting_for_output = FALSE;
}

ControlConnection *
control_connection_dummy_new(ControlServer *server)
{
  ControlConnectionDummy *self = g_new0(ControlConnectionDummy, 1);

  control_connection_init_instance(&self->super, server);
  self->super.read = control_connection_dummy_read;
  self->super.write = control_connection_dummy_write;
  self->super.events.start_watches = control_connection_dummy_start_watches;
  self->super.events.update_watches = control_connection_dummy_update_watches;
  self->super.events.stop_watches = control_connection_dummy_stop_watches;
  self->super.free_fn = control_connection_dummy_free;

  self->input = g_string_new("");
  self->output = g_string_new("");
  return &self->super;
}

typedef struct _ControlServerDummy
{
  ControlServer super;
} ControlServerDummy;


ControlServer *
control_server_dummy_new(void)
{
  ControlServerDummy *self = g_new0(ControlServerDummy, 1);

  control_server_init_instance(&self->super, "dummy-socket-name");
  return &self->super;
}
