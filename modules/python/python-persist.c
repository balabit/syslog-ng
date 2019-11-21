/*
 * Copyright (c) 2019 Balabit
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

#include "python-persist.h"
#include "python-helpers.h"
#include "driver.h"

static PyObject *
_call_generate_persist_name_method(PyObject *generate_persist_name_method, GHashTable *options,
                                   const gchar *class, const gchar *id)
{
  PyObject *py_options = options ? _py_create_arg_dict(options) : NULL;
  PyObject *ret = _py_invoke_function(generate_persist_name_method, py_options, class, id);
  Py_XDECREF(py_options);
  return ret;
}

static void
format_default_stats_name(gchar *buffer, gsize size, const gchar *module, const gchar *name)
{
  g_snprintf(buffer, size, "%s,%s", module, name);
}

static void
copy_stats_instance(const LogPipe *self, PyObject *generate_persist_name_method, GHashTable *options,
                    const gchar *module, const gchar *class, gchar *buffer, gsize size)
{
  PyGILState_STATE gstate;
  gstate = PyGILState_Ensure();

  PyObject *ret =_call_generate_persist_name_method(generate_persist_name_method,
                                                    options, class, ((LogDriver *)self)->id);
  if (ret)
    g_snprintf(buffer, size, "%s,%s", module, _py_get_string_as_string(ret));
  else
    {
      msg_error("Failed while generating persist name",
                evt_tag_str("driver", ((LogDriver *)self)->id),
                evt_tag_str("class", class));
      format_default_stats_name(buffer, size, module, class);
    }
  Py_XDECREF(ret);

  PyGILState_Release(gstate);
}

const gchar *
python_format_stats_instance(LogPipe *p, PyObject *generate_persist_name_method, GHashTable *options,
                             const gchar *module, const gchar *class)
{
  static gchar persist_name[1024];

  if (p->persist_name)
    format_default_stats_name(persist_name, sizeof(persist_name), module, p->persist_name);
  else if (generate_persist_name_method)
    copy_stats_instance(p, generate_persist_name_method, options, module, class, persist_name, sizeof(persist_name));
  else
    format_default_stats_name(persist_name, sizeof(persist_name), module, class);

  return persist_name;
}

static void
format_default_persist_name_with_persist_name(gchar *buffer, gsize size, const gchar *module, const gchar *persist_name)
{
  g_snprintf(buffer, size, "%s.%s", module, persist_name);
}

static void
format_default_persist_name_with_class(gchar *buffer, gsize size, const gchar *module, const gchar *class)
{
  g_snprintf(buffer, size, "%s(%s)", module, class);
}

static void
copy_persist_name(const LogPipe *self, PyObject *generate_persist_name_method, GHashTable *options,
                  const gchar *module, const gchar *class, gchar *buffer, gsize size)
{
  PyGILState_STATE gstate;
  gstate = PyGILState_Ensure();

  PyObject *ret =_call_generate_persist_name_method(generate_persist_name_method,
                                                    options, class, ((LogDriver *)self)->id);
  if (ret)
    g_snprintf(buffer, size, "%s.%s", module, _py_get_string_as_string(ret));
  else
    {
      msg_error("Failed while generating persist name",
                evt_tag_str("driver", ((LogDriver *)self)->id),
                evt_tag_str("class", class));
      format_default_persist_name_with_class(buffer, size, module, class);
    }
  Py_XDECREF(ret);

  PyGILState_Release(gstate);
}

const gchar *
python_format_persist_name(const LogPipe *p, PyObject *generate_persist_name_method, GHashTable *options,
                           const gchar *module, const gchar *class)
{
  static gchar persist_name[1024];

  if (p->persist_name)
    format_default_persist_name_with_persist_name(persist_name, sizeof(persist_name), module, p->persist_name);
  else if (generate_persist_name_method)
    copy_persist_name(p, generate_persist_name_method, options, module, class, persist_name, sizeof(persist_name));
  else
    format_default_persist_name_with_class(persist_name, sizeof(persist_name), module, class);

  return persist_name;
}
