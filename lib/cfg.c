/*
 * Copyright (c) 2002-2014 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
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

#include "cfg.h"
#include "cfg-path.h"
#include "cfg-grammar.h"
#include "module-config.h"
#include "cfg-tree.h"
#include "messages.h"
#include "template/templates.h"
#include "userdb.h"
#include "logmsg/logmsg.h"
#include "dnscache.h"
#include "serialize.h"
#include "plugin.h"
#include "cfg-parser.h"
#include "stats/stats-registry.h"
#include "logproto/logproto-builtins.h"
#include "reloc.h"
#include "hostname.h"
#include "rcptid.h"
#include "resolved-configurable-paths.h"
#include "mainloop.h"
#include "timeutils/format.h"

#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <iv_work.h>

/* PersistConfig */

struct _PersistConfig
{
  GHashTable *keys;
};

typedef struct _PersistConfigEntry
{
  gpointer value;
  GDestroyNotify destroy;
} PersistConfigEntry;

static void
persist_config_entry_free(PersistConfigEntry *self)
{
  if (self->destroy)
    {
      self->destroy(self->value);
    }
  g_free(self);
}

PersistConfig *
persist_config_new(void)
{
  PersistConfig *self = g_new0(PersistConfig, 1);

  self->keys = g_hash_table_new_full(g_str_hash, g_str_equal, (GDestroyNotify) g_free,
                                     (GDestroyNotify) persist_config_entry_free);
  return self;
}

void
persist_config_free(PersistConfig *self)
{
  g_hash_table_destroy(self->keys);
  g_free(self);
}

gint
cfg_ts_format_value(gchar *format)
{
  if (strcmp(format, "rfc3164") == 0 || strcmp(format, "bsd") == 0)
    return TS_FMT_BSD;
  else if (strcmp(format, "rfc3339") == 0 || strcmp(format, "iso") == 0)
    return TS_FMT_ISO;
  else if (strcmp(format, "full") == 0)
    return TS_FMT_FULL;
  else if (strcmp(format, "unix") == 0 || strcmp(format, "utc") == 0)
    return TS_FMT_UNIX;
  else
    {
      msg_error("Invalid ts_format() value",
                evt_tag_str("value", format));
      return TS_FMT_BSD;
    }
}

void
cfg_bad_hostname_set(GlobalConfig *self, gchar *bad_hostname_re)
{
  if (self->bad_hostname_re)
    g_free(self->bad_hostname_re);
  self->bad_hostname_re = g_strdup(bad_hostname_re);
}

gint
cfg_lookup_mark_mode(const gchar *mark_mode)
{
  if (!strcmp(mark_mode, "internal"))
    return MM_INTERNAL;
  if (!strcmp(mark_mode, "dst_idle") || !strcmp(mark_mode, "dst-idle"))
    return MM_DST_IDLE;
  if (!strcmp(mark_mode, "host_idle") || !strcmp(mark_mode, "host-idle"))
    return MM_HOST_IDLE;
  if (!strcmp(mark_mode, "periodical"))
    return MM_PERIODICAL;
  if (!strcmp(mark_mode, "none"))
    return MM_NONE;
  if (!strcmp(mark_mode, "global"))
    return MM_GLOBAL;

  return -1;
}

void
cfg_set_mark_mode(GlobalConfig *self, const gchar *mark_mode)
{
  self->mark_mode = cfg_lookup_mark_mode(mark_mode);
}

static void
_invoke_module_init(gchar *key, ModuleConfig *mc, gpointer *args)
{
  GlobalConfig *cfg = (GlobalConfig *) args[0];
  gboolean *result = (gboolean *) args[1];

  if (!module_config_init(mc, cfg))
    *result = FALSE;
}

static void
_invoke_module_deinit(gchar *key, ModuleConfig *mc, gpointer user_data)
{
  GlobalConfig *cfg = (GlobalConfig *) user_data;

  module_config_deinit(mc, cfg);
}

