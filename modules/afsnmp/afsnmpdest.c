/*
 * Copyright (c) 2019 Balabit
 * Copyright (c) 2002-2011 BalaBit IT Ltd, Budapest, Hungary
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

#include <time.h>

/* undef the PACKAGE_... symbols (from net-snmp) in order to avoid redefinition warnings */
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION

#include "syslog-ng.h"
#include "plugin.h"
#include "cfg-parser.h"
#include "plugin-types.h"

#include "messages.h"
#include "misc.h"
#include "stats/stats.h"
#include "logmsg/nvtable.h"
#include "logqueue.h"

#include "afsnmpdest.h"
#include "afsnmp-parser.h"
#include "logthrdest/logthrdestdrv.h"

const gchar *s_v2c = "v2c";
const gchar *s_v3 = "v3";
const gchar *s_sha = "SHA";
const gchar *s_aes = "AES";
const gchar *s_snmp_name = "syslog-ng";

typedef struct _snmp_obj_type_list
{
  const gchar *type;
  const gchar type_code;
} SnmpObjTypeList;

static SnmpObjTypeList snmp_obj_types[] =
{
  {
    .type = "integer",
    .type_code = 'i'
  },
  {
    .type = "timeticks",
    .type_code = 't'
  },
  {
    .type = "octetstring",
    .type_code = 's'
  },
  {
    .type = "counter32",
    .type_code = 'c'
  },
  {
    .type = "ipaddress",
    .type_code = 'a'
  },
  {
    .type = "objectid",
    .type_code = 'o'
  }
};

static gboolean
snmp_dd_find_object_type(const gchar *type, gint *type_index)
{
  gint object_types_count = G_N_ELEMENTS(snmp_obj_types);
  gint index_;

  /* check the type */
  for (index_ = 0; index_ < object_types_count; ++index_)
    if (!strcasecmp(type, snmp_obj_types[index_].type))
      {
        *type_index = index_;
        return TRUE;
      }

  return FALSE;
}

static gchar
snmp_dd_get_object_type_code(gint type_index)
{
  if (type_index >= 0 && type_index < G_N_ELEMENTS(snmp_obj_types))
    return snmp_obj_types[type_index].type_code;
  else
    return '?';
}

void
snmpdest_dd_set_version(LogDriver *d, const gchar *version)
{
  SNMPDestDriver *self = (SNMPDestDriver *)d;
  g_free(self->version);
  self->version = g_strdup(version);
}

void
snmpdest_dd_set_host(LogDriver *d, const gchar *host)
{
  SNMPDestDriver *self = (SNMPDestDriver *)d;
  g_free(self->host);
  self->host = g_strdup(host);
}

void
snmpdest_dd_set_port(LogDriver *d, gint port)
{
  SNMPDestDriver *self = (SNMPDestDriver *)d;

  self->port = (int)port;
}

static gint
snmp_dd_compare_object_ids(gconstpointer a, gconstpointer b)
{
  return strcmp((gchar *) a, (gchar *) b);
}

gboolean
snmpdest_dd_set_snmp_obj(LogDriver *d, GlobalConfig *cfg, const gchar *objectid, const gchar *type, const gchar *value)
{
  SNMPDestDriver *self = (SNMPDestDriver *)d;
  LogTemplate *template = NULL;
  gint code;

  if (snmp_dd_find_object_type(type, &code) == FALSE)
    {
      msg_error("SNMP: invalid oid type",
                evt_tag_str("type", type));
      return FALSE;
    }

  gchar *s_objectid = "objectid";

  /* check the multiple 'objectid' types - only one type='objectid' is allowed */
  if (!strcmp(type, s_objectid) && self->snmp_objs)
    {
      GList *item = g_list_find_custom(self->snmp_objs, (gconstpointer)s_objectid, snmp_dd_compare_object_ids);

      if (item != NULL)
        {
          msg_error("SNMP: multiple Objectid");
          return FALSE;
        }
    }

  /* register the string values */
  self->snmp_objs = g_list_append(self->snmp_objs, g_strdup(objectid));
  self->snmp_objs = g_list_append(self->snmp_objs, g_strdup(type));
  self->snmp_objs = g_list_append(self->snmp_objs, g_strdup(value));

  /* register the type code, therefore we won't need to calculate it for each incoming message */
  gint *pcode = g_new(gint, 1);
  *pcode = code;

  self->snmp_codes = g_list_append(self->snmp_codes, pcode);

  /* register the template */
  template = log_template_new(cfg, NULL);
  if (log_template_compile(template, value, NULL) == FALSE)
    {
      msg_error("SNMP: invalid log template");
      log_template_unref(template);
      return FALSE;
    }

  self->snmp_templates = g_list_append(self->snmp_templates, template);

  return TRUE;
}

