/*
 * Copyright (c) 2002-2010 BalaBit IT Ltd, Budapest, Hungary
 * Copyright (c) 1998-2010 Balázs Scheidler
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */
  
#include "stats.h"
#include "messages.h"
#include "misc.h"
#include "syslog-names.h"
#include "hds.h"
#include "nv_property_container.h"

#include <string.h>

/*
 * The statistics module
 *
 * Various components of syslog-ng require counters to keep track of various
 * metrics, such as number of messages processed, dropped or stored in a
 * queue. For this purpose, this module provides an easy to use API to
 * register and keep track these counters, and also to publish them to
 * external programs via a UNIX domain socket.
 *
 * Each counter has the following properties:
 *   * source component: enumerable type, that specifies the syslog-ng
 *     component that the given counter belongs to, examples:
 *       source.file, destination.file, center, source.socket, etc.
 *
 *   * id: the unique identifier of the syslog-ng configuration item that
 *     this counter belongs to. Named configuration elements (source,
 *     destination, etc) use their "name" here. Other components without a
 *     name use either an autogenerated ID (that can change when the
 *     configuration file changes), or an explicit ID configured by the
 *     administrator.
 * 
 *   * instance: each configuration element may track several sets of
 *     counters. This field specifies an identifier that makes a group of
 *     counters unique. For instance:
 *      - source TCP drivers use the IP address of the client here
 *      - destination file writers use the expanded filename
 *      - for those which have no notion for instance, NULL is used
 *
 *   * state: dynamic, active or orphaned, this indicates whether the given
 *     counter is in use or in orphaned state
 *
 *   * type: counter type (processed, dropped, stored, etc)
 *
 * Threading
 *
 * Once registered, changing the counters is thread safe (but see the
 * note on set/get), inc/dec is generally safe. To register counters,
 * the stats code must run in the main thread (assuming init/deinit is
 * running) or the stats lock must be acquired using stats_lock() and
 * stats_unlock(). This API is used to allow batching multiple stats
 * operations under the protection of the same lock acquiral.
 */

/* Static counters for severities and facilities */
/* LOG_DEBUG 0x7 */
#define SEVERITY_MAX   (0x7 + 1)
/* LOG_LOCAL7 23<<3, one additional slot for "everything-else" counter */
#define FACILITY_MAX   (23 + 1 + 1)

static StatsCounterItem *severity_counters[SEVERITY_MAX];
static StatsCounterItem *facility_counters[FACILITY_MAX];


gint current_stats_level;

const gchar *tag_names[SC_TYPE_MAX] =
{
  /* [SC_TYPE_DROPPED]   = */ "dropped",
  /* [SC_TYPE_PROCESSED] = */ "processed",
  /* [SC_TYPE_STORED]   = */  "stored",
  /* [SC_TYPE_SUPPRESSED] =*/ "suppressed",
  /* [SC_TYPE_STAMP] = */     "stamp",
};

const gchar *source_names[SCS_MAX] =
{
  "none",
  "file",
  "pipe",
  "tcp",
  "udp",
  "tcp6",
  "udp6",
  "unix-stream",
  "unix-dgram",
  "syslog",
  "internal",
  "logstore",
  "program",
  "sql",
  "sun-streams",
  "usertty",
  "group",
  "center",
  "host",
  "global",
  "mongodb",
  "class",
  "rule_id",
  "tag",
  "severity",
  "facility",
  "sender",
  "snmp",
  "smtp",
  "journald",
  "java"
};

static StatsCounterType
__lookup_stats_counter_type(const gchar *name)
{
  StatsCounterType result = SC_TYPE_MAX;
  for (result = SC_TYPE_DROPPED; result != SC_TYPE_MAX; result++)
    {
      if (strcmp(tag_names[result], name) == 0)
        break;
    }
  return result;
}

