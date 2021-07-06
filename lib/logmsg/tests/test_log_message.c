/*
 * Copyright (c) 2002-2016 Balabit
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
 *
 */

#include <criterion/criterion.h>

#include "msg_parse_lib.h"
#include "apphook.h"
#include "logpipe.h"
#include "rcptid.h"
#include "libtest/persist_lib.h"

#include <stdlib.h>
#include <glib/gprintf.h>

MsgFormatOptions parse_options;

typedef struct _LogMessageTestParams
{
  LogMessage *message;
  LogMessage *cloned_message;
  NVHandle nv_handle;
  NVHandle sd_handle;
  const gchar *tag_name;
} LogMessageTestParams;

static LogMessage *
_construct_log_message(void)
{
  const gchar *raw_msg = "foo";
  LogMessage *msg;

  msg = log_msg_new(raw_msg, strlen(raw_msg), &parse_options);
  log_msg_set_value(msg, LM_V_HOST, raw_msg, -1);
  return msg;
}

static LogMessage *
_construct_merge_base_message(void)
{
  LogMessage *msg;

  msg = log_msg_new_empty();
  log_msg_set_value_by_name(msg, "base", "basevalue", -1);
  log_msg_set_tag_by_name(msg, "basetag");
  return msg;
}

static LogMessage *
_construct_merged_message(const gchar *name, const gchar *value)
{
  LogMessage *msg;

  msg = log_msg_new_empty();
  log_msg_set_value_by_name(msg, name, value, -1);
  log_msg_set_tag_by_name(msg, "mergedtag");
  return msg;
}

static void
assert_log_msg_clear_clears_all_properties(LogMessage *message, NVHandle nv_handle,
                                           NVHandle sd_handle, const gchar *tag_name)
{
  message->flags |= LF_LOCAL + LF_UTF8 + LF_INTERNAL + LF_MARK;
  log_msg_clear(message);

  cr_assert_str_empty(log_msg_get_value(message, nv_handle, NULL),
                      "Message still contains value after log_msg_clear");

  cr_assert_str_empty(log_msg_get_value(message, sd_handle, NULL),
                      "Message still contains sdata value after log_msg_clear");

  cr_assert_null(message->saddr, "Message still contains an saddr after log_msg_clear");
  cr_assert_not(log_msg_is_tag_by_name(message, tag_name),
                "Message still contains a valid tag after log_msg_clear");
  cr_assert((message->flags & LF_LOCAL) == 0, "Message still contains the 'local' flag after log_msg_clear");
  cr_assert((message->flags & LF_UTF8) == 0, "Message still contains the 'utf8' flag after log_msg_clear");
  cr_assert((message->flags & LF_MARK) == 0, "Message still contains the 'mark' flag after log_msg_clear");
  cr_assert((message->flags & LF_INTERNAL) == 0, "Message still contains the 'internal' flag after log_msg_clear");
}

static void
assert_sdata_value_with_seqnum_equals(LogMessage *msg, guint32 seq_num, const gchar *expected)
{
  GString *result = g_string_sized_new(0);

  log_msg_append_format_sdata(msg, result, seq_num);
  cr_assert_str_eq(result->str, expected, "SDATA value does not match, '%s' vs '%s'", expected, result->str);
  g_string_free(result, TRUE);
}

static void
assert_sdata_value_equals(LogMessage *msg, const gchar *expected)
{
  assert_sdata_value_with_seqnum_equals(msg, 0, expected);
}

static LogMessageTestParams *
log_message_test_params_new(void)
{
  LogMessageTestParams *params = g_new0(LogMessageTestParams, 1);

  params->tag_name = "tag";
  params->message = _construct_log_message();

  params->nv_handle = log_msg_get_value_handle("foo");
  params->sd_handle = log_msg_get_value_handle(".SDATA.foo.bar");

  log_msg_set_value(params->message, params->nv_handle, "value", -1);
  log_msg_set_value(params->message, params->sd_handle, "value", -1);
  params->message->saddr = g_sockaddr_inet_new("1.2.3.4", 5050);
  log_msg_set_tag_by_name(params->message, params->tag_name);

  return params;
}

void
log_message_test_params_free(LogMessageTestParams *params)
{
  log_msg_unref(params->message);

  if (params->cloned_message)
    log_msg_unref(params->cloned_message);

  g_free(params);
}

LogMessage *
log_message_test_params_clone_message(LogMessageTestParams *params)
{
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;
  params->cloned_message = log_msg_clone_cow(params->message, &path_options);

  return params->cloned_message;
}


