/*
 * Copyright (c) 2014 Pierre-Yves Ritschard <pyr@spootnik.org>
 * Copyright (c) 2013-2019 Balabit
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

#ifndef KAFKA_H_INCLUDED
#define KAFKA_H_INCLUDED

#include "logthrdest/logthrdestdrv.h"
#include <librdkafka/rdkafka.h>

typedef struct
{
  LogThreadedDestDriver super;

  LogTemplateOptions template_options;
  LogTemplate *key;
  LogTemplate *message;

  gchar *topic_name;
  GList *config;
  gchar *bootstrap_servers;
  rd_kafka_topic_t *topic;
  rd_kafka_t *kafka;
  gint flush_timeout_on_shutdown;
  gint flush_timeout_on_reload;
  gint poll_timeout;
  struct iv_timer poll_timer;
} KafkaDestDriver;

void kafka_dd_set_topic(LogDriver *d, const gchar *topic);
void kafka_dd_merge_config(LogDriver *d, GList *props);
void kafka_dd_set_bootstrap_servers(LogDriver *d, const gchar *bootstrap_servers);

void kafka_dd_set_key_ref(LogDriver *d, LogTemplate *key);
void kafka_dd_set_message_ref(LogDriver *d, LogTemplate *message);
void kafka_dd_set_flush_timeout_on_shutdown(LogDriver *d, gint shutdown_timeout);
void kafka_dd_set_flush_timeout_on_reload(LogDriver *d, gint reload_timeout);
void kafka_dd_set_poll_timeout(LogDriver *d, gint poll_timeout);

LogTemplateOptions *kafka_dd_get_template_options(LogDriver *d);

LogDriver *kafka_dd_new(GlobalConfig *cfg);

#endif