static gboolean
stats_counter_equal(gconstpointer p1, gconstpointer p2)
{
  const StatsCounter *sc1 = (StatsCounter *) p1;
  const StatsCounter *sc2 = (StatsCounter *) p2;
  
  return sc1->source == sc2->source && strcmp(sc1->id, sc2->id) == 0 && strcmp(sc1->instance, sc2->instance) == 0;
}

static guint
stats_counter_hash(gconstpointer p)
{
  const StatsCounter *sc = (StatsCounter *) p;
  
  return g_str_hash(sc->id) + g_str_hash(sc->instance) + sc->source;
}

static void
stats_counter_free(PropertyContainer *s)
{ 
  StatsCounter *sc = (StatsCounter *) s;
  gint i;

  for (i = 0; i < SC_TYPE_MAX; i++)
    {
      if (sc->counters[i].super.free_fn)
        sc->counters[i].super.free_fn(&sc->counters[i].super);
    }
  g_free(sc->id);
  g_free(sc->instance);
}

void
__add_child_name(GString *string, const gchar *name)
{
  if ((name[0]!= '\0') && string->len != 0)
    {
        g_string_append_c(string,  '.');
    }
  g_string_append(string, name);
}

static gchar *
__build_hds_path(StatsCounter *sc)
{
  GString *result = g_string_sized_new(256);

  __add_child_name(result, (sc->source & SCS_SOURCE ? "source" : (sc->source & SCS_DESTINATION ? "destination" : "")));
  __add_child_name(result, ((sc->source & SCS_SOURCE_MASK) == SCS_GROUP) ? "" : source_names[sc->source & SCS_SOURCE_MASK]);
  __add_child_name(result, sc->id);
  __add_child_name(result, sc->instance);
  __add_child_name(result, "stats");

  return g_string_free(result, FALSE);
}

static const gchar *
_stats_counter_item_to_string (Property *prop)
{
  StatsCounterItem *self = (StatsCounterItem *) prop;
  if (!self->value_str)
    self->value_str = g_string_sized_new(64);

  g_string_printf(self->value_str, "%d", self->value);
  return self->value_str->str;
}

static void
_stats_counter_item_free (Property *prop)
{
  StatsCounterItem *self = (StatsCounterItem *) prop;
  if (self->value_str)
    g_string_free(self->value_str, TRUE);
}

static void
stats_counter_item_init_instance (StatsCounterItem *item)
{
  item->super.to_string = _stats_counter_item_to_string;
  item->super.free_fn = _stats_counter_item_free;
}

static void
stats_counter_init_instance(StatsCounter *key, gint source, const gchar *id, const gchar *instance)
{
  gint i;
  key->source = source;
  key->id = id ? (gchar *)id : "";
  key->instance = instance ? (gchar *)instance : "";
  key->ref_cnt = 1;
  for (i = 0; i < SC_TYPE_MAX; i++)
    {
      stats_counter_item_init_instance (&key->counters[i]);
    }
}

static inline gboolean
__is_alive(StatsCounter *self, StatsCounterType type)
{
  return self->live_mask & (1 << type);
}

static void
__foreach(PropertyContainer *s, PROPERTIES_CALLBACK func, gpointer user_data)
{
  StatsCounter *self = (StatsCounter *)s;
  gint i;
  for (i = 0; i < SC_TYPE_MAX; i++)
    {
      if (__is_alive(self, i))
        func(s, tag_names[i], &self->counters[i].super, user_data);
    }
}


static Property *
__get_property(PropertyContainer *s, const gchar *key)
{
  StatsCounter *self = (StatsCounter *)s;
  gint type = __lookup_stats_counter_type(key);
  if (__is_alive(self, type))
    {
      return &self->counters[type].super;
    }
  return NULL;
}

static PropertyContainer*
stats_counter_new(gpointer owner)
{
  StatsCounter *self = g_new0(StatsCounter, 1);
  property_container_init_instance(&self->super, owner);
  self->super.foreach = __foreach;
  self->super.free_fn = stats_counter_free;
  self->super.get_property = __get_property;
  return &self->super;
}

