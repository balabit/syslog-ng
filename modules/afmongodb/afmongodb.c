/*
 * Copyright (c) 2010-2016 Balabit
 * Copyright (c) 2010-2014 Gergely Nagy <algernon@balabit.hu>
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


#include "afmongodb.h"
#include "afmongodb-parser.h"
#include "messages.h"
#include "stats/stats-registry.h"
#include "logmsg/nvtable.h"
#include "logqueue.h"
#include "value-pairs/evttag.h"
#include "plugin.h"
#include "plugin-types.h"

#include <time.h>

#include "afmongodb-private.h"

#define DEFAULT_URI \
      "mongodb://127.0.0.1:27017/syslog"\
      "?wtimeoutMS=60000&socketTimeoutMS=60000&connectTimeoutMS=60000"

/*
 * Configuration
 */

LogTemplateOptions *
afmongodb_dd_get_template_options(LogDriver *s)
{
  MongoDBDestDriver *self = (MongoDBDestDriver *)s;

  return &self->template_options;
}

void
afmongodb_dd_set_uri(LogDriver *d, const gchar *uri)
{
  MongoDBDestDriver *self = (MongoDBDestDriver *)d;

  if (self->uri_str)
    g_string_assign(self->uri_str, uri);
  else
    self->uri_str = g_string_new(uri);
}

void
afmongodb_dd_set_collection(LogDriver *d, const gchar *collection)
{
  MongoDBDestDriver *self = (MongoDBDestDriver *)d;

  g_free(self->coll);
  self->coll = g_strdup(collection);
}

void
afmongodb_dd_set_value_pairs(LogDriver *d, ValuePairs *vp)
{
  MongoDBDestDriver *self = (MongoDBDestDriver *)d;

  value_pairs_unref(self->vp);
  self->vp = vp;
}

/*
 * Utilities
 */

static gchar *
_format_instance_id(const LogThreadedDestDriver *d, const gchar *format)
{
  const MongoDBDestDriver *self = (const MongoDBDestDriver *)d;
  static gchar args[1024];
  static gchar id[1024];

  if (((LogPipe *)d)->persist_name)
    {
      g_snprintf(args, sizeof(args), "%s", ((LogPipe *)d)->persist_name);
    }
  else
    {
      const mongoc_host_list_t *hosts = mongoc_uri_get_hosts(self->uri_obj);
      const gchar *first_host = "";
      if (hosts)
        {
          if (hosts->family == AF_UNIX)
            first_host = hosts->host;
          else
            first_host = hosts->host_and_port;
        }

      const gchar *db = self->const_db ? self->const_db : "";

      const gchar *replica_set = mongoc_uri_get_replica_set(self->uri_obj);
      if (!replica_set)
        replica_set = "";

      const gchar *coll = self->coll ? self->coll : "";

      g_snprintf(args, sizeof(args), "%s,%s,%s,%s", first_host, db, replica_set, coll);
    }
  g_snprintf(id, sizeof(id), format, args);
  return id;
}

static const gchar *
_format_stats_instance(LogThreadedDestDriver *d)
{
  return _format_instance_id(d, "mongodb,%s");
}

static const gchar *
_format_persist_name(const LogPipe *s)
{
  const LogThreadedDestDriver *self = (const LogThreadedDestDriver *)s;

  return s->persist_name ? _format_instance_id(self, "afmongodb.%s")
         : _format_instance_id(self, "afmongodb(%s)");
}

static void
_worker_disconnect(LogThreadedDestDriver *s)
{
  MongoDBDestDriver *self = (MongoDBDestDriver *)s;

  mongoc_client_destroy(self->client);
  self->client = NULL;
}

