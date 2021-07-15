/*
 * Copyright (c) 2002-2013 Balabit
 * Copyright (c) 1998-2013 Balázs Scheidler
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 */
#include <criterion/criterion.h>
#include "apphook.h"
#include "transport/transport-aux-data.h"

LogTransportAuxData *aux = NULL;

static LogTransportAuxData *
construct_empty_aux(void)
{
  LogTransportAuxData *self = g_new(LogTransportAuxData, 1);

  log_transport_aux_data_init(self);
  return self;
}

static void
free_aux(LogTransportAuxData *self)
{
  log_transport_aux_data_destroy(self);
  g_free(self);
}

static LogTransportAuxData *
construct_aux_with_some_data(void)
{
  LogTransportAuxData *self = construct_empty_aux();

  /* set peer_addr twice to validate that peer_addr is correctly reference counted */
  log_transport_aux_data_set_peer_addr_ref(self, g_sockaddr_inet_new("1.2.3.4", 5555));
  log_transport_aux_data_set_peer_addr_ref(self, g_sockaddr_inet_new("1.2.3.4", 5555));
  log_transport_aux_data_add_nv_pair(self, "foo", "bar");
  return self;
}

Test(aux_data, test_aux_data_reinit_returns_aux_into_initial_state_without_leaks)
{
  log_transport_aux_data_reinit(aux);
  cr_assert_null(aux->peer_addr, "aux->peer_addr is not NULL after reinit");
}

static void
_concat_nvpairs_helper(const gchar *name, const gchar *value, gsize value_len, gpointer user_data)
{
  GString *concatenated = (GString *) user_data;

  g_string_sprintfa(concatenated, "%s=%s\n", name, value);
  cr_assert_eq(value_len, strlen(value), "foreach() length mismatch");
}

static gchar *
_concat_nvpairs(LogTransportAuxData *self)
{
  GString *concatenated = g_string_sized_new(0);

  log_transport_aux_data_foreach(self, _concat_nvpairs_helper, concatenated);
  return g_string_free(concatenated, FALSE);
}

static void
assert_concatenated_nvpairs(LogTransportAuxData *self, const gchar *expected)
{
  gchar *concatenated = _concat_nvpairs(self);

  cr_assert_str_eq(concatenated, expected, "foreach() didn't return all-added nvpairs");
  g_free(concatenated);
}

Test(aux_data, test_aux_data_added_nvpairs_are_returned_by_foreach_in_order)
{
  log_transport_aux_data_add_nv_pair(aux, "super", "lativus");
  assert_concatenated_nvpairs(aux, "foo=bar\nsuper=lativus\n");
}

Test(aux_data, test_aux_data_copy_creates_an_identical_copy)
{
  LogTransportAuxData aux_copy;
  gchar *orig, *copy;

  log_transport_aux_data_copy(&aux_copy, aux);

  orig = _concat_nvpairs(aux);
  copy = _concat_nvpairs(&aux_copy);
  cr_assert_str_eq(orig, copy, "copy incorrectly copied aux->nvpairs");
  g_free(orig);
  g_free(copy);
  log_transport_aux_data_destroy(&aux_copy);
}

Test(aux_data, test_aux_data_copy_separates_the_copies)
{
  LogTransportAuxData aux_copy;
  gchar *orig, *copy;

  log_transport_aux_data_copy(&aux_copy, aux);
  log_transport_aux_data_add_nv_pair(aux, "super", "lativus");

  orig = _concat_nvpairs(aux);
  copy = _concat_nvpairs(&aux_copy);
  cr_assert_str_neq(orig, copy,
                    "copy incorrectly copied aux->nvpairs as change to one of them affected the other, orig=%s, copy=%s", orig, copy);
  g_free(orig);
  g_free(copy);
  log_transport_aux_data_destroy(&aux_copy);
}

Test(aux_data, test_add_nv_pair_to_a_NULL_aux_data_will_do_nothing)
{
  log_transport_aux_data_add_nv_pair(NULL, "foo", "bar");
  assert_concatenated_nvpairs(NULL, "");
}

Test(aux_data, test_aux_data_functions_with_NULL_instance_does_nothing)
{
  aux = NULL;

  log_transport_aux_data_init(aux);
  log_transport_aux_data_reinit(aux);
  log_transport_aux_data_destroy(aux);

  log_transport_aux_data_init(aux);
  /* set peer_addr twice to validate that peer_addr is correctly reference counted */
  log_transport_aux_data_set_peer_addr_ref(aux, g_sockaddr_inet_new("1.2.3.4", 5555));
  log_transport_aux_data_set_peer_addr_ref(aux, g_sockaddr_inet_new("1.2.3.4", 5555));
#if 0
  log_transport_aux_data_add_nv_pair(aux, "foo", "bar");
  assert_concatenated_nvpairs(aux, "");
  log_transport_aux_data_destroy(aux);
#endif
}

static void
setup(void)
{
  app_startup();
  aux = construct_aux_with_some_data();
}

static void
teardown(void)
{
  free_aux(aux);
  app_shutdown();
}

TestSuite(aux_data, .init = setup, .fini = teardown);
