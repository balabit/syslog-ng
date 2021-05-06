/*
 * Copyright (c) 2012 Nagy, Attila <bra@fsn.hu>
 * Copyright (c) 2014 Balabit
 * Copyright (c) 2013 Viktor Tusa <tusa@balabit.hu>
 * Copyright (c) 2014 Gergely Nagy <algernon@balabit.hu>
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

#include "afstomp.h"
#include "afstomp-parser.h"
#include "plugin.h"
#include "messages.h"
#include "stats/stats-registry.h"
#include "logmsg/nvtable.h"
#include "logqueue.h"
#include "scratch-buffers.h"
#include "plugin-types.h"

#include <glib.h>
#include <stomp.h>
#include "logthrdest/logthrdestdrv.h"

typedef struct
{
  LogThreadedDestDriver super;

  gchar *destination;
  LogTemplate *body_template;

  gboolean persistent;
  gboolean ack_needed;

  gchar *host;
  gint port;

  gchar *user;
  gchar *password;

  LogTemplateOptions template_options;

  ValuePairs *vp;

  stomp_connection *conn;
} STOMPDestDriver;

/*
 * Configuration
 */

void
afstomp_dd_set_user(LogDriver *d, const gchar *user)
{
  STOMPDestDriver *self = (STOMPDestDriver *) d;

  g_free(self->user);
  self->user = g_strdup(user);
}

void
afstomp_dd_set_password(LogDriver *d, const gchar *password)
{
  STOMPDestDriver *self = (STOMPDestDriver *) d;

  g_free(self->password);
  self->password = g_strdup(password);
}

void
afstomp_dd_set_host(LogDriver *d, const gchar *host)
{
  STOMPDestDriver *self = (STOMPDestDriver *) d;

  g_free(self->host);
  self->host = g_strdup(host);
}

void
afstomp_dd_set_port(LogDriver *d, gint port)
{
  STOMPDestDriver *self = (STOMPDestDriver *) d;

  self->port = (int) port;
}

void
afstomp_dd_set_destination(LogDriver *d, const gchar *destination)
{
  STOMPDestDriver *self = (STOMPDestDriver *) d;

  g_free(self->destination);
  self->destination = g_strdup(destination);
}

void
afstomp_dd_set_body(LogDriver *d, LogTemplate *body_template)
{
  STOMPDestDriver *self = (STOMPDestDriver *) d;

  log_template_unref(self->body_template);
  self->body_template = body_template;
}

void
afstomp_dd_set_persistent(LogDriver *s, gboolean persistent)
{
  STOMPDestDriver *self = (STOMPDestDriver *) s;

  self->persistent = persistent;
}

void
afstomp_dd_set_ack(LogDriver *s, gboolean ack_needed)
{
  STOMPDestDriver *self = (STOMPDestDriver *) s;

  self->ack_needed = ack_needed;
}

void
afstomp_dd_set_value_pairs(LogDriver *s, ValuePairs *vp)
{
  STOMPDestDriver *self = (STOMPDestDriver *) s;

  value_pairs_unref(self->vp);
  self->vp = vp;
}


LogTemplateOptions *
afstomp_dd_get_template_options(LogDriver *s)
{
  STOMPDestDriver *self = (STOMPDestDriver *) s;

  return &self->template_options;
}

/*
 * Utilities
 */

static const gchar *
afstomp_dd_format_stats_instance(LogThreadedDestDriver *s)
{
  STOMPDestDriver *self = (STOMPDestDriver *) s;
  static gchar persist_name[1024];

  if (s->super.super.super.persist_name)
    g_snprintf(persist_name, sizeof(persist_name), "afstomp,%s", s->super.super.super.persist_name);
  else
    g_snprintf(persist_name, sizeof(persist_name), "afstomp,%s,%u,%s", self->host, self->port,
               self->destination);

  return persist_name;
}