static gboolean
_sync_plugin_module_path_with_global_define(GlobalConfig *self)
{
  const gchar *module_path;

  /* Sync the @define module-path with the actual module search path as implemented by plugin.
   *
   * if @define module-path is not defined, we use whatever there's in
   * PluginContext by default */
  if (self->lexer)
    {
      module_path = cfg_args_get(self->globals, "module-path");
      if (module_path && strcmp(module_path, self->plugin_context.module_path) != 0)
        {
          plugin_context_set_module_path(&self->plugin_context, module_path);
          return TRUE;
        }
    }
  return FALSE;
}

gboolean
cfg_load_module_with_args(GlobalConfig *cfg, const gchar *module_name, CfgArgs *args)
{
  _sync_plugin_module_path_with_global_define(cfg);
  return plugin_load_module(&cfg->plugin_context, module_name, args);
}

gboolean
cfg_load_module(GlobalConfig *cfg, const gchar *module_name)
{
  return cfg_load_module_with_args(cfg, module_name, NULL);
}

void
cfg_load_forced_modules(GlobalConfig *self)
{
  static const gchar *module_list[] =
  {
#if (!SYSLOG_NG_ENABLE_FORCED_SERVER_MODE)
    "license"
#endif
  };

  if (!self->enable_forced_modules)
    return;

  int i;
  for (i=0; i<sizeof(module_list)/sizeof(gchar *); ++i)
    {
      const gchar *name = module_list[i];

      if (!cfg_load_module(self, name))
        {
          msg_error("Error loading module, forcing exit", evt_tag_str("module", name));
          exit(1);
        }
    }
}

static gboolean
_is_module_autoload_enabled(GlobalConfig *self)
{
  return atoi(cfg_args_get(self->globals, "autoload-compiled-modules") ? : "1");
}

void
cfg_discover_candidate_modules(GlobalConfig *self)
{
  if (self->use_plugin_discovery && _is_module_autoload_enabled(self))
    {
      if (_sync_plugin_module_path_with_global_define(self) || !plugin_has_discovery_run(&self->plugin_context))
        plugin_discover_candidate_modules(&self->plugin_context);
    }
}

gboolean
cfg_is_module_available(GlobalConfig *self, const gchar *module_name)
{
  cfg_discover_candidate_modules(self);
  if (!_is_module_autoload_enabled(self))
    return cfg_load_module(self, module_name);

  return plugin_is_module_available(&self->plugin_context, module_name);
}

Plugin *
cfg_find_plugin(GlobalConfig *cfg, gint plugin_type, const gchar *plugin_name)
{
  return plugin_find(&cfg->plugin_context, plugin_type, plugin_name);
}

/* construct a plugin instance by parsing its relevant portion from the
 * configuration file */
gpointer
cfg_parse_plugin(GlobalConfig *cfg, Plugin *plugin, CFG_LTYPE *yylloc, gpointer arg)
{
  CfgTokenBlock *block;
  CFG_STYPE token;


  /* we add two tokens to an inserted token-block:
   *  1) the plugin type (in order to make it possible to support different
   *  plugins from the same grammar)
   *
   *  2) the keyword equivalent of the plugin name
   */
  block = cfg_token_block_new();

  /* add plugin->type as a token */
  memset(&token, 0, sizeof(token));
  token.type = LL_TOKEN;
  token.token = plugin->type;
  cfg_token_block_add_and_consume_token(block, &token);

  /* start a new lexer context, so plugin specific keywords are recognized,
   * we only do this to lookup the parser name. */
  cfg_lexer_push_context(cfg->lexer, plugin->parser->context, plugin->parser->keywords, plugin->parser->name);
  cfg_lexer_lookup_keyword(cfg->lexer, &token, yylloc, plugin->name);
  cfg_lexer_pop_context(cfg->lexer);

  /* add plugin name token */
  cfg_token_block_add_and_consume_token(block, &token);

  cfg_lexer_inject_token_block(cfg->lexer, block);

  return plugin_construct_from_config(plugin, cfg->lexer, arg);
}

static gboolean
cfg_init_modules(GlobalConfig *cfg)
{
  gboolean result = TRUE;
  gpointer args[] = { cfg, &result };
  g_hash_table_foreach(cfg->module_config, (GHFunc) _invoke_module_init, args);

  return result;
}

