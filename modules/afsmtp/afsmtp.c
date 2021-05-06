/*
 * Copyright (c) 2011-2014 Balabit
 * Copyright (c) 2011-2014 Gergely Nagy <algernon@balabit.hu>
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

#include "afsmtp.h"
#include "afsmtp-parser.h"
#include "plugin.h"
#include "messages.h"
#include "stats/stats-registry.h"
#include "logqueue.h"
#include "plugin-types.h"
#include "logthrdest/logthrdestdrv.h"

#include <libesmtp.h>
#include <signal.h>

typedef struct
{
  gchar *name;
  LogTemplate *template;
} AFSMTPHeader;

typedef struct
{
  gchar *phrase;
  LogTemplate *template;
  afsmtp_rcpt_type_t type;
} AFSMTPRecipient;

typedef struct
{
  LogThreadedDestDriver super;

  /* Shared between main/writer; only read by the writer, never
     written */
  gchar *host;
  gint port;

  AFSMTPRecipient *mail_from;
  GList *rcpt_tos;
  GList *headers;

  LogTemplate *subject_template;
  LogTemplate *body_template;

  /* Writer-only stuff */
  GString *str;
  LogTemplateOptions template_options;
} AFSMTPDriver;

typedef struct
{
  gboolean success;
  AFSMTPDriver *driver;
} LogRcptStatusData;

static gchar *
afsmtp_wash_string (gchar *str)
{
  gint i;

  for (i = 0; i < strlen (str); i++)
    if (str[i] == '\n' ||
        str[i] == '\r')
      str[i] = ' ';

  return str;
}

LogTemplateOptions *
afsmtp_dd_get_template_options(LogDriver *d)
{
  AFSMTPDriver *self = (AFSMTPDriver *)d;
  return &self->template_options;
}

/*
 * Configuration
 */

void
afsmtp_dd_set_host(LogDriver *d, const gchar *host)
{
  AFSMTPDriver *self = (AFSMTPDriver *)d;

  g_free(self->host);
  self->host = g_strdup (host);
}

void
afsmtp_dd_set_port(LogDriver *d, gint port)
{
  AFSMTPDriver *self = (AFSMTPDriver *)d;

  self->port = (int)port;
}

void
afsmtp_dd_set_subject(LogDriver *d, LogTemplate *subject)
{
  AFSMTPDriver *self = (AFSMTPDriver *)d;

  log_template_unref(self->subject_template);
  self->subject_template = log_template_ref(subject);
}

void
afsmtp_dd_set_from(LogDriver *d, LogTemplate *phrase, LogTemplate *mbox)
{
  AFSMTPDriver *self = (AFSMTPDriver *)d;

  g_free(self->mail_from->phrase);
  self->mail_from->phrase = afsmtp_wash_string(g_strdup(phrase->template));
  log_template_unref(self->mail_from->template);
  self->mail_from->template = log_template_ref(mbox);
}

void
afsmtp_dd_add_rcpt(LogDriver *d, afsmtp_rcpt_type_t type, LogTemplate *phrase,
                   LogTemplate *mbox)
{
  AFSMTPDriver *self = (AFSMTPDriver *)d;
  AFSMTPRecipient *rcpt;

  rcpt = g_new0(AFSMTPRecipient, 1);
  rcpt->phrase = afsmtp_wash_string(g_strdup(phrase->template));
  log_template_unref(rcpt->template);
  rcpt->template = log_template_ref(mbox);
  rcpt->type = type;

  self->rcpt_tos = g_list_append(self->rcpt_tos, rcpt);
}

void
afsmtp_dd_set_body(LogDriver *d, LogTemplate *body)
{
  AFSMTPDriver *self = (AFSMTPDriver *)d;

  log_template_unref(self->body_template);
  self->body_template = log_template_ref(body);
}

