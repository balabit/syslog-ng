/*
 * Copyright (c) 2013, 2014, 2015 Balabit
 * Copyright (c) 2013, 2014, 2015 Gergely Nagy
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

#include "riemann.h"
#include "riemann-worker.h"
#include "string-list.h"
#include "stats/stats.h"

#include <riemann/riemann-client.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>


/*
 * RiemanDestDriver
 */

void
riemann_dd_set_server(LogDriver *d, const gchar *host)
{
  RiemannDestDriver *self = (RiemannDestDriver *)d;

  g_free(self->server);
  self->server = g_strdup(host);
}

void
riemann_dd_set_port(LogDriver *d, gint port)
{
  RiemannDestDriver *self = (RiemannDestDriver *)d;

  self->port = port;
}

void
riemann_dd_set_field_host(LogDriver *d, LogTemplate *value)
{
  RiemannDestDriver *self = (RiemannDestDriver *)d;

  log_template_unref(self->fields.host);
  self->fields.host = log_template_ref(value);
}

void
riemann_dd_set_field_service(LogDriver *d, LogTemplate *value)
{
  RiemannDestDriver *self = (RiemannDestDriver *)d;

  log_template_unref(self->fields.service);
  self->fields.service = log_template_ref(value);
}

void
riemann_dd_set_field_event_time(LogDriver *d, LogTemplate *value)
{
  RiemannDestDriver *self = (RiemannDestDriver *)d;

  log_template_unref(self->fields.event_time);
  self->fields.event_time = log_template_ref(value);
}

void
riemann_dd_set_event_time_unit(LogDriver *d, gint unit)
{
  RiemannDestDriver *self = (RiemannDestDriver *)d;

  self->fields.event_time_unit = unit;
}

void
riemann_dd_set_field_state(LogDriver *d, LogTemplate *value)
{
  RiemannDestDriver *self = (RiemannDestDriver *)d;

  log_template_unref(self->fields.state);
  self->fields.state = log_template_ref(value);
}

void
riemann_dd_set_field_description(LogDriver *d, LogTemplate *value)
{
  RiemannDestDriver *self = (RiemannDestDriver *)d;

  log_template_unref(self->fields.description);
  self->fields.description = log_template_ref(value);
}

void
riemann_dd_set_field_metric(LogDriver *d, LogTemplate *value)
{
  RiemannDestDriver *self = (RiemannDestDriver *)d;

  log_template_unref(self->fields.metric);
  self->fields.metric = log_template_ref(value);
}

void
riemann_dd_set_field_ttl(LogDriver *d, LogTemplate *value)
{
  RiemannDestDriver *self = (RiemannDestDriver *)d;

  log_template_unref(self->fields.ttl);
  self->fields.ttl = log_template_ref(value);
}

void
riemann_dd_set_field_tags(LogDriver *d, GList *taglist)
{
  RiemannDestDriver *self = (RiemannDestDriver *)d;

  string_list_free(self->fields.tags);
  self->fields.tags = taglist;
}

void
riemann_dd_set_field_attributes(LogDriver *d, ValuePairs *vp)
{
  RiemannDestDriver *self = (RiemannDestDriver *)d;

  value_pairs_unref(self->fields.attributes);
  self->fields.attributes = vp;
}

gboolean
riemann_dd_set_connection_type(LogDriver *d, const gchar *type)
{
  RiemannDestDriver *self = (RiemannDestDriver *)d;

  if (strcmp(type, "tcp") == 0)
    self->type = RIEMANN_CLIENT_TCP;
  else if (strcmp(type, "udp") == 0)
    self->type = RIEMANN_CLIENT_UDP;
  else if (strcmp(type, "tls") == 0)
    self->type = RIEMANN_CLIENT_TLS;
  else
    return FALSE;

  return TRUE;
}

void
riemann_dd_set_timeout(LogDriver *d, guint timeout)
{
  RiemannDestDriver *self = (RiemannDestDriver *)d;
  self->timeout = timeout;
}

void
riemann_dd_set_tls_cacert(LogDriver *d, const gchar *path)
{
  RiemannDestDriver *self = (RiemannDestDriver *)d;

  g_free(self->tls.cacert);
  self->tls.cacert = g_strdup(path);
}

void
riemann_dd_set_tls_cert(LogDriver *d, const gchar *path)
{
  RiemannDestDriver *self = (RiemannDestDriver *)d;

  g_free(self->tls.cert);
  self->tls.cert = g_strdup(path);
}

void
riemann_dd_set_tls_key(LogDriver *d, const gchar *path)
{
  RiemannDestDriver *self = (RiemannDestDriver *)d;

  g_free(self->tls.key);
  self->tls.key = g_strdup(path);
}