static gboolean
_connect(MongoDBDestDriver *self, gboolean reconnect)
{
  if (!self->client)
    {
      self->client = mongoc_client_new_from_uri(self->uri_obj);

      if (!self->client)
        {
          msg_error("Error creating MongoDB URI",
                    evt_tag_str("driver", self->super.super.super.id));
          return FALSE;
        }
    }

  if (!self->coll_obj)
    {
      self->coll_obj = mongoc_client_get_collection(self->client, self->const_db, self->coll);

      if (!self->coll_obj)
        {
          msg_error("Error getting specified MongoDB collection",
                    evt_tag_str("collection", self->coll),
                    evt_tag_str("driver", self->super.super.super.id));

          mongoc_client_destroy(self->client);
          self->client = NULL;

          return FALSE;
        }
    }

  bson_t reply;
  bson_error_t error;
  bson_t *cmd = BCON_NEW("serverStatus", "1");
  const mongoc_read_prefs_t *read_prefs = mongoc_collection_get_read_prefs(self->coll_obj);
  gboolean ok = mongoc_client_command_simple(self->client, self->const_db ? : "", cmd, read_prefs, &reply, &error);
  bson_destroy(&reply);
  bson_destroy(cmd);
  if (!ok)
    {
      msg_error("Error connecting to MongoDB",
                evt_tag_str("driver", self->super.super.super.id),
                evt_tag_str("reason", error.message));

      mongoc_collection_destroy(self->coll_obj);
      self->coll_obj = NULL;
      mongoc_client_destroy(self->client);
      self->client = NULL;

      return FALSE;
    }

  return TRUE;
}

/*
 * Worker thread
 */
static gboolean
_vp_obj_start(const gchar *name,
              const gchar *prefix, gpointer *prefix_data,
              const gchar *prev, gpointer *prev_data,
              gpointer user_data)
{
  bson_t *o;

  if (prefix_data)
    {
      o = bson_new();
      *prefix_data = o;
    }
  return FALSE;
}

static gboolean
_vp_obj_end(const gchar *name,
            const gchar *prefix, gpointer *prefix_data,
            const gchar *prev, gpointer *prev_data,
            gpointer user_data)
{
  MongoDBDestDriver *self = (MongoDBDestDriver *)user_data;
  bson_t *root;

  if (prev_data)
    root = (bson_t *)*prev_data;
  else
    root = self->bson;

  if (prefix_data)
    {
      bson_t *d = (bson_t *)*prefix_data;

      bson_append_document(root, name, -1, d);
      bson_destroy(d);
    }
  return FALSE;
}

static gboolean
_vp_process_value(const gchar *name, const gchar *prefix, TypeHint type,
                  const gchar *value, gsize value_len, gpointer *prefix_data, gpointer user_data)
{
  bson_t *o;
  MongoDBDestDriver *self = (MongoDBDestDriver *)user_data;
  gboolean fallback = self->template_options.on_error & ON_ERROR_FALLBACK_TO_STRING;

  if (prefix_data)
    o = (bson_t *)*prefix_data;
  else
    o = self->bson;

  switch (type)
    {
    case TYPE_HINT_BOOLEAN:
    {
      gboolean b;

      if (type_cast_to_boolean(value, &b, NULL))
        bson_append_bool(o, name, -1, b);
      else
        {
          gboolean r = type_cast_drop_helper(self->template_options.on_error, value, "boolean");

          if (fallback)
            bson_append_utf8(o, name, -1, value, value_len);
          else
            return r;
        }
      break;
    }
    case TYPE_HINT_INT32:
    {
      gint32 i;

      if (type_cast_to_int32(value, &i, NULL))
        bson_append_int32(o, name, -1, i);
      else
        {
          gboolean r = type_cast_drop_helper(self->template_options.on_error, value, "int32");

          if (fallback)
            bson_append_utf8(o, name, -1, value, value_len);
          else
            return r;
        }
      break;
    }
    case TYPE_HINT_INT64:
    {
      gint64 i;

      if (type_cast_to_int64(value, &i, NULL))
        bson_append_int64(o, name, -1, i);
      else
        {
          gboolean r = type_cast_drop_helper(self->template_options.on_error, value, "int64");

          if (fallback)
            bson_append_utf8(o, name, -1, value, value_len);
          else
            return r;
        }

      break;
    }
    case TYPE_HINT_DOUBLE:
    {
      gdouble d;

      if (type_cast_to_double(value, &d, NULL))
        bson_append_double(o, name, -1, d);
      else
        {
          gboolean r = type_cast_drop_helper(self->template_options.on_error, value, "double");
          if (fallback)
            bson_append_utf8(o, name, -1, value, value_len);
          else
            return r;
        }

      break;
    }
    case TYPE_HINT_DATETIME:
    {
      guint64 i;

      if (type_cast_to_datetime_int(value, &i, NULL))
        bson_append_date_time(o, name, -1, (gint64)i);
      else
        {
          gboolean r = type_cast_drop_helper(self->template_options.on_error, value, "datetime");

          if (fallback)
            bson_append_utf8(o, name, -1, value, value_len);
          else
            return r;
        }

      break;
    }
    case TYPE_HINT_STRING:
    case TYPE_HINT_LITERAL:
      bson_append_utf8(o, name, -1, value, value_len);
      break;
    default:
      return TRUE;
    }

  return FALSE;
}

