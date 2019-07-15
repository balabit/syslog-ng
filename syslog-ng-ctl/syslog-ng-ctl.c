/*
 * Copyright (c) 2002-2017 Balabit
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

#include "syslog-ng.h"
#include "gsocket.h"
#include "control-client.h"
#include "cfg.h"
#include "reloc.h"
#include "secret-storage/secret-storage.h"
#include "commands/commands.h"
#include "commands/config.h"
#include "commands/credentials.h"
#include "commands/verbose.h"
#include "commands/ctl-stats.h"
#include "commands/query.h"
#include "commands/license.h"

#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <stdlib.h>
#include <errno.h>

#if SYSLOG_NG_HAVE_GETOPT_H
#include <getopt.h>
#endif

static const gchar *control_name;
static void print_usage(const gchar *bin_name, CommandDescriptor *descriptors);

static gint
slng_stop(int argc, char *argv[], const gchar *mode, GOptionContext *ctx)
{
  return dispatch_command("STOP");
}

static gint
slng_reload(int argc, char *argv[], const gchar *mode, GOptionContext *ctx)
{
  return dispatch_command("RELOAD");
}

static gboolean config_options_start = FALSE;
static gboolean config_options_stop = FALSE;

static GOptionEntry internal_options[] =
{
  { "start", 's', 0, G_OPTION_ARG_NONE, &config_options_start, "start live collection of internal logs", NULL },
  { "stop", 'x', 0, G_OPTION_ARG_NONE, &config_options_stop, "stop live collection of internal logs", NULL },
  { NULL,           0,   0, G_OPTION_ARG_NONE, NULL,                         NULL,           NULL }
};

static gint
slng_internal(int argc, char *argv[], const gchar *mode, GOptionContext *ctx)
{
  GString *cmd = g_string_new("");

  if (config_options_start)
    g_string_assign(cmd, "INTERLOGS START");
  else if (config_options_stop)
    g_string_assign(cmd, "INTERLOGS STOP");
  else
    return 1;

  gint res = dispatch_command(cmd->str);
  g_string_free(cmd, TRUE);

  return res;
}

static gint
slng_reopen(int argc, char *argv[], const gchar *mode, GOptionContext *ctx)
{
  return dispatch_command("REOPEN");
}

static gint
slng_listfiles(int argc, char *argv[], const gchar *mode, GOptionContext *ctx)
{
  return dispatch_command("LISTFILES");
}

const gchar *
get_mode(int *argc, char **argv[])
{
  gint i;
  const gchar *mode;

  for (i = 1; i < (*argc); i++)
    {
      if ((*argv)[i][0] != '-')
        {
          mode = (*argv)[i];
          memmove(&(*argv)[i], &(*argv)[i+1], ((*argc) - i) * sizeof(gchar *));
          (*argc)--;
          return mode;
        }
    }
  return NULL;
}

static GOptionEntry slng_options[] =
{
  {
    "control", 'c', 0, G_OPTION_ARG_STRING, &control_name,
    "syslog-ng control socket", "<socket>"
  },
  { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL }
};

gint
slng_export_config_graph(int argc, char *argv[], const gchar *mode, GOptionContext *ctx)
{
  return dispatch_command("EXPORT_CONFIG_GRAPH");
}

static CommandDescriptor modes[] =
{
  { "stats", stats_options, "Get syslog-ng statistics in CSV format", slng_stats, NULL },
  { "verbose", verbose_options, "Enable/query verbose messages", slng_verbose, NULL },
  { "debug", verbose_options, "Enable/query debug messages", slng_verbose, NULL },
  { "trace", verbose_options, "Enable/query trace messages", slng_verbose, NULL },
  { "stop", no_options, "Stop syslog-ng process", slng_stop, NULL },
  { "reload", no_options, "Reload syslog-ng", slng_reload, NULL },
  { "reopen", no_options, "Re-open of log destination files", slng_reopen, NULL },
  { "query", query_options, "Query syslog-ng statistics. Possible commands: list, get, get --sum", slng_query, NULL },
  { "show-license-info", license_options, "Show information about the license", slng_license, NULL },
  { "credentials", no_options, "Credentials manager", NULL, credentials_commands },
  { "config", config_options, "Print current config", slng_config, NULL },
  { "list-files", no_options, "Print files present in config", slng_listfiles, NULL },
  { "export-config-graph", no_options, "export configuration graph", slng_export_config_graph, NULL },
  { "internal-logs", internal_options, "Collect internal syslog-ng logs", slng_internal, NULL },
  { NULL, NULL },
};

static void
print_usage(const gchar *bin_name, CommandDescriptor *descriptors)
{
  gint mode;

  fprintf(stderr, "Syntax: %s <command> [options]\nPossible commands are:\n", bin_name);
  for (mode = 0; descriptors[mode].mode; mode++)
    {
      fprintf(stderr, "    %-20s %s\n", descriptors[mode].mode, descriptors[mode].description);
    }
}

gboolean
_is_help(gchar *cmd)
{
  return g_str_equal(cmd, "--help");
}

static CommandDescriptor *
find_active_mode(CommandDescriptor descriptors[], gint *argc, char **argv, GString *cmdname_accumulator)
{
  const gchar *mode_string = get_mode(argc, &argv);
  if (!mode_string)
    {
      print_usage(cmdname_accumulator->str, descriptors);
      exit(1);
    }

  for (gint mode = 0; descriptors[mode].mode; mode++)
    if (strcmp(descriptors[mode].mode, mode_string) == 0)
      {
        if (descriptors[mode].main)
          return &descriptors[mode];

        g_assert(descriptors[mode].subcommands);
        g_string_append_printf(cmdname_accumulator, " %s", mode_string);
        return find_active_mode(descriptors[mode].subcommands, argc, argv, cmdname_accumulator);
      }

  return NULL;
}

static GOptionContext *
setup_help_context(const gchar *cmdname, CommandDescriptor *active_mode)
{
  if (!active_mode)
    return NULL;

  GOptionContext *ctx = g_option_context_new(cmdname);
#if GLIB_CHECK_VERSION (2, 12, 0)
  g_option_context_set_summary(ctx, active_mode->description);
#endif
  g_option_context_add_main_entries(ctx, active_mode->options, NULL);
  g_option_context_add_main_entries(ctx, slng_options, NULL);

  return ctx;
}

int
main(int argc, char *argv[])
{
  GError *error = NULL;
  int result;

  setlocale(LC_ALL, "");

  control_name = get_installation_path_for(PATH_CONTROL_SOCKET);

  if (argc > 1 && _is_help(argv[1]))
    {
      print_usage(argv[0], modes);
      exit(0);
    }

  GString *cmdname_accumulator = g_string_new(argv[0]);
  CommandDescriptor *active_mode = find_active_mode(modes, &argc, argv, cmdname_accumulator);
  GOptionContext *ctx = setup_help_context(cmdname_accumulator->str, active_mode);
  g_string_free(cmdname_accumulator, TRUE);

  if (!ctx)
    {
      fprintf(stderr, "Unknown command\n");
      print_usage(argv[0], modes);
      exit(1);
    }

  if (!g_option_context_parse(ctx, &argc, &argv, &error))
    {
      fprintf(stderr, "Error parsing command line arguments: %s\n", error ? error->message : "Invalid arguments");
      g_clear_error(&error);
      g_option_context_free(ctx);
      return 1;
    }

  result = run(control_name, argc, argv, active_mode, ctx);
  g_option_context_free(ctx);
  return result;
}