static const gchar *
afstomp_dd_format_persist_name(const LogPipe *s)
{
  const STOMPDestDriver *self = (const STOMPDestDriver *)s;
  static gchar persist_name[1024];

  if (s->persist_name)
    g_snprintf(persist_name, sizeof(persist_name), "afstomp.%s", s->persist_name);
  else
    g_snprintf(persist_name, sizeof(persist_name), "afstomp(%s,%u,%s)", self->host, self->port,
               self->destination);

  return persist_name;
}

static void
afstomp_create_connect_frame(STOMPDestDriver *self, stomp_frame *frame)
{
  stomp_frame_init(frame, "CONNECT", sizeof("CONNECT"));
  stomp_frame_add_header(frame, "login", self->user);
  stomp_frame_add_header(frame, "passcode", self->password);
};

static gboolean afstomp_try_connect(STOMPDestDriver *self)
{
  return stomp_connect(&self->conn, self->host, self->port);
};

static gboolean
afstomp_send_frame(STOMPDestDriver *self, stomp_frame *frame)
{
  return stomp_write(self->conn, frame);
}

static gboolean
afstomp_dd_connect(STOMPDestDriver *self, gboolean reconnect)
{
  stomp_frame frame;

  if (reconnect && self->conn)
    return TRUE;

  if (!afstomp_try_connect(self))
    return FALSE;

  afstomp_create_connect_frame(self, &frame);
  if (!afstomp_send_frame(self, &frame))
    {
      msg_error("Sending CONNECT frame to STOMP server failed!");
      return FALSE;
    }

  gboolean frame_read = stomp_receive_frame(self->conn, &frame);
  if (!frame_read || strcmp(frame.command, "CONNECTED") != 0)
    {
      msg_debug("Error connecting to STOMP server, stomp server did not accept CONNECT request");
      stomp_frame_deinit(&frame);

      return FALSE;
    }
  msg_debug("Connecting to STOMP succeeded",
            evt_tag_str("driver", self->super.super.super.id));

  stomp_frame_deinit(&frame);

  return TRUE;
}

static void
afstomp_dd_disconnect(LogThreadedDestDriver *s)
{
  STOMPDestDriver *self = (STOMPDestDriver *)s;

  stomp_disconnect(&self->conn);
  self->conn = NULL;
}

/* TODO escape '\0' when passing down the value */
static gboolean
afstomp_vp_foreach(const gchar *name, TypeHint type, const gchar *value, gsize value_len,
                   gpointer user_data)
{
  stomp_frame *frame = (stomp_frame *) (user_data);

  stomp_frame_add_header(frame, name, value);

  return FALSE;
}

static void
afstomp_set_frame_body(STOMPDestDriver *self, GString *body, stomp_frame *frame, LogMessage *msg)
{
  if (self->body_template)
    {
      LogTemplateEvalOptions options = {&self->template_options, LTZ_LOCAL,
                                        self->super.worker.instance.seq_num, NULL
                                       };
      log_template_format(self->body_template, msg, &options, body);
      stomp_frame_set_body(frame, body->str, body->len);
    }
}

static gboolean
afstomp_worker_publish(STOMPDestDriver *self, LogMessage *msg)
{
  gboolean success = TRUE;
  GString *body = NULL;
  stomp_frame frame;
  stomp_frame recv_frame;
  gchar seq_num[16];

  if (!self->conn)
    {
      msg_error("STOMP server is not connected, not sending message!");
      return FALSE;
    }

  body = scratch_buffers_alloc();
  stomp_frame_init(&frame, "SEND", sizeof("SEND"));

  if (self->persistent)
    stomp_frame_add_header(&frame, "persistent", "true");

  stomp_frame_add_header(&frame, "destination", self->destination);
  if (self->ack_needed)
    {
      g_snprintf(seq_num, sizeof(seq_num), "%i", self->super.worker.instance.seq_num);
      stomp_frame_add_header(&frame, "receipt", seq_num);
    };

  LogTemplateEvalOptions options = {&self->template_options, LTZ_SEND, self->super.worker.instance.seq_num, NULL};
  value_pairs_foreach(self->vp, afstomp_vp_foreach, msg, &options, &frame);

  afstomp_set_frame_body(self, body, &frame, msg);

  if (!afstomp_send_frame(self, &frame))
    {
      msg_error("Error while inserting into STOMP server");
      success = FALSE;
    }

  if (success && self->ack_needed)
    success = stomp_receive_frame(self->conn, &recv_frame);

  return success;
}