static void
cfg_deinit_modules(GlobalConfig *cfg)
{
  g_hash_table_foreach(cfg->module_config, (GHFunc) _invoke_module_deinit, cfg);
}

/* Request that this configuration shuts down and terminates.  Right now, as
 * there's only one configuration executed in a syslog-ng process, it will
 * cause the mainloop to exit.  Should there be multiple configs running in
 * the same process at a future point, this would only terminate one and
 * continue with the rest.
 */
void
cfg_shutdown(GlobalConfig *cfg)
{
  MainLoop *main_loop = main_loop_get_instance();
  main_loop_exit(main_loop);
}

gboolean
cfg_is_shutting_down(GlobalConfig *cfg)
{
  MainLoop *main_loop = main_loop_get_instance();
  return main_loop_is_terminating(main_loop);
}

gboolean
cfg_init(GlobalConfig *cfg)
{
  gint regerr;

  if (cfg->file_template_name && !(cfg->file_template = cfg_tree_lookup_template(&cfg->tree, cfg->file_template_name)))
    msg_error("Error resolving file template",
              evt_tag_str("name", cfg->file_template_name));
  if (cfg->proto_template_name && !(cfg->proto_template = cfg_tree_lookup_template(&cfg->tree, cfg->proto_template_name)))
    msg_error("Error resolving protocol template",
              evt_tag_str("name", cfg->proto_template_name));

  if (cfg->bad_hostname_re)
    {
      if ((regerr = regcomp(&cfg->bad_hostname, cfg->bad_hostname_re, REG_NOSUB | REG_EXTENDED)) != 0)
        {
          gchar buf[256];

          regerror(regerr, &cfg->bad_hostname, buf, sizeof(buf));
          msg_error("Error compiling bad_hostname regexp",
                    evt_tag_str("error", buf));
        }
      else
        {
          cfg->bad_hostname_compiled = TRUE;
        }
    }

  if (!rcptid_init(cfg->state, cfg->use_uniqid))
    return FALSE;

  stats_reinit(&cfg->stats_options);

  dns_caching_update_options(&cfg->dns_cache_options);
  hostname_reinit(cfg->custom_domain);
  host_resolve_options_init_globals(&cfg->host_resolve_options);
  log_template_options_init(&cfg->template_options, cfg);
  if (!cfg_init_modules(cfg))
    return FALSE;
  if (!cfg_tree_start(&cfg->tree))
    return FALSE;

  /*
   * TLDR: A half-initialized pipeline turned out to be really hard to deinitialize
   * correctly when dedicated source/destination threads are spawned (because we
   * would have to wait for workers to stop and guarantee some internal
   * task/timer/fdwatch ordering in ivykis during this action).
   * See: https://github.com/syslog-ng/syslog-ng/pull/3176#issuecomment-638849597
   */
  g_assert(cfg_tree_on_inited(&cfg->tree));
  return TRUE;
}

gboolean
cfg_deinit(GlobalConfig *cfg)
{
  cfg_deinit_modules(cfg);
  rcptid_deinit();
  return cfg_tree_stop(&cfg->tree);
}

void
cfg_set_version_without_validation(GlobalConfig *self, gint version)
{
  self->user_version = version;
}

