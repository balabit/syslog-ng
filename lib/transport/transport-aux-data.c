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
 */
#include "transport-aux-data.h"
#include "messages.h"

#include <sys/uio.h>

void
log_transport_aux_data_add_nv_pair(LogTransportAuxData *self, const gchar *name, const gchar *value)
{
  if (!self)
    return;

  gsize name_len = strlen(name);
  gsize value_len = strlen(value);
  if (self->end_ptr + name_len + 1 + value_len + 1 + 1 < sizeof(self->data))
    {
      /* copy NUL too */
      memcpy(&self->data[self->end_ptr], name, name_len + 1);
      self->end_ptr += name_len + 1;
      memcpy(&self->data[self->end_ptr], value, value_len + 1);
      self->end_ptr += value_len + 1;
      /* append final NULL that indicates the end of name-value pairs */
      self->data[self->end_ptr] = 0;
    }
  else
    {
      static gboolean warned = FALSE;

      if (!warned)
        {
          msg_notice("Transport aux data overflow, some fields may not be associated with the message, please increase aux buffer size",
                     evt_tag_int("aux_size", sizeof(self->data)));
          warned = TRUE;
        }
    }
}

void
log_transport_aux_data_foreach(LogTransportAuxData *self, void (*func)(const gchar *, const gchar *, gsize, gpointer),
                               gpointer user_data)
{
  const gchar *p;

  if (!self)
    return;

  p = self->data;
  while (*p)
    {
      const gchar *name = p;
      gsize name_len = strlen(p);
      const gchar *value = name + name_len + 1;
      gsize value_len = strlen(value);

      func(name, value, value_len, user_data);
      p = value + value_len + 1;
    }
}
