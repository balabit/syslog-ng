#!/usr/bin/env python
#############################################################################
# Copyright (c) 2020 One Identity
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
import atexit
from enum import auto
from enum import Enum

from pathlib2 import Path

import src.testcase_parameters.testcase_parameters as tc_parameters
from src.common.file import File
from src.common.random_id import get_unique_id
from src.helpers.loggen.loggen import Loggen
from src.helpers.netcat.netcat import Netcat


class NetworkIO():
    def __init__(self, ip, port, transport):
        self.__ip = ip
        self.__port = port
        self.__transport = transport
        self.__listener = None
        self.__listener_output_file = None

        atexit.register(self.stop_listener)

    def write(self, content, rate=None):
        loggen_input_file_path = Path(tc_parameters.WORKING_DIR, "loggen_input_{}.txt".format(get_unique_id()))

        loggen_input_file = File(loggen_input_file_path)
        loggen_input_file.open(mode="w")
        loggen_input_file.write(content)
        loggen_input_file.close()

        Loggen().start(self.__ip, self.__port, read_file=str(loggen_input_file_path), dont_parse=True, permanent=True, rate=rate, **self.__transport.to_loggen_params())

    def start_listener(self):
        self.__listener = Netcat()
        self.__listener.start(self.__ip, self.__port, l=True, k=True, **self.__transport.to_netcat_params())  # noqa: E741
        self.__listener_output_file = File(self.__listener.output_path)
        self.__listener_output_file.open(mode="r")

    def stop_listener(self):
        if self.__listener is not None:
            self.__listener.stop()

    def read_number_of_lines(self, counter):
        return self.__listener_output_file.wait_for_number_of_lines(counter)

    def read_until_lines(self, lines):
        return self.__listener_output_file.wait_for_lines(lines)

    class Transport(Enum):
        TCP = auto()
        UDP = auto()
        TLS = auto()
        PROXIED_TCP = auto()
        PROXIED_TLS = auto()

        def to_loggen_params(self):
            loggen_params_mapping = {
                NetworkIO.Transport.TCP: {"inet": True, "stream": True},
                NetworkIO.Transport.UDP: {"dgram": True},
                NetworkIO.Transport.TLS: {"use_ssl": True},
                NetworkIO.Transport.PROXIED_TCP: {"inet": True, "stream": True},
                NetworkIO.Transport.PROXIED_TLS: {"use_ssl": True},
            }
            return loggen_params_mapping[self]

        def to_netcat_params(self):
            netcat_params_mapping = {
                NetworkIO.Transport.TCP: {},
                NetworkIO.Transport.UDP: {"u": True},
            }
            return netcat_params_mapping[self]
