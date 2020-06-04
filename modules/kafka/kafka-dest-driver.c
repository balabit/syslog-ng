/*
 * Copyright (c) 2013 Tihamer Petrovics <tihameri@gmail.com>
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


#include "kafka-dest-driver.h"
#include "kafka-props.h"
#include "kafka-dest-worker.h"
#include "timeutils/misc.h"
#include "stats/stats-registry.h"

#include <librdkafka/rdkafka.h>
#include <stdlib.h>

/*
 * Configuration
 */

void
kafka_dd_set_topic(LogDriver *d, const gchar *topic)
{
  KafkaDestDriver *self = (KafkaDestDriver *)d;

  g_free(self->topic_name);
  self->topic_name = g_strdup(topic);
}

void
kafka_dd_merge_config(LogDriver *d, GList *props)
{
  KafkaDestDriver *self = (KafkaDestDriver *)d;

  self->config = g_list_concat(self->config, props);
}

void
kafka_dd_set_bootstrap_servers(LogDriver *d, const gchar *bootstrap_servers)
{
  KafkaDestDriver *self = (KafkaDestDriver *)d;

  g_free(self->bootstrap_servers);
  self->bootstrap_servers = g_strdup(bootstrap_servers);
}

void
kafka_dd_set_key_ref(LogDriver *d, LogTemplate *key)
{
  KafkaDestDriver *self = (KafkaDestDriver *)d;

  log_template_unref(self->key);
  self->key = key;
}

void
kafka_dd_set_message_ref(LogDriver *d, LogTemplate *message)
{
  KafkaDestDriver *self = (KafkaDestDriver *)d;

  log_template_unref(self->message);
  self->message = message;
}

void
kafka_dd_set_flush_timeout_on_shutdown(LogDriver *d, gint flush_timeout_on_shutdown)
{
  KafkaDestDriver *self = (KafkaDestDriver *)d;

  self->flush_timeout_on_shutdown = flush_timeout_on_shutdown;
}

void
kafka_dd_set_flush_timeout_on_reload(LogDriver *d, gint flush_timeout_on_reload)
{
  KafkaDestDriver *self = (KafkaDestDriver *)d;

  self->flush_timeout_on_reload = flush_timeout_on_reload;
}

void
kafka_dd_set_poll_timeout(LogDriver *d, gint poll_timeout)
{
  KafkaDestDriver *self = (KafkaDestDriver *)d;

  self->poll_timeout = poll_timeout;
}

LogTemplateOptions *
kafka_dd_get_template_options(LogDriver *d)
{
  KafkaDestDriver *self = (KafkaDestDriver *)d;

  return &self->template_options;
}

/* methods */

static const gchar *
_format_stats_instance(LogThreadedDestDriver *d)
{
  KafkaDestDriver *self = (KafkaDestDriver *)d;
  static gchar stats_name[1024];

  g_snprintf(stats_name, sizeof(stats_name), "kafka,%s", self->topic_name);
  return stats_name;
}

static const gchar *
_format_persist_name(const LogPipe *d)
{
  const KafkaDestDriver *self = (const KafkaDestDriver *)d;
  static gchar persist_name[1024];

  if (d->persist_name)
    g_snprintf(persist_name, sizeof(persist_name), "kafka.%s", d->persist_name);
  else
    g_snprintf(persist_name, sizeof(persist_name), "kafka(%s)", self->topic_name);
  return persist_name;
}

void
_kafka_log_callback(const rd_kafka_t *rkt, int level, const char *fac, const char *msg)
{
  gchar *buf = g_strdup_printf("librdkafka: %s(%d): %s", fac, level, msg);
  msg_event_send(msg_event_create(level, buf, NULL));
  g_free(buf);
}

static void
_kafka_delivery_report_cb(rd_kafka_t *rk,
                          void *payload, size_t len,
                          rd_kafka_resp_err_t err,
                          void *opaque, void *msg_opaque)
{
  KafkaDestDriver *self = (KafkaDestDriver *) opaque;
  LogMessage *msg = (LogMessage *) msg_opaque;
  log_msg_unref(msg);

  if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
    // message.send.max.retries exceeded the message was dropped because kafka
    // isn't available.
    stats_counter_inc(self->super.dropped_messages);
    msg_trace("kafka: dropped a message",
              evt_tag_str("topic", self->topic_name),
              evt_tag_str("error", rd_kafka_err2str(err)),
              evt_tag_str("driver", self->super.super.super.id),
              log_pipe_location_tag(&self->super.super.super.super));
  } else {
    stats_counter_inc(self->super.written_messages);
  }
}

static void
_conf_set_prop(rd_kafka_conf_t *conf, const gchar *name, const gchar *value)
{
  gchar errbuf[1024];

  msg_debug("kafka: setting librdkafka config property",
            evt_tag_str("name", name),
            evt_tag_str("value", value));
  if (rd_kafka_conf_set(conf, name, value, errbuf, sizeof(errbuf)) < 0)
    {
      msg_error("kafka: error setting librdkafka config property",
                evt_tag_str("name", name),
                evt_tag_str("value", value),
                evt_tag_str("error", errbuf));
    }
}

