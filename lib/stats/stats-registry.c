/*
 * Copyright (c) 2002-2013 Balabit
 * Copyright (c) 1998-2013 Balázs Scheidler
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
#include "stats/stats-registry.h"
#include "stats/stats-query.h"
#include "cfg.h"
#include <string.h>

typedef struct _StatsClusterContainer
{
  GHashTable *static_clusters;
  GHashTable *dynamic_clusters;
} StatsClusterContainer;

static StatsClusterContainer stats_cluster_container;

static guint
_number_of_dynamic_clusters(void)
{
  return g_hash_table_size(stats_cluster_container.dynamic_clusters);
}

static GStaticMutex stats_mutex = G_STATIC_MUTEX_INIT;
gboolean stats_locked;

static void
_insert_cluster(StatsCluster *sc)
{
  if (sc->dynamic)
    g_hash_table_insert(stats_cluster_container.dynamic_clusters, &sc->key, sc);
  else
    g_hash_table_insert(stats_cluster_container.static_clusters, &sc->key, sc);
}

void
stats_lock(void)
{
  g_static_mutex_lock(&stats_mutex);
  stats_locked = TRUE;
}

void
stats_unlock(void)
{
  stats_locked = FALSE;
  g_static_mutex_unlock(&stats_mutex);
}

static StatsCluster *
_grab_dynamic_cluster(const StatsClusterKey *sc_key)
{
  StatsCluster *sc;

  sc = g_hash_table_lookup(stats_cluster_container.dynamic_clusters, sc_key);
  if (!sc)
    {
      if (!stats_check_dynamic_clusters_limit(_number_of_dynamic_clusters()))
        return NULL;
      sc = stats_cluster_dynamic_new(sc_key);
      _insert_cluster(sc);
      if ( !stats_check_dynamic_clusters_limit(_number_of_dynamic_clusters()))
        {
          msg_warning("Number of dynamic cluster limit has been reached.",
                      evt_tag_int("allowed_clusters", stats_number_of_dynamic_clusters_limit()));
        }
    }

  return sc;

}

static StatsCluster *
_grab_static_cluster(const StatsClusterKey *sc_key)
{
  StatsCluster *sc;

  sc = g_hash_table_lookup(stats_cluster_container.static_clusters, sc_key);
  if (!sc)
    {
      sc = stats_cluster_new(sc_key);
      _insert_cluster(sc);
    }

  return sc;
}

static StatsCluster *
_grab_cluster(gint stats_level, const StatsClusterKey *sc_key, gboolean dynamic)
{
  if (!stats_check_level(stats_level))
    return NULL;

  StatsCluster *sc = NULL;

  if (dynamic)
    sc = _grab_dynamic_cluster(sc_key);
  else
    sc = _grab_static_cluster(sc_key);

  if (!sc)
    return NULL;

  /* check that we are not overwriting a dynamic counter with a
   * non-dynamic one or vica versa.  This could only happen if the same
   * key is used for both a dynamic counter and a non-dynamic one, which
   * is a programming error */

  g_assert(sc->dynamic == dynamic);
  return sc;
}

static StatsCluster *
_register_counter(gint stats_level, const StatsClusterKey *sc_key, gint type,
                  gboolean dynamic, StatsCounterItem **counter)
{
  StatsCluster *sc;

  g_assert(stats_locked);

  sc = _grab_cluster(stats_level, sc_key, dynamic);
  if (sc)
    {
      StatsCounterItem *ctr = stats_cluster_get_counter(sc, type);
      *counter = stats_cluster_track_counter(sc, type);
      if (ctr && ctr->external)
        return sc;
      (*counter)->type = type;
      (*counter)->external = FALSE;
    }
  else
    {
      *counter = NULL;
    }
  return sc;
}

static void
_assert_when_internal_or_stores_different_ref(StatsCluster *sc, gint type, atomic_gssize *external_counter)
{
  StatsCounterItem *counter = stats_cluster_get_counter(sc, type);
  if (counter)
    {
      g_assert(counter->external && counter->value_ref == external_counter);
    }
}

static StatsCluster *
_register_external_counter(gint stats_level, const StatsClusterKey *sc_key, gint type,
                           gboolean dynamic, atomic_gssize *external_counter)
{
  StatsCluster *sc;

  g_assert(stats_locked);

  sc = _grab_cluster(stats_level, sc_key, dynamic);
  if (sc)
    {
      _assert_when_internal_or_stores_different_ref(sc, type, external_counter);
      StatsCounterItem *ctr = stats_cluster_track_counter(sc, type);
      ctr->external = TRUE;
      ctr->value_ref = external_counter;
      ctr->type = type;
    }

  return sc;
}