gboolean
afsmtp_dd_add_header(LogDriver *d, const gchar *header, LogTemplate *value)
{
  AFSMTPDriver *self = (AFSMTPDriver *)d;
  AFSMTPHeader *h;

  if (!g_ascii_strcasecmp(header, "to") ||
      !g_ascii_strcasecmp(header, "cc") ||
      !g_ascii_strcasecmp(header, "bcc") ||
      !g_ascii_strcasecmp(header, "from") ||
      !g_ascii_strcasecmp(header, "sender") ||
      !g_ascii_strcasecmp(header, "reply-to") ||
      !g_ascii_strcasecmp(header, "date"))
    return FALSE;

  h = g_new0(AFSMTPHeader, 1);
  h->name = g_strdup(header);
  log_template_unref(h->template);
  h->template = log_template_ref(value);

  self->headers = g_list_append(self->headers, h);

  return TRUE;
}

/*
 * Utilities
 */
void
ignore_sigpipe (void)
{
  struct sigaction sa;

  sa.sa_handler = SIG_IGN;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGPIPE, &sa, NULL);
}

static const gchar *
afsmtp_dd_format_stats_instance(LogThreadedDestDriver *d)
{
  AFSMTPDriver *self = (AFSMTPDriver *)d;
  static gchar persist_name[1024];

  if (d->super.super.super.persist_name)
    g_snprintf(persist_name, sizeof(persist_name), "smtp,%s", d->super.super.super.persist_name);
  else
    g_snprintf(persist_name, sizeof(persist_name), "smtp,%s,%u", self->host, self->port);

  return persist_name;
}

static const gchar *
afsmtp_dd_format_persist_name(const LogPipe *s)
{
  const AFSMTPDriver *self = (const AFSMTPDriver *)s;
  static gchar persist_name[1024];

  if (s->persist_name)
    g_snprintf(persist_name, sizeof(persist_name), "smtp.%s", s->persist_name);
  else
    g_snprintf(persist_name, sizeof(persist_name), "smtp(%s,%u)", self->host, self->port);

  return persist_name;
}

/*
 * Worker thread
 */

static void
_smtp_message_add_recipient_header(smtp_message_t self, AFSMTPRecipient *rcpt, AFSMTPDriver *driver)
{
  const gchar *hdr;
  switch (rcpt->type)
    {
    case AFSMTP_RCPT_TYPE_TO:
      hdr = "To";
      break;
    case AFSMTP_RCPT_TYPE_CC:
      hdr = "Cc";
      break;
    case AFSMTP_RCPT_TYPE_REPLY_TO:
      hdr = "Reply-To";
      break;
    default:
      return;
    }

  smtp_set_header(self, hdr, rcpt->phrase, afsmtp_wash_string (driver->str->str));
  smtp_set_header_option(self, hdr, Hdr_OVERRIDE, 1);
}

static void
_smtp_message_add_recipient_from_template(smtp_message_t self, AFSMTPDriver *driver, LogTemplate *template,
                                          LogMessage *msg)
{
  LogTemplateEvalOptions options = {&driver->template_options, LTZ_SEND,
                                    driver->super.worker.instance.seq_num, NULL
                                   };
  log_template_format(template, msg, &options, driver->str);
  smtp_add_recipient(self, afsmtp_wash_string (driver->str->str));
}

static void
afsmtp_dd_msg_add_recipient(AFSMTPRecipient *rcpt, gpointer user_data)
{
  AFSMTPDriver *self = ((gpointer *)user_data)[0];
  LogMessage *msg = ((gpointer *)user_data)[1];
  smtp_message_t message = ((gpointer *)user_data)[2];

  _smtp_message_add_recipient_from_template(message, self, rcpt->template, msg);
  _smtp_message_add_recipient_header(message, rcpt, self);
}

static void
afsmtp_dd_msg_add_header(AFSMTPHeader *hdr, gpointer user_data)
{
  AFSMTPDriver *self = ((gpointer *)user_data)[0];
  LogMessage *msg = ((gpointer *)user_data)[1];
  smtp_message_t message = ((gpointer *)user_data)[2];

  LogTemplateEvalOptions options = {&self->template_options, LTZ_LOCAL,
                                    self->super.worker.instance.seq_num, NULL
                                   };
  log_template_format(hdr->template, msg, &options, self->str);

  smtp_set_header(message, hdr->name, afsmtp_wash_string (self->str->str), NULL);
  smtp_set_header_option(message, hdr->name, Hdr_OVERRIDE, 1);
}