void
snmpdest_dd_set_trap_obj(LogDriver *d, GlobalConfig *cfg, const gchar *objectid, const gchar *type, const gchar *value)
{
  SNMPDestDriver *self = (SNMPDestDriver *)d;

  g_free(self->trap_oid);
  g_free(self->trap_type);
  g_free(self->trap_value);

  self->trap_oid = g_strdup(objectid);
  self->trap_type = g_strdup(type);
  self->trap_value = g_strdup(value);

  /* register the snmp trap as a simple snmp_obj */
  snmpdest_dd_set_snmp_obj(&self->super.super.super, cfg, self->trap_oid, self->trap_type, self->trap_value);
}

void snmpdest_dd_set_community(LogDriver *d, const gchar *community)
{
  SNMPDestDriver *self = (SNMPDestDriver *)d;

  g_free(self->community);
  self->community = g_strdup(community);
}

static gboolean
_has_hexadecimal_prefix(const gchar *s)
{
  return (s[0] == '0') && (s[1] == 'x');
}

static gboolean
_is_hexadecimal(const gchar *s, gint length)
{
  gint i;
  for (i = 0; i < length; ++i)
    {
      if (!g_ascii_isxdigit(s[i]))
        {
          return FALSE;
        }
    }
  return TRUE;
}

gboolean snmpdest_dd_set_engine_id(LogDriver *d, const gchar *eid)
{
  SNMPDestDriver *self = (SNMPDestDriver *)d;
  gsize eidlength = strlen(eid);

  if (eidlength < ENGINE_ID_MIN_LENGTH)
    return FALSE;

  if (_has_hexadecimal_prefix(eid))
    {
      eidlength = eidlength -2;
      eid = eid + 2;
    }

  if (eidlength < ENGINE_ID_MIN_LENGTH || eidlength > ENGINE_ID_MAX_LENGTH)
    return FALSE;

  if (!_is_hexadecimal(eid, eidlength))
    return FALSE;

  g_free(self->engine_id);
  self->engine_id = g_strdup(eid);
  return TRUE;
}

void snmpdest_dd_set_auth_username(LogDriver *d, const gchar *auth_username)
{
  SNMPDestDriver *self = (SNMPDestDriver *)d;

  g_free(self->auth_username);
  self->auth_username = g_strdup(auth_username);
}

void snmpdest_dd_set_auth_algorithm(LogDriver *d, const gchar *auth_algo)
{
  SNMPDestDriver *self = (SNMPDestDriver *)d;

  g_free(self->auth_algorithm);
  self->auth_algorithm = g_strdup(auth_algo);
}

void snmpdest_dd_set_auth_password(LogDriver *d, const gchar *auth_pwd)
{
  SNMPDestDriver *self = (SNMPDestDriver *)d;

  g_free(self->auth_password);
  self->auth_password = g_strdup(auth_pwd);
}

void snmpdest_dd_set_enc_algorithm(LogDriver *d, const gchar *enc_algo)
{
  SNMPDestDriver *self = (SNMPDestDriver *)d;

  g_free(self->enc_algorithm);
  self->enc_algorithm = g_strdup(enc_algo);
}

