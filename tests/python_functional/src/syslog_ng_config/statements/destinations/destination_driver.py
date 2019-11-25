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
from src.syslog_ng_config.statements.destinations.destination_reader import DestinationReader


class DestinationDriver(object):
    group_type = "destination"

    def __init__(self, positional_parameters=None, options=None, driver_io_cls=None):
        if positional_parameters is None:
            positional_parameters = []
        self.positional_parameters = positional_parameters
        if options is None:
            options = {}
        self.options = options

        self.driver_io_cls = driver_io_cls
        self.destination_reader = None
        self.init_destination_reader()

    def init_destination_reader(self):
        if self.driver_io_cls and self.positional_parameters:
            self.destination_reader = DestinationReader(self.driver_io_cls)
            self.destination_reader.init_driver_io(self.positional_parameters[0])

    def read_log(self):
        return self.destination_reader.read_logs(self.positional_parameters[0], counter=1)[0]

    def read_logs(self, counter):
        return self.destination_reader.read_logs(self.positional_parameters[0], counter=counter)