static void
afsmtp_dd_log_rcpt_status(smtp_recipient_t rcpt, const char *mailbox,
                          gpointer user_data)
{
  const smtp_status_t *status;
  LogRcptStatusData *status_data = (LogRcptStatusData *)user_data;

  status = smtp_recipient_status(rcpt);
  if (status->code != 250)
    {
      status_data->success = FALSE;
      msg_error("SMTP recipient result",
                evt_tag_str("driver", status_data->driver->super.super.super.id),
                evt_tag_str("recipient", mailbox),
                evt_tag_int("code", status->code),
                evt_tag_str("text", status->text));
    }
  else
    {
      msg_debug("SMTP recipient result",
                evt_tag_str("driver", status_data->driver->super.super.super.id),
                evt_tag_str("recipient", mailbox),
                evt_tag_int("code", status->code),
                evt_tag_str("text", status->text));
    }
}

static void
afsmtp_dd_cb_event(smtp_session_t session, int event, AFSMTPDriver *self)
{
  switch (event)
    {
    case SMTP_EV_CONNECT:
      msg_verbose("Connected to SMTP server",
                  evt_tag_str("driver", self->super.super.super.id),
                  evt_tag_str("host", self->host),
                  evt_tag_int("port", self->port));
      break;
    case SMTP_EV_MAILSTATUS:
    case SMTP_EV_RCPTSTATUS:
    case SMTP_EV_MESSAGEDATA:
    case SMTP_EV_MESSAGESENT:
      /* Ignore */
      break;
    case SMTP_EV_DISCONNECT:
      msg_verbose("Disconnected from SMTP server",
                  evt_tag_str("driver", self->super.super.super.id),
                  evt_tag_str("host", self->host),
                  evt_tag_int("port", self->port));
      break;
    default:
      msg_verbose("Unknown SMTP event",
                  evt_tag_str("driver", self->super.super.super.id),
                  evt_tag_int("event_id", event));
      break;
    }
}

static void
afsmtp_dd_cb_monitor(const gchar *buf, gint buflen, gint writing,
                     AFSMTPDriver *self)
{
  switch (writing)
    {
    case SMTP_CB_READING:
      msg_debug ("SMTP Session: SERVER",
                 evt_tag_str("driver", self->super.super.super.id),
                 evt_tag_printf("message", "%.*s", buflen, buf));
      break;
    case SMTP_CB_WRITING:
      msg_debug("SMTP Session: CLIENT",
                evt_tag_str("driver", self->super.super.super.id),
                evt_tag_printf("message", "%.*s", buflen, buf));
      break;
    case SMTP_CB_HEADERS:
      msg_debug("SMTP Session: HEADERS",
                evt_tag_str("driver", self->super.super.super.id),
                evt_tag_printf("data", "%.*s", buflen, buf));
      break;
    }
}

static smtp_session_t
__build_session(AFSMTPDriver *self, LogMessage *msg)
{
  smtp_session_t session;

  session = smtp_create_session();

  g_string_printf(self->str, "%s:%d", self->host, self->port);
  smtp_set_server(session, self->str->str);

  smtp_set_eventcb(session, (smtp_eventcb_t) afsmtp_dd_cb_event, (void *) self);
  smtp_set_monitorcb(session, (smtp_monitorcb_t) afsmtp_dd_cb_monitor, (void *) self, 1);

  return session;
}

static smtp_message_t
__build_message(AFSMTPDriver *self, LogMessage *msg, smtp_session_t session)
{
  smtp_message_t message;
  gpointer args[] = { self, NULL, NULL };

  message = smtp_add_message(session);

  LogTemplateEvalOptions options = {&self->template_options, LTZ_SEND,
                                    self->super.worker.instance.seq_num, NULL
                                   };
  log_template_format(self->mail_from->template, msg, &options, self->str);
  smtp_set_reverse_path(message, afsmtp_wash_string(self->str->str));

  /* Defaults */
  smtp_set_header(message, "To", NULL, NULL);
  smtp_set_header(message, "From", NULL, NULL);

  LogTemplateEvalOptions eval_options = {&self->template_options, LTZ_SEND,
                                         self->super.worker.instance.seq_num, NULL
                                        };
  log_template_format(self->subject_template, msg, &eval_options, self->str);
  smtp_set_header(message, "Subject", afsmtp_wash_string(self->str->str));
  smtp_set_header_option(message, "Subject", Hdr_OVERRIDE, 1);

  /* Add recipients */
  args[1] = msg;
  args[2] = message;
  g_list_foreach(self->rcpt_tos, (GFunc) afsmtp_dd_msg_add_recipient, args);

  /* Add custom header (overrides anything set before, or in the
   body). */

  g_list_foreach(self->headers, (GFunc) afsmtp_dd_msg_add_header, args);

  /* Set the body.
   *
   * We add a header to the body, otherwise libesmtp will not
   * recognise headers, and will append them to the end of the body.
   */
  g_string_assign(self->str, "X-Mailer: syslog-ng " SYSLOG_NG_VERSION "\r\n\r\n");
  log_template_append_format(self->body_template, msg, &eval_options, self->str);
  smtp_set_message_str(message, self->str->str);
  return message;
}