static StatsCounter *
stats_add_counter(gint stats_level, gint source, const gchar *id, const gchar *instance, gboolean *new)
{
  StatsCounter key;
  StatsCounter *sc;
  gchar *hds_name;
  HDSHandle handle;

  if (!stats_check_level(stats_level))
    return NULL;
  
  stats_counter_init_instance(&key, source, id, instance);

  sc = g_hash_table_lookup(counter_hash, &key);
  hds_name = __build_hds_path(&key);
  handle = hds_register_handle(hds_name);

  if (!sc)
    {
      sc = (StatsCounter *)hds_acquire_property_container(handle, stats_counter_new);
      stats_counter_init_instance(sc, source, g_strdup(id ? id : ""), g_strdup(instance ? instance : ""));
      g_hash_table_insert(counter_hash, sc, sc);
      *new = TRUE;
    }
  else
    {
      if (sc->ref_cnt == 0)
        /* it just haven't been cleaned up */
        *new = TRUE;
      else
        *new = FALSE;

      sc->ref_cnt++;
    }
  g_free(hds_name);
  return sc;
}

/**
 * stats_register_counter:
 * @stats_level: the required statistics level to make this counter available
 * @source: a reference to the syslog-ng component that this counter belongs to (SCS_*)
 * @id: the unique identifier of the configuration item that this counter belongs to
 * @instance: if a given configuration item manages multiple similar counters
 *            this makes those unique (like destination filename in case macros are used)
 * @type: the counter type (processed, dropped, etc)
 * @counter: returned pointer to the counter
 *
 * This fuction registers a general purpose counter. Whenever multiple
 * objects touch the same counter all of these should register the counter
 * with the same name. Internally the stats subsystem counts the number of
 * users of the same counter in this case, thus the counter will only be
 * freed when all of these uses are unregistered.
 **/
void
stats_register_counter(gint stats_level, gint source, const gchar *id, const gchar *instance, StatsCounterType type, StatsCounterItem **counter)
{
  StatsCounter *sc;
  gboolean new;

  g_assert(type < SC_TYPE_MAX);
  
  hds_lock();

  *counter = NULL;
  sc = stats_add_counter(stats_level, source, id, instance, &new);
  if (!sc)
    goto exit;

  *counter = &sc->counters[type];
  sc->live_mask |= 1 << type;
exit:
  hds_unlock();
}

StatsCounter *
stats_register_dynamic_counter(gint stats_level, gint source, const gchar *id, const gchar *instance, StatsCounterType type, StatsCounterItem **counter, gboolean *new)
{
  StatsCounter *sc;
  gboolean local_new;

  g_assert(type < SC_TYPE_MAX);
  
  hds_lock();
  *counter = NULL;
  *new = FALSE;
  sc = stats_add_counter(stats_level, source, id, instance, &local_new);
  if (new)
    *new = local_new;
  if (!sc)
    goto exit;

  if (!local_new && !sc->dynamic)
    g_assert_not_reached();

  sc->dynamic = TRUE;
  *counter = &sc->counters[type];
  sc->live_mask |= 1 << type;
exit:
  hds_unlock();
  return sc;
}

/*
 * stats_instant_inc_dynamic_counter
 * @timestamp: if non-negative, an associated timestamp will be created and set
 *
 * Instantly create (if not exists) and increment a dynamic counter.
 */
void
stats_instant_inc_dynamic_counter(gint stats_level, gint source_mask, const gchar *id, const gchar *instance, time_t timestamp)
{
  StatsCounterItem *counter, *stamp;
  gboolean new;
  StatsCounter *handle;

  handle = stats_register_dynamic_counter(stats_level, source_mask, id, instance, SC_TYPE_PROCESSED, &counter, &new);
  stats_counter_inc(counter);
  if (timestamp >= 0)
    {

      stats_register_associated_counter(handle, SC_TYPE_STAMP, &stamp);
      stats_counter_set(stamp, timestamp);
      stats_unregister_dynamic_counter(handle, SC_TYPE_STAMP, &stamp);
    }
  stats_unregister_dynamic_counter(handle, SC_TYPE_PROCESSED, &counter);
}

