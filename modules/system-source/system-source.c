/*
 * Copyright (c) 2012-2014 Balabit
 * Copyright (c) 2012-2014 Gergely Nagy <algernon@balabit.hu>
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

#include "cfg.h"
#include "cfg-block-generator.h"
#include "block-ref-parser.h"
#include "messages.h"
#include "plugin.h"
#include "plugin-types.h"
#include "str-utils.h"

#include <fcntl.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <iv.h>
#include "service-management.h"

#if defined (__FreeBSD__)
#include <sys/sysctl.h>
#include <inttypes.h>
#endif

static void
system_sysblock_add_unix_dgram_driver(GString *sysblock, const gchar *path,
                                      const gchar *perms, const gchar *recvbuf_size)
{
  g_string_append_printf(sysblock, "unix-dgram(\"%s\"", path);
  if (perms)
    g_string_append_printf(sysblock, " perm(%s)", perms);
  if (recvbuf_size)
    g_string_append_printf(sysblock, " so_rcvbuf(%s)", recvbuf_size);
  g_string_append(sysblock, " flags(syslog-protocol)");
  g_string_append(sysblock, ");\n");
}

static void
system_sysblock_add_unix_dgram(GString *sysblock, const gchar *path,
                               const gchar *perms, const gchar *recvbuf_size)
{
  GString *unix_driver = g_string_sized_new(0);

  system_sysblock_add_unix_dgram_driver(unix_driver, path, perms, recvbuf_size);

  g_string_append_printf(sysblock,
                         "channel {\n"
                         "    source { %s };\n"
                         "    rewrite { set(\"${.unix.pid}\" value(\"PID\") condition(\"${.unix.pid}\" ne \"\")); };\n"
                         "};\n", unix_driver->str);

  g_string_free(unix_driver, TRUE);
}

static void
system_sysblock_add_file(GString *sysblock, const gchar *path,
                         gint follow_freq, const gchar *prg_override,
                         const gchar *flags, const gchar *format,
                         gboolean ignore_timestamp)
{
  g_string_append_printf(sysblock, "file(\"%s\"", path);
  if (follow_freq >= 0)
    g_string_append_printf(sysblock, " follow-freq(%d)", follow_freq);
  if (prg_override)
    g_string_append_printf(sysblock, " program-override(\"%s\")", prg_override);
  if (flags)
    g_string_append_printf(sysblock, " flags(%s)", flags);
  if (format)
    g_string_append_printf(sysblock, " format(%s)", format);
  if (ignore_timestamp)
    g_string_append_printf(sysblock, " keep-timestamp(no)");
  g_string_append(sysblock, ");\n");
}

static void
system_sysblock_add_module(GString *sysblock, const gchar *mod)
{
  g_string_append_printf(sysblock, "@module %s\n", mod);
}

static void
system_sysblock_add_sun_streams(GString *sysblock, const gchar *path,
                                const gchar *door)
{
  GString *solaris_driver = g_string_sized_new(0);



  g_string_append_printf(solaris_driver, "sun-streams(\"%s\"", path);
  if (door)
    g_string_append_printf(solaris_driver, " door(\"%s\")", door);
  g_string_append(solaris_driver, ");\n");


  g_string_append_printf(sysblock,
                         "channel {\n"
                         "    source { %s };\n"
                         "    parser { extract-solaris-msgid(); };\n"
                         "};\n", solaris_driver->str);
  g_string_free(solaris_driver, TRUE);
}

static void
system_sysblock_add_pipe(GString *sysblock, const gchar *path, gint pad_size)
{
  g_string_append_printf(sysblock, "pipe(\"%s\"", path);
  if (pad_size >= 0)
    g_string_append_printf(sysblock, " pad_size(%d)", pad_size);
  g_string_append(sysblock, ");\n");
}

#if defined (__FreeBSD__)
static gboolean
system_freebsd_is_jailed(void)
{
  int r;
  int32_t is_jailed;
  size_t len = 4;

  r = sysctlbyname("security.jail.jailed", &is_jailed, &len, NULL, 0);
  if (r != 0)
    return FALSE;
  return !!is_jailed;
}
#else
static gboolean
system_freebsd_is_jailed(void)
{
  return FALSE;
}
#endif

static void
system_sysblock_add_freebsd_klog(GString *sysblock, const gchar *release)
{
  /* /dev/klog on FreeBSD prior to 9.1-RC-something is not
     pollable with kqueue(), so use timers for it instead, by
     forcing follow-freq(1). */
  if (strncmp(release, "7.", 2) == 0 ||
      strncmp(release, "8.", 2) == 0 ||
      strncmp(release, "9.0", 3) == 0)
    system_sysblock_add_file(sysblock, "/dev/klog", 1, "kernel", "no-parse", NULL, FALSE);
  else
    system_sysblock_add_file(sysblock, "/dev/klog", 0, "kernel", NULL, NULL, FALSE);
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