static LogThreadedResult
afstomp_worker_insert(LogThreadedDestDriver *s, LogMessage *msg)
{
  STOMPDestDriver *self = (STOMPDestDriver *)s;

  if (!afstomp_dd_connect(self, TRUE))
    return LTR_NOT_CONNECTED;

  if (!afstomp_worker_publish (self, msg))
    return LTR_ERROR;

  return LTR_SUCCESS;
}

static void
afstomp_worker_thread_init(LogThreadedDestDriver *s)
{
  STOMPDestDriver *self = (STOMPDestDriver *) s;

  afstomp_dd_connect(self, FALSE);
}

static gboolean
afstomp_dd_init(LogPipe *s)
{
  STOMPDestDriver *self = (STOMPDestDriver *) s;
  GlobalConfig *cfg = log_pipe_get_config(s);

  if (!log_threaded_dest_driver_init_method(s, afstomp_dd_format_persist_name(s)))
    return FALSE;

  log_template_options_init(&self->template_options, cfg);

  self->conn = NULL;

  msg_verbose("Initializing STOMP destination",
              evt_tag_str("host", self->host),
              evt_tag_int("port", self->port),
              evt_tag_str("destination", self->destination));

  return TRUE;
}

static void
afstomp_dd_free(LogPipe *d)
{
  STOMPDestDriver *self = (STOMPDestDriver *) d;

  log_template_options_destroy(&self->template_options);

  g_free(self->destination);
  log_template_unref(self->body_template);
  g_free(self->user);
  g_free(self->password);
  g_free(self->host);
  value_pairs_unref(self->vp);
  log_threaded_dest_driver_free(d);
}

LogDriver *
afstomp_dd_new(GlobalConfig *cfg)
{
  STOMPDestDriver *self = g_new0(STOMPDestDriver, 1);

  log_threaded_dest_driver_init_instance(&self->super, cfg);
  self->super.super.super.super.init = afstomp_dd_init;
  self->super.super.super.super.free_fn = afstomp_dd_free;
  self->super.super.super.super.generate_persist_name = afstomp_dd_format_persist_name;

  self->super.worker.thread_init = afstomp_worker_thread_init;
  self->super.worker.disconnect = afstomp_dd_disconnect;
  self->super.worker.insert = afstomp_worker_insert;

  self->super.format_stats_instance = afstomp_dd_format_stats_instance;
  self->super.stats_source = stats_register_type("stomp");

  afstomp_dd_set_host((LogDriver *) self, "127.0.0.1");
  afstomp_dd_set_port((LogDriver *) self, 61613);
  afstomp_dd_set_destination((LogDriver *) self, "/topic/syslog");
  afstomp_dd_set_persistent((LogDriver *) self, TRUE);
  afstomp_dd_set_ack((LogDriver *) self, FALSE);

  log_template_options_defaults(&self->template_options);
  afstomp_dd_set_value_pairs(&self->super.super.super, value_pairs_new_default(cfg));

  return (LogDriver *) self;
}

extern CfgParser afstomp_dd_parser;

static Plugin afstomp_plugin =
{
  .type = LL_CONTEXT_DESTINATION,
  .name = "stomp",
  .parser = &afstomp_parser
};

gboolean
afstomp_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, &afstomp_plugin, 1);
  return TRUE;
}

const ModuleInfo module_info =
{
  .canonical_name = "afstomp",
  .version = SYSLOG_NG_VERSION,
  .description = "The afstomp module provides STOMP destination support for syslog-ng.",
  .core_revision = SYSLOG_NG_SOURCE_REVISION,
  .plugins = &afstomp_plugin,
  .plugins_len = 1,
};