/**
 * stats_register_associated_counter:
 * @sc: the dynamic counter that was registered with stats_register_dynamic_counter
 * @type: the type that we want to use in the same StatsCounter instance
 * @counter: the returned pointer to the counter itself
 *
 * This function registers another counter type in the same StatsCounter
 * instance in order to avoid an unnecessary lookup.
 **/
void
stats_register_associated_counter(StatsCounter *sc, StatsCounterType type, StatsCounterItem **counter)
{
  *counter = NULL;
  if (!sc)
    return;

  hds_lock();
  g_assert(sc->dynamic);

  *counter = &sc->counters[type];
  sc->live_mask |= 1 << type;
  hds_unlock();
}

void
stats_unregister_counter(gint source, const gchar *id, const gchar *instance, StatsCounterType type, StatsCounterItem **counter)
{
  StatsCounter *sc;
  StatsCounter key;
  
  if (*counter == NULL)
    return;

  stats_counter_init_instance(&key, source, id, instance);

  hds_lock();

  sc = g_hash_table_lookup(counter_hash, &key);

  g_assert(sc && (sc->live_mask & (1 << type)) && &sc->counters[type] == (*counter));
  
  *counter = NULL;
  sc->ref_cnt--;
  hds_unlock();
}

void
stats_unregister_dynamic_counter(StatsCounter *sc, StatsCounterType type, StatsCounterItem **counter)
{
  if (!sc)
    return;
  hds_lock();
  g_assert(sc && (sc->live_mask & (1 << type)) && &sc->counters[type] == (*counter));
  sc->ref_cnt--;
  hds_unlock();
}

static gboolean
stats_counter_is_orphaned(gpointer key, gpointer value, gpointer user_data)
{
  StatsCounter *sc = (StatsCounter *) value;

  if (sc->ref_cnt == 0 && !sc->dynamic)
    return TRUE;
  return FALSE;    
}

void
stats_cleanup_orphans(void)
{
  g_hash_table_foreach_remove(counter_hash, stats_counter_is_orphaned, NULL);
}

void
stats_counter_inc_pri(guint16 pri)
{
  int lpri = LOG_FAC(pri);

  stats_counter_inc(severity_counters[LOG_PRI(pri)]);
  if (lpri > (FACILITY_MAX - 1))
    {
      /* the large facilities (=facility.other) are collected in the last array item */
      lpri = FACILITY_MAX - 1;
    }
  stats_counter_inc(facility_counters[lpri]);
}

static void
stats_format_log_counter(gpointer key, gpointer value, gpointer user_data)
{
  GString *message = (GString *)user_data;
  StatsCounter *sc = (StatsCounter *) value;
  StatsCounterType type;


  for (type = 0; type < SC_TYPE_MAX; type++)
    {
      if (sc->live_mask & (1 << type))
        {
          const gchar *source_name;
          if ((sc->source & SCS_SOURCE_MASK) == SCS_GROUP)
            {
              if (sc->source & SCS_SOURCE)
                source_name = "source";
              else if (sc->source & SCS_DESTINATION)
                source_name = "destination";
              else
                g_assert_not_reached();
              g_string_append_printf(message,"; %s='%s(%s%s%s)=%u'",
                                              tag_names[type],
                                              source_name,
                                              sc->id,
                                              (sc->id[0] && sc->instance[0]) ? "," : "",
                                              sc->instance,
                                              stats_counter_get(&sc->counters[type]));
            }
          else
            {
              g_string_append_printf(message,"; %s='%s%s(%s%s%s)=%u'",
                                      tag_names[type],
                                      (sc->source & SCS_SOURCE ? "src." : (sc->source & SCS_DESTINATION ? "dst." : "")),
                                      source_names[sc->source & SCS_SOURCE_MASK],
                                      sc->id,
                                      (sc->id[0] && sc->instance[0]) ? "," : "",
                                      sc->instance,
                                      stats_counter_get(&sc->counters[type]));
            }
        }
    }

}

