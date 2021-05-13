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
from pathlib2 import Path
from psutil import TimeoutExpired

import src.testcase_parameters.testcase_parameters as tc_parameters
from src.executors.process_executor import ProcessExecutor


class Netcat(object):

    instanceIndex = -1
    @staticmethod
    def __get_new_instance_index():
        Netcat.instanceIndex += 1
        return Netcat.instanceIndex

    def __init__(self):
        self.netcat_proc = None
        self.__netcat_stdout_path = None

    @property
    def output_path(self):
        return str(self.__netcat_stdout_path.absolute())

    def __decode_start_parameters(self, k, l, u):
        start_parameters = []

        if k is True:
            start_parameters.append("-k")

        if l is True:
            start_parameters.append("-l")

        if u is True:
            start_parameters.append("-u")

        return start_parameters

    def start(self, destination, port, k=None, l=None, u=None):  # noqa: E741
        if self.netcat_proc is not None and self.netcat_proc.is_running():
            raise Exception("Netcat is already running, you shouldn't call start")

        instanceIndex = Netcat.__get_new_instance_index()
        self.__netcat_stdout_path = Path(tc_parameters.WORKING_DIR, "netcat_stdout_{}".format(instanceIndex))
        netcat_stderr_path = Path(tc_parameters.WORKING_DIR, "netcat_stderr_{}".format(instanceIndex))

        self.parameters = self.__decode_start_parameters(k, l, u)

        self.netcat_proc = ProcessExecutor().start(
            ["nc"] + self.parameters + [destination, port],
            self.__netcat_stdout_path,
            netcat_stderr_path,
        )

        return self.netcat_proc

    def stop(self):
        if self.netcat_proc is None:
            return

        self.netcat_proc.terminate()
        try:
            self.netcat_proc.wait(4)
        except TimeoutExpired:
            self.netcat_proc.kill()

        self.netcat_proc = None