/**
 * stats_register_counter:
 * @stats_level: the required statistics level to make this counter available
 * @component: a reference to the syslog-ng component that this counter belongs to (SCS_*)
 * @id: the unique identifier of the configuration item that this counter belongs to
 * @instance: if a given configuration item manages multiple similar counters
 *            this makes those unique (like destination filename in case macros are used)
 * @type: the counter type (processed, dropped, etc)
 * @counter: returned pointer to the counter
 *
 * This function registers a general purpose counter. Whenever multiple
 * objects touch the same counter all of these should register the counter
 * with the same name. Internally the stats subsystem counts the number of
 * users of the same counter in this case, thus the counter will only be
 * freed when all of these uses are unregistered.
 **/
StatsCluster *
stats_register_counter(gint stats_level, const StatsClusterKey *sc_key, gint type,
                       StatsCounterItem **counter)
{
  return _register_counter(stats_level, sc_key, type, FALSE, counter);
}

StatsCluster *
stats_register_external_counter(gint stats_level, const StatsClusterKey *sc_key, gint type,
                                atomic_gssize *external_counter)
{
  return _register_external_counter(stats_level, sc_key, type, FALSE, external_counter);
}

StatsCluster *
stats_register_alias_counter(gint level, const StatsClusterKey *sc_key, gint type, StatsCounterItem *aliased_counter)
{
  return stats_register_external_counter(level, sc_key, type, &aliased_counter->value);
}

StatsCluster *
stats_register_counter_and_index(gint stats_level, const StatsClusterKey *sc_key, gint type,
                                 StatsCounterItem **counter)
{
  StatsCluster *cluster =  _register_counter(stats_level, sc_key, type, FALSE, counter);
  if (cluster)
    stats_query_index_counter(cluster, type);

  return cluster;
}

StatsCluster *
stats_register_dynamic_counter(gint stats_level, const StatsClusterKey *sc_key,
                               gint type, StatsCounterItem **counter)
{
  return _register_counter(stats_level, sc_key, type, TRUE, counter);
}

/*
 * stats_instant_inc_dynamic_counter
 * @timestamp: if non-negative, an associated timestamp will be created and set
 *
 * Instantly create (if not exists) and increment a dynamic counter.
 */
