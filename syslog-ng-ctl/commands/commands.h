/*
 * Copyright (c) 2019 Balabit
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

#ifndef SYSLOG_NG_CTL_COMMANDS_H_INCLUDED
#define SYSLOG_NG_CTL_COMMANDS_H_INCLUDED 1

#include "syslog-ng.h"
#include "secret-storage/secret-storage.h"

#include <stdio.h>

extern GOptionEntry no_options[];

typedef struct _CommandDescriptor
{
  const gchar *mode;
  const GOptionEntry *options;
  const gchar *description;
  gint (*main)(gint argc, gchar *argv[], const gchar *mode, GOptionContext *ctx);
  struct _CommandDescriptor *subcommands;
} CommandDescriptor;

typedef gint (*response_handle)(GString *response, gpointer user_data);

gint dispatch_command(const gchar *cmd);
gint slng_run_command(const gchar *command, response_handle cb, gpointer user_data);
gint process_response_status(GString *response);
gboolean is_syslog_ng_running(void);

gint run(const gchar *control_name, gint argc, gchar **argv, CommandDescriptor *mode, GOptionContext *ctx);
#endif