void
setup(void)
{
  app_startup();
  init_parse_options_and_load_syslogformat(&parse_options);
}

void
teardown(void)
{
  deinit_syslogformat_module();
  app_shutdown();
}

TestSuite(log_message, .init = setup, .fini = teardown);

Test(log_message, test_log_message_can_be_created_and_freed)
{
  LogMessage *msg = _construct_log_message();
  log_msg_unref(msg);
}

Test(log_message, test_log_message_can_be_cleared)
{
  LogMessageTestParams *params = log_message_test_params_new();

  log_message_test_params_clone_message(params);

  assert_log_msg_clear_clears_all_properties(params->message, params->nv_handle,
                                             params->sd_handle, params->tag_name);
  assert_log_msg_clear_clears_all_properties(params->cloned_message, params->nv_handle,
                                             params->sd_handle, params->tag_name);

  log_message_test_params_free(params);
}

Test(log_message, test_log_msg_clear_handles_cloned_noninline_tags_properly)
{
  LogMessage *msg = _construct_log_message();

  for (gint i = 0; i < 100; i++)
    {
      gchar tag_name[32];

      g_snprintf(tag_name, sizeof(tag_name), "tag%d", i);
      log_msg_set_tag_by_name(msg, tag_name);
    }

  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;
  LogMessage *cloned = log_msg_clone_cow(msg, &path_options);

  log_msg_clear(cloned);

  for (gint i = 0; i < 100; i++)
    {
      gchar tag_name[32];

      g_snprintf(tag_name, sizeof(tag_name), "tag%d", i);
      cr_assert(log_msg_is_tag_by_name(cloned, tag_name) == FALSE);
    }
  log_msg_unref(cloned);
  log_msg_unref(msg);

}

Test(log_message, test_rcptid_is_automatically_assigned_to_a_newly_created_log_message)
{
  LogMessage *msg;
  PersistState *state = clean_and_create_persist_state_for_test("test_values.persist");
  rcptid_init(state, TRUE);

  msg = log_msg_new_empty();
  cr_assert_eq(msg->rcptid, 1, "rcptid is not automatically set");
  log_msg_unref(msg);

  commit_and_destroy_persist_state(state);
  rcptid_deinit();
}

Test(log_message, test_log_message_merge_with_empty_context)
{
  LogMessageTestParams *params = log_message_test_params_new();
  LogMessage *context[] = {};

  log_message_test_params_clone_message(params);

  log_msg_merge_context(params->message, context, 0);
  assert_log_messages_equal(params->message, params->cloned_message);

  log_message_test_params_free(params);
}

Test(log_message, test_log_message_merge_unset_value)
{
  LogMessage *msg;
  GPtrArray *context = g_ptr_array_sized_new(0);

  msg = _construct_merge_base_message();
  g_ptr_array_add(context, _construct_merged_message("merged", "mergedvalue"));
  log_msg_merge_context(msg, (LogMessage **) context->pdata, context->len);

  assert_log_message_value_by_name(msg, "base", "basevalue");
  assert_log_message_value_by_name(msg, "merged", "mergedvalue");
  g_ptr_array_foreach(context, (GFunc) log_msg_unref, NULL);
  g_ptr_array_free(context, TRUE);
  log_msg_unref(msg);
}

Test(log_message, test_log_message_merge_doesnt_overwrite_already_set_values)
{
  LogMessage *msg;
  GPtrArray *context = g_ptr_array_sized_new(0);

  msg = _construct_merge_base_message();
  g_ptr_array_add(context, _construct_merged_message("base", "mergedvalue"));
  log_msg_merge_context(msg, (LogMessage **) context->pdata, context->len);

  assert_log_message_value_by_name(msg, "base", "basevalue");
  g_ptr_array_foreach(context, (GFunc) log_msg_unref, NULL);
  g_ptr_array_free(context, TRUE);
  log_msg_unref(msg);
}

Test(log_message, test_log_message_merge_merges_the_closest_value_in_the_context)
{
  LogMessage *msg;
  GPtrArray *context = g_ptr_array_sized_new(0);

  msg = _construct_merge_base_message();
  g_ptr_array_add(context, _construct_merged_message("merged", "mergedvalue1"));
  g_ptr_array_add(context, _construct_merged_message("merged", "mergedvalue2"));
  log_msg_merge_context(msg, (LogMessage **) context->pdata, context->len);

  assert_log_message_value_by_name(msg, "merged", "mergedvalue2");
  g_ptr_array_foreach(context, (GFunc) log_msg_unref, NULL);
  g_ptr_array_free(context, TRUE);
  log_msg_unref(msg);
}

