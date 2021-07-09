#!/usr/bin/env python
#############################################################################
# Copyright (c) 2020 Balabit
# Copyright (c) 2021 Xiaoyu Qiu
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


test_parameters_raw = [
    ("foo", '''""''', '''"(?<DN>foo)"''', "", "${DN}", True, "foo"),
    ("foo", '''""''', '''"(?<DN>fo*)"''', "", "${DN}", True, "foo"),
    ("foo", '''".reg."''', '''"(?<DN>foo)"''', "", "${.reg.DN}", True, "foo"),
    ("foo", '''".reg."''', '''"(?<DN>foo)"''', "", "${DN}", True, ""),
    ("foo", '''".reg."''', '''"(?<DN>foo)|(?<DN>bar)"''', "dupnames", "${.reg.DN}", True, "foo"),
    ("abc", '''""''', '''"(?<DN>Abc)"''', "", "${DN}", False, ""),
    ("abc", '''""''', '''"(?<DN>Abc)"''', "ignore-case", "${DN}", True, "abc"),
]


@pytest.mark.parametrize(
    "input_message, prefix, patterns, flags, template, expected_result, expected_value", test_parameters_raw,
    ids=list(map(str, range(len(test_parameters_raw)))),
)
def test_regexp_parser(config, syslog_ng, input_message, prefix, patterns, template, flags, expected_result, expected_value):
    config.add_include("scl.conf")

    generator_source = config.create_example_msg_generator_source(num=1, template=config.stringify(input_message))
    regexp_parser = config.create_regexp_parser(prefix=prefix, patterns=patterns, flags=flags)

    file_destination = config.create_file_destination(file_name="output.log", template=config.stringify(template + "\n"))
    config.create_logpath(statements=[generator_source, regexp_parser, file_destination])

    syslog_ng.start(config)

    if expected_result:
        assert file_destination.read_log().strip() == expected_value
    else:
        assert file_destination.get_path().exists() is False