static gboolean
_detect_linux_dev_kmsg(void)
{
  gint fd = open("/dev/kmsg", O_RDONLY);

  if (fd == -1)
    return FALSE;

  gboolean seekable = lseek(fd, 0, SEEK_END) != -1;
  gboolean pollable = _is_fd_pollable(fd);;

  close(fd);
  return seekable && pollable;
}

static gboolean
_detect_linux_proc_kmsg(void)
{
  gint fd = open("/proc/kmsg", O_RDONLY);

  if (fd == -1)
    return FALSE;

  gboolean pollable = _is_fd_pollable(fd);

  close(fd);
  return pollable;
}

static void
system_sysblock_add_systemd_source(GString *sysblock)
{
  g_string_append(sysblock, "systemd-journal();\n");
}

static void
system_sysblock_add_linux_kmsg(GString *sysblock)
{
  const gchar *kmsg = NULL;
  const gchar *format = NULL;

  if (_detect_linux_dev_kmsg())
    {
      kmsg = "/dev/kmsg";
      format = "linux-kmsg";
    }
  else if (_detect_linux_proc_kmsg())
    {
      kmsg = "/proc/kmsg";
      format = NULL;
    }
  else
    {
      msg_notice("system(): Neither of the Linux kernel log devices was detected, continuing without polling either /proc/kmsg or /dev/kmsg");
    }

  if (kmsg)
    {
      msg_debug("system(): Enabling Linux kernel log device",
                evt_tag_str("device", kmsg),
                evt_tag_str("format", format));
      system_sysblock_add_file(sysblock, kmsg, -1,
                               "kernel", "kernel", format, TRUE);
    }
}

static void
system_sysblock_add_linux(GString *sysblock)
{
  if (service_management_get_type() == SMT_SYSTEMD)
    system_sysblock_add_systemd_source(sysblock);
  else
    {
      system_sysblock_add_unix_dgram(sysblock, "/dev/log", NULL, "8192");
      system_sysblock_add_linux_kmsg(sysblock);
    }
}

