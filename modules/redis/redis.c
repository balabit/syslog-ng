/*
 * Copyright (c) 2014 Balabit
 * Copyright (c) 2013 Tihamer Petrovics <tihameri@gmail.com>
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

#include <hiredis/hiredis.h>

#include "redis.h"
#include "redis-parser.h"
#include "plugin.h"
#include "stats/stats-registry.h"
#include "logqueue.h"
#include "driver.h"
#include "plugin-types.h"
#include "logthrdest/logthrdestdrv.h"

#include "redis-worker.h"


/*
 * Configuration
 */

void
redis_dd_set_host(LogDriver *d, const gchar *host)
{
  RedisDriver *self = (RedisDriver *)d;

  g_free(self->host);
  self->host = g_strdup(host);
}

void
redis_dd_set_port(LogDriver *d, gint port)
{
  RedisDriver *self = (RedisDriver *)d;

  self->port = (int)port;
}

void
redis_dd_set_auth(LogDriver *d, const gchar *auth)
{
  RedisDriver *self = (RedisDriver *)d;

  g_free(self->auth);
  self->auth = g_strdup(auth);
}

void
redis_dd_set_timeout(LogDriver *d, const glong timeout)
{
  RedisDriver *self = (RedisDriver *)d;
  self->timeout.tv_sec = timeout;

}

static void
_template_unref(gpointer data)
{
  LogTemplate *template = (LogTemplate *)data;
  log_template_unref(template);
}

void
redis_dd_set_command_ref(LogDriver *d, const gchar *command,
                         GList *arguments)
{
  RedisDriver *self = (RedisDriver *)d;

  g_string_assign(self->command, command);
  g_list_free_full(self->arguments, _template_unref);
  self->arguments = arguments;
}

LogTemplateOptions *
redis_dd_get_template_options(LogDriver *d)
{
  RedisDriver *self = (RedisDriver *)d;

  return &self->template_options;
}

/*
 * Utilities
 */

static const gchar *
redis_dd_format_stats_instance(LogThreadedDestDriver *d)
{
  RedisDriver *self = (RedisDriver *)d;
  static gchar persist_name[1024];

  if (d->super.super.super.persist_name)
    g_snprintf(persist_name, sizeof(persist_name), "redis,%s", d->super.super.super.persist_name);
  else
    g_snprintf(persist_name, sizeof(persist_name), "redis,%s,%u", self->host, self->port);

  return persist_name;
}

static const gchar *
redis_dd_format_persist_name(const LogPipe *s)
{
  const RedisDriver *self = (const RedisDriver *)s;
  static gchar persist_name[1024];

  if (s->persist_name)
    g_snprintf(persist_name, sizeof(persist_name), "redis.%s", s->persist_name);
  else
    g_snprintf(persist_name, sizeof(persist_name), "redis(%s,%u)", self->host, self->port);

  return persist_name;
}




/*
 * Main thread
 */

static gboolean
redis_dd_init(LogPipe *s)
{
  RedisDriver *self = (RedisDriver *)s;
  GlobalConfig *cfg = log_pipe_get_config(s);

  if (g_list_length(self->arguments) == 0)
    {
      msg_error("Error initializing Redis destination, command option MUST be set",
                log_pipe_location_tag(s));
      return FALSE;
    }

  if (!log_threaded_dest_driver_init_method(s))
    return FALSE;

  log_template_options_init(&self->template_options, cfg);

  msg_verbose("Initializing Redis destination",
              evt_tag_str("driver", self->super.super.super.id),
              evt_tag_str("host", self->host),
              evt_tag_int("port", self->port));

  return TRUE;
}

static void
redis_dd_free(LogPipe *d)
{
  RedisDriver *self = (RedisDriver *) d;

  log_template_options_destroy(&self->template_options);

  g_free(self->host);
  g_free(self->auth);
  g_string_free(self->command, TRUE);
  g_list_free_full(self->arguments, _template_unref);

  log_threaded_dest_driver_free(d);
}

/*
 * Plugin glue.
 */

LogDriver *
redis_dd_new(GlobalConfig *cfg)
{
  RedisDriver *self = g_new0(RedisDriver, 1);

  log_threaded_dest_driver_init_instance(&self->super, cfg);
  self->super.super.super.super.init = redis_dd_init;
  self->super.super.super.super.free_fn = redis_dd_free;
  self->super.super.super.super.generate_persist_name = redis_dd_format_persist_name;

  self->super.worker.construct = redis_dw_new;

  self->super.format_stats_instance = redis_dd_format_stats_instance;
  self->super.stats_source = stats_register_type("redis");

  redis_dd_set_host((LogDriver *)self, "127.0.0.1");
  redis_dd_set_port((LogDriver *)self, 6379);
  redis_dd_set_auth((LogDriver *)self, NULL);
  redis_dd_set_timeout((LogDriver *)self, 10);

  self->command = g_string_sized_new(32);

  log_template_options_defaults(&self->template_options);

  return (LogDriver *)self;
}

extern CfgParser redis_dd_parser;

static Plugin redis_plugin =
{
  .type = LL_CONTEXT_DESTINATION,
  .name = "redis",
  .parser = &redis_parser,
};

gboolean
redis_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, &redis_plugin, 1);

  return TRUE;
}

const ModuleInfo module_info =
{
  .canonical_name = "redis",
  .version = SYSLOG_NG_VERSION,
  .description = "The afredis module provides Redis destination support for syslog-ng.",
  .core_revision = SYSLOG_NG_SOURCE_REVISION,
  .plugins = &redis_plugin,
  .plugins_len = 1,
};