/*
 * Main thread
 */

static gboolean
_apply_config_props(rd_kafka_conf_t *conf, GList *props)
{
  GList *ll;

  for (ll = props; ll != NULL; ll = g_list_next(ll))
    {
      KafkaProperty *kp = ll->data;
      _conf_set_prop(conf, kp->name, kp->value);
    }
  return TRUE;
}

static rd_kafka_t *
_construct_client(KafkaDestDriver *self)
{
  rd_kafka_t *client;
  rd_kafka_conf_t *conf;
  gchar errbuf[1024];

  conf = rd_kafka_conf_new();
  _conf_set_prop(conf, "metadata.broker.list", self->bootstrap_servers);
  _conf_set_prop(conf, "topic.partitioner", "murmur2_random");

  _apply_config_props(conf, self->config);
  rd_kafka_conf_set_log_cb(conf, _kafka_log_callback);
  rd_kafka_conf_set_dr_cb(conf, _kafka_delivery_report_cb);
  rd_kafka_conf_set_opaque(conf, self);
  client = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errbuf, sizeof(errbuf));
  if (!client)
    {
      msg_error("kafka: error constructing the kafka connection object",
                evt_tag_str("topic", self->topic_name),
                evt_tag_str("error", errbuf),
                evt_tag_str("driver", self->super.super.super.id),
                log_pipe_location_tag(&self->super.super.super.super));
    }
  return client;
}

rd_kafka_topic_t *
_construct_topic(KafkaDestDriver *self)
{
  g_assert(self->kafka != NULL);

  return rd_kafka_topic_new(self->kafka, self->topic_name, NULL);
}

static LogThreadedDestWorker *
_construct_worker(LogThreadedDestDriver *s, gint worker_index)
{
  return kafka_dest_worker_new(s, worker_index);
}

static gint
_get_flush_timeout(KafkaDestDriver *self)
{
  GlobalConfig *cfg = log_pipe_get_config(&self->super.super.super.super);
  if (cfg_is_shutting_down(cfg))
    return self->flush_timeout_on_shutdown;
  return self->flush_timeout_on_reload;
}

static void
_flush_inflight_messages(KafkaDestDriver *self)
{
  rd_kafka_resp_err_t err;
  gint outq_len = rd_kafka_outq_len(self->kafka);
  gint timeout = _get_flush_timeout(self);

  if (outq_len > 0)
    {
      msg_notice("kafka: shutting down kafka producer, while messages are still in-flight, waiting for messages to flush",
                 evt_tag_str("topic", self->topic_name),
                 evt_tag_int("outq_len", outq_len),
                 evt_tag_int("timeout", timeout),
                 evt_tag_str("driver", self->super.super.super.id),
                 log_pipe_location_tag(&self->super.super.super.super));
    }
  err = rd_kafka_flush(self->kafka, timeout);
  if (err != RD_KAFKA_RESP_ERR_NO_ERROR)
    {
      msg_error("kafka: error flushing accumulated messages during shutdown, rd_kafka_flush() returned failure, this might indicate that some in-flight messages are lost",
                evt_tag_str("topic", self->topic_name),
                evt_tag_int("outq_len", rd_kafka_outq_len(self->kafka)),
                evt_tag_str("error", rd_kafka_err2str(err)),
                evt_tag_str("driver", self->super.super.super.id),
                log_pipe_location_tag(&self->super.super.super.super));
    }
  outq_len = rd_kafka_outq_len(self->kafka);

  if (outq_len != 0)
    msg_notice("kafka: timeout while waiting for the librdkafka queue to empty, the "
               "remaining entries will be purged and readded to the syslog-ng queue",
               evt_tag_int("timeout", timeout),
               evt_tag_int("outq_len", outq_len));
}

static void
_purge_remaining_messages(KafkaDestDriver *self)
{
  /* we are purging all messages, those ones that are sitting in the queue
   * and also those that were sent and not yet acknowledged.  The purged
   * messages will generate failed delivery reports, which in turn will put
   * them back to the head of our queue. */

  /* FIXME: Need to check their order!!!! */

  rd_kafka_purge(self->kafka, RD_KAFKA_PURGE_F_QUEUE | RD_KAFKA_PURGE_F_INFLIGHT);
  rd_kafka_poll(self->kafka, 0);

  gint outq_len = rd_kafka_outq_len(self->kafka);
  if (outq_len != 0)
    msg_notice("kafka: failed to completely empty rdkafka queues, as we still have entries in "
               "the queue after flush() and purge(), this is probably causing a memory leak, "
               "please contact syslog-ng authors for support",
               evt_tag_int("outq_len", outq_len));

}