static gboolean
system_generate_system_transports(GString *sysblock, CfgArgs *args)
{
  struct utsname u;

  if (uname(&u) < 0)
    {
      msg_error("system(): Cannot get information about the running kernel",
                evt_tag_error("error"));
      return FALSE;
    }

  if (strcmp(u.sysname, "Linux") == 0)
    {
      system_sysblock_add_linux(sysblock);
    }
  else if (strcmp(u.sysname, "SunOS") == 0)
    {
      system_sysblock_add_module(sysblock, "afstreams");

      if (strcmp(u.release, "5.8") == 0)
        system_sysblock_add_sun_streams(sysblock, "/dev/log", NULL);
      else if (strcmp(u.release, "5.9") == 0)
        system_sysblock_add_sun_streams(sysblock, "/dev/log", "/etc/.syslog_door");
      else
        system_sysblock_add_sun_streams(sysblock, "/dev/log", "/var/run/syslog_door");
    }
  else if (strcmp(u.sysname, "FreeBSD") == 0)
    {
      system_sysblock_add_unix_dgram(sysblock, "/var/run/log", NULL, NULL);
      system_sysblock_add_unix_dgram(sysblock, "/var/run/logpriv", "0600", NULL);

      if (!system_freebsd_is_jailed())
        system_sysblock_add_freebsd_klog(sysblock, u.release);
    }
  else if (strcmp(u.sysname, "GNU/kFreeBSD") == 0)
    {
      system_sysblock_add_unix_dgram(sysblock, "/var/run/log", NULL, NULL);
      system_sysblock_add_freebsd_klog(sysblock, u.release);
    }
  else if (strcmp(u.sysname, "HP-UX") == 0)
    {
      system_sysblock_add_pipe(sysblock, "/dev/log", 2048);
    }
  else if (strcmp(u.sysname, "AIX") == 0 ||
           strcmp(u.sysname, "OSF1") == 0 ||
           strncmp(u.sysname, "CYGWIN", 6) == 0)
    {
      system_sysblock_add_unix_dgram(sysblock, "/dev/log", NULL, NULL);
    }
  else if (strcmp(u.sysname, "OpenBSD") == 0)
    {
      g_string_append(sysblock, "openbsd();");
    }
  else if (strcmp(u.sysname, "Darwin") == 0)
    {
      system_sysblock_add_file(sysblock, "/var/log/system.log", 1,NULL, NULL, NULL, FALSE);
    }
  else
    {
      msg_error("system(): Error detecting platform, unable to define the system() source. "
                "Please send your system information to the developers!",
                evt_tag_str("sysname", u.sysname),
                evt_tag_str("release", u.release));
      return FALSE;
    }
  g_string_append(sysblock, "\n");
  return TRUE;
}

static void
system_generate_app_parser(GlobalConfig *cfg, GString *sysblock, CfgArgs *args)
{
  gchar *varargs = cfg_args_format_varargs(args, NULL);

  g_string_append_printf(sysblock,
                         "channel {\n"
                         "  channel {\n"
                         "    parser {\n"
                         "      app-parser(topic(syslog) %s);\n"
                         "    };\n"
                         "    flags(final);\n"
                         "  };\n"
                         "  channel { flags(final); };\n"
                         "};\n",
                         varargs);
  g_free(varargs);
}

static gboolean
system_source_generate(CfgBlockGenerator *self, GlobalConfig *cfg, gpointer args, GString *sysblock,
                       const gchar *reference)
{
  gboolean result = FALSE;
  CfgArgs *cfgargs = (CfgArgs *)args;

  /* NOTE: we used to have an exclude-kmsg() option that was removed, still
   * we need to ignore it */

  if (cfgargs)
    cfg_args_remove(cfgargs, "exclude-kmsg");

  g_string_append(sysblock,
                  "channel {\n"
                  "    source {\n");

  if (!system_generate_system_transports(sysblock, cfgargs))
    {
      goto exit;
    }

  g_string_append(sysblock, "    }; # source\n");

  system_generate_app_parser(cfg, sysblock, cfgargs);

  g_string_append(sysblock, "}; # channel\n");
  result = TRUE;
exit:
  return result;
}


CfgBlockGenerator *
system_source_generator_new(gint context, const gchar *name)
{
  CfgBlockGenerator *self = g_new0(CfgBlockGenerator, 1);

  cfg_block_generator_init_instance(self, context, name);
  self->generate = system_source_generate;
  return self;
}

gpointer
system_source_construct(Plugin *p)
{
  return system_source_generator_new(p->type, p->name);
}

Plugin system_plugins[] =
{
  {
    .type = LL_CONTEXT_SOURCE | LL_CONTEXT_FLAG_GENERATOR,
    .name = "system",
    .construct = system_source_construct,
    .parser = &block_ref_parser
  }
};

gboolean
system_source_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, system_plugins, 1);

  return TRUE;
}


const ModuleInfo module_info =
{
  .canonical_name = "system-source",
  .version = SYSLOG_NG_VERSION,
  .description = "The system-source module provides support for determining the system log sources at run time.",
  .core_revision = SYSLOG_NG_SOURCE_REVISION,
  .plugins = system_plugins,
  .plugins_len = G_N_ELEMENTS(system_plugins),
};
