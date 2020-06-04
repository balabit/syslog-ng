/*
 * Copyright (c) 2013 Tihamer Petrovics <tihameri@gmail.com>
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
#include "kafka-dest-worker.h"
#include "kafka-dest-driver.h"
#include "str-utils.h"
#include <zlib.h>


typedef struct _KafkaDestWorker
{
  LogThreadedDestWorker super;
  GString *key;
  GString *message;
} KafkaDestWorker;

static void
_format_message_and_key(KafkaDestWorker *self, LogMessage *msg)
{
  KafkaDestDriver *owner = (KafkaDestDriver *) self->super.owner;

  log_template_format(owner->message, msg, &owner->template_options, LTZ_SEND,
                      self->super.seq_num, NULL, self->message);

  if (owner->key)
    log_template_format(owner->key, msg, &owner->template_options, LTZ_SEND,
                        self->super.seq_num, NULL, self->key);
}

static gboolean
_publish_message(KafkaDestWorker *self, LogMessage *msg)
{
  KafkaDestDriver *owner = (KafkaDestDriver *) self->super.owner;

  if (rd_kafka_produce(owner->topic,
                       RD_KAFKA_PARTITION_UA,
                       RD_KAFKA_MSG_F_FREE,
                       self->message->str, self->message->len,
                       self->key->len ? self->key->str : NULL, self->key->len,
                       log_msg_ref(msg)) == -1)
    {
      msg_error("kafka: failed to publish message",
                evt_tag_str("topic", owner->topic_name),
                evt_tag_str("error", rd_kafka_err2str(rd_kafka_last_error())),
                evt_tag_str("driver", owner->super.super.super.id),
                log_pipe_location_tag(&owner->super.super.super.super));

      return FALSE;
    }

  msg_debug("kafka: message published",
            evt_tag_str("topic", owner->topic_name),
            evt_tag_str("key", self->key->len ? self->key->str : "NULL"),
            evt_tag_str("message", self->message->str),
            evt_tag_str("driver", owner->super.super.super.id),
            log_pipe_location_tag(&owner->super.super.super.super));

  /* we passed the allocated buffers to rdkafka, which will eventually free them */
  g_string_steal(self->message);
  return TRUE;
}

/*
 * Worker thread
 */

static LogThreadedResult
kafka_dest_worker_insert(LogThreadedDestWorker *s, LogMessage *msg)
{
  KafkaDestWorker *self = (KafkaDestWorker *)s;

  _format_message_and_key(self, msg);
  // rdkafka buffer is full
  if (!_publish_message(self, msg))
    return LTR_RETRY;

  return LTR_SUCCESS;
}

static LogThreadedResult
kafka_dest_worker_flush(LogThreadedDestWorker *s, LogThreadedFlushMode mode)
{
  KafkaDestDriver* owner = (KafkaDestDriver*) s->owner;
  gint count = rd_kafka_poll(owner->kafka, 0);
  if (count != 0)
    {
      msg_trace("kafka: destination side (worker) rd_kafka_poll() processed some responses",
                evt_tag_str("topic", owner->topic_name),
                evt_tag_int("count", count),
                evt_tag_str("driver", owner->super.super.super.id),
                log_pipe_location_tag(&owner->super.super.super.super));
    }
  return LTR_SUCCESS;
}

static void
kafka_dest_worker_free(LogThreadedDestWorker *s)
{
  KafkaDestWorker *self = (KafkaDestWorker *)s;
  g_string_free(self->key, TRUE);
  g_string_free(self->message, TRUE);
  log_threaded_dest_worker_free_method(s);
}

LogThreadedDestWorker *
kafka_dest_worker_new(LogThreadedDestDriver *o, gint worker_index)
{
  KafkaDestWorker *self = g_new0(KafkaDestWorker, 1);

  log_threaded_dest_worker_init_instance(&self->super, o, worker_index);
  self->super.insert = kafka_dest_worker_insert;
  self->super.flush = kafka_dest_worker_flush;
  self->super.free_fn = kafka_dest_worker_free;

  self->key = g_string_sized_new(0);
  self->message = g_string_sized_new(1024);
  return &self->super;
}