static gboolean
__check_transfer_status(AFSMTPDriver *self, smtp_message_t message)
{
  LogRcptStatusData status_data;
  const smtp_status_t *status = smtp_message_transfer_status(message);

  status_data.success = TRUE;
  status_data.driver = self;

  if (status->code != 250)
    {
      status_data.success = FALSE;
      msg_error("Failed to send message",
                evt_tag_str("driver", self->super.super.super.id),
                evt_tag_int("code", status->code),
                evt_tag_str("text", status->text));
    }
  else
    {
      msg_debug("SMTP result",
                evt_tag_str("driver", self->super.super.super.id),
                evt_tag_int("code", status->code),
                evt_tag_str("text", status->text));
      smtp_enumerate_recipients(message, afsmtp_dd_log_rcpt_status, &status_data);
    }

  return status_data.success;
}

static gboolean
__send_message(AFSMTPDriver *self, smtp_session_t session)
{
  gboolean success = smtp_start_session(session);

  if (!(success))
    {
      gchar error[1024] = {0};

      smtp_strerror(smtp_errno(), error, sizeof (error) - 1);

      msg_error("SMTP server error, suspending",
                evt_tag_str("driver", self->super.super.super.id),
                evt_tag_str("error", error),
                evt_tag_int("time_reopen", self->super.time_reopen));
      success = FALSE;
    }
  return success;
}

static LogThreadedResult
afsmtp_worker_insert(LogThreadedDestDriver *s, LogMessage *msg)
{
  AFSMTPDriver *self = (AFSMTPDriver *)s;

  if (msg->flags & LF_MARK)
    {
      msg_debug("Mark messages are dropped by SMTP destination",
                evt_tag_str("driver", self->super.super.super.id));
      return LTR_SUCCESS;
    }

  smtp_session_t session = __build_session(self, msg);
  smtp_message_t message = __build_message(self, msg, session);

  gboolean message_sent = __send_message(self, session);
  gboolean success = message_sent && __check_transfer_status(self, message);

  smtp_destroy_session(session);

  if (!success)
    {
      if (!message_sent)
        return LTR_NOT_CONNECTED;
      else
        return LTR_ERROR;
    }

  return LTR_SUCCESS;
}

static void
afsmtp_worker_thread_init(LogThreadedDestDriver *d)
{
  AFSMTPDriver *self = (AFSMTPDriver *)d;

  self->str = g_string_sized_new(1024);

  ignore_sigpipe();
}

static void
afsmtp_worker_thread_deinit(LogThreadedDestDriver *d)
{
  AFSMTPDriver *self = (AFSMTPDriver *)d;

  g_string_free(self->str, TRUE);
}

/*
 * Main thread
 */

static gboolean
__check_rcpt_tos(AFSMTPDriver *self)
{
  gboolean result = FALSE;
  GList *l = self->rcpt_tos;

  while (l)
    {
      AFSMTPRecipient *rcpt = (AFSMTPRecipient *)l->data;
      gboolean rcpt_type_accepted = rcpt->type == AFSMTP_RCPT_TYPE_BCC ||
                                    rcpt->type == AFSMTP_RCPT_TYPE_CC  ||
                                    rcpt->type == AFSMTP_RCPT_TYPE_TO;

      if (rcpt->template && rcpt_type_accepted)
        {
          result = TRUE;
          break;
        }
      l = l->next;
    }

  return result;
}

