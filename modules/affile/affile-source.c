/*
 * Copyright (c) 2002-2014 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
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
#include "affile-source.h"
#include "driver.h"
#include "messages.h"
#include "serialize.h"
#include "gprocess.h"
#include "stats/stats-registry.h"
#include "mainloop.h"
#include "transport/transport-file.h"
#include "transport/transport-pipe.h"
#include "transport/transport-device.h"
#include "logproto/logproto-record-server.h"
#include "logproto/logproto-text-server.h"
#include "logproto/logproto-dgram-server.h"
#include "logproto/logproto-indented-multiline-server.h"
#include "logproto-linux-proc-kmsg-reader.h"
#include "poll-fd-events.h"
#include "poll-file-changes.h"
#include "compat/lfs.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdlib.h>

#include <iv.h>

#define DEFAULT_SD_OPEN_FLAGS (O_RDONLY | O_NOCTTY | O_NONBLOCK | O_LARGEFILE)
#define DEFAULT_SD_OPEN_FLAGS_PIPE (O_RDWR | O_NOCTTY | O_NONBLOCK | O_LARGEFILE)

gboolean
affile_sd_set_multi_line_mode(LogDriver *s, const gchar *mode)
{
  AFFileSourceDriver *self = (AFFileSourceDriver *) s;

  if (strcasecmp(mode, "indented") == 0)
    self->file_reader_options.multi_line_mode = MLM_INDENTED;
  else if (strcasecmp(mode, "regexp") == 0)
    self->file_reader_options.multi_line_mode = MLM_PREFIX_GARBAGE;
  else if (strcasecmp(mode, "prefix-garbage") == 0)
    self->file_reader_options.multi_line_mode = MLM_PREFIX_GARBAGE;
  else if (strcasecmp(mode, "prefix-suffix") == 0)
    self->file_reader_options.multi_line_mode = MLM_PREFIX_SUFFIX;
  else if (strcasecmp(mode, "none") == 0)
    self->file_reader_options.multi_line_mode = MLM_NONE;
  else
    return FALSE;
  return TRUE;
}

gboolean
affile_sd_set_multi_line_prefix(LogDriver *s, const gchar *prefix_regexp, GError **error)
{
  AFFileSourceDriver *self = (AFFileSourceDriver *) s;

  self->file_reader_options.multi_line_prefix = multi_line_regexp_compile(prefix_regexp, error);
  return self->file_reader_options.multi_line_prefix != NULL;
}

gboolean
affile_sd_set_multi_line_garbage(LogDriver *s, const gchar *garbage_regexp, GError **error)
{
  AFFileSourceDriver *self = (AFFileSourceDriver *) s;

  self->file_reader_options.multi_line_garbage = multi_line_regexp_compile(garbage_regexp, error);
  return self->file_reader_options.multi_line_garbage != NULL;
}

void
affile_sd_set_follow_freq(LogDriver *s, gint follow_freq)
{
  AFFileSourceDriver *self = (AFFileSourceDriver *) s;

  self->file_reader_options.follow_freq = follow_freq;
}

static inline gboolean
affile_is_linux_proc_kmsg(const gchar *filename)
{
#ifdef __linux__
  if (strcmp(filename, "/proc/kmsg") == 0)
    return TRUE;
#endif
  return FALSE;
}

static inline gboolean
affile_is_linux_dev_kmsg(const gchar *filename)
{
#ifdef __linux__
  if (strcmp(filename, "/dev/kmsg") == 0)
    return TRUE;
#endif
  return FALSE;
}

static inline gboolean
affile_is_device_node(const gchar *filename)
{
  struct stat st;

  if (stat(filename, &st) < 0)
    return FALSE;
  return !S_ISREG(st.st_mode);
}

gboolean
_sd_open_file(FileReader *self, gchar *name, gint *fd)
{
  return affile_open_file(name,
                          &self->file_reader_options->file_open_options,
                          &self->file_reader_options->file_perm_options, fd);
}

static inline const gchar *
affile_sd_format_persist_name(const LogPipe *s)
{
  AFFileSourceDriver *self = (AFFileSourceDriver *)s;
  return log_pipe_get_persist_name(&self->file_reader->super);
}


static inline const gchar *
file_reader_format_persist_name(const LogPipe *s)
{
  const FileReader *self = (const FileReader *)s;
  static gchar persist_name[1024];

  if (self->owner->super.super.persist_name)
    g_snprintf(persist_name, sizeof(persist_name), "affile_sd.%s.curpos", self->owner->super.super.persist_name);
  else
    g_snprintf(persist_name, sizeof(persist_name), "affile_sd_curpos(%s)", self->filename->str);

  return persist_name;
}

static void
file_reader_recover_state(LogPipe *s, GlobalConfig *cfg, LogProtoServer *proto)
{
  FileReader *self = (FileReader *) s;

  if (self->file_reader_options->file_open_options.is_pipe || self->file_reader_options->follow_freq <= 0)
    return;

  if (!log_proto_server_restart_with_state(proto, cfg->state, file_reader_format_persist_name(s)))
    {
      msg_error("Error converting persistent state from on-disk format, losing file position information",
                evt_tag_str("filename", self->filename->str));
      return;
    }
}

static gboolean
_is_fd_pollable(gint fd)
{
  struct iv_fd check_fd;
  gboolean pollable;

  IV_FD_INIT(&check_fd);
  check_fd.fd = fd;
  check_fd.cookie = NULL;

  pollable = (iv_fd_register_try(&check_fd) == 0);
  if (pollable)
    iv_fd_unregister(&check_fd);
  return pollable;
}

static PollEvents *
file_reader_construct_poll_events(FileReader *self, gint fd)
{
  if (self->file_reader_options->follow_freq > 0)
    return poll_file_changes_new(fd, self->filename->str, self->file_reader_options->follow_freq, &self->super);
  else if (fd >= 0 && _is_fd_pollable(fd))
    return poll_fd_events_new(fd);
  else
    {
      msg_error("Unable to determine how to monitor this file, follow_freq() unset and it is not possible to poll it "
                "with the current ivykis polling method. Set follow-freq() for regular files or change "
                "IV_EXCLUDE_POLL_METHOD environment variable to override the automatically selected polling method",
                evt_tag_str("filename", self->filename->str),
                evt_tag_int("fd", fd));
      return NULL;
    }
}

static LogTransport *
file_reader_construct_transport(FileReader *self, gint fd)
{
  if (self->file_reader_options->file_open_options.is_pipe)
    return log_transport_pipe_new(fd);
  else if (self->file_reader_options->follow_freq > 0)
    return log_transport_file_new(fd);
  else if (affile_is_linux_proc_kmsg(self->filename->str))
    return log_transport_device_new(fd, 10);
  else if (affile_is_linux_dev_kmsg(self->filename->str))
    {
      if (lseek(fd, 0, SEEK_END) < 0)
        {
          msg_error("Error seeking /dev/kmsg to the end",
                    evt_tag_str("error", g_strerror(errno)));
        }
      return log_transport_device_new(fd, 0);
    }
  else
    return log_transport_pipe_new(fd);
}

static LogProtoServer *
file_reader_construct_proto(FileReader *self, gint fd)
{
  LogReaderOptions *reader_options = &self->file_reader_options->reader_options;
  LogProtoServerOptions *proto_options = &reader_options->proto_options.super;
  LogTransport *transport;
  MsgFormatHandler *format_handler;

  transport = file_reader_construct_transport(self, fd);

  format_handler = reader_options->parse_options.format_handler;
  if ((format_handler && format_handler->construct_proto))
    {
      proto_options->position_tracking_enabled = TRUE;
      return format_handler->construct_proto(&reader_options->parse_options, transport, proto_options);
    }

  if (self->file_reader_options->pad_size)
    {
      proto_options->position_tracking_enabled = TRUE;
      return log_proto_padded_record_server_new(transport, proto_options, self->file_reader_options->pad_size);
    }
  else if (affile_is_linux_proc_kmsg(self->filename->str))
    return log_proto_linux_proc_kmsg_reader_new(transport, proto_options);
  else if (affile_is_linux_dev_kmsg(self->filename->str))
    return log_proto_dgram_server_new(transport, proto_options);
  else
    {
      proto_options->position_tracking_enabled = TRUE;
      switch (self->file_reader_options->multi_line_mode)
        {
        case MLM_INDENTED:
          return log_proto_indented_multiline_server_new(transport, proto_options);
        case MLM_PREFIX_GARBAGE:
          return log_proto_prefix_garbage_multiline_server_new(transport, proto_options,
                 self->file_reader_options->multi_line_prefix,
                 self->file_reader_options->multi_line_garbage);
        case MLM_PREFIX_SUFFIX:
          return log_proto_prefix_suffix_multiline_server_new(transport, proto_options,
                 self->file_reader_options->multi_line_prefix,
                 self->file_reader_options->multi_line_garbage);
        default:
          return log_proto_text_server_new(transport, proto_options);
        }
    }
}

static void
_deinit_sd_logreader(FileReader *self)
{
  log_pipe_deinit((LogPipe *) self->reader);
  log_pipe_unref((LogPipe *) self->reader);
  self->reader = NULL;
}

static void
_setup_logreader(LogPipe *s, PollEvents *poll_events, LogProtoServer *proto, gboolean check_immediately)
{
  FileReader *self = (FileReader *) s;
  self->reader = log_reader_new(log_pipe_get_config(s));
  log_reader_reopen(self->reader, proto, poll_events);

  log_reader_set_options(self->reader,
                         s,
                         &self->file_reader_options->reader_options,
                         STATS_LEVEL1,
                         SCS_FILE,
                         self->owner->super.id,
                         self->filename->str);
  if (check_immediately)
    log_reader_set_immediate_check(self->reader);

  /* NOTE: if the file could not be opened, we ignore the last
   * remembered file position, if the file is created in the future
   * we're going to read from the start. */
  log_pipe_append((LogPipe *) self->reader, s);
}