void snmpdest_dd_set_enc_password(LogDriver *d, const gchar *epwd)
{
  SNMPDestDriver *self = (SNMPDestDriver *)d;

  g_free(self->enc_password);
  self->enc_password = g_strdup(epwd);
}

void snmpdest_dd_set_transport(LogDriver *d, const gchar *transport)
{
  SNMPDestDriver *self = (SNMPDestDriver *)d;

  g_free(self->transport);
  self->transport = g_strdup(transport);
}

void snmpdest_dd_set_time_zone(LogDriver *s, const gchar *time_zone)
{
  SNMPDestDriver *self = (SNMPDestDriver *) s;

  g_free(self->template_options.time_zone[LTZ_LOCAL]);
  self->template_options.time_zone[LTZ_LOCAL] = g_strdup(time_zone);
}

int snmp_input(int operation, netsnmp_session *session, int reqid, netsnmp_pdu *pdu, void *magic)
{
  return 1;
}

/* dummy proc for snmp_parse_args */
static void
optProc(int argc, char *const *argv, int opt)
{
}

static void
free_session(netsnmp_session *s)
{
  free(s->securityEngineID);
  free(s->securityName);
}

static guint
parse_oid_tokens(gchar *oid_str, oid *parsed_oids, guint max_oid_number)
{
  if (oid_str[0] == '.')
    {
      /* skip the leading '.' in order not to parse an empty first token */
      ++oid_str;
    }

  /* parse the oid string into oid values */
  gchar **oid_tokens = g_strsplit(oid_str, ".", max_oid_number);
  guint oid_cnt = 0;
  while (oid_tokens[oid_cnt])
    {
      gint val;
      if (sscanf(oid_tokens[oid_cnt], "%d", &val) != 1 )
        {
          msg_warning("SNMP: invalid OID token",
                      evt_tag_str("value", oid_tokens[oid_cnt]));
        }
      parsed_oids[oid_cnt] = val;
      oid_cnt += 1;
    }

  g_strfreev(oid_tokens);
  return oid_cnt;
}

static void
sanitize_fs(GString *fs, gint code)
{
  /* check the number format and replace it with zero if necessary */
  if (code == 0 || code == 1 || code == 3)
    {
      gboolean replace = FALSE;
      gint i,
           len = fs->len;
      if (!len)
        replace = TRUE;
      else
        for (i = 0; i < len && !replace; ++i)
          if (fs->str[i] < '0' || fs->str[i] > '9')
            replace = TRUE;
      if (replace)
        {
          msg_warning("SNMP: invalid number replaced with '0'",
                      evt_tag_str("value", fs->str));
          g_string_assign(fs, "0");
        }
    }
}

#define MAX_OIDS 128

static LogThreadedResult
snmpdest_worker_insert(LogThreadedDestDriver *s, LogMessage *msg)
{
  SNMPDestDriver *self = (SNMPDestDriver *) s;
  netsnmp_pdu *pdu;

  pdu = snmp_pdu_create(SNMP_MSG_TRAP2);
  if (!pdu)
    return LTR_ERROR;

  oid parsed_oids[MAX_OIDS];
  guint oid_cnt;
  GList *snmp_obj = self->snmp_objs;
  GList *snmp_code = self->snmp_codes;
  GList *snmp_template = self->snmp_templates;
  GString *fs = g_string_sized_new(128);

  LogTemplateEvalOptions options = {&self->template_options, LTZ_LOCAL, 0, NULL};
  /* go through the snmp objects and add them to the message */
  while (snmp_obj)
    {
      oid_cnt = parse_oid_tokens(snmp_obj->data, parsed_oids, MAX_OIDS);

      log_template_format(snmp_template->data, msg, &options, fs);

      gint code = *((gint *)snmp_code->data);
      sanitize_fs(fs, code);

      gchar type_code = snmp_dd_get_object_type_code(code);
      if (snmp_add_var(pdu, parsed_oids, oid_cnt, type_code, fs->str) != 0)
        {
          msg_warning("SNMP: error adding variable",
                      evt_tag_str("value", fs->str));
          log_msg_unref(msg);
          g_string_free(fs, TRUE);
          snmp_free_pdu(pdu);
          return LTR_ERROR;
        }

      snmp_obj = snmp_obj->next->next->next; /* go to the next triplet and the corresponding items */
      snmp_code = snmp_code->next;
      snmp_template = snmp_template->next;
    }
  g_string_free(fs, TRUE);

  gint success = snmp_send(self->ss, pdu);
  if (success == 0)
    {
      msg_error("SNMP: send error",
                evt_tag_int("code", snmp_errno),
                evt_tag_str("message", snmp_api_errstring(snmp_errno)));
      snmp_free_pdu(pdu);
      return LTR_NOT_CONNECTED;
    }

  return LTR_SUCCESS;
}