static void
_update_drain_timer(KafkaDestDriver *self)
{
  if (iv_timer_registered(&self->poll_timer))
    iv_timer_unregister(&self->poll_timer);
  iv_validate_now();
  self->poll_timer.expires = iv_now;
  timespec_add_msec(&self->poll_timer.expires, self->poll_timeout);
  iv_timer_register(&self->poll_timer);
}

static void
_drain_responses(KafkaDestDriver *self)
{
  _update_drain_timer(self);
  gint count = rd_kafka_poll(self->kafka, 0);
  if (count != 0)
    {
      msg_trace("kafka: destination side (driver) rd_kafka_poll() processed some responses",
                evt_tag_str("topic", self->topic_name),
                evt_tag_int("count", count),
                evt_tag_str("driver", self->super.super.super.id),
                log_pipe_location_tag(&self->super.super.super.super));
    }
}


static gboolean
kafka_dd_init(LogPipe *s)
{
  KafkaDestDriver *self = (KafkaDestDriver *)s;
  GlobalConfig *cfg = log_pipe_get_config(s);

  if (!log_threaded_dest_driver_init_method(s))
    return FALSE;

  if (!self->topic_name)
    {
      msg_error("kafka: the topic() argument is required for kafka destinations",
                evt_tag_str("driver", self->super.super.super.id),
                log_pipe_location_tag(&self->super.super.super.super));
      return FALSE;
    }

  self->kafka = _construct_client(self);
  if (self->kafka == NULL)
    {
      msg_error("kafka: error constructing kafka connection object, perhaps metadata.broker.list property is missing?",
                evt_tag_str("topic", self->topic_name),
                evt_tag_str("driver", self->super.super.super.id),
                log_pipe_location_tag(&self->super.super.super.super));
      return FALSE;
    }

  self->topic = _construct_topic(self);
  if (self->topic == NULL)
    {
      msg_error("kafka: error constructing the kafka topic object",
                evt_tag_str("topic", self->topic_name),
                evt_tag_str("driver", self->super.super.super.id),
                log_pipe_location_tag(&self->super.super.super.super));
      return FALSE;
    }

  if (self->message == NULL)
    {
      self->message = log_template_new(cfg, NULL);
      log_template_compile(self->message, "$ISODATE $HOST $MSGHDR$MSG", NULL);
    }

  log_template_options_init(&self->template_options, cfg);
  msg_verbose("kafka: Kafka destination initialized",
              evt_tag_str("topic", self->topic_name),
              evt_tag_str("key", self->key ? self->key->template : "NULL"),
              evt_tag_str("message", self->message->template),
              evt_tag_str("driver", self->super.super.super.id),
              log_pipe_location_tag(&self->super.super.super.super));

  IV_TIMER_INIT(&self->poll_timer);
  self->poll_timer.cookie = self;
  self->poll_timer.handler = (void (*)(void *)) _drain_responses;
  self->poll_timer.expires = iv_now;
  self->poll_timer.expires.tv_sec++;
  self->poll_timer.expires.tv_nsec = 0;
  iv_timer_register(&self->poll_timer);

  return log_threaded_dest_driver_start_workers(&self->super);
}

static gboolean
kafka_dd_deinit(LogPipe *s)
{
  KafkaDestDriver *self = (KafkaDestDriver *)s;

  _flush_inflight_messages(self);
  _purge_remaining_messages(self);
  return log_threaded_dest_driver_deinit_method(s);
}

static void
kafka_dd_free(LogPipe *d)
{
  KafkaDestDriver *self = (KafkaDestDriver *)d;

  log_template_options_destroy(&self->template_options);

  log_template_unref(self->key);
  log_template_unref(self->message);
  if (self->topic)
    rd_kafka_topic_destroy(self->topic);
  if (self->kafka)
    rd_kafka_destroy(self->kafka);
  if (self->topic_name)
    g_free(self->topic_name);
  if (iv_timer_registered(&self->poll_timer))
    iv_timer_unregister(&self->poll_timer);
  g_free(self->bootstrap_servers);
  kafka_property_list_free(self->config);
  log_threaded_dest_driver_free(d);
}

/*
 * Plugin glue.
 */

LogDriver *
kafka_dd_new(GlobalConfig *cfg)
{
  KafkaDestDriver *self = g_new0(KafkaDestDriver, 1);

  log_threaded_dest_driver_init_instance(&self->super, cfg);
  self->super.super.super.super.init = kafka_dd_init;
  self->super.super.super.super.deinit = kafka_dd_deinit;
  self->super.super.super.super.free_fn = kafka_dd_free;
  self->super.super.super.super.generate_persist_name = _format_persist_name;

  self->super.format_stats_instance = _format_stats_instance;
  self->super.stats_source = stats_register_type("kafka");
  self->super.worker.construct = _construct_worker;
  /* one minute */
  self->flush_timeout_on_shutdown = 60000;
  self->flush_timeout_on_reload = 1000;
  self->poll_timeout = 1000;

  log_template_options_defaults(&self->template_options);

  return (LogDriver *)self;
}
