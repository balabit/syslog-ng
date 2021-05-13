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
import pytest


@pytest.mark.netcat
@pytest.mark.parametrize(
    "transport", [
        None,
        "tcp",
        "udp",
    ], ids=["default", "tcp", "udp"],
)
def test_network_destination_transport(config, syslog_ng, port_allocator, transport):
    counter = 100
    message = "message text"

    generator_source = config.create_example_msg_generator_source(num=counter, freq=0.0001, template=config.stringify(message))
    if transport is not None:
        network_destination = config.create_network_destination(ip="localhost", port=port_allocator(), transport=transport)
    else:
        network_destination = config.create_network_destination(ip="localhost", port=port_allocator())
    config.create_logpath(statements=[generator_source, network_destination])

    network_destination.start_listener()
    syslog_ng.start(config)
    assert network_destination.read_until_logs([message] * counter)