static const gchar *
snmpdest_dd_format_persist_name(const LogPipe *s)
{
  SNMPDestDriver *self = (SNMPDestDriver *) s;
  static gchar persist_name[1024];

  g_snprintf(persist_name, sizeof(persist_name), "snmpdest(%s,%u)", self->host, self->port);
  return persist_name;
}

static const gchar *
snmpdest_dd_format_stats_instance(LogThreadedDestDriver *s)
{
  SNMPDestDriver *self = (SNMPDestDriver *)s;

  static gchar persist_name[1024];

  g_snprintf(persist_name, sizeof(persist_name), "snmpdest,%s,%u", self->host, self->port);
  return persist_name;
}

static gboolean
snmpdest_dd_session_init(SNMPDestDriver *self)
{
  /* SNMP session setup */
  memset(&self->session, 0, sizeof(self->session));

  putenv("POSIXLY_CORRECT=1");
  gchar *args[24];
  gint argc = 0;
  gint i;

  /* add a new 'cmdline' argument to the array */
#define ADD_ARG(str) args[argc++] = str ? g_strdup(str) : g_strdup("")

  ADD_ARG("snmptrap");
  ADD_ARG("-v");
  if (!strcasecmp(self->version, s_v2c))
    {
      /* v2c parameters */
      ADD_ARG("2c");
      ADD_ARG("-c");
      ADD_ARG(self->community);
    }
  else
    {
      /* v3 parameters */
      ADD_ARG("3");
      ADD_ARG("-e");
      ADD_ARG(self->engine_id);
      if (self->auth_username || self->auth_password)
        {
          ADD_ARG("-u");
          ADD_ARG(self->auth_username);
          if (!self->auth_password)
            {
              ADD_ARG("-l");
              ADD_ARG("noAuthNoPriv");
            }
          else
            {
              ADD_ARG("-a");
              ADD_ARG(self->auth_algorithm);
              ADD_ARG("-A");
              ADD_ARG(self->auth_password);
              ADD_ARG("-l");
              if (self->enc_password)
                {
                  ADD_ARG("authPriv");
                  ADD_ARG("-x");
                  ADD_ARG(self->enc_algorithm);
                  ADD_ARG("-X");
                  ADD_ARG(self->enc_password);
                }
              else
                ADD_ARG("authNoPriv");
            }
        }
    }
  ADD_ARG("localhost");
  ADD_ARG("42");
  ADD_ARG("coldStart.0");
  gint arg = snmp_parse_args(argc, args, &self->session, "C:", optProc);

  if (arg == NETSNMP_PARSE_ARGS_ERROR_USAGE || arg == NETSNMP_PARSE_ARGS_SUCCESS_EXIT)
    {
      goto error;
    }
  /* set our own address - the parse_args uses 'localhost' */
  self->session.peername = self->host_port->str;

  self->session.callback = snmp_input;
  self->session.callback_magic = NULL;

  /* this block is copied from the snmptrap.c - necessary? */
  if (self->session.version == SNMP_VERSION_3)
    {
      /*
       * for traps, we use ourselves as the authoritative engine
       * which is really stupid since command line apps don't have a
       * notion of a persistent engine.  Hence, our boots and time
       * values are probably always really wacked with respect to what
       * a manager would like to see.
       *
       * The following should be enough to:
       *
       * 1) prevent the library from doing discovery for engineid & time.
       * 2) use our engineid instead of the remote engineid for
       * authoritative & privacy related operations.
       * 3) The remote engine must be configured with users for our engineID.
       *
       * -- Wes
       */

      /*
       * setup the engineID based on IP addr.  Need a different
       * algorithm here.  This will cause problems with agents on the
       * same machine sending traps.
       */
      setup_engineID(NULL, NULL);

      /*
       * pick our own engineID
       */
      if (self->session.securityEngineIDLen == 0 ||
          self->session.securityEngineID == NULL)
        {
          self->session.securityEngineID =
            snmpv3_generate_engineID(&self->session.securityEngineIDLen);
        }
      if (self->session.contextEngineIDLen == 0 ||
          self->session.contextEngineID == NULL)
        {
          self->session.contextEngineID =
            snmpv3_generate_engineID(&self->session.contextEngineIDLen);
        }

      /*
       * set boots and time, which will cause problems if this
       * machine ever reboots and a remote trap receiver has cached our
       * boots and time...  I'll cause a not-in-time-window report to
       * be sent back to this machine.
       */
      if (self->session.engineBoots == 0)
        self->session.engineBoots = 1;
      if (self->session.engineTime == 0)    /* not really correct, */
        self->session.engineTime = get_uptime();  /* but it'll work. Sort of. */
    }

  self->ss = snmp_add(&self->session,
                      netsnmp_transport_open_client("snmptrap", self->session.peername),
                      NULL, NULL);
  if (self->ss == NULL)
    {
      goto error;
    }

  self->session_initialized = TRUE;
  return TRUE;

error:
  for (i = 0; i < argc; ++i)
    g_free(args[i]);
  free_session(&self->session);
  return FALSE;

}

