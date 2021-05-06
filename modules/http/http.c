/*
 * Copyright (c) 2016 Marc Falzon
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

#include "http.h"
#include "http-worker.h"

/* HTTPDestinationDriver */
void
http_dd_insert_response_handler(LogDriver *d, HttpResponseHandler *response_handler)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;
  http_response_handlers_insert(self->response_handlers, response_handler);
}

void
http_dd_set_urls(LogDriver *d, GList *url_strings)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  http_load_balancer_drop_all_targets(self->load_balancer);
  for (GList *l = url_strings; l; l = l->next)
    {
      const gchar *url_string = (const gchar *) l->data;
      gchar **urls = g_strsplit(url_string, " ", -1);

      for (gint url = 0; urls[url]; url++)
        {
          http_load_balancer_add_target(self->load_balancer, urls[url]);
        }
      g_strfreev(urls);
    }
}

void
http_dd_set_user(LogDriver *d, const gchar *user)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  g_free(self->user);
  self->user = g_strdup(user);
}

void
http_dd_set_password(LogDriver *d, const gchar *password)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  g_free(self->password);
  self->password = g_strdup(password);
}

void
http_dd_set_user_agent(LogDriver *d, const gchar *user_agent)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  g_free(self->user_agent);
  self->user_agent = g_strdup(user_agent);
}

void
http_dd_set_headers(LogDriver *d, GList *headers)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  g_list_free_full(self->headers, g_free);
  self->headers = g_list_copy_deep(headers, ((GCopyFunc)g_strdup), NULL);
}

void
http_dd_set_method(LogDriver *d, const gchar *method)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  if (g_ascii_strcasecmp(method, "POST") == 0)
    self->method_type = METHOD_TYPE_POST;
  else if (g_ascii_strcasecmp(method, "PUT") == 0)
    self->method_type = METHOD_TYPE_PUT;
  else
    {
      msg_warning("Unsupported method is set(Only POST and PUT are supported), default method POST will be used",
                  evt_tag_str("method", method));
      self->method_type = METHOD_TYPE_POST;
    }
}

void
http_dd_set_body(LogDriver *d, LogTemplate *body)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  log_template_unref(self->body_template);
  self->body_template = log_template_ref(body);
}

void
http_dd_set_delimiter(LogDriver *d, const gchar *delimiter)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  g_string_assign(self->delimiter, delimiter);
}

LogTemplateOptions *
http_dd_get_template_options(LogDriver *d)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  return &self->template_options;
}

void
http_dd_set_ca_dir(LogDriver *d, const gchar *ca_dir)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  g_free(self->ca_dir);
  self->ca_dir = g_strdup(ca_dir);
}

void
http_dd_set_ca_file(LogDriver *d, const gchar *ca_file)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  g_free(self->ca_file);
  self->ca_file = g_strdup(ca_file);
}

void
http_dd_set_cert_file(LogDriver *d, const gchar *cert_file)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  g_free(self->cert_file);
  self->cert_file = g_strdup(cert_file);
}

void
http_dd_set_key_file(LogDriver *d, const gchar *key_file)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  g_free(self->key_file);
  self->key_file = g_strdup(key_file);
}

void
http_dd_set_cipher_suite(LogDriver *d, const gchar *ciphers)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  g_free(self->ciphers);
  self->ciphers = g_strdup(ciphers);
}

void
http_dd_set_proxy(LogDriver *d, const gchar *proxy)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  g_free(self->proxy);
  self->proxy = g_strdup(proxy);
}

gboolean
http_dd_set_ssl_version(LogDriver *d, const gchar *value)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  if (strcmp(value, "default") == 0)
    {
      /*
       * Negotiate the version based on what the remote server supports.
       * SSLv2 is disabled by default as of libcurl 7.18.1.
       * SSLv3 is disabled by default as of libcurl 7.39.0.
       */
      self->ssl_version = CURL_SSLVERSION_DEFAULT;

    }
  else if (strcmp(value, "tlsv1") == 0)
    {
      /* TLS 1.x */
      self->ssl_version = CURL_SSLVERSION_TLSv1;
    }
  else if (strcmp(value, "sslv2") == 0)
    {
      /* SSL 2 only */
      self->ssl_version = CURL_SSLVERSION_SSLv2;

    }
  else if (strcmp(value, "sslv3") == 0)
    {
      /* SSL 3 only */
      self->ssl_version = CURL_SSLVERSION_SSLv3;
    }
#if SYSLOG_NG_HAVE_DECL_CURL_SSLVERSION_TLSV1_0
  else if (strcmp(value, "tlsv1_0") == 0)
    {
      /* TLS 1.0 only */
      self->ssl_version = CURL_SSLVERSION_TLSv1_0;
    }
#endif
#if SYSLOG_NG_HAVE_DECL_CURL_SSLVERSION_TLSV1_1
  else if (strcmp(value, "tlsv1_1") == 0)
    {
      /* TLS 1.1 only */
      self->ssl_version = CURL_SSLVERSION_TLSv1_1;
    }
#endif
#if SYSLOG_NG_HAVE_DECL_CURL_SSLVERSION_TLSV1_2
  else if (strcmp(value, "tlsv1_2") == 0)
    {
      /* TLS 1.2 only */
      self->ssl_version = CURL_SSLVERSION_TLSv1_2;
    }
#endif
#if SYSLOG_NG_HAVE_DECL_CURL_SSLVERSION_TLSV1_3
  else if (strcmp(value, "tlsv1_3") == 0)
    {
      /* TLS 1.3 only */
      self->ssl_version = CURL_SSLVERSION_TLSv1_3;
    }
#endif
  else
    return FALSE;

  return TRUE;
}