static gboolean
_is_immediate_check_needed(gboolean file_opened, gboolean open_deferred)
{
  if (file_opened)
    return TRUE;
  else if (open_deferred)
    return FALSE;
  return FALSE;
}

static gboolean
file_reader_open_file(LogPipe *s, gboolean recover_state)
{
  FileReader *self = (FileReader *) s;
  GlobalConfig *cfg = log_pipe_get_config(s);
  gint fd;
  gboolean file_opened, open_deferred = FALSE;

  file_opened = _sd_open_file(self, self->filename->str, &fd);
  if (!file_opened && self->file_reader_options->follow_freq > 0)
    {
      msg_info("Follow-mode file source not found, deferring open", evt_tag_str("filename", self->filename->str));
      open_deferred = TRUE;
      fd = -1;
    }

  if (file_opened || open_deferred)
    {
      LogProtoServer *proto;
      PollEvents *poll_events;
      gboolean check_immediately;

      poll_events = file_reader_construct_poll_events(self, fd);
      if (!poll_events)
        {
          close(fd);
          return FALSE;
        }
      proto = file_reader_construct_proto(self, fd);

      check_immediately = _is_immediate_check_needed(file_opened, open_deferred);
      _setup_logreader(s, poll_events, proto, check_immediately);
      if (!log_pipe_init((LogPipe *) self->reader))
        {
          msg_error("Error initializing log_reader, closing fd", evt_tag_int("fd", fd));
          log_pipe_unref((LogPipe *) self->reader);
          self->reader = NULL;
          close(fd);
          return FALSE;
        }
      if (recover_state)
        file_reader_recover_state(s, cfg, proto);
    }
  else
    {
      msg_error("Error opening file for reading",
                evt_tag_str("filename", self->filename->str),
                evt_tag_errno(EVT_TAG_OSERROR, errno));
      return self->owner->super.optional;
    }
  return TRUE;

}