Test(log_message, test_log_message_merge_merges_from_all_messages_in_the_context)
{
  LogMessage *msg;
  GPtrArray *context = g_ptr_array_sized_new(0);

  msg = _construct_merge_base_message();
  g_ptr_array_add(context, _construct_merged_message("merged1", "mergedvalue1"));
  g_ptr_array_add(context, _construct_merged_message("merged2", "mergedvalue2"));
  g_ptr_array_add(context, _construct_merged_message("merged3", "mergedvalue3"));
  log_msg_merge_context(msg, (LogMessage **) context->pdata, context->len);

  assert_log_message_value_by_name(msg, "merged1", "mergedvalue1");
  assert_log_message_value_by_name(msg, "merged2", "mergedvalue2");
  assert_log_message_value_by_name(msg, "merged3", "mergedvalue3");
  g_ptr_array_foreach(context, (GFunc) log_msg_unref, NULL);
  g_ptr_array_free(context, TRUE);
  log_msg_unref(msg);
}

Test(log_message, test_log_message_merge_leaves_base_tags_intact)
{
  LogMessage *msg;
  GPtrArray *context = g_ptr_array_sized_new(0);

  msg = _construct_merge_base_message();
  g_ptr_array_add(context, _construct_merged_message("merged1", "mergedvalue1"));
  log_msg_merge_context(msg, (LogMessage **) context->pdata, context->len);

  assert_log_message_has_tag(msg, "basetag");
  assert_log_message_doesnt_have_tag(msg, "mergedtag");
  g_ptr_array_foreach(context, (GFunc) log_msg_unref, NULL);
  g_ptr_array_free(context, TRUE);
  log_msg_unref(msg);
}

Test(log_message, test_log_msg_set_value_indirect_with_self_referencing_handle_results_in_a_nonindirect_value)
{
  LogMessageTestParams *params = log_message_test_params_new();
  gssize value_len;

  log_msg_set_value_indirect(params->message, params->nv_handle, params->nv_handle, 0, 0, 5);
  cr_assert_str_eq(log_msg_get_value(params->message, params->nv_handle, &value_len), "value",
                   "indirect self-reference value doesn't match");

  log_message_test_params_free(params);
}

Test(log_message, test_log_msg_get_value_with_time_related_macro)
{
  LogMessage *msg;
  gssize value_len;
  NVHandle handle;
  const char *date_value;

  msg = log_msg_new_empty();
  msg->timestamps[LM_TS_STAMP].ut_sec = 1389783444;
  msg->timestamps[LM_TS_STAMP].ut_gmtoff = 3600;

  handle = log_msg_get_value_handle("ISODATE");
  date_value = log_msg_get_value(msg, handle, &value_len);
  cr_assert_str_eq(date_value, "2014-01-15T11:57:24+01:00", "ISODATE macro value does not match! value=%s", date_value);

  log_msg_unref(msg);
}

Test(log_message, test_local_logmsg_created_with_the_right_flags_and_timestamps)
{
  LogMessage *msg = log_msg_new_local();

  gboolean are_equals = unix_time_eq(&msg->timestamps[LM_TS_STAMP], &msg->timestamps[LM_TS_RECVD]);

  cr_assert_neq((msg->flags & LF_LOCAL), 0, "LogMessage created by log_msg_new_local() should have LF_LOCAL flag set");
  cr_assert(are_equals, "The timestamps in a LogMessage created by log_msg_new_local() should be equals");

  log_msg_unref(msg);
}

Test(log_message, test_sdata_sanitization)
{
  LogMessage *msg;
  /* These keys looks strange, but JSON object can be parsed to SDATA,
   * so the key could contain any character, while the specification
   * does not declare any way to encode the keys, just the values.
   * The goal is to have a syntactically valid syslog message.
   *
   * Block names are sanitized with the same function as the keys,
   * thus no need for exhaust testing, added just one case to the end
   * to see if business logic applied. */

  msg = log_msg_new_empty();
  log_msg_set_value_by_name(msg, ".SDATA.foo.bar[0]", "value[0]", -1);
  assert_sdata_value_equals(msg, "[foo bar%5B0%5D=\"value[0\\]\"]");
  log_msg_unref(msg);

  msg = log_msg_new_empty();
  log_msg_set_value_by_name(msg, ".SDATA.foo.bácsi", "bácsi", -1);
  assert_sdata_value_equals(msg, "[foo b%C3%A1csi=\"bácsi\"]");
  log_msg_unref(msg);

  msg = log_msg_new_empty();
  log_msg_set_value_by_name(msg, ".SDATA.foo.sp ace", "sp ace", -1);
  assert_sdata_value_equals(msg, "[foo sp%20ace=\"sp ace\"]");
  log_msg_unref(msg);

  msg = log_msg_new_empty();
  log_msg_set_value_by_name(msg, ".SDATA.foo.eq=al", "eq=al", -1);
  assert_sdata_value_equals(msg, "[foo eq%3Dal=\"eq=al\"]");
  log_msg_unref(msg);

  msg = log_msg_new_empty();
  log_msg_set_value_by_name(msg, ".SDATA.foo.quo\"te", "quo\"te", -1);
  assert_sdata_value_equals(msg, "[foo quo%22te=\"quo\\\"te\"]");
  log_msg_unref(msg);

  msg = log_msg_new_empty();
  log_msg_set_value_by_name(msg, ".SDATA.fo@o[0].bar", "value", -1);
  assert_sdata_value_equals(msg, "[fo o%5B0%5D.bar=\"value\"]");
  log_msg_unref(msg);
}

