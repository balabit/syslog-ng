#!/usr/bin/env python
#############################################################################
# Copyright (c) 2021 Balabit
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
from src.driver_io.network.network_io import NetworkIO
from src.syslog_ng_config.statements.destinations.destination_driver import DestinationDriver


def map_transport(transport):
    mapping = {
        "tcp": NetworkIO.Transport.TCP,
        "udp": NetworkIO.Transport.UDP,
    }
    transport = transport.replace("_", "-").replace("'", "").replace('"', "").lower()

    return mapping[transport]


def create_io(ip, options):
    transport = options["transport"] if "transport" in options else "tcp"

    return NetworkIO(ip, options["port"], map_transport(transport))


class NetworkDestination(DestinationDriver):
    def __init__(self, ip, **options):
        self.driver_name = "network"
        self.ip = ip
        self.io = create_io(self.ip, options)
        super(NetworkDestination, self).__init__([self.ip], options)

    def start_listener(self):
        self.io.start_listener()

    def stop_listener(self):
        self.io.stop_listener()

    def read_log(self):
        return self.read_logs(1)[0]

    def read_logs(self, counter):
        return self.io.read_number_of_lines(counter)

    def read_until_logs(self, logs):
        return self.io.read_until_lines(logs)