static void
file_reader_reopen_on_notify(LogPipe *s, gboolean recover_state)
{
  FileReader *self = (FileReader *) s;

  _deinit_sd_logreader(self);
  file_reader_open_file(s, recover_state);
}

/* NOTE: runs in the main thread */
static void
file_reader_notify(LogPipe *s, gint notify_code, gpointer user_data)
{
  FileReader *self = (FileReader *) s;

  switch (notify_code)
    {
    case NC_FILE_MOVED:
    {
      msg_verbose("Follow-mode file source moved, tracking of the new file is started",
                  evt_tag_str("filename", self->filename->str));
      file_reader_reopen_on_notify(s, TRUE);
      break;
    }
    case NC_READ_ERROR:
    {
      msg_verbose("Error while following source file, reopening in the hope it would work",
                  evt_tag_str("filename", self->filename->str));
      file_reader_reopen_on_notify(s, FALSE);
      break;
    }
    default:
      break;
    }
}

static void
affile_sd_queue(LogPipe *s, LogMessage *msg, const LogPathOptions *path_options, gpointer user_data)
{
  log_src_driver_queue_method(s, msg, path_options, user_data);
}

static void
file_reader_queue(LogPipe *s, LogMessage *msg, const LogPathOptions *path_options, gpointer user_data)
{
  FileReader *self = (FileReader *)s;
  static NVHandle filename_handle = 0;

  if (!filename_handle)
    filename_handle = log_msg_get_value_handle("FILE_NAME");

  log_msg_set_value(msg, filename_handle, self->filename->str, self->filename->len);
  log_pipe_forward_msg(s, msg, path_options);
}

