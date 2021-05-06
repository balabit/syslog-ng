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
#include "messages.h"
#include "stats/stats-registry.h"
#include "logqueue.h"
#include "driver.h"
#include "plugin-types.h"
#include "logthrdest/logthrdestdrv.h"
#include "scratch-buffers.h"
#include "utf8utils.h"

typedef struct
{
  LogThreadedDestDriver super;

  gchar *host;
  gint   port;
  gchar *auth;

  LogTemplateOptions template_options;

  GString *command;
  GList *arguments;

  /* Worker thread */
  redisContext *c;
  gint argc;
  gchar **argv;
  size_t *argvlen;
  struct timeval timeout;
} RedisDriver;

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

static inline void
_trace_reply_message(redisReply *r)
{
  if (trace_flag)
    {
      if (r->elements > 0)
        {
          msg_trace(">>>>>> REDIS command reply begin",
                    evt_tag_long("elements", r->elements));

          for (gsize i = 0; i < r->elements; i++)
            {
              _trace_reply_message(r->element[i]);
            }

          msg_trace("<<<<<< REDIS command reply end");
        }
      else if (r->type == REDIS_REPLY_STRING || r->type == REDIS_REPLY_STATUS || r->type == REDIS_REPLY_ERROR)
        {
          msg_trace("REDIS command reply",
                    evt_tag_str("str", r->str));
        }
      else
        {
          msg_trace("REDIS command unhandled reply",
                    evt_tag_int("type", r->type));
        }
    }
}

static gboolean
send_redis_command(RedisDriver *self, const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  redisReply *reply = redisvCommand(self->c, format, ap);
  va_end(ap);

  gboolean retval = reply && (reply->type != REDIS_REPLY_ERROR);
  if (reply)
    {
      _trace_reply_message(reply);
      freeReplyObject(reply);
    }
  return retval;
}

static gboolean
check_connection_to_redis(RedisDriver *self)
{
  return send_redis_command(self, "ping");
}

static gboolean
authenticate_to_redis(RedisDriver *self, const gchar *password)
{
  return send_redis_command(self, "AUTH %s", self->auth);
}

static gboolean
redis_dd_connect(LogThreadedDestDriver *s)
{
  RedisDriver *self = (RedisDriver *)s;

  if (self->c && check_connection_to_redis(self))
    return TRUE;

  if (self->c)
    redisFree(self->c);

  self->c = redisConnectWithTimeout(self->host, self->port, self->timeout);

  if (self->c == NULL || self->c->err)
    {
      if (self->c)
        {
          msg_error("REDIS server error during connection",
                    evt_tag_str("driver", self->super.super.super.id),
                    evt_tag_str("error", self->c->errstr),
                    evt_tag_int("time_reopen", self->super.time_reopen));
        }
      else
        {
          msg_error("REDIS server can't allocate redis context");
        }
      return FALSE;
    }

  if (self->auth)
    if (!authenticate_to_redis(self, self->auth))
      {
        msg_error("REDIS: failed to authenticate",
                  evt_tag_str("driver", self->super.super.super.id));
        return FALSE;
      }

  if (!check_connection_to_redis(self))
    {
      msg_error("REDIS: failed to connect",
                evt_tag_str("driver", self->super.super.super.id));
      return FALSE;
    }

  if (self->c->err)
    return FALSE;

  msg_debug("Connecting to REDIS succeeded",
            evt_tag_str("driver", self->super.super.super.id));

  return TRUE;
}

static void
redis_dd_disconnect(LogThreadedDestDriver *s)
{
  RedisDriver *self = (RedisDriver *)s;

  if (self->c)
    redisFree(self->c);
  self->c = NULL;
}