gboolean
cfg_set_version(GlobalConfig *self, gint version)
{
  if (self->user_version != 0)
    {
      msg_warning("WARNING: you have multiple @version directives in your configuration, only the first one is considered",
                  cfg_format_config_version_tag(self),
                  cfg_format_version_tag("new-version", version));
      return TRUE;
    }
  cfg_set_version_without_validation(self, version);
  if (cfg_is_config_version_older(self, VERSION_VALUE_3_0))
    {
      msg_error("ERROR: compatibility with configurations below 3.0 was dropped in " VERSION_3_13
                ", please update your configuration accordingly",
                cfg_format_config_version_tag(self));
      return FALSE;
    }

  if (cfg_is_config_version_older(self, VERSION_VALUE_LAST_SEMANTIC_CHANGE))
    {
      msg_warning("WARNING: Configuration file format is too old, syslog-ng is running in compatibility mode. "
                  "Please update it to use the " VERSION_PRODUCT_CURRENT " format at your time of convenience. "
                  "To upgrade the configuration, please review the warnings about incompatible changes printed "
                  "by syslog-ng, and once completed change the @version header at the top of the configuration "
                  "file",
                  cfg_format_config_version_tag(self));
    }
  else if (version_convert_from_user(self->user_version) > VERSION_VALUE_CURRENT)
    {
      msg_warning("WARNING: Configuration file format is newer than the current version, please specify the "
                  "current version number ("  VERSION_STR_CURRENT ") in the @version directive. "
                  "syslog-ng will operate at its highest supported version in this mode",
                  cfg_format_config_version_tag(self));
      self->user_version = VERSION_VALUE_CURRENT;
    }

  if (cfg_is_config_version_older(self, VERSION_VALUE_3_3))
    {
      msg_warning("WARNING: global: the default value of log_fifo_size() has changed to 10000 in " VERSION_3_3
                  " to reflect log_iw_size() changes for tcp()/udp() window size changes",
                  cfg_format_config_version_tag(self));
    }

  return TRUE;
}

gboolean
cfg_set_current_version(GlobalConfig *self)
{
  msg_info("Setting current version as config version", evt_tag_str("version", VERSION_STR_CURRENT));
  return cfg_set_version(self, VERSION_VALUE_CURRENT);
}

gboolean
cfg_allow_config_dups(GlobalConfig *self)
{
  const gchar *s;

  if (cfg_is_config_version_older(self, VERSION_VALUE_3_3))
    return TRUE;

  s = cfg_args_get(self->globals, "allow-config-dups");
  if (s && atoi(s))
    {
      return TRUE;
    }
  else
    {
      /* duplicate found, but allow-config-dups is not enabled, hint the user that he might want to use allow-config-dups */
      msg_warning_once("WARNING: Duplicate configuration objects (sources, destinations, ...) are not allowed by default starting with syslog-ng 3.3, add \"@define allow-config-dups 1\" to your configuration to re-enable");
      return FALSE;
    }
}

static void
cfg_register_builtin_plugins(GlobalConfig *self)
{
  log_proto_register_builtin_plugins(&self->plugin_context);
}

GlobalConfig *
cfg_new(gint version)
{
  GlobalConfig *self = g_new0(GlobalConfig, 1);

  self->module_config = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify) module_config_free);
  self->globals = cfg_args_new();
  self->user_version = version;

  self->flush_lines = 100;
  self->mark_freq = 1200; /* 20 minutes */
  self->mark_mode = MM_HOST_IDLE;
  self->chain_hostnames = 0;
  self->time_reopen = 60;
  self->time_reap = 60;

  self->log_fifo_size = 10000;
  self->log_msg_size = 65536;

  file_perm_options_global_defaults(&self->file_perm_options);

  dns_cache_options_defaults(&self->dns_cache_options);
  self->threaded = TRUE;
  self->pass_unix_credentials = TRUE;

  log_template_options_defaults(&self->template_options);
  self->template_options.ts_format = TS_FMT_BSD;
  self->template_options.frac_digits = 0;
  self->template_options.on_error = ON_ERROR_DROP_MESSAGE;

  host_resolve_options_global_defaults(&self->host_resolve_options);

  self->recv_time_zone = NULL;
  self->keep_timestamp = TRUE;

  self->use_uniqid = FALSE;

  stats_options_defaults(&self->stats_options);

  self->min_iw_size_per_reader = 100;

  cfg_tree_init_instance(&self->tree, self);
  plugin_context_init_instance(&self->plugin_context);
  self->use_plugin_discovery = TRUE;
  self->enable_forced_modules = TRUE;

  cfg_register_builtin_plugins(self);
  return self;
}

GlobalConfig *
cfg_new_snippet(void)
{
  GlobalConfig *self = cfg_new(VERSION_VALUE_CURRENT);

  self->use_plugin_discovery = FALSE;
  self->enable_forced_modules = FALSE;
  self->tree.check_zero_source_count = FALSE;
  return self;
}

/* This function creates a GlobalConfig instance that shares the set of
 * available plugins as a master one.  */