static gboolean
_are_multi_line_settings_invalid(AFFileSourceDriver *self)
{
  gboolean is_garbage_mode = self->file_reader_options.multi_line_mode == MLM_PREFIX_GARBAGE;
  gboolean is_suffix_mode = self->file_reader_options.multi_line_mode == MLM_PREFIX_SUFFIX;

  return (!is_garbage_mode && !is_suffix_mode) && (self->file_reader_options.multi_line_prefix
         || self->file_reader_options.multi_line_garbage);
}

static gboolean
file_reader_init(LogPipe *s)
{
  return file_reader_open_file(s, TRUE);
}

static gboolean
affile_sd_init(LogPipe *s)
{
  AFFileSourceDriver *self = (AFFileSourceDriver *) s;
  GlobalConfig *cfg = log_pipe_get_config(s);

  if (!log_src_driver_init_method(s))
    return FALSE;

  log_reader_options_init(&self->file_reader_options.reader_options, cfg, self->super.super.group);

  if (_are_multi_line_settings_invalid(self))
    {
      msg_error("multi-line-prefix() and/or multi-line-garbage() specified but multi-line-mode() is not regexp based "
                "(prefix-garbage or prefix-suffix), please set multi-line-mode() properly");
      return FALSE;
    }

  log_pipe_append(&self->file_reader->super, &self->super.super.super);
  return log_pipe_init(&self->file_reader->super);
}

static gboolean
file_reader_deinit(LogPipe *s)
{
  FileReader *self = (FileReader *)s;
  if (self->reader)
    _deinit_sd_logreader(self);
  return TRUE;
}

static gboolean
affile_sd_deinit(LogPipe *s)
{
  AFFileSourceDriver *self = (AFFileSourceDriver *) s;

  log_pipe_deinit(&self->file_reader->super);
  if (!log_src_driver_deinit_method(s))
    return FALSE;

  return TRUE;
}