void
stats_generate_log(void)
{
  LogMessage *lm;
  GString *message = g_string_new("Log statistics");

  g_hash_table_foreach(counter_hash, stats_format_log_counter, message);
  lm = msg_event_create(EVT_PRI_INFO, message->str, evt_tag_id(MSG_LOG_STATISTIC), NULL);
  g_string_free(message, TRUE);
  msg_event_send(lm);
}

static gboolean
has_csv_special_character(const gchar *var)
{
  gchar *p1 = strchr(var, ';');
  if (p1)
    return TRUE;
  p1 = strchr(var, '\n');
  if (p1)
    return TRUE;
  if (var[0] == '"')
    return TRUE;
  return FALSE;
}

static gchar *
stats_format_csv_escapevar(const gchar *var)
{
  guint32 index;
  guint32 e_index;
  guint32 varlen = strlen(var);
  gchar *result, *escaped_result;

  if (varlen != 0 && has_csv_special_character(var))
    {
      result = g_malloc(varlen*2);

      result[0] = '"';
      e_index = 1;
      for (index = 0; index < varlen; index++)
        {
          if (var[index] == '"')
            {
              result[e_index] = '\\';
              e_index++;
            }
          result[e_index] = var[index];
          e_index++;
        }
      result[e_index] = '"';
      result[e_index + 1] = 0;

      escaped_result = utf8_escape_string(result, e_index + 2);
      g_free(result);
    }
  else
    {
      escaped_result = utf8_escape_string(var, strlen(var));
    }
  return escaped_result;
}

static void
stats_format_csv(gpointer key, gpointer value, gpointer user_data)
{
  GString *csv = (GString *) user_data;
  StatsCounter *sc = (StatsCounter *) value;
  StatsCounterType type;
  gchar *s_id, *s_instance, *tag_name;
  gchar buf[32];

  s_id = stats_format_csv_escapevar(sc->id);
  s_instance = stats_format_csv_escapevar(sc->instance);
  for (type = 0; type < SC_TYPE_MAX; type++)
    {

      if (sc->live_mask & (1 << type))
        {
          const gchar *source_name;
          gchar state;
          
          if (sc->dynamic)
            state = 'd';
          else if (sc->ref_cnt == 0)
            state = 'o';
          else
            state = 'a';
          if ((sc->source & SCS_SOURCE_MASK) == SCS_GROUP)
            {
              if (sc->source & SCS_SOURCE)
                source_name = "source";
              else if (sc->source & SCS_DESTINATION)
                source_name = "destination";
              else
                g_assert_not_reached();
            }
          else
            {
              source_name = buf;
              g_snprintf(buf, sizeof(buf), "%s%s", 
                         (sc->source & SCS_SOURCE ? "src." : (sc->source & SCS_DESTINATION ? "dst." : "")),
                         source_names[sc->source & SCS_SOURCE_MASK]);
            }
          tag_name = stats_format_csv_escapevar(tag_names[type]);
          g_string_append_printf(csv, "%s;%s;%s;%c;%s;%u\n", source_name, s_id, s_instance, state, tag_name, stats_counter_get(&sc->counters[type]));
          g_free(tag_name);
        }
    }
    g_free(s_id);
    g_free(s_instance);
}


gchar *
stats_generate_csv(void)
{
  GString *csv = g_string_sized_new(1024);

  g_string_append_printf(csv, "%s;%s;%s;%s;%s;%s\n", "SourceName", "SourceId", "SourceInstance", "State", "Type", "Number");
  g_hash_table_foreach(counter_hash, stats_format_csv, csv);
  return g_string_free(csv, FALSE);
}