GlobalConfig *
cfg_new_subordinate(GlobalConfig *master)
{
  GlobalConfig *self = cfg_new_snippet();

  plugin_context_copy_candidates(&self->plugin_context, &master->plugin_context);
  return self;
}

void
cfg_set_global_paths(GlobalConfig *self)
{
  gchar *include_path;

  cfg_args_set(self->globals, "syslog-ng-root", get_installation_path_for(SYSLOG_NG_PATH_PREFIX));
  cfg_args_set(self->globals, "syslog-ng-data", get_installation_path_for(SYSLOG_NG_PATH_DATADIR));
  cfg_args_set(self->globals, "syslog-ng-include", get_installation_path_for(SYSLOG_NG_PATH_CONFIG_INCLUDEDIR));
  cfg_args_set(self->globals, "syslog-ng-sysconfdir", get_installation_path_for(SYSLOG_NG_PATH_SYSCONFDIR));
  cfg_args_set(self->globals, "scl-root", get_installation_path_for(SYSLOG_NG_PATH_SCLDIR));
  cfg_args_set(self->globals, "module-path", resolvedConfigurablePaths.initial_module_path);
  cfg_args_set(self->globals, "module-install-dir", resolvedConfigurablePaths.initial_module_path);

  include_path = g_strdup_printf("%s:%s",
                                 get_installation_path_for(SYSLOG_NG_PATH_SYSCONFDIR),
                                 get_installation_path_for(SYSLOG_NG_PATH_CONFIG_INCLUDEDIR));
  cfg_args_set(self->globals, "include-path", include_path);
  g_free(include_path);
}

gboolean
cfg_run_parser(GlobalConfig *self, CfgLexer *lexer, CfgParser *parser, gpointer *result, gpointer arg)
{
  gboolean res;
  GlobalConfig *old_cfg;
  CfgLexer *old_lexer;

  old_cfg = configuration;
  configuration = self;
  old_lexer = self->lexer;
  self->lexer = lexer;

  cfg_set_global_paths(self);

  res = cfg_parser_parse(parser, lexer, result, arg);

  cfg_lexer_free(lexer);
  self->lexer = NULL;
  self->lexer = old_lexer;
  configuration = old_cfg;
  return res;
}

gboolean
cfg_run_parser_with_main_context(GlobalConfig *self, CfgLexer *lexer, CfgParser *parser, gpointer *result, gpointer arg,
                                 const gchar *desc)
{
  gboolean ret_val;

  cfg_lexer_push_context(lexer, main_parser.context, main_parser.keywords, desc);
  ret_val = cfg_run_parser(self, lexer, parser, result, arg);

  return ret_val;
}

static void
cfg_dump_processed_config(GString *preprocess_output, gchar *output_filename)
{
  FILE *output_file;

  if (strcmp(output_filename, "-")==0)
    {
      fprintf(stdout, "%s", preprocess_output->str);
      return;
    }

  output_file = fopen(output_filename, "w+");
  if (!output_file)
    {
      msg_error("Error opening preprocess-into file",
                evt_tag_str(EVT_TAG_FILENAME, output_filename),
                evt_tag_error(EVT_TAG_OSERROR));
      return;
    }
  fprintf(output_file, "%s", preprocess_output->str);
  fclose(output_file);
}

static GString *
_load_file_into_string(const gchar *fname)
{
  gchar *buff;
  GString *content = g_string_new("");

  if (g_file_get_contents(fname, &buff, NULL, NULL))
    {
      g_string_append(content, buff);
      g_free(buff);
    }

  return content;
}

static void
_cfg_file_path_free(gpointer data)
{
  CfgFilePath *self = (CfgFilePath *)data;

  g_free(self->file_path);
  g_free(self->path_type);
  g_free(self);
}