void
stats_register_and_increment_dynamic_counter(gint stats_level, const StatsClusterKey *sc_key,
                                             time_t timestamp)
{
  StatsCounterItem *counter, *stamp;
  StatsCluster *handle;

  g_assert(stats_locked);
  handle = stats_register_dynamic_counter(stats_level, sc_key, SC_TYPE_PROCESSED, &counter);
  if (!handle)
    return;
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
 * @type: the type that we want to use in the same StatsCluster instance
 * @counter: the returned pointer to the counter itself
 *
 * This function registers another counter type in the same StatsCounter
 * instance in order to avoid an unnecessary lookup.
 **/
void
stats_register_associated_counter(StatsCluster *sc, gint type, StatsCounterItem **counter)
{
  g_assert(stats_locked);

  *counter = NULL;
  if (!sc)
    return;
  g_assert(sc->dynamic);

  *counter = stats_cluster_track_counter(sc, type);
}

void
stats_unregister_counter(const StatsClusterKey *sc_key, gint type,
                         StatsCounterItem **counter)
{
  StatsCluster *sc;

  g_assert(stats_locked);

  if (*counter == NULL)
    return;

  sc = g_hash_table_lookup(stats_cluster_container.static_clusters, sc_key);

  stats_cluster_untrack_counter(sc, type, counter);
}

void
stats_unregister_external_counter(const StatsClusterKey *sc_key, gint type,
                                  atomic_gssize *external_counter)
{
  StatsCluster *sc;

  g_assert(stats_locked);

  sc = g_hash_table_lookup(stats_cluster_container.static_clusters, sc_key);
  StatsCounterItem *ctr = stats_cluster_get_counter(sc, type);
  g_assert(ctr->value_ref == external_counter);

  stats_cluster_untrack_counter(sc, type, &ctr);
}

void
stats_unregister_alias_counter(const StatsClusterKey *sc_key, gint type, StatsCounterItem *aliased_counter)
{
  stats_unregister_external_counter(sc_key, type, &aliased_counter->value);
}

void
stats_unregister_dynamic_counter(StatsCluster *sc, gint type, StatsCounterItem **counter)
{
  g_assert(stats_locked);
  if (!sc)
    return;
  stats_cluster_untrack_counter(sc, type, counter);
}

static StatsCluster *
_lookup_cluster(const StatsClusterKey *sc_key)
{
  g_assert(stats_locked);

  StatsCluster *sc = g_hash_table_lookup(stats_cluster_container.static_clusters, sc_key);

  if (!sc)
    sc = g_hash_table_lookup(stats_cluster_container.dynamic_clusters, sc_key);

  return sc;
}

gboolean
stats_contains_counter(const StatsClusterKey *sc_key, gint type)
{
  g_assert(stats_locked);

  StatsCluster *sc = _lookup_cluster(sc_key);
  if (!sc)
    {
      return FALSE;
    }

  return stats_cluster_is_alive(sc, type);
}

StatsCounterItem *
stats_get_counter(const StatsClusterKey *sc_key, gint type)
{
  g_assert(stats_locked);
  StatsCluster *sc = _lookup_cluster(sc_key);

  if (!sc)
    return NULL;

  return stats_cluster_get_counter(sc, type);
}

static void
_foreach_cluster_helper(gpointer key, gpointer value, gpointer user_data)
{
  gpointer *args = (gpointer *) user_data;
  StatsForeachClusterFunc func = args[0];
  gpointer func_data = args[1];
  StatsCluster *sc = (StatsCluster *) value;

  func(sc, func_data);
}

static void
_foreach_cluster(GHashTable *clusters, gpointer *args, gboolean *cancelled)
{
  GHashTableIter iter;
  g_hash_table_iter_init(&iter, clusters);
  gpointer key, value;

  while (g_hash_table_iter_next(&iter, &key, &value))
    {
      if (cancelled && *cancelled)
        break;
      _foreach_cluster_helper(key, value, args);
    }
}

void
stats_foreach_cluster(StatsForeachClusterFunc func, gpointer user_data, gboolean *cancelled)
{
  gpointer args[] = { func, user_data };

  g_assert(stats_locked);
  _foreach_cluster(stats_cluster_container.static_clusters, args, cancelled);
  _foreach_cluster(stats_cluster_container.dynamic_clusters, args, cancelled);
}

static gboolean
_foreach_cluster_remove_helper(gpointer key, gpointer value, gpointer user_data)
{
  gpointer *args = (gpointer *) user_data;
  StatsForeachClusterRemoveFunc func = args[0];
  gpointer func_data = args[1];
  StatsCluster *sc = (StatsCluster *) value;

  return func(sc, func_data);
}

void
stats_foreach_cluster_remove(StatsForeachClusterRemoveFunc func, gpointer user_data)
{
  gpointer args[] = { func, user_data };
  g_hash_table_foreach_remove(stats_cluster_container.static_clusters, _foreach_cluster_remove_helper, args);
  g_hash_table_foreach_remove(stats_cluster_container.dynamic_clusters, _foreach_cluster_remove_helper, args);
}

static void
_foreach_counter_helper(StatsCluster *sc, gpointer user_data)
{
  gpointer *args = (gpointer *) user_data;
  StatsForeachCounterFunc func = args[0];
  gpointer func_data = args[1];

  stats_cluster_foreach_counter(sc, func, func_data);
}

void
stats_foreach_counter(StatsForeachCounterFunc func, gpointer user_data, gboolean *cancelled)
{
  gpointer args[] = { func, user_data };

  g_assert(stats_locked);
  stats_foreach_cluster(_foreach_counter_helper, args, cancelled);
}

void
stats_registry_init(void)
{
  stats_cluster_container.static_clusters = g_hash_table_new_full((GHashFunc) stats_cluster_hash,
                                            (GEqualFunc) stats_cluster_equal, NULL,
                                            (GDestroyNotify) stats_cluster_free);
  stats_cluster_container.dynamic_clusters = g_hash_table_new_full((GHashFunc) stats_cluster_hash,
                                             (GEqualFunc) stats_cluster_equal, NULL,
                                             (GDestroyNotify) stats_cluster_free);

  g_static_mutex_init(&stats_mutex);
}

void
stats_registry_deinit(void)
{
  g_hash_table_destroy(stats_cluster_container.static_clusters);
  g_hash_table_destroy(stats_cluster_container.dynamic_clusters);
  stats_cluster_container.static_clusters = NULL;
  stats_cluster_container.dynamic_clusters = NULL;
  g_static_mutex_free(&stats_mutex);
}

