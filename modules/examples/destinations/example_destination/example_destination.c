/*
 * Copyright (c) 2020 Balabit
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

#include "example_destination.h"
#include "example_destination_worker.h"
#include "example_destination-parser.h"

#include "plugin.h"
#include "messages.h"
#include "misc.h"
#include "stats/stats-registry.h"
#include "logqueue.h"
#include "driver.h"
#include "plugin-types.h"
#include "logthrdest/logthrdestdrv.h"


/*
 * Configuration
 */

void
example_destination_dd_set_filename(LogDriver *d, const gchar *filename)
{
  ExampleDestinationDriver *self = (ExampleDestinationDriver *)d;

  g_string_assign(self->filename, filename);
}

/*
 * Utilities
 */

static const gchar *
_format_stats_instance(LogThreadedDestDriver *d)
{
  ExampleDestinationDriver *self = (ExampleDestinationDriver *)d;
  static gchar persist_name[1024];

  g_snprintf(persist_name, sizeof(persist_name),
             "example-destination,%s", self->filename->str);
  return persist_name;
}

static const gchar *
_format_persist_name(const LogPipe *d)
{
  ExampleDestinationDriver *self = (ExampleDestinationDriver *)d;
  static gchar persist_name[1024];

  if (d->persist_name)
    g_snprintf(persist_name, sizeof(persist_name), "example-destination.%s", d->persist_name);
  else
    g_snprintf(persist_name, sizeof(persist_name), "example-destination.%s", self->filename->str);

  return persist_name;
}

static gboolean
_dd_init(LogPipe *d)
{
  ExampleDestinationDriver *self = (ExampleDestinationDriver *)d;

  if (!self->filename->len)
    g_string_assign(self->filename, "/tmp/example-destination-output.txt");

  if (!log_threaded_dest_driver_init_method(d, _format_persist_name(d)))
    return FALSE;

  return TRUE;
}

gboolean
_dd_deinit(LogPipe *s)
{
  /*
     If you created resources during init,
     you need to destroy them here.
  */

  return log_threaded_dest_driver_deinit_method(s);
}

static void
_dd_free(LogPipe *d)
{
  ExampleDestinationDriver *self = (ExampleDestinationDriver *)d;

  g_string_free(self->filename, TRUE);

  log_threaded_dest_driver_free(d);
}

LogDriver *
example_destination_dd_new(GlobalConfig *cfg)
{
  ExampleDestinationDriver *self = g_new0(ExampleDestinationDriver, 1);
  self->filename = g_string_new("");

  log_threaded_dest_driver_init_instance(&self->super, cfg);
  self->super.super.super.super.init = _dd_init;
  self->super.super.super.super.deinit = _dd_deinit;
  self->super.super.super.super.free_fn = _dd_free;

  self->super.format_stats_instance = _format_stats_instance;
  self->super.super.super.super.generate_persist_name = _format_persist_name;
  self->super.stats_source = stats_register_type("example-destination");
  self->super.worker.construct = example_destination_dw_new;

  return (LogDriver *)self;
}