static gboolean
snmpdest_dd_init(LogPipe *s)
{
  SNMPDestDriver *self = (SNMPDestDriver *)s;
  GlobalConfig *cfg = log_pipe_get_config(s);

  msg_verbose("Initializing SNMP destination",
              evt_tag_str("driver", self->super.super.super.id),
              evt_tag_str("host", self->host),
              evt_tag_int("port", self->port));

  gchar err_msg[128];
  if (!snmpdest_check_required_params(&self->super.super.super, err_msg))
    {
      msg_error(err_msg);
      return FALSE;
    }

  if (!log_threaded_dest_driver_init_method(s, snmpdest_dd_format_persist_name(s)))
    return FALSE;

  log_template_options_init(&self->template_options, cfg);
  return TRUE;
}

static void
snmpdest_worker_thread_deinit(LogThreadedDestDriver *d)
{
  SNMPDestDriver *self = (SNMPDestDriver *)d;

  /* SNMP deinit */
  if (self->session_initialized)
    {
      snmp_close(self->ss);
      free_session(&self->session);
    }
}

static gint snmp_dest_counter = 0;

static void
snmpdest_dd_free(LogPipe *d)
{
  SNMPDestDriver *self = (SNMPDestDriver *)d;

  if (snmp_dest_counter == 1)
    {
      /* this is the last snmp driver, close the snmp */
      SOCK_CLEANUP;
      snmp_shutdown(s_snmp_name);
    }
  --snmp_dest_counter;

  g_free(self->version);
  g_free(self->host);
  if (self->host_port)
    g_string_free(self->host_port, TRUE);

  g_list_free_full(self->snmp_objs, g_free);
  g_list_free_full(self->snmp_codes, g_free);
  g_list_free_full(self->snmp_templates, (GDestroyNotify)log_template_unref);
  g_free(self->trap_oid);
  g_free(self->trap_type);
  g_free(self->trap_value);
  g_free(self->community);
  g_free(self->engine_id);
  g_free(self->auth_username);
  g_free(self->auth_algorithm);
  g_free(self->auth_password);
  g_free(self->enc_algorithm);
  g_free(self->enc_password);
  g_free(self->transport);

  log_template_options_destroy(&self->template_options);

  log_threaded_dest_driver_free(d);
}