static LogThreadedResult
_worker_insert(LogThreadedDestDriver *s, LogMessage *msg)
{
  MongoDBDestDriver *self = (MongoDBDestDriver *)s;
  gboolean success;
  gboolean drop_silently = self->template_options.on_error & ON_ERROR_SILENT;

  if (!_connect(self, TRUE))
    return LTR_NOT_CONNECTED;

  bson_reinit(self->bson);

  LogTemplateEvalOptions options = {&self->template_options, LTZ_SEND, self->super.worker.instance.seq_num, NULL};
  success = value_pairs_walk(self->vp,
                             _vp_obj_start,
                             _vp_process_value,
                             _vp_obj_end,
                             msg, &options,
                             self);

  if (!success)
    {
      if (!drop_silently)
        {
          msg_error("Failed to format message for MongoDB, dropping message",
                    evt_tag_value_pairs("message", self->vp, msg, &options),
                    evt_tag_str("driver", self->super.super.super.id));
        }
      return LTR_DROP;
    }

  msg_debug("Outgoing message to MongoDB destination",
            evt_tag_value_pairs("message", self->vp, msg, &options),
            evt_tag_str("driver", self->super.super.super.id));

  bson_error_t error;
  success = mongoc_collection_insert(self->coll_obj, MONGOC_INSERT_NONE,
                                     (const bson_t *)self->bson, NULL, &error);
  if (!success)
    {
      if (error.domain == MONGOC_ERROR_STREAM)
        {
          msg_error("Network error while inserting into MongoDB",
                    evt_tag_int("time_reopen", self->super.time_reopen),
                    evt_tag_str("reason", error.message),
                    evt_tag_str("driver", self->super.super.super.id));
          return LTR_NOT_CONNECTED;
        }
      else
        {
          msg_error("Failed to insert into MongoDB",
                    evt_tag_int("time_reopen", self->super.time_reopen),
                    evt_tag_str("reason", error.message),
                    evt_tag_str("driver", self->super.super.super.id));
          return LTR_ERROR;
        }
    }

  return LTR_SUCCESS;
}

gboolean
afmongodb_dd_private_uri_init(LogDriver *d)
{
  MongoDBDestDriver *self = (MongoDBDestDriver *)d;

  if (!self->uri_str)
    self->uri_str = g_string_new(DEFAULT_URI);

  self->uri_obj = mongoc_uri_new(self->uri_str->str);
  if (!self->uri_obj)
    {
      msg_error("Error parsing MongoDB URI",
                evt_tag_str("uri", self->uri_str->str),
                evt_tag_str("driver", self->super.super.super.id));
      return FALSE;
    }

  self->const_db = mongoc_uri_get_database(self->uri_obj);
  if (!self->const_db || !strlen(self->const_db))
    {
      msg_error("Missing DB name from MongoDB URI",
                evt_tag_str("uri", self->uri_str->str),
                evt_tag_str("driver", self->super.super.super.id));
      return FALSE;
    }

  msg_verbose("Initializing MongoDB destination",
              evt_tag_str("uri", self->uri_str->str),
              evt_tag_str("db", self->const_db),
              evt_tag_str("collection", self->coll),
              evt_tag_str("driver", self->super.super.super.id));

  return TRUE;
}

