/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
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

#ifndef SDINTER_H_INCLUDED
#define SDINTER_H_INCLUDED

#include "driver.h"
#include "logsource.h"
#include "mainloop-worker.h"

typedef struct AFInterSourceOptions
{
  LogSourceOptions super;
  gint queue_capacity;
} AFInterSourceOptions;

typedef enum
{
  AFINTER_LIVE_COLLECTION_STARTED,
  AFINTER_LIVE_COLLECTION_RUNNING,
  AFINTER_INTERNAL_SRC_PRESENT,
} AFInterLive;

/*
 * This is the actual source driver, linked into the configuration tree.
 */
typedef struct _AFInterSourceDriver
{
  LogSrcDriver super;
  LogSource *source;
  AFInterSourceOptions source_options;
  WorkerOptions worker_options;
} AFInterSourceDriver;

void afinter_postpone_mark(gint mark_freq);
LogDriver *afinter_sd_new(GlobalConfig *cfg);
void afinter_global_init(void);
void afinter_global_deinit(void);

AFInterLive afinter_start_live_collection(void);
void afinter_stop_live_collection(void);
void afinter_reset_live_collection(void);
void afinter_get_collected_messages(GString *result);
#endif