static gboolean
__check_required_options(AFSMTPDriver *self)
{
  if (!self->mail_from->template)
    {
      msg_error("Error: from or sender option is required",
                evt_tag_str("driver", self->super.super.super.id));
      return FALSE;
    }

  if (!__check_rcpt_tos(self))
    {
      msg_error("Error: to or bcc option is required",
                evt_tag_str("driver", self->super.super.super.id));
      return FALSE;
    }

  if (!self->subject_template)
    {
      msg_error("Error: subject is required option",
                evt_tag_str("driver", self->super.super.super.id));
      return FALSE;
    }

  if (!self->body_template)
    {
      msg_error("Error: body is required option",
                evt_tag_str("driver", self->super.super.super.id));
      return FALSE;
    }
  return TRUE;
}

static gboolean
afsmtp_dd_init(LogPipe *s)
{
  AFSMTPDriver *self = (AFSMTPDriver *)s;
  GlobalConfig *cfg = log_pipe_get_config(s);

  msg_verbose("Initializing SMTP destination",
              evt_tag_str("driver", self->super.super.super.id),
              evt_tag_str("host", self->host),
              evt_tag_int("port", self->port));

  if (!__check_required_options(self))
    return FALSE;

  if (!log_threaded_dest_driver_init_method(s, afsmtp_dd_format_persist_name(s)))
    return FALSE;

  log_template_options_init(&self->template_options, cfg);
  return TRUE;
}

static void
afsmtp_dd_free(LogPipe *d)
{
  AFSMTPDriver *self = (AFSMTPDriver *)d;
  GList *l;

  g_free(self->host);
  g_free(self->mail_from->phrase);
  log_template_unref(self->mail_from->template);
  g_free(self->mail_from);
  log_template_unref(self->subject_template);
  log_template_unref(self->body_template);

  l = self->rcpt_tos;
  while (l)
    {
      AFSMTPRecipient *rcpt = (AFSMTPRecipient *)l->data;
      g_free(rcpt->phrase);
      log_template_unref(rcpt->template);
      g_free(rcpt);
      l = g_list_delete_link(l, l);
    }

  l = self->headers;
  while (l)
    {
      AFSMTPHeader *hdr = (AFSMTPHeader *)l->data;

      g_free(hdr->name);
      log_template_unref(hdr->template);
      g_free(hdr);
      l = g_list_delete_link(l, l);
    }
  log_template_options_destroy(&self->template_options);
  log_threaded_dest_driver_free(d);
}

/*
 * Plugin glue.
 */

LogDriver *
afsmtp_dd_new(GlobalConfig *cfg)
{
  AFSMTPDriver *self = g_new0(AFSMTPDriver, 1);

  log_threaded_dest_driver_init_instance(&self->super, cfg);
  self->super.super.super.super.init = afsmtp_dd_init;
  self->super.super.super.super.free_fn = afsmtp_dd_free;
  self->super.super.super.super.generate_persist_name = afsmtp_dd_format_persist_name;

  self->super.worker.thread_init = afsmtp_worker_thread_init;
  self->super.worker.thread_deinit = afsmtp_worker_thread_deinit;
  self->super.worker.insert = afsmtp_worker_insert;

  self->super.format_stats_instance = afsmtp_dd_format_stats_instance;
  self->super.stats_source = stats_register_type("smtp");

  afsmtp_dd_set_host((LogDriver *)self, "127.0.0.1");
  afsmtp_dd_set_port((LogDriver *)self, 25);

  self->mail_from = g_new0(AFSMTPRecipient, 1);

  log_template_options_defaults(&self->template_options);

  return (LogDriver *)self;
}

extern CfgParser afsmtp_dd_parser;

static Plugin afsmtp_plugin =
{
  .type = LL_CONTEXT_DESTINATION,
  .name = "smtp",
  .parser = &afsmtp_parser,
};

gboolean
afsmtp_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, &afsmtp_plugin, 1);
  return TRUE;
}

const ModuleInfo module_info =
{
  .canonical_name = "afsmtp",
  .version = SYSLOG_NG_VERSION,
  .description = "The afsmtp module provides SMTP destination support for syslog-ng.",
  .core_revision = SYSLOG_NG_SOURCE_REVISION,
  .plugins = &afsmtp_plugin,
  .plugins_len = 1,
};