Test(log_message, test_sdata_value_is_updated_by_sdata_name_value_pairs)
{
  LogMessage *msg;

  msg = log_msg_new_empty();
  log_msg_set_value_by_name(msg, ".SDATA.foo.bar1", "value", -1);
  assert_sdata_value_equals(msg, "[foo bar1=\"value\"]");
  log_msg_set_value_by_name(msg, ".SDATA.foo.bar2", "value", -1);
  assert_sdata_value_equals(msg, "[foo bar1=\"value\" bar2=\"value\"]");
  log_msg_set_value_by_name(msg, ".SDATA.foo.bar3", "value", -1);
  assert_sdata_value_equals(msg, "[foo bar1=\"value\" bar2=\"value\" bar3=\"value\"]");
  log_msg_set_value_by_name(msg, ".SDATA.post.value1", "value", -1);
  assert_sdata_value_equals(msg, "[post value1=\"value\"][foo bar1=\"value\" bar2=\"value\" bar3=\"value\"]");
  log_msg_set_value_by_name(msg, ".SDATA.post.value2", "value", -1);
  assert_sdata_value_equals(msg,
                            "[post value1=\"value\" value2=\"value\"][foo bar1=\"value\" bar2=\"value\" bar3=\"value\"]");
  log_msg_unref(msg);
}

Test(log_message, test_sdata_seqnum_adds_meta_sequence_id)
{
  LogMessage *msg;

  msg = log_msg_new_empty();
  log_msg_set_value_by_name(msg, ".SDATA.foo.bar1", "value", -1);
  log_msg_set_value_by_name(msg, ".SDATA.foo.bar2", "value", -1);
  log_msg_set_value_by_name(msg, ".SDATA.foo.bar3", "value", -1);
  assert_sdata_value_with_seqnum_equals(msg, 5,
                                        "[foo bar1=\"value\" bar2=\"value\" bar3=\"value\"][meta sequenceId=\"5\"]");
  log_msg_set_value_by_name(msg, ".SDATA.meta.foobar", "value", -1);
  assert_sdata_value_with_seqnum_equals(msg, 6,
                                        "[meta sequenceId=\"6\" foobar=\"value\"][foo bar1=\"value\" bar2=\"value\" bar3=\"value\"]");
  log_msg_unref(msg);
}

Test(log_message, test_sdata_value_omits_unset_values)
{
  LogMessage *msg;

  msg = log_msg_new_empty();
  log_msg_set_value_by_name(msg, ".SDATA.foo.bar1", "value", -1);
  log_msg_set_value_by_name(msg, ".SDATA.foo.bar2", "value", -1);
  log_msg_set_value_by_name(msg, ".SDATA.foo.bar3", "value", -1);
  assert_sdata_value_equals(msg, "[foo bar1=\"value\" bar2=\"value\" bar3=\"value\"]");
  log_msg_unset_value_by_name(msg, ".SDATA.foo.bar2");
  assert_sdata_value_equals(msg, "[foo bar1=\"value\" bar3=\"value\"]");
  log_msg_unset_value_by_name(msg, ".SDATA.foo.bar1");
  log_msg_unset_value_by_name(msg, ".SDATA.foo.bar3");
  assert_sdata_value_equals(msg, "");
  log_msg_unref(msg);
}