static void
file_reader_free(LogPipe *s)
{
  FileReader *self = (FileReader *)s;
  g_string_free(self->filename, TRUE);
  g_assert(!self->reader);
}
static void
affile_sd_free(LogPipe *s)
{
  AFFileSourceDriver *self = (AFFileSourceDriver *) s;

  log_pipe_unref(&self->file_reader->super);
  g_string_free(self->filename, TRUE);
  log_reader_options_destroy(&self->file_reader_options.reader_options);

  multi_line_regexp_free(self->file_reader_options.multi_line_prefix);
  multi_line_regexp_free(self->file_reader_options.multi_line_garbage);

  log_src_driver_free(s);
}

static FileReader *
file_reader_new(gchar *filename, LogSrcDriver *owner, GlobalConfig *cfg)
{
  FileReader *self = g_new0(FileReader, 1);
  log_pipe_init_instance(&self->super, cfg);
  self->owner = owner;
  self->filename = g_string_new(filename);
  self->super.init = file_reader_init;
  self->super.queue = file_reader_queue;
  self->super.deinit = file_reader_deinit;
  self->super.notify = file_reader_notify;
  self->super.free_fn = file_reader_free;
  self->super.generate_persist_name = file_reader_format_persist_name;
  return self;
}

static AFFileSourceDriver *
affile_sd_new_instance(gchar *filename, GlobalConfig *cfg)
{
  AFFileSourceDriver *self = g_new0(AFFileSourceDriver, 1);

  log_src_driver_init_instance(&self->super, cfg);
  self->filename = g_string_new(filename);
  self->file_reader = file_reader_new(filename, &self->super, cfg);
  self->file_reader->file_reader_options = &self->file_reader_options;
  self->super.super.super.init = affile_sd_init;
  self->super.super.super.queue = affile_sd_queue;
  self->super.super.super.deinit = affile_sd_deinit;
  self->super.super.super.free_fn = affile_sd_free;
  self->super.super.super.generate_persist_name = affile_sd_format_persist_name;
  log_reader_options_defaults(&self->file_reader_options.reader_options);
  file_perm_options_defaults(&self->file_reader_options.file_perm_options);
  self->file_reader_options.reader_options.parse_options.flags |= LP_LOCAL;

  if (affile_is_linux_proc_kmsg(filename))
    self->file_reader_options.file_open_options.needs_privileges = TRUE;
  return self;
}

LogDriver *
affile_sd_new(gchar *filename, GlobalConfig *cfg)
{
  AFFileSourceDriver *self = affile_sd_new_instance(filename, cfg);

  self->file_reader_options.file_open_options.is_pipe = FALSE;
  self->file_reader_options.file_open_options.open_flags = DEFAULT_SD_OPEN_FLAGS;

  if (cfg_is_config_version_older(cfg, 0x0300))
    {
      msg_warning_once("WARNING: file source: default value of follow_freq in file sources has changed in " VERSION_3_0
                       " to '1' for all files except /proc/kmsg");
      self->file_reader_options.follow_freq = -1;
    }
  else
    {
      if (affile_is_device_node(filename) || affile_is_linux_proc_kmsg(filename))
        self->file_reader_options.follow_freq = 0;
      else
        self->file_reader_options.follow_freq = 1000;
    }

  return &self->super.super;
}

LogDriver *
afpipe_sd_new(gchar *filename, GlobalConfig *cfg)
{
  AFFileSourceDriver *self = affile_sd_new_instance(filename, cfg);

  self->file_reader_options.file_open_options.is_pipe = TRUE;
  self->file_reader_options.file_open_options.open_flags = DEFAULT_SD_OPEN_FLAGS_PIPE;

  if (cfg_is_config_version_older(cfg, 0x0302))
    {
      msg_warning_once("WARNING: the expected message format is being changed for pipe() to improve "
                       "syslogd compatibity with " VERSION_3_2 ". If you are using custom "
                       "applications which bypass the syslog() API, you might "
                       "need the 'expect-hostname' flag to get the old behaviour back");
    }
  else
    {
      self->file_reader_options.reader_options.parse_options.flags &= ~LP_EXPECT_HOSTNAME;
    }

  return &self->super.super;
}
