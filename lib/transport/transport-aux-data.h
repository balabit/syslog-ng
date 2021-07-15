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
#ifndef TRANSPORT_TRANSPORT_AUX_DATA_H_INCLUDED
#define TRANSPORT_TRANSPORT_AUX_DATA_H_INCLUDED

#include "gsockaddr.h"
#include <string.h>

typedef struct _LogTransportAuxData
{
  GSockAddr *peer_addr;
  GSockAddr *local_addr;
  gint proto;
  gchar data[1536];
  gsize end_ptr;
} LogTransportAuxData;

static inline void
log_transport_aux_data_init(LogTransportAuxData *self)
{
  if (self)
    {
      self->peer_addr = NULL;
      self->local_addr = NULL;
      self->end_ptr = 0;
      self->data[0] = 0;
    }
}

static inline void
log_transport_aux_data_destroy(LogTransportAuxData *self)
{
  if (self)
    {
      g_sockaddr_unref(self->peer_addr);
      g_sockaddr_unref(self->local_addr);
    }
}

static inline void
log_transport_aux_data_reinit(LogTransportAuxData *self)
{
  log_transport_aux_data_destroy(self);
  log_transport_aux_data_init(self);
}

static inline void
log_transport_aux_data_copy(LogTransportAuxData *dst, LogTransportAuxData *src)
{
  gsize data_to_copy = sizeof(*src) - sizeof(src->data) + src->end_ptr;

  if (dst)
    {
      memcpy(dst, src, data_to_copy);
      g_sockaddr_ref(dst->peer_addr);
      g_sockaddr_ref(dst->local_addr);
    }
}

static inline void
log_transport_aux_data_set_peer_addr_ref(LogTransportAuxData *self, GSockAddr *peer_addr)
{
  if (self)
    {
      if (self->peer_addr)
        g_sockaddr_unref(self->peer_addr);
      self->peer_addr = peer_addr;
    }
}

static inline void
log_transport_aux_data_set_local_addr_ref(LogTransportAuxData *self, GSockAddr *local_addr)
{
  if (self)
    {
      if (self->local_addr)
        g_sockaddr_unref(self->local_addr);
      self->local_addr = local_addr;
    }
}

void log_transport_aux_data_add_nv_pair(LogTransportAuxData *self, const gchar *name, const gchar *value);
void log_transport_aux_data_foreach(LogTransportAuxData *self, void (*func)(const gchar *, const gchar *, gsize,
                                    gpointer), gpointer user_data);

#endif
