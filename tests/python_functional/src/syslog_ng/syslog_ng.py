#!/usr/bin/env python
#############################################################################
# Copyright (c) 2015-2018 Balabit
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
from src.syslog_ng.console_log_reader import ConsoleLogReader
from src.syslog_ng.syslog_ng_cli import SyslogNgCli


class SyslogNg(object):
    def __init__(self, instance_paths, testcase_parameters):
        self.instance_paths = instance_paths
        self.__syslog_ng_cli = SyslogNgCli(instance_paths, testcase_parameters)

    def start(self, config, stderr=True, debug=True, trace=True, verbose=True, startup_debug=True, no_caps=True, config_path=None, persist_path=None, pid_path=None, control_socket_path=None):
        return self.__syslog_ng_cli.start(config, stderr, debug, trace, verbose, startup_debug, no_caps, config_path, persist_path, pid_path, control_socket_path)

    def syntax_check(self, config):
        return self.__syslog_ng_cli.syntax_check(config)

    def stop(self, unexpected_messages=None):
        self.__syslog_ng_cli.stop(unexpected_messages)

    def reload(self, config):
        self.__syslog_ng_cli.reload(config)

    def restart(self, config):
        self.__syslog_ng_cli.stop()
        self.__syslog_ng_cli.start(config)

    def get_version(self):
        return self.__syslog_ng_cli.get_version()

    def is_process_running(self):
        return self.__syslog_ng_cli.is_process_running()

    def wait_for_messages_in_console_log(self, expected_messages):
        assert issubclass(type(expected_messages), list)
        console_log_reader = ConsoleLogReader(self.instance_paths)
        return console_log_reader.wait_for_messages_in_console_log(expected_messages)

    def wait_for_message_in_console_log(self, expected_message):
        return self.wait_for_messages_in_console_log([expected_message])