static void
snmpdest_worker_thread_init(LogThreadedDestDriver *d)
{
  SNMPDestDriver *self = (SNMPDestDriver *)d;

  GlobalConfig *cfg = log_pipe_get_config((LogPipe *)d);

  if (!self->host_port)
    {
      /* prepare the host:port format - the session.remote_port doesn't work */
      self->host_port = g_string_sized_new(64);

      if (self->transport)
        g_string_append_printf(self->host_port, "%s:", self->transport);
      g_string_append_printf(self->host_port, "%s:%d", self->host, self->port);
    }

  gchar *tz = self->template_options.time_zone[LTZ_LOCAL];
  gchar *time_zone = cfg->template_options.time_zone[LTZ_SEND];

  /* inherit the global set_time_zone if the local one hasn't been set */
  if (!tz && time_zone)
    snmpdest_dd_set_time_zone((LogDriver *)self, time_zone);

  log_template_options_init(&self->template_options, cfg);

  snmpdest_dd_session_init(self);
}

LogDriver *
snmpdest_dd_new(GlobalConfig *cfg)
{
  SNMPDestDriver *self = g_new0(SNMPDestDriver, 1);

  log_threaded_dest_driver_init_instance(&self->super, cfg);
  self->super.super.super.super.init = snmpdest_dd_init;
  self->super.super.super.super.free_fn = snmpdest_dd_free;
  self->super.super.super.super.generate_persist_name = snmpdest_dd_format_persist_name;

  self->super.worker.thread_init = snmpdest_worker_thread_init;
  self->super.worker.thread_deinit = snmpdest_worker_thread_deinit;
  self->super.worker.insert = snmpdest_worker_insert;

  self->super.format_stats_instance = snmpdest_dd_format_stats_instance;
  self->super.stats_source = stats_register_type("snmp");

  if (snmp_dest_counter == 0)
    {
      /* initialize the SNMP only once */
      init_snmp(s_snmp_name);
      SOCK_STARTUP;
    }
  ++snmp_dest_counter;

  /* default values */
  self->version = g_strdup(s_v2c);
  self->port = 162;
  self->community = g_strdup("public");
  self->auth_algorithm = g_strdup(s_sha);
  self->enc_algorithm = g_strdup(s_aes);
  self->transport = g_strdup("UDP");

  log_template_options_defaults(&self->template_options);
  self->worker_options.is_output_thread = TRUE;

  return (LogDriver *)self;
}

gboolean snmpdest_check_required_params(LogDriver *d, gchar *err_msg)
{
  SNMPDestDriver *self = (SNMPDestDriver *)d;

  /* required parameters */
  if (!self->snmp_objs)
    {
      sprintf(err_msg, "missing SNMP object");
      return FALSE;
    }

  if (!self->host)
    {
      sprintf(err_msg, "missing host");
      return FALSE;
    }

  if (!self->trap_oid || !self->trap_type || !self->trap_value)
    {
      sprintf(err_msg, "missing trap_obj");
      return FALSE;
    }

  if (strcasecmp(self->trap_type, "objectid"))
    {
      sprintf(err_msg, "wrong trap object type: %s", self->trap_type);
      return FALSE;
    }

  /* v3-specific parameters */
  if (!strcmp(self->version, s_v3) && !self->engine_id)
    {
      sprintf(err_msg, "missing engine id");
      return FALSE;
    }

  return TRUE;
}

gchar *snmpdest_dd_get_version(LogDriver *d)
{
  SNMPDestDriver *self = (SNMPDestDriver *)d;

  return self->version;
}

gboolean snmpdest_dd_check_auth_algorithm(gchar *algo)
{
  if (strcasecmp(algo, s_sha) == 0)
    return TRUE;

  return FALSE;
}

gboolean snmpdest_dd_check_enc_algorithm(gchar *algo)
{
  if (strcasecmp(algo, s_aes) == 0)
    return TRUE;

  return FALSE;
}
