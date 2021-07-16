#!/usr/bin/env python
#############################################################################
# Copyright (c) 2021 One Identity
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published
# by the Free Software Foundation, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
# As an additional exemption you are allowed to compile & link against the
# OpenSSL libraries as published by the OpenSSL project. See the file
# COPYING for details.
#
#############################################################################
import pytest

SYSLOG_NG_DEFAULT_PRI = "38"


@pytest.mark.parametrize(
    "pri_number, expected_pri", [
        ("'0'", "0"),
        ("'42'", "42"),
        ("'1023'", "1023"),
        ("'5789'", SYSLOG_NG_DEFAULT_PRI),
        ("'-2'", SYSLOG_NG_DEFAULT_PRI),
        ("'test'", SYSLOG_NG_DEFAULT_PRI),
        ("''", SYSLOG_NG_DEFAULT_PRI),
    ], ids=["min_value", "valid_value", "max_value", "invalid_big_number", "invalid_negative", "invalid_letters", "empty_value", ],
)
def test_set_pri(config, syslog_ng, log_message, bsd_formatter, pri_number, expected_pri):
    input_message = bsd_formatter.format_message(log_message.priority(38))

    file_source = config.create_file_source(file_name="input.log")
    rewrite_pri = config.create_rewrite_set_pri(pri_number)
    file_destination = config.create_file_destination(file_name="output.log", template=config.stringify("$PRI\n"))
    config.create_logpath(statements=[file_source, rewrite_pri, file_destination])

    file_source.write_log(input_message)
    syslog_ng.start(config)

    assert file_destination.read_log().strip() == expected_pri
