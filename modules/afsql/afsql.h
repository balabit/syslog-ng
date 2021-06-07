/*
 * Copyright (c) 2002-2012 Balabit
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

#ifndef AFSQL_H_INCLUDED
#define AFSQL_H_INCLUDED

#include "logthrdest/logthrdestdrv.h"
#include "mainloop-worker.h"
#include "string-list.h"

#include <dbi.h>

#define AFSQL_COLUMN_DEFAULT GUINT_TO_POINTER(1)

/* field flags */
enum
{
  AFSQL_FF_DEFAULT = 0x0001,
};

/* destination driver flags */
enum
{
  AFSQL_DDF_EXPLICIT_COMMITS = 0x0001,
  AFSQL_DDF_DONT_CREATE_TABLES = 0x0002,
};

typedef struct _AFSqlField
{
  guint32 flags;
  gchar *name;
  gchar *type;
  LogTemplate *value;
} AFSqlField;

/**
 * AFSqlDestDriver:
 *
 * This structure encapsulates an SQL destination driver. SQL insert
 * statements are generated from a separate thread because of the blocking
 * nature of the DBI API. It is ensured that while the thread is running,
 * the reference count to the driver structure is increased, thus the db
 * thread can read any of the fields in this structure. To do anything more
 * than simple reading out a value, some kind of locking mechanism shall be
 * used.
 **/
typedef struct _AFSqlDestDriver
{
  LogThreadedDestDriver super;
  /* read by the db thread */
  gchar *type;
  gchar *host;
  gchar *port;
  gchar *user;
  gchar *password;
  gchar *database;
  gchar *encoding;
  gchar *create_statement_append;
  GList *columns;
  GList *values;
  GList *indexes;
  LogTemplate *table;
  gint fields_len;
  AFSqlField *fields;
  gchar *null_value;
  gint flags;
  gboolean ignore_tns_config;
  GList *session_statements;

  LogTemplateOptions template_options;

  GHashTable *dbd_options;
  GHashTable *dbd_options_numeric;

  /* used exclusively by the db thread */
  dbi_conn dbi_ctx;
  GHashTable *syslogng_conform_tables;
  guint32 failed_message_counter;
  gboolean transaction_active;
} AFSqlDestDriver;


void afsql_dd_set_type(LogDriver *s, const gchar *type);
void afsql_dd_set_host(LogDriver *s, const gchar *host);
gboolean afsql_dd_check_port(const gchar *port);
void afsql_dd_set_port(LogDriver *s, const gchar *port);
void afsql_dd_set_user(LogDriver *s, const gchar *user);
void afsql_dd_set_password(LogDriver *s, const gchar *password);
void afsql_dd_set_database(LogDriver *s, const gchar *database);
void afsql_dd_set_table(LogDriver *s, LogTemplate *table_template);
void afsql_dd_set_columns(LogDriver *s, GList *columns);
void afsql_dd_set_values(LogDriver *s, GList *values);
void afsql_dd_set_null_value(LogDriver *s, const gchar *null);
void afsql_dd_set_indexes(LogDriver *s, GList *indexes);
void afsql_dd_set_session_statements(LogDriver *s, GList *session_statements);
void afsql_dd_set_flags(LogDriver *s, gint flags);
void afsql_dd_set_create_statement_append(LogDriver *s, const gchar *create_statement_append);
LogDriver *afsql_dd_new(GlobalConfig *cfg);
gint afsql_dd_lookup_flag(const gchar *flag);
void afsql_dd_add_dbd_option(LogDriver *s, const gchar *name, const gchar *value);
void afsql_dd_add_dbd_option_numeric(LogDriver *s, const gchar *name, gint value);
void afsql_dd_set_ignore_tns_config(LogDriver *s, const gboolean ignore_tns_config);

#endif