LogTemplateOptions *
riemann_dd_get_template_options(LogDriver *d)
{
  RiemannDestDriver *self = (RiemannDestDriver *)d;

  return &self->template_options;
}

/*
 * Utilities
 */

static const gchar *
riemann_dd_format_stats_instance(LogThreadedDestDriver *s)
{
  RiemannDestDriver *self = (RiemannDestDriver *)s;
  static gchar persist_name[1024];

  if (s->super.super.super.persist_name)
    g_snprintf(persist_name, sizeof(persist_name), "riemann,%s", s->super.super.super.persist_name);
  else
    g_snprintf(persist_name, sizeof(persist_name), "riemann,%s,%u", self->server, self->port);

  return persist_name;
}

static const gchar *
riemann_dd_format_persist_name(const LogPipe *s)
{
  const RiemannDestDriver *self = (const RiemannDestDriver *)s;
  static gchar persist_name[1024];

  if (s->persist_name)
    g_snprintf(persist_name, sizeof(persist_name), "riemann.%s", s->persist_name);
  else
    g_snprintf(persist_name, sizeof(persist_name), "riemann(%s,%u)", self->server, self->port);

  return persist_name;
}

static void
_value_pairs_always_exclude_properties(RiemannDestDriver *self)
{
  static const gchar *properties[] = {"host", "service", "description", "state",
                                      "ttl", "metric", "tags", NULL
                                     };
  gint i;

  if (!self->fields.attributes)
    {
      return;
    }

  for (i = 0; properties[i]; i++)
    value_pairs_add_glob_pattern(self->fields.attributes, properties[i], FALSE);
}

static gboolean
riemann_dd_init(LogPipe *s)
{
  RiemannDestDriver *self = (RiemannDestDriver *)s;
  GlobalConfig *cfg = log_pipe_get_config(s);

  log_template_options_init(&self->template_options, cfg);

  if (!self->server)
    self->server = g_strdup("127.0.0.1");
  if (self->port == -1)
    self->port = 5555;

  if (!log_threaded_dest_driver_init_method(s, riemann_dd_format_persist_name(s)))
    return FALSE;

  if (!self->fields.host)
    {
      self->fields.host = log_template_new(cfg, NULL);
      log_template_compile(self->fields.host, "${HOST}", NULL);
    }
  if (!self->fields.service)
    {
      self->fields.service = log_template_new(cfg, NULL);
      log_template_compile(self->fields.service, "${PROGRAM}", NULL);
    }

  if (!self->fields.event_time)
    {
      self->fields.event_time = log_template_new(cfg, NULL);
      log_template_compile(self->fields.event_time, "${UNIXTIME}", NULL);
      self->fields.event_time_unit = RIEMANN_EVENT_FIELD_TIME;
    }

  _value_pairs_always_exclude_properties(self);


  msg_verbose("Initializing Riemann destination",
              evt_tag_str("server", self->server),
              evt_tag_int("port", self->port),
              evt_tag_str("driver", self->super.super.super.id),
              log_pipe_location_tag(&self->super.super.super.super));

  return TRUE;
}

static void
riemann_dd_free(LogPipe *d)
{
  RiemannDestDriver *self = (RiemannDestDriver *)d;

  g_free(self->server);
  g_free(self->tls.cacert);
  g_free(self->tls.cert);
  g_free(self->tls.key);

  log_template_options_destroy(&self->template_options);


  log_template_unref(self->fields.host);
  log_template_unref(self->fields.service);
  log_template_unref(self->fields.event_time);
  log_template_unref(self->fields.state);
  log_template_unref(self->fields.description);
  log_template_unref(self->fields.metric);
  log_template_unref(self->fields.ttl);
  string_list_free(self->fields.tags);
  value_pairs_unref(self->fields.attributes);

  log_threaded_dest_driver_free(d);
}

LogDriver *
riemann_dd_new(GlobalConfig *cfg)
{
  RiemannDestDriver *self = g_new0(RiemannDestDriver, 1);

  log_threaded_dest_driver_init_instance(&self->super, cfg);

  self->super.super.super.super.init = riemann_dd_init;
  self->super.super.super.super.free_fn = riemann_dd_free;
  self->super.super.super.super.generate_persist_name = riemann_dd_format_persist_name;
  self->super.worker.construct = riemann_dw_new;

  self->super.format_stats_instance = riemann_dd_format_stats_instance;
  self->super.stats_source = stats_register_type("riemann");

  self->port = -1;
  self->type = RIEMANN_CLIENT_TCP;
  self->super.batch_lines = 0; /* don't inherit global value */

  log_template_options_defaults(&self->template_options);

  return (LogDriver *)self;
}
