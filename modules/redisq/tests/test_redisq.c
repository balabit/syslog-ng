/*
 * Copyright (c) 2018 Balabit
 * Copyright (c) 2018 Mehul Prajapati <mehulprajapati2802@gmail.com>
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

#include "logqueue-redis.h"
#include "logmsg/logmsg-serialize.h"
#include "logpipe.h"
#include <criterion/criterion.h>
#include "plugin.h"
#include "apphook.h"
#include "redisq-options.h"

#define HELLO_MSG "Hello redis queue"
#define ITEMS_PER_MESSAGE 2

static LogQueueRedis logqredis;
static RedisQueueOptions options;
static LogMessage *test_msg = NULL;

static LogMessage *
_construct_msg(const gchar *msg)
{
  LogMessage *logmsg;

  logmsg = log_msg_new_empty();
  log_msg_set_value_by_name(logmsg, "MESSAGE", msg, -1);
  return logmsg;
}

void *redisvCommand(redisContext *c, const char *format, va_list ap)
{
  redisReply *reply = g_new0(redisReply, 1);
  GString *serialized;
  SerializeArchive *sa;

  if (strstr(format, "RPUSH"))
    {
      test_msg = _construct_msg(HELLO_MSG);
    }
  else if (strstr(format, "LRANGE") && test_msg)
    {
      serialized = g_string_sized_new(4096);
      sa = serialize_string_archive_new(serialized);
      log_msg_serialize(test_msg, sa);

      reply->elements = 1;
      reply->type = REDIS_REPLY_ARRAY;
      reply->element = g_malloc0(sizeof(redisReply *));
      reply->element[0] = g_new0(redisReply, 1);
      reply->element[0]->str = g_malloc0(2048);

      memcpy(reply->element[0]->str, serialized->str, serialized->len);
      reply->element[0]->len = serialized->len;

      serialize_archive_free(sa);
      g_string_free(serialized, TRUE);
      log_msg_unref(test_msg);
      test_msg = NULL;

      return reply;
    }

  return NULL;
}

void freeReplyObject(void *reply)
{
  redisReply *rep = (redisReply *) reply;

  if (rep->elements > 0)
    {
      g_free(rep->element[0]->str);
      g_free(rep->element[0]);
      g_free(rep->element);
    }

  g_free(reply);
}

static gboolean
_is_conn_alive(LogQueueRedis *self)
{
  return TRUE;
}

static LogQueue *
_logq_redis_new(LogQueueRedis *self)
{
  const gchar *persist_name = "test_redisq";
  LogQueue *q;

  redis_queue_options_set_default_options(&options);
  self->redis_options = &options;
  self->check_conn = _is_conn_alive;
  self->redis_thread_mutex = g_mutex_new();

  q = log_queue_redis_new(self, persist_name);
  log_queue_set_use_backlog(q, TRUE);

  return q;
}

static void
_logq_redis_free(LogQueueRedis *self)
{
  redis_queue_options_destroy(&options);
  g_static_mutex_free(&self->super.lock);
  g_free(self->super.persist_name);
}

static void
_compare_log_msg(LogMessage *msg1, LogMessage *msg2)
{
  gchar *msgstr1, *msgstr2;
  gssize msglen1, msglen2;

  cr_assert_not_null(msg1, "msg1 is NULL");
  cr_assert_not_null(msg2, "msg2 is NULL");

  msgstr1 = (gchar *) log_msg_get_value(msg1, LM_V_MESSAGE, &msglen1);
  msgstr2 = (gchar *) log_msg_get_value(msg2, LM_V_MESSAGE, &msglen2);

  cr_assert_str_eq(msgstr1, msgstr2, "log messages are not identical");
}

Test(redisq, test_push_pop_msg)
{
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;
  LogMessage *msg = _construct_msg(HELLO_MSG);
  LogMessage *out_msg = NULL;
  LogQueue *q;

  q = _logq_redis_new(&logqredis);

  log_queue_push_tail(q, msg, &path_options);
  out_msg = log_queue_pop_head(q, &path_options);

  _compare_log_msg(msg, out_msg);

  log_msg_unref(msg);
  log_msg_unref(out_msg);

  _logq_redis_free(&logqredis);
}

Test(redisq, test_pop_empty_msg)
{
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;
  LogQueue *q;
  LogMessage *msg;
  guint qlen = 0;

  q = _logq_redis_new(&logqredis);

  msg = log_queue_pop_head(q, &path_options);
  cr_assert_null(msg, "Queue is not empty");

  qlen = logqredis.super.get_length((LogQueue *)&logqredis);
  cr_assert_eq(qlen, 0, "Message is present in a Queue");

  _logq_redis_free(&logqredis);
}

Test(redisq, test_rewind_backlog)
{
  guint qlen = 0;
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;
  LogQueue *q;
  LogMessage *msg = _construct_msg(HELLO_MSG);
  LogMessage *backlog_msg, *out_msg;

  q = _logq_redis_new(&logqredis);

  log_queue_push_tail(q, msg, &path_options);
  out_msg = log_queue_pop_head(q, &path_options);
  log_queue_rewind_backlog(q, 1);

  qlen = logqredis.qbacklog->length / ITEMS_PER_MESSAGE;
  cr_assert_eq(qlen, 0, "Backlog is not empty");

  backlog_msg = log_queue_pop_head(q, &path_options);
  _compare_log_msg(out_msg, backlog_msg);

  log_msg_unref(msg);
  log_msg_unref(out_msg);
  log_msg_unref(backlog_msg);

  _logq_redis_free(&logqredis);
}

Test(redisq, test_ack_backlog)
{
  guint qlen = 0;
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;
  LogQueue *q;
  LogMessage *msg = _construct_msg(HELLO_MSG), *out_msg;

  q = _logq_redis_new(&logqredis);

  log_queue_push_tail(q, msg, &path_options);
  log_queue_pop_head(q, &path_options);
  log_queue_ack_backlog(q, 1);

  qlen = logqredis.qbacklog->length / ITEMS_PER_MESSAGE;
  cr_assert_eq(qlen, 0, "Backlog is not empty");

  out_msg = log_queue_pop_head(q, &path_options);
  cr_assert_null(out_msg, "Queue is not empty");

  log_msg_unref(msg);
  _logq_redis_free(&logqredis);
}

static void
setup(void)
{
  app_startup();
}

static void
teardown(void)
{
  app_shutdown();
}

TestSuite(redisq, .init = setup, .fini = teardown);
