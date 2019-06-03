/*
 * Copyright (c) 2016 Balabit
 * Copyright (c) 2016 Laszlo Budai <laszlo.budai@balabit.com>
 *
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

package io.searchbox.core;

public final class JestIndex extends Index {

  public JestIndex() { super(new Index.Builder(null)); }


  public JestIndex setIndexName(final String index) {
    super.indexName = index;
    return this;
  }

  public JestIndex setId(final String id) {
    super.id = id;
    return this;
  }

  public JestIndex setType(final String type) {
    super.typeName = type;
    return this;
  }

  public JestIndex setFormattedMessage(final String message) {
    super.payload = message;
    return this;
  }

}
