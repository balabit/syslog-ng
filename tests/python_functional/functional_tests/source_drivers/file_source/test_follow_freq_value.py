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
import pytest


@pytest.mark.parametrize(
    "follow_freq,expected", [
        (1, True),
        (1.0, True),
        (0.1, True),
        (0, True),
        (0.0, True),
        (-1, False),
        (-1.0, False),
        (-0.1, False),
    ],
)
def test_follow_freq_value(config, syslog_ng, follow_freq, expected):
    raw_config = '@version: {}\nsource s_file {{ file("input.log" follow-freq({})); }};'.format(config.get_version(), follow_freq)
    config.set_raw_config(raw_config)

    if expected is True:
        syslog_ng.syntax_check(config)
    else:
        with pytest.raises(Exception):
            syslog_ng.syntax_check(config)