void
stats_set_stats_level(gint stats_level)
{
  hds_lock();
  current_stats_level = stats_level;
  hds_unlock();
  return;
}

void
stats_reinit(GlobalConfig *cfg)
{
  gint i;
  gchar name[11] = "";

  if (stats_check_level(3))
    {
      /* we need these counters, register them */
      for (i = 0; i < SEVERITY_MAX; i++)
        {
          g_snprintf(name, sizeof(name), "%" G_GUINT16_FORMAT, i);
          stats_register_counter(3, SCS_SEVERITY | SCS_SOURCE, NULL, name, SC_TYPE_PROCESSED, &severity_counters[i]);
        }

      for (i = 0; i < FACILITY_MAX - 1; i++)
        {
          g_snprintf(name, sizeof(name), "%" G_GUINT16_FORMAT, i);
          stats_register_counter(3, SCS_FACILITY | SCS_SOURCE, NULL, name, SC_TYPE_PROCESSED, &facility_counters[i]);
        }
      stats_register_counter(3, SCS_FACILITY | SCS_SOURCE, NULL, "other", SC_TYPE_PROCESSED, &facility_counters[FACILITY_MAX - 1]);
    }
  else
    {
      /* no need for facility/severity counters, unregister them */
      for (i = 0; i < SEVERITY_MAX; i++)
        {
          g_snprintf(name, sizeof(name), "%" G_GUINT16_FORMAT, i);
          stats_unregister_counter(SCS_SEVERITY | SCS_SOURCE, NULL, name, SC_TYPE_PROCESSED, &severity_counters[i]);
        }

      for (i = 0; i < FACILITY_MAX - 1; i++)
        {
          g_snprintf(name, sizeof(name), "%" G_GUINT16_FORMAT, i);
          stats_unregister_counter(SCS_FACILITY | SCS_SOURCE, NULL, "other", SC_TYPE_PROCESSED, &facility_counters[i]);
        }
      stats_unregister_counter(SCS_FACILITY | SCS_SOURCE, NULL, "other", SC_TYPE_PROCESSED, &facility_counters[FACILITY_MAX - 1]);
    }
}

typedef struct _GlobalStatsProperty {
  Property super;
  GHashTable *counter_hash;
} GlobalStatsProperty;

static void
__free_fn(Property *s)
{
  GlobalStatsProperty *self = (GlobalStatsProperty *)s;
  g_hash_table_unref(self->counter_hash);
}

static gpointer
__get_object(Property *s)
{
  GlobalStatsProperty *self = (GlobalStatsProperty *)s;
  return self->counter_hash;
}

static Property *
global_stats_property_new()
{
  GlobalStatsProperty *self = g_new0(GlobalStatsProperty, 1);
  self->counter_hash = g_hash_table_new(stats_counter_hash, stats_counter_equal);
  self->super.free_fn = __free_fn;
  self->super.get_object = __get_object;
  return &self->super;
}

void
__init_counter_hash()
{
  PropertyContainer *root_container;
  GlobalStatsProperty *global_stats_property;
  root_container = hds_acquire_property_container(hds_get_root(), nv_property_container_new);
  global_stats_property = (GlobalStatsProperty *)property_container_get_property(root_container, "stats");
  if (!global_stats_property)
    {
      global_stats_property = (GlobalStatsProperty *)global_stats_property_new();
      property_container_add_property(root_container, "stats", &global_stats_property->super);
    }
  counter_hash = g_hash_table_ref(global_stats_property->counter_hash);
}

void
stats_init(void)
{
  hds_init();
  __init_counter_hash();

}

void
stats_destroy(void)
{
  g_hash_table_unref(counter_hash);
  counter_hash = NULL;
}
