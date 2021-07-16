#!/usr/bin/env python
#############################################################################
# Copyright (c) 2020 Balabit
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


class Rewrite(object):
    group_type = "rewrite"

    def __init__(self, driver_name, positional_parameters, **options):
        self.driver_name = driver_name
        self.options = options
        self.positional_parameters = positional_parameters


class SetTag(Rewrite):
    def __init__(self, tag, **options):
        super(SetTag, self).__init__("set_tag", [tag], **options)


class SetPri(Rewrite):
    def __init__(self, pri, **options):
        super(SetPri, self).__init__("set_pri", [pri], **options)