#define DEFUN_KEY_VALUE(name, key, value, size) \
  gchar name ## _key[size]; \
  gchar name ## _value[size]; \
  name ## _key[size-1] = name ## _value[size-1] = 0; \
  memset(name ## _key, key, sizeof(name ##_key)-1); \
  memset(name ## _value, value, sizeof(name ##_value)-1); \


typedef struct
{
  gssize nvtable_size_old;
  gssize nvtable_size_new;
  gssize msg_size_old;
  gssize msg_size_new;
} sizes_t;

static sizes_t
add_key_value(LogMessage *msg, gchar *key, gchar *value)
{
  sizes_t sizes;
  sizes.msg_size_old = log_msg_get_size(msg);
  sizes.nvtable_size_old = nv_table_get_memory_consumption(msg->payload);
  log_msg_set_value_by_name(msg, key, value, -1);
  sizes.nvtable_size_new = nv_table_get_memory_consumption(msg->payload);
  sizes.msg_size_new = log_msg_get_size(msg);
  return sizes;
}


static void
test_with_sdata(LogMessage *msg, guint32 old_msg_size)
{
  sizes_t sizes;
  gchar key[]          = ".SDATA.**";
  gchar value[] = "AAAAAAA";

  guint32 single_sdata_kv_size;
  guint32 sdata_payload_array_size;

  const char iter_length = 17;

  for (char i = 0; i < iter_length; i++)
    {
      g_sprintf(key, ".SDATA.%02d", i);
      sizes = add_key_value(msg, key, value);

      single_sdata_kv_size = NV_ENTRY_DIRECT_HDR + NV_TABLE_BOUND(strlen(key)+1 + strlen(value)+1);

      /* i+1 is stored, but the sdata array size is calculated when adding the i-th */
      sdata_payload_array_size = STRICT_ROUND_TO_NEXT_EIGHT(i) * sizeof(msg->sdata[0]);
      cr_assert_eq(old_msg_size + (i+1) * single_sdata_kv_size + sdata_payload_array_size, sizes.msg_size_new);
    }
}

#define SMALL_LENGTH 10
#define LARGE_LENGTH 256

Test(log_message, test_message_size)
{
  LogMessage *msg = log_msg_new_empty();

  sizes_t sizes;

  DEFUN_KEY_VALUE(small, 'C', 'D', SMALL_LENGTH);
  sizes = add_key_value(msg, small_key, small_value);
  // (SMALL_LENGTH-1)*'C'+'\0' + (SMALL_LENGTH-1)*'D'+'\0'
  guint32 entry_size = NV_ENTRY_DIRECT_HDR + NV_TABLE_BOUND(SMALL_LENGTH + SMALL_LENGTH);
  cr_assert_eq(sizes.nvtable_size_old + entry_size, sizes.nvtable_size_new);
  cr_assert_eq(sizes.msg_size_old + entry_size, sizes.msg_size_new);  // Size increased because of nvtable

  guint32 msg_size = sizes.msg_size_new;
  log_msg_set_tag_by_name(msg, "test_tag_storage");
  cr_assert(log_msg_is_tag_by_name(msg, "test_tag_storage"));
  cr_assert_eq(msg_size, log_msg_get_size(msg)); // Tag is not increased until tag id 65

  char *tag_name = strdup("00tagname");
  // (*8 to convert to bits) + no need plus 1 bcause we already added one tag: test_tag_storage
  for (int i = 0; i < GLIB_SIZEOF_LONG*8; i++)
    {
      sprintf(tag_name, "%dtagname", i);
      log_msg_set_tag_by_name(msg, tag_name);
    }
  free(tag_name);
  cr_assert_eq(msg_size + 2*GLIB_SIZEOF_LONG, log_msg_get_size(msg));


  DEFUN_KEY_VALUE(big, 'A', 'B', LARGE_LENGTH);
  sizes = add_key_value(msg, big_key, big_value); // nvtable is expanded
  entry_size = NV_ENTRY_DIRECT_HDR + NV_TABLE_BOUND(LARGE_LENGTH + LARGE_LENGTH);
  cr_assert_eq(sizes.nvtable_size_old + entry_size, sizes.nvtable_size_new); // but only increased by the entry
  cr_assert_eq(sizes.msg_size_old + entry_size, sizes.msg_size_new);  // nvtable is doubled

  test_with_sdata(msg, sizes.msg_size_new);

  log_msg_unref(msg);
}

Test(log_message, when_get_indirect_value_with_null_value_len_abort_instead_of_sigsegv, .signal=SIGABRT)
{
  LogMessageTestParams *params = log_message_test_params_new();

  NVHandle indirect = log_msg_get_value_handle("INDIRECT");
  log_msg_set_value_indirect(params->message, indirect, params->nv_handle, 0, 0, 5);
  log_msg_get_value(params->message, indirect, NULL);

  log_message_test_params_free(params);
}