void
http_dd_set_peer_verify(LogDriver *d, gboolean verify)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  self->peer_verify = verify;
}

void
http_dd_set_accept_redirects(LogDriver *d, gboolean accept_redirects)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  self->accept_redirects = accept_redirects;
}

void
http_dd_set_timeout(LogDriver *d, glong timeout)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  self->timeout = timeout;
}

void
http_dd_set_batch_bytes(LogDriver *d, glong batch_bytes)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  self->batch_bytes = batch_bytes;
}

void
http_dd_set_body_prefix(LogDriver *d, const gchar *body_prefix)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  g_string_assign(self->body_prefix, body_prefix);
}

void
http_dd_set_body_suffix(LogDriver *d, const gchar *body_suffix)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *) d;

  g_string_assign(self->body_suffix, body_suffix);
}

static const gchar *
_format_persist_name(const LogPipe *s)
{
  const HTTPDestinationDriver *self = (const HTTPDestinationDriver *)s;
  static gchar persist_name[1024];

  if (s->persist_name)
    g_snprintf(persist_name, sizeof(persist_name), "http.%s", s->persist_name);
  else
    g_snprintf(persist_name, sizeof(persist_name), "http(%s,)", self->url);

  return persist_name;
}

static const gchar *
_format_stats_instance(LogThreadedDestDriver *s)
{
  static gchar stats[1024];

  HTTPDestinationDriver *self = (HTTPDestinationDriver *) s;

  g_snprintf(stats, sizeof(stats), "http,%s", self->url);

  return stats;
}

gboolean
http_dd_deinit(LogPipe *s)
{
  return log_threaded_dest_driver_deinit_method(s);
}

gboolean
http_dd_init(LogPipe *s)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *)s;
  GlobalConfig *cfg = log_pipe_get_config(s);

  if (self->load_balancer->num_targets == 0)
    http_load_balancer_add_target(self->load_balancer, HTTP_DEFAULT_URL);

  if (self->load_balancer->num_targets > 1 && s->persist_name == NULL)
    {
      msg_warning("WARNING: your http() driver instance uses multiple urls without persist-name(). "
                  "It is recommended that you set persist-name() in this case as syslog-ng will be "
                  "using the first URL in urls() to register persistent data, such as the disk queue "
                  "name, which might change",
                  evt_tag_str("url", self->load_balancer->targets[0].url),
                  log_pipe_location_tag(&self->super.super.super.super));
    }
  if (self->load_balancer->num_targets > self->super.num_workers)
    {
      msg_warning("WARNING: your http() driver instance uses less workers than urls. "
                  "It is recommended to increase the number of workers to at least the number of servers, "
                  "otherwise not all urls will be used for load-balancing",
                  evt_tag_int("urls", self->load_balancer->num_targets),
                  evt_tag_int("workers", self->super.num_workers),
                  log_pipe_location_tag(&self->super.super.super.super));
    }
  /* we need to set up url before we call the inherited init method, so our stats key is correct */
  self->url = self->load_balancer->targets[0].url;

  if (!log_threaded_dest_driver_init_method(s, _format_persist_name(s)))
    return FALSE;

  log_template_options_init(&self->template_options, cfg);

  http_load_balancer_set_recovery_timeout(self->load_balancer, self->super.time_reopen);

  return TRUE;
}

static void
http_dd_free(LogPipe *s)
{
  HTTPDestinationDriver *self = (HTTPDestinationDriver *)s;

  log_template_options_destroy(&self->template_options);

  g_string_free(self->delimiter, TRUE);
  g_string_free(self->body_prefix, TRUE);
  g_string_free(self->body_suffix, TRUE);
  log_template_unref(self->body_template);

  curl_global_cleanup();

  g_free(self->user);
  g_free(self->password);
  g_free(self->user_agent);
  g_free(self->ca_dir);
  g_free(self->ca_file);
  g_free(self->cert_file);
  g_free(self->key_file);
  g_free(self->ciphers);
  g_free(self->proxy);
  g_list_free_full(self->headers, g_free);
  g_mutex_free(self->workers_lock);
  http_load_balancer_free(self->load_balancer);
  http_response_handlers_free(self->response_handlers);

  log_threaded_dest_driver_free(s);
}

LogDriver *
http_dd_new(GlobalConfig *cfg)
{
  HTTPDestinationDriver *self = g_new0(HTTPDestinationDriver, 1);

  log_threaded_dest_driver_init_instance(&self->super, cfg);
  log_template_options_defaults(&self->template_options);

  self->super.super.super.super.init = http_dd_init;
  self->super.super.super.super.deinit = http_dd_deinit;
  self->super.super.super.super.free_fn = http_dd_free;
  self->super.super.super.super.generate_persist_name = _format_persist_name;
  self->super.format_stats_instance = _format_stats_instance;
  self->super.stats_source = stats_register_type("http");
  self->super.worker.construct = http_dw_new;

  curl_global_init(CURL_GLOBAL_ALL);

  self->ssl_version = CURL_SSLVERSION_DEFAULT;
  self->peer_verify = TRUE;
  /* disable batching even if the global batch_lines is specified */
  self->super.batch_lines = 0;
  self->batch_bytes = 0;
  self->body_prefix = g_string_new("");
  self->body_suffix = g_string_new("");
  self->delimiter = g_string_new("\n");
  self->workers_lock = g_mutex_new();
  self->load_balancer = http_load_balancer_new();
  curl_version_info_data *curl_info = curl_version_info(CURLVERSION_NOW);
  if (!self->user_agent)
    self->user_agent = g_strdup_printf("syslog-ng %s/libcurl %s",
                                       SYSLOG_NG_VERSION, curl_info->version);

  self->response_handlers = http_response_handlers_new();

  return &self->super.super.super;
}
