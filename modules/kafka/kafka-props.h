/*
 * Copyright (c) 2018 Kokan <kokaipeter@gmail.com>
 * Copyright (c) 2014 Pierre-Yves Ritschard <pyr@spootnik.org>
 * Copyright (c) 2019 Balabit
 * Copyright (c) 2019 Balazs Scheidler
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#ifndef KAFKA_PROPS_H_INCLUDED
#define KAFKA_PROPS_H_INCLUDED

#include "syslog-ng.h"

typedef struct _KafkaProperty
{
  gchar *name;
  gchar *value;
} KafkaProperty;

KafkaProperty *kafka_property_new(const gchar *name, const gchar *value);
void kafka_property_free(KafkaProperty *self);
void kafka_property_list_free(GList *l);

#endif
