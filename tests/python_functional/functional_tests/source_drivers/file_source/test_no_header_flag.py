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


def test_no_header_flag(config, syslog_ng, log_message, bsd_formatter):
    input_message = bsd_formatter.format_message(log_message)

    file_source = config.create_file_source(file_name="input.log", flags="no-header")
    file_destination = config.create_file_destination(file_name="output.log", template="'<$PRI>$MSG\n'")
    config.create_logpath(statements=[file_source, file_destination])

    file_source.write_log(input_message)
    syslog_ng.start(config)

    expected_msg_value_for_no_header_flag = "%s %s %s[%s]: %s\n" % (
        log_message.bsd_timestamp_value,
        log_message.hostname_value,
        log_message.program_value,
        log_message.pid_value,
        log_message.message_value,
    )
    full_expected_message = "<{}>{}".format(log_message.priority_value, expected_msg_value_for_no_header_flag)

    assert file_destination.read_log() == full_expected_message