static inline void _fill_template(RedisDriver *self, LogMessage *msg, LogTemplate *template, gchar **str, gsize *size)
{
  if (log_template_is_trivial(template))
    {
      gssize unsigned_size;
      *str = (gchar *)log_template_get_trivial_value(template, msg, &unsigned_size);
      *size = unsigned_size;
    }
  else
    {
      GString *buffer = scratch_buffers_alloc();
      LogTemplateEvalOptions options = {&self->template_options, LTZ_SEND,
                                        self->super.worker.instance.seq_num, NULL
                                       };
      log_template_format(template, msg, &options, buffer);
      *size = buffer->len;
      *str = buffer->str;
    }
}

static void
_fill_argv_from_template_list(RedisDriver *self, LogMessage *msg)
{
  for (gint i = 1; i < self->argc; i++)
    {
      LogTemplate *redis_command = g_list_nth_data(self->arguments, i-1);
      _fill_template(self, msg, redis_command, &self->argv[i], &self->argvlen[i]);
    }
}

static const gchar *
_argv_to_string(RedisDriver *self)
{
  GString *full_command = scratch_buffers_alloc();

  full_command = g_string_append(full_command, self->argv[0]);
  for (gint i = 1; i < self->argc; i++)
    {
      g_string_append(full_command, " \"");
      append_unsafe_utf8_as_escaped_text(full_command, self->argv[i], self->argvlen[i], "\"");
      g_string_append(full_command, "\"");
    }
  return full_command->str;
}

/*
 * Worker thread
 */

static LogThreadedResult
redis_worker_insert(LogThreadedDestDriver *s, LogMessage *msg)
{
  RedisDriver *self = (RedisDriver *)s;

  ScratchBuffersMarker marker;
  scratch_buffers_mark(&marker);

  _fill_argv_from_template_list(self, msg);

  redisReply *reply = redisCommandArgv(self->c, self->argc, (const gchar **)self->argv, self->argvlen);

  if (!reply)
    {
      msg_error("REDIS server error, suspending",
                evt_tag_str("driver", self->super.super.super.id),
                evt_tag_str("command", _argv_to_string(self)),
                evt_tag_str("error", self->c->errstr),
                evt_tag_int("time_reopen", self->super.time_reopen));
      scratch_buffers_reclaim_marked(marker);
      return LTR_ERROR;
    }

  msg_debug("REDIS command sent",
            evt_tag_str("driver", self->super.super.super.id),
            evt_tag_str("command", _argv_to_string(self)));

  freeReplyObject(reply);
  scratch_buffers_reclaim_marked(marker);
  return LTR_SUCCESS;
}

static void
redis_worker_thread_init(LogThreadedDestDriver *d)
{
  RedisDriver *self = (RedisDriver *)d;

  self->argc = g_list_length(self->arguments) + 1;
  self->argv = g_malloc(self->argc * sizeof(char *));
  self->argvlen = g_malloc(self->argc * sizeof(size_t));
  self->argv[0] = self->command->str;
  self->argvlen[0] = self->command->len;

  msg_debug("Worker thread started",
            evt_tag_str("driver", self->super.super.super.id));
}

static void
redis_worker_thread_deinit(LogThreadedDestDriver *d)
{
  RedisDriver *self = (RedisDriver *)d;

  g_free(self->argv);
  g_free(self->argvlen);
  redis_dd_disconnect(d);
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

  if (!log_threaded_dest_driver_init_method(s, redis_dd_format_persist_name(s)))
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
  RedisDriver *self = (RedisDriver *)d;

  log_template_options_destroy(&self->template_options);

  g_free(self->host);
  g_free(self->auth);
  g_string_free(self->command, TRUE);
  g_list_free_full(self->arguments, _template_unref);
  if (self->c)
    redisFree(self->c);

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

  self->super.worker.thread_init = redis_worker_thread_init;
  self->super.worker.thread_deinit = redis_worker_thread_deinit;
  self->super.worker.connect = redis_dd_connect;
  self->super.worker.disconnect = redis_dd_disconnect;
  self->super.worker.insert = redis_worker_insert;

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
