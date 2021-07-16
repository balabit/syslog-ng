/*
 * Copyright (c) 2014 Balabit
 * Copyright (c) 2013 Tihamer Petrovics <tihameri@gmail.com>
 * Copyright (c) 2014 Gergely Nagy <algernon@balabit.hu>
 * Copyright (c) 2021 Szilárd Parrag
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

#include "redis-worker.h"
#include "redis.h"
#include "messages.h"
#include "scratch-buffers.h"
#include "utf8utils.h"



static inline void _fill_template(RedisDestWorker *self, LogMessage *msg, LogTemplate *template, gchar **str,
                                  gsize *size)
{
  RedisDestWorker *s = (RedisDestWorker *) self;
  RedisDriver *owner = (RedisDriver *) s->super.owner;

  if (log_template_is_trivial(template))
    {
      gssize unsigned_size;
      *str = (gchar *)log_template_get_trivial_value(template, msg, &unsigned_size);
      *size = unsigned_size;
    }
  else
    {
      GString *buffer = scratch_buffers_alloc();
      LogTemplateEvalOptions options = {&owner->template_options, LTZ_SEND,
                                        owner->super.worker.instance.seq_num, NULL
                                       };
      log_template_format(template, msg, &options, buffer);
      *size = buffer->len;
      *str = buffer->str;
    }
}

static void
_fill_argv_from_template_list(RedisDestWorker *s, LogMessage *msg)
{
  RedisDestWorker *self = (RedisDestWorker *) s;
  RedisDriver *owner = (RedisDriver *) self->super.owner;
  for (gint i = 1; i < self->argc; i++)
    {
      LogTemplate *redis_command = g_list_nth_data(owner->arguments, i-1);
      _fill_template(s, msg, redis_command, &self->argv[i], &self->argvlen[i]);
    }
}

static const gchar *
_argv_to_string(RedisDestWorker *self)
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


static LogThreadedResult
redis_worker_insert(LogThreadedDestWorker *s, LogMessage *msg)
{
  RedisDestWorker *self = (RedisDestWorker *)s;
  RedisDriver *owner = (RedisDriver *) self->super.owner;

  ScratchBuffersMarker marker;
  scratch_buffers_mark(&marker);

  _fill_argv_from_template_list(self, msg);

  redisReply *reply = redisCommandArgv(self->c, self->argc, (const gchar **)self->argv, self->argvlen);

  if (!reply)
    {
      msg_error("REDIS server error, suspending",
                evt_tag_str("driver", owner->super.super.super.id),
                evt_tag_str("command", _argv_to_string(self)),
                evt_tag_str("error", self->c->errstr),
                evt_tag_int("time_reopen", self->super.time_reopen));
      scratch_buffers_reclaim_marked(marker);
      return LTR_ERROR;
    }

  msg_debug("REDIS command sent",
            evt_tag_str("driver", owner->super.super.super.id),
            evt_tag_str("command", _argv_to_string(self)));

  freeReplyObject(reply);
  scratch_buffers_reclaim_marked(marker);
  return LTR_SUCCESS;
}


static gboolean
redis_worker_thread_init(LogThreadedDestWorker *d)
{
  RedisDestWorker *self = (RedisDestWorker *) d;
  RedisDriver *owner = (RedisDriver *) self->super.owner;

  self->argc = g_list_length(owner->arguments) + 1;
  self->argv = g_malloc(self->argc * sizeof(char *));
  self->argvlen = g_malloc(self->argc * sizeof(size_t));

  self->argv[0] = owner->command->str;
  self->argvlen[0] = owner->command->len;

  msg_debug("Worker thread started",
            evt_tag_str("driver", self->super.owner->super.super.id));


  return log_threaded_dest_worker_init_method(d);

}


static void
redis_worker_disconnect(LogThreadedDestWorker *s)
{
  RedisDestWorker *self = (RedisDestWorker *)s;

  if (self->c)
    redisFree(self->c);
  self->c = NULL;
}

static void
redis_worker_thread_deinit(LogThreadedDestWorker *d)
{
  RedisDestWorker *self = (RedisDestWorker *)d;

  g_free(self->argv);
  g_free(self->argvlen);
  redis_worker_disconnect(d);

  log_threaded_dest_worker_deinit_method(d);
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
send_redis_command(RedisDestWorker *self, const char *format, ...)
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
check_connection_to_redis(RedisDestWorker *self)
{
  return send_redis_command(self, "ping");
}

static gboolean
authenticate_to_redis(RedisDestWorker *self, const gchar *password)
{
  return send_redis_command(self, "AUTH %s", ((RedisDriver *) self->super.owner)->auth);
}

static gboolean
redis_worker_connect(LogThreadedDestWorker *s)
{
  RedisDestWorker *self = (RedisDestWorker *)s;
  RedisDriver *owner = (RedisDriver *) self->super.owner;

  if (self->c && check_connection_to_redis(self))
    return TRUE;

  if (self->c)
    redisFree(self->c);

  self->c = redisConnectWithTimeout(owner->host, owner->port, owner->timeout);

  if (self->c == NULL || self->c->err)
    {
      if (self->c)
        {
          msg_error("REDIS server error during connection",
                    evt_tag_str("driver", owner->super.super.super.id),
                    evt_tag_str("error", self->c->errstr),
                    evt_tag_int("time_reopen", self->super.time_reopen));
        }
      else
        {
          msg_error("REDIS server can't allocate redis context");
        }
      return FALSE;
    }

  if (owner->auth)
    if (!authenticate_to_redis(self, owner->auth))
      {
        msg_error("REDIS: failed to authenticate",
                  evt_tag_str("driver", owner->super.super.super.id));
        return FALSE;
      }

  if (!check_connection_to_redis(self))
    {
      msg_error("REDIS: failed to connect",
                evt_tag_str("driver", owner->super.super.super.id));
      return FALSE;
    }

  if (self->c->err)
    return FALSE;

  msg_debug("Connecting to REDIS succeeded",
            evt_tag_str("driver", owner->super.super.super.id));

  return TRUE;
}


LogThreadedDestWorker *redis_worker_new(LogThreadedDestDriver *owner, gint worker_index)
{
  RedisDestWorker *self = g_new0(RedisDestWorker, 1);

  log_threaded_dest_worker_init_instance(&self->super, owner, worker_index);

  self->super.thread_init = redis_worker_thread_init;
  self->super.thread_deinit = redis_worker_thread_deinit;
  self->super.connect = redis_worker_connect;
  self->super.disconnect = redis_worker_disconnect;
  self->super.insert = redis_worker_insert;

  return &self->super;
}