gboolean
cfg_read_config(GlobalConfig *self, const gchar *fname, gchar *preprocess_into)
{
  FILE *cfg_file;
  gint res;

  self->filename = fname;

  if ((cfg_file = fopen(fname, "r")) != NULL)
    {
      CfgLexer *lexer;
      self->preprocess_config = g_string_sized_new(8192);
      self->original_config = _load_file_into_string(fname);

      lexer = cfg_lexer_new(self, cfg_file, fname, self->preprocess_config);
      res = cfg_run_parser(self, lexer, &main_parser, (gpointer *) &self, NULL);
      fclose(cfg_file);
      if (preprocess_into)
        {
          cfg_dump_processed_config(self->preprocess_config, preprocess_into);
        }

      if (res)
        {
          /* successfully parsed */
          return TRUE;
        }
    }
  else
    {
      msg_error("Error opening configuration file",
                evt_tag_str(EVT_TAG_FILENAME, fname),
                evt_tag_error(EVT_TAG_OSERROR));
    }

  return FALSE;
}

void
cfg_free(GlobalConfig *self)
{
  g_assert(self->persist == NULL);

  g_free(self->file_template_name);
  g_free(self->proto_template_name);
  log_template_unref(self->file_template);
  log_template_unref(self->proto_template);
  log_template_options_destroy(&self->template_options);
  host_resolve_options_destroy(&self->host_resolve_options);

  if (self->bad_hostname_compiled)
    regfree(&self->bad_hostname);
  g_free(self->recv_time_zone);
  if (self->source_mangle_callback_list)
    g_list_free(self->source_mangle_callback_list);
  g_free(self->bad_hostname_re);
  dns_cache_options_destroy(&self->dns_cache_options);
  g_free(self->custom_domain);
  plugin_context_deinit_instance(&self->plugin_context);
  cfg_tree_free_instance(&self->tree);
  g_hash_table_unref(self->module_config);
  cfg_args_unref(self->globals);

  if (self->state)
    persist_state_free(self->state);

  if (self->preprocess_config)
    g_string_free(self->preprocess_config, TRUE);
  if (self->original_config)
    g_string_free(self->original_config, TRUE);

  g_list_free_full(self->file_list, _cfg_file_path_free);

  g_free(self);
}

void
cfg_persist_config_move(GlobalConfig *src, GlobalConfig *dest)
{
  if (dest->persist != NULL)
    persist_config_free(dest->persist);
  dest->persist = src->persist;
  dest->state = src->state;
  src->persist = NULL;
  src->state = NULL;
}

void
cfg_persist_config_add(GlobalConfig *cfg, const gchar *name, gpointer value, GDestroyNotify destroy,
                       gboolean force)
{
  PersistConfigEntry *p;

  if (cfg->persist && value)
    {
      if (g_hash_table_lookup(cfg->persist->keys, name))
        {
          if (!force)
            {
              msg_error("Internal error, duplicate configuration elements refer to the same persistent config",
                        evt_tag_str("name", name));
              if (destroy)
                destroy(value);
              return;
            }
        }

      p = g_new0(PersistConfigEntry, 1);

      p->value = value;
      p->destroy = destroy;
      g_hash_table_insert(cfg->persist->keys, g_strdup(name), p);
      return;
    }
  else if (destroy && value)
    {
      destroy(value);
    }
  return;
}

gpointer
cfg_persist_config_fetch(GlobalConfig *cfg, const gchar *name)
{
  gpointer res = NULL;
  gchar *orig_key;
  PersistConfigEntry *p;
  gpointer tmp1, tmp2;

  if (cfg->persist && g_hash_table_lookup_extended(cfg->persist->keys, name, &tmp1, &tmp2))
    {
      orig_key = (gchar *) tmp1;
      p = (PersistConfigEntry *) tmp2;

      res = p->value;

      g_hash_table_steal(cfg->persist->keys, name);
      g_free(orig_key);
      g_free(p);
    }
  return res;
}

gint
cfg_get_user_version(const GlobalConfig *cfg)
{
  return cfg->user_version;
}

void register_source_mangle_callback(GlobalConfig *src, mangle_callback cb)
{
  src->source_mangle_callback_list = g_list_append(src->source_mangle_callback_list, cb);
}

void uregister_source_mangle_callback(GlobalConfig *src, mangle_callback cb)
{
  src->source_mangle_callback_list = g_list_remove(src->source_mangle_callback_list, cb);
}

const gchar *
cfg_get_filename(const GlobalConfig *cfg)
{
  return cfg->filename;
}

