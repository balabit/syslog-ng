/*
 * Copyright (c) 2002-2014 BalaBit IT Ltd, Budapest, Hungary
 * Copyright (c) 1998-2014 Balázs Scheidler
 * Copyright (c) 2013-2014 Benke Tibor
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */


#ifndef SYSTEMD_SOURCE_H_INCLUDED
#define SYSTEMD_SOURCE_H_INCLUDED 1

#include "afsocket-source.h"
#include "transport-mapper-unix.h"

typedef struct _SystemDSourceDriver
{
  AFSocketSourceDriver super;
} SystemDSourceDriver;

SystemDSourceDriver *systemd_sd_new(GlobalConfig *cfg);

#endif
