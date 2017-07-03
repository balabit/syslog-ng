/*
 * Copyright (c) 2002-2013 Balabit
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
#include "file-opener.h"
#include "messages.h"
#include "gprocess.h"
#include "fdhelpers.h"
#include "pathutils.h"
#include "cfg.h"
#include "transport/transport-file.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdlib.h>

static const gchar *spurious_paths[] = {"../", "/..", NULL};

static inline gboolean
_string_contains_fragment(const gchar *str, const gchar *fragments[])
{
  int i;

  for (i = 0; fragments[i]; i++)
    {
      if (strstr(str, fragments[i]))
        return TRUE;
    }

  return FALSE;
}

static inline gboolean
_is_path_spurious(const gchar *name)
{
  return _string_contains_fragment(name, spurious_paths);
}

static inline gboolean
_obtain_capabilities(gchar *name, FileOpenerOptions *options, FilePermOptions *perm_opts, cap_t *act_caps)
{
  if (options->needs_privileges)
    {
      g_process_cap_modify(CAP_DAC_READ_SEARCH, TRUE);
      g_process_cap_modify(CAP_SYSLOG, TRUE);
    }
  else
    {
      g_process_cap_modify(CAP_DAC_OVERRIDE, TRUE);
    }

  if (options->create_dirs && perm_opts &&
      !file_perm_options_create_containing_directory(perm_opts, name))
    {
      return FALSE;
    }

  return TRUE;
}

static inline void
_set_fd_permission(FilePermOptions *perm_opts, int fd)
{
  if (fd != -1)
    {
      g_fd_set_cloexec(fd, TRUE);

      g_process_cap_modify(CAP_CHOWN, TRUE);
      g_process_cap_modify(CAP_FOWNER, TRUE);

      if (perm_opts)
        file_perm_options_apply_fd(perm_opts, fd);
    }
}

static inline int
_open_fd(const gchar *name, FileOpenerOptions *options, FilePermOptions *perm_opts)
{
  int fd;
  int mode = (perm_opts && (perm_opts->file_perm >= 0))
             ? perm_opts->file_perm : 0600;

  fd = open(name, options->open_flags, mode);

  if (options->is_pipe && fd < 0 && errno == ENOENT)
    {
      if (mkfifo(name, mode) >= 0)
        fd = open(name, options->open_flags, mode);
    }

  return fd;
}

static inline void
_validate_file_type(const gchar *name, FileOpenerOptions *options)
{
  struct stat st;

  if (stat(name, &st) >= 0)
    {
      if (options->is_pipe && !S_ISFIFO(st.st_mode))
        {
          msg_warning("WARNING: you are using the pipe driver, underlying file is not a FIFO, it should be used by file()",
                      evt_tag_str("filename", name));
        }
      else if (!options->is_pipe && S_ISFIFO(st.st_mode))
        {
          msg_warning("WARNING: you are using the file driver, underlying file is a FIFO, it should be used by pipe()",
                      evt_tag_str("filename", name));
        }
    }
}

static gboolean
affile_open_file(gchar *name, FileOpenerOptions *options, gint *fd)
{
  cap_t saved_caps;

  if (_is_path_spurious(name))
    {
      msg_error("Spurious path, logfile not created",
                evt_tag_str("path", name));
      return FALSE;
    }

  saved_caps = g_process_cap_save();

  if (!_obtain_capabilities(name, options, &options->file_perm_options, &saved_caps))
    {
      g_process_cap_restore(saved_caps);
      return FALSE;
    }

  _validate_file_type(name, options);

  *fd = _open_fd(name, options, &options->file_perm_options);

  if (!is_file_device(name))
    _set_fd_permission(&options->file_perm_options, *fd);

  g_process_cap_restore(saved_caps);

  msg_trace("affile_open_file",
            evt_tag_str("path", name),
            evt_tag_int("fd", *fd));

  return (*fd != -1);
}

gboolean
file_opener_open_fd(FileOpener *self, gchar *name, gint *fd)
{
  return affile_open_file(name, self->options, fd);
}

void
file_opener_set_options(FileOpener *self, FileOpenerOptions *options)
{
  self->options = options;
}

FileOpener *
file_opener_new(void)
{
  FileOpener *self = g_new0(FileOpener, 1);
  return self;
}

void
file_opener_free(FileOpener *self)
{
  g_free(self);
}

void
file_opener_options_defaults(FileOpenerOptions *options)
{
  file_perm_options_defaults(&options->file_perm_options);
  options->create_dirs = -1;
  options->is_pipe = FALSE;
  options->needs_privileges = FALSE;
}

void
file_opener_options_init(FileOpenerOptions *options, GlobalConfig *cfg)
{
  file_perm_options_inherit_from(&options->file_perm_options, &cfg->file_perm_options);
}

void
file_opener_options_deinit(FileOpenerOptions *options)
{
  /* empty, this function only serves to meet the conventions of *Options */
}
