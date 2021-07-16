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
#ifndef STATS_REGISTRY_H_INCLUDED
#define STATS_REGISTRY_H_INCLUDED 1

#include "stats/stats.h"
#include "stats/stats-cluster.h"

typedef void (*StatsForeachClusterFunc)(StatsCluster *sc, gpointer user_data);
typedef gboolean (*StatsForeachClusterRemoveFunc)(StatsCluster *sc, gpointer user_data);

void stats_lock(void);
void stats_unlock(void);
gboolean stats_check_level(gint level);
StatsCluster *stats_register_counter(gint level, const StatsClusterKey *sc_key, gint type, StatsCounterItem **counter);

StatsCluster *stats_register_external_counter(gint level, const StatsClusterKey *sc_key, gint type,
                                              atomic_gssize *external_counter);

StatsCluster *stats_register_alias_counter(gint level, const StatsClusterKey *sc_key, gint type,
                                           StatsCounterItem *aliased_counter);

StatsCluster *stats_register_counter_and_index(gint level, const StatsClusterKey *sc_key, gint type,
                                               StatsCounterItem **counter);
StatsCluster *stats_register_dynamic_counter(gint stats_level, const StatsClusterKey *sc_key, gint type,
                                             StatsCounterItem **counter);
void stats_register_and_increment_dynamic_counter(gint stats_level, const StatsClusterKey *sc_key, time_t timestamp);
void stats_register_associated_counter(StatsCluster *handle, gint type, StatsCounterItem **counter);
void stats_unregister_counter(const StatsClusterKey *sc_key, gint type, StatsCounterItem **counter);
void stats_unregister_external_counter(const StatsClusterKey *sc_key, gint type,
                                       atomic_gssize *external_counter);
void stats_unregister_alias_counter(const StatsClusterKey *sc_key, gint type, StatsCounterItem *aliased_counter);
void stats_unregister_dynamic_counter(StatsCluster *handle, gint type, StatsCounterItem **counter);

gboolean stats_contains_counter(const StatsClusterKey *sc_key, gint type);
StatsCounterItem *stats_get_counter(const StatsClusterKey *sc_key, gint type);

void stats_foreach_counter(StatsForeachCounterFunc func, gpointer user_data, gboolean *cancelled);
void stats_foreach_cluster(StatsForeachClusterFunc func, gpointer user_data, gboolean *cancelled);
void stats_foreach_cluster_remove(StatsForeachClusterRemoveFunc func, gpointer user_data);

void stats_registry_init(void);
void stats_registry_deinit(void);

gboolean stats_check_dynamic_clusters_limit(guint number_of_clusters);
gint stats_number_of_dynamic_clusters_limit(void);

#endif
