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

#include "verbose.h"
#include <strings.h>

static gchar *verbose_set = NULL;

GOptionEntry verbose_options[] =
{
  {
    "set", 's', 0, G_OPTION_ARG_STRING, &verbose_set,
    "enable/disable messages", "<on|off|0|1>"
  },
  { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL }
};

static gboolean
_is_response_empty(GString *response)
{
  return (response == NULL || g_str_equal(response->str, ""));
}

static gint
_print_reply_to_stdout(GString *reply, gpointer user_data)
{
  gboolean first_response = *((gboolean *)user_data);
  gint retval = 0;
  if (first_response)
    {
      if (_is_response_empty(reply))
        retval = 1;
      else retval = process_response_status(reply);
    }

  printf("%s\n", reply->str);

  return retval;
}

gint
slng_verbose(int argc, char *argv[], const gchar *mode, GOptionContext *ctx)
{
  gboolean first_response = TRUE;
  gchar buff[256];

  if (!verbose_set)
    g_snprintf(buff, 255, "LOG %s\n", mode);
  else
    g_snprintf(buff, 255, "LOG %s %s\n", mode,
               strncasecmp(verbose_set, "on", 2) == 0 || verbose_set[0] == '1' ? "ON" : "OFF");

  g_strup(buff);

  return slng_run_command(buff, _print_reply_to_stdout, &first_response);
}
