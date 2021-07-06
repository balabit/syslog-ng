/*
 * Copyright (c) 2020 Balabit
 * Copyright (c) 2020 Kokan <kokaipeter@gmail.com>
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

#include <criterion/criterion.h>

#include "syslog-ng.h"
#include "logmsg/logmsg.h"
#include "apphook.h"

static LogMessage *msg = NULL;
static GString *result = NULL;

void
setup(void)
{
  app_startup();
  msg = log_msg_new_empty();
  result = g_string_new(NULL);
}

void
teardown(void)
{
  g_string_free(result, true);
  result = NULL;
  log_msg_unref(msg);
  msg = NULL;

  app_shutdown();
}

TestSuite(sdata_format, .init = setup, .fini = teardown);


Test(sdata_format, no_sdata)
{
  log_msg_format_sdata(msg, result, 0);

  cr_assert_str_eq(result->str, "");
}

Test(sdata_format, sequence_id_from_argument)
{
  log_msg_format_sdata(msg, result, 112);

  cr_assert_str_eq(result->str, "[meta sequenceId=\"112\"]");
}

Test(sdata_format, sequence_id_from_msg)
{
  log_msg_set_value_by_name(msg, ".SDATA.meta.sequenceId", "111", -1);

  log_msg_format_sdata(msg, result, 0);

  cr_assert_str_eq(result->str, "[meta sequenceId=\"111\"]");
}

Test(sdata_format, sequence_id_from_msg_and_argument)
{
  log_msg_set_value_by_name(msg, ".SDATA.meta.sequenceId", "111", -1);

  log_msg_format_sdata(msg, result, 112);

  cr_assert_str_eq(result->str, "[meta sequenceId=\"111\"]");
}

Test(sdata_format, sdata)
{
  log_msg_set_value_by_name(msg, ".SDATA.junos.reason", "TCP FIN", -1);
  log_msg_set_value_by_name(msg, ".SDATA.foo.bar.reason", "TCP FIN", -1);

  log_msg_format_sdata(msg, result, 0);

  cr_assert_str_eq(result->str, "[foo.bar reason=\"TCP FIN\"][junos reason=\"TCP FIN\"]");
}

Test(sdata_format, enterpriseid)
{
  log_msg_set_value_by_name(msg, ".SDATA.test@1.2.3.log.fac", "14", -1);

  log_msg_format_sdata(msg, result, 0);

  cr_assert_str_eq(result->str, "[test@1.2.3 log.fac=\"14\"]");
}

Test(sdata_format, only_enterpriseid)
{
  log_msg_set_value_by_name(msg, ".SDATA.test@123.bar", "33", -1);

  log_msg_format_sdata(msg, result, 0);

  cr_assert_str_eq(result->str, "[test@123 bar=\"33\"]");
}

Test(sdata_format, invalid_enterprise_id)
{
  log_msg_set_value_by_name(msg, ".SDATA.a@123", "1228", -1);

  log_msg_format_sdata(msg, result, 0);

  cr_assert_str_eq(result->str, "[a@123]");
}

Test(sdata_format, invalid_enterprise_id_2)
{
  log_msg_set_value_by_name(msg, ".SDATA.a@123abc", "0914", -1);

  log_msg_format_sdata(msg, result, 0);

  cr_assert_str_eq(result->str, "[a@123 abc=\"0914\"]");
}