static void
_worker_thread_init(LogThreadedDestDriver *d)
{
  MongoDBDestDriver *self = (MongoDBDestDriver *)d;

  _connect(self, FALSE);

  self->current_value = g_string_sized_new(256);

  self->bson = bson_sized_new(4096);
}

static void
_worker_thread_deinit(LogThreadedDestDriver *d)
{
  MongoDBDestDriver *self = (MongoDBDestDriver *)d;

  if (self->current_value)
    {
      g_string_free(self->current_value, TRUE);
      self->current_value = NULL;
    }

  bson_destroy(self->bson);
  self->bson = NULL;
}

/*
 * Main thread
 */

static void
_init_value_pairs_dot_to_underscore_transformation(MongoDBDestDriver *self)
{
  ValuePairsTransformSet *vpts;

  /* Always replace a leading dot with an underscore. */
  vpts = value_pairs_transform_set_new(".*");
  value_pairs_transform_set_add_func(vpts, value_pairs_new_transform_replace_prefix(".", "_"));
  value_pairs_add_transforms(self->vp, vpts);
}

static gboolean
_init(LogPipe *s)
{
  MongoDBDestDriver *self = (MongoDBDestDriver *)s;
  GlobalConfig *cfg = log_pipe_get_config(s);

  log_template_options_init(&self->template_options, cfg);

  _init_value_pairs_dot_to_underscore_transformation(self);

  if (!afmongodb_dd_private_uri_init(&self->super.super.super))
    return FALSE;

  if (!log_threaded_dest_driver_init_method(s, _format_persist_name(s)))
    return FALSE;

  return TRUE;
}

static void
_free(LogPipe *d)
{
  MongoDBDestDriver *self = (MongoDBDestDriver *)d;

  log_template_options_destroy(&self->template_options);

  if (self->uri_str)
    {
      g_string_free(self->uri_str, TRUE);
      self->uri_str = NULL;
    }
  g_free(self->coll);

  value_pairs_unref(self->vp);

  if (self->uri_obj)
    mongoc_uri_destroy(self->uri_obj);
  if (self->coll_obj)
    mongoc_collection_destroy(self->coll_obj);
  if (self->client)
    mongoc_cleanup();
  log_threaded_dest_driver_free(d);
}

/*
 * Plugin glue.
 */

LogDriver *
afmongodb_dd_new(GlobalConfig *cfg)
{
  MongoDBDestDriver *self = g_new0(MongoDBDestDriver, 1);

  mongoc_init();

  log_threaded_dest_driver_init_instance(&self->super, cfg);

  self->super.super.super.super.init = _init;
  self->super.super.super.super.free_fn = _free;
  self->super.super.super.super.generate_persist_name = _format_persist_name;

  self->super.worker.thread_init = _worker_thread_init;
  self->super.worker.thread_deinit = _worker_thread_deinit;
  self->super.worker.disconnect = _worker_disconnect;
  self->super.worker.insert = _worker_insert;
  self->super.format_stats_instance = _format_stats_instance;
  self->super.stats_source = stats_register_type("mongodb");

  afmongodb_dd_set_collection(&self->super.super.super, "messages");

  log_template_options_defaults(&self->template_options);
  afmongodb_dd_set_value_pairs(&self->super.super.super, value_pairs_new_default(cfg));

  return &self->super.super.super;
}

extern CfgParser afmongodb_dd_parser;

static Plugin afmongodb_plugin =
{
  .type = LL_CONTEXT_DESTINATION,
  .name = "mongodb",
  .parser = &afmongodb_parser,
};

gboolean
afmongodb_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, &afmongodb_plugin, 1);
  return TRUE;
}

const ModuleInfo module_info =
{
  .canonical_name = "afmongodb",
  .version = SYSLOG_NG_VERSION,
  .description = "The afmongodb module provides MongoDB destination support for syslog-ng.",
  .core_revision = SYSLOG_NG_SOURCE_REVISION,
  .plugins = &afmongodb_plugin,
  .plugins_len = 1,
};
