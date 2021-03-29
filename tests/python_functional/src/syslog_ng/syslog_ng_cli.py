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
import logging

from pathlib2 import Path

import src.testcase_parameters.testcase_parameters as tc_parameters
from src.common.blocking import wait_until_false
from src.common.blocking import wait_until_true
from src.syslog_ng.console_log_reader import ConsoleLogReader
from src.syslog_ng.syslog_ng_executor import SyslogNgExecutor
from src.syslog_ng_ctl.syslog_ng_ctl import SyslogNgCtl

logger = logging.getLogger(__name__)


class SyslogNgCli(object):
    def __init__(self, instance_paths, testcase_parameters):
        self.__instance_paths = instance_paths
        self.__console_log_reader = ConsoleLogReader(instance_paths)
        self.__syslog_ng_executor = SyslogNgExecutor(instance_paths)
        self.__syslog_ng_ctl = SyslogNgCtl(instance_paths)
        self.__external_tool = testcase_parameters.get_external_tool()
        self.__process = None
        self.__stderr = None
        self.__debug = None
        self.__trace = None
        self.__verbose = None
        self.__startup_debug = None
        self.__no_caps = None
        self.__config_path = None
        self.__persist_path = None
        self.__pid_path = None
        self.__control_socket_path = None

    # Application commands
    def get_version(self):
        version_output = self.__syslog_ng_executor.run_command(command_short_name="version", command=["--version"])[
            "stdout"
        ]
        for version_output_line in version_output.splitlines():
            if "Config version:" in version_output_line:
                return version_output_line.split()[2]
        raise Exception("Can not parse 'Config version' from ./syslog-ng --version")

    def __syntax_only(self, config_path=None):
        if config_path is None:
            config_path = self.__instance_paths.get_config_path()
        return self.__syslog_ng_executor.run_command(
            command_short_name="syntax_only", command=["--syntax-only", "--cfgfile={}".format(config_path)],
        )

    def syntax_check(self, config):
        config.write_config(self.__instance_paths.get_config_path())
        result = self.__syntax_only()
        if result["exit_code"] != 0:
            logger.error(result["stderr"])
            raise Exception("syslog-ng can not started exit_code={}".format(result["exit_code"]))

    def is_process_running(self):
        return self.__process.poll() is None

    def __wait_for_control_socket_alive(self):
        def is_alive(s):
            if not s.is_process_running():
                self.__process = None
                self.__error_handling("syslog-ng is not running")
            return s.__syslog_ng_ctl.is_control_socket_alive()
        return wait_until_true(is_alive, self)

    def __wait_for_start(self):
        # wait for start and check start result
        if not self.__wait_for_control_socket_alive():
            self.__error_handling("Control socket not alive")
        if not self.__console_log_reader.wait_for_start_message():
            self.__error_handling("Start message not arrived")

    def __start_syslog_ng(self):
        if self.__external_tool:
            self.__process = self.__syslog_ng_executor.run_process_with_external_tool(self.__external_tool)
        else:
            self.__process = self.__syslog_ng_executor.run_process(self.__stderr, self.__debug, self.__trace, self.__verbose, self.__startup_debug, self.__no_caps, self.__config_path, self.__persist_path, self.__pid_path, self.__control_socket_path)
        if self.__stderr and self.__debug and self.__verbose:
            self.__wait_for_start()

    # Process commands
    def start(self, config, stderr, debug, trace, verbose, startup_debug, no_caps, config_path, persist_path, pid_path, control_socket_path):
        self.set_start_parameters(stderr, debug, trace, verbose, startup_debug, no_caps, config_path, persist_path, pid_path, control_socket_path)
        if self.__process:
            raise Exception("syslog-ng has been already started")

        config.write_config(self.__instance_paths.get_config_path())

        self.__start_syslog_ng()

        logger.info("syslog-ng process has been started with PID: {}\n".format(self.__process.pid))

        return self.__process

    def reload(self, config):
        config.write_config(self.__instance_paths.get_config_path())

        # effective reload
        result = self.__syslog_ng_ctl.reload()

        # wait for reload and check reload result
        if result["exit_code"] != 0:
            self.__error_handling("Control socket fails to reload syslog-ng")
        if not self.__wait_for_control_socket_alive():
            self.__error_handling("Control socket not alive")
        if self.__stderr and self.__debug and self.__verbose:
            if not self.__console_log_reader.wait_for_reload_message():
                self.__error_handling("Reload message not arrived")
        logger.info("syslog-ng process has been reloaded with PID: {}\n".format(self.__process.pid))

    def stop(self, unexpected_messages=None):
        if self.__process:
            saved_pid = self.__process.pid
            # effective stop
            result = self.__syslog_ng_ctl.stop()

            # wait for stop and check stop result
            if result["exit_code"] != 0:
                self.__error_handling("Control socket fails to stop syslog-ng")
            if not wait_until_false(self.is_process_running):
                self.__error_handling("syslog-ng did not stop")
            if self.__stderr and self.__debug and self.__verbose:
                if not self.__console_log_reader.wait_for_stop_message():
                    self.__error_handling("Stop message not arrived")
            self.__console_log_reader.check_for_unexpected_messages(unexpected_messages)
            if self.__external_tool == "valgrind":
                self.__console_log_reader.handle_valgrind_log(self.__instance_paths.get_external_tool_output_path(self.__external_tool))
            self.__process = None
            logger.info("syslog-ng process has been stopped with PID: {}\n".format(saved_pid))

    # Helper functions
    def __error_handling(self, error_message):
        self.__console_log_reader.dump_stderr()
        self.__handle_core_file()
        raise Exception(error_message)

    def __handle_core_file(self):
        if not self.is_process_running():
            core_file_found = False
            for core_file in Path(".").glob("*core*"):
                core_file_found = True
                self.__process = None
                self.__syslog_ng_executor.get_backtrace_from_core(core_file=str(core_file))
                core_file.replace(Path(tc_parameters.WORKING_DIR, core_file))
            if core_file_found:
                raise Exception("syslog-ng core file was found and processed")

    def set_start_parameters(self, stderr, debug, trace, verbose, startup_debug, no_caps, config_path, persist_path, pid_path, control_socket_path):
        self.__stderr = stderr
        self.__debug = debug
        self.__trace = trace
        self.__verbose = verbose
        self.__startup_debug = startup_debug
        self.__no_caps = no_caps
        self.__config_path = config_path
        self.__persist_path = persist_path
        self.__pid_path = pid_path
        self.__control_socket_path = control_socket_path
