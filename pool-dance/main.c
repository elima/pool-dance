/*
 * main.c
 *
 * pool-dance: Bitcoin mining pool based on EventDance
 *             <http://eventdance.org/pool-dance>
 *
 * Copyright (C) 2012, Eduardo Lima Mitev <elima@igalia.com>
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License
 * version 3, or (at your option) any later version as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Affero General Public License at http://www.gnu.org/licenses/agpl.html
 * for more details.
 */

#include <evd.h>

#include "upstream-service.h"
#include "block-monitor.h"
#include "pool-server.h"
#include "work-validator.h"
#include "event-dispatcher.h"
#include "round-manager.h"

#define CONFIG_GROUP_NAME "pool-dance"

#define DEFAULT_CONFIG_FILENAME "/etc/pool-dance/pool-dance.conf"
#define DEFAULT_LOG_FILENAME    "/var/log/pool-dance.log"
#define DEFAULT_PID_FILENAME    "/var/run/pool-dance.pid"

#define EASY_TARGET "ffffffffffffffffffffffffffffffffffffffffffffffffffffffff00000000"

static UpstreamService *upstream_service;
static BlockMonitor *block_monitor;
static PoolServer *pool_server;
static WorkValidator *work_validator;
static EventDispatcher *event_dispatcher;
static RoundManager *round_manager;

static guint current_block = 0;
static GError *error = NULL;

static gchar *config_file_name = NULL;
static gboolean daemonize = FALSE;
static gchar *log_file_name = NULL;
static guint8 log_level = 0;
static gchar *pid_file_name = NULL;
static gchar *run_as_user = NULL;
static gchar *run_as_group = NULL;

static GOptionEntry option_entries[] =
{
  { "conf", 'c', 0, G_OPTION_ARG_STRING, &config_file_name, "Absolute path for the configuration file, default is '" DEFAULT_CONFIG_FILENAME "'", "filename" },
  { "daemonize", 'D', 0, G_OPTION_ARG_NONE, &daemonize, "Run service in the background", NULL },
  { NULL }
};

static void
on_submit_work (GObject      *obj,
                GAsyncResult *result,
                gpointer      user_data)
{
  JsonNode *json_result;
  JsonNode *json_error;
  GError *error = NULL;
  WorkResult *work_result = user_data;

  if (! evd_jsonrpc_http_client_call_method_finish (EVD_JSONRPC_HTTP_CLIENT (obj),
                                                    result,
                                                    &json_result,
                                                    &json_error,
                                                    &error))
    {
      g_print ("Work submit failed: %s\n", error->message);
      g_error_free (error);
    }
  else
    {
      if (json_node_get_boolean (json_result))
        {
          /* new block found! \o/ */
          event_dispatcher_notify_block_found (event_dispatcher,
                                               current_block,
                                               work_result);
        }

      json_node_free (json_result);
      json_node_free (json_error);
    }

  work_result_unref (work_result);
}

static void
work_validator_on_validate (GObject      *obj,
                            GAsyncResult *res,
                            gpointer      user_data)
{
  GError *error = NULL;
  WorkResult *work_result = user_data;

  if (work_validator_validate_finish (work_validator, res, &error))
    {
      /* work is accepted! */
      event_dispatcher_notify_work_validated (event_dispatcher,
                                              work_result,
                                              WORK_VALIDATOR_ERROR_SUCCESS,
                                              NULL);

      pool_server_respond_putwork (pool_server, work_result, TRUE, NULL);

      /* submit work upstream to try find a block */
      work_result_ref (work_result);
      evd_jsonrpc_http_client_call_method (upstream_service_get_rpc (upstream_service),
                                           "getwork",
                                           work_result_get_json_node (work_result),
                                           NULL,
                                           on_submit_work,
                                           work_result);
    }
  else
    {
      /* work is rejected */
      event_dispatcher_notify_work_validated (event_dispatcher,
                                              work_result,
                                              error->code,
                                              error->message);

      pool_server_respond_putwork (pool_server,
                                   work_result,
                                   FALSE,
                                   error->message);
      g_error_free (error);
    }

  work_result_unref (work_result);
}

static void
pool_server_on_putwork (PoolServer *self,
                        WorkResult *work_result,
                        gpointer    user_data)
{
  /* pass work to validator */
  work_validator_validate (work_validator,
                           work_result,
                           NULL,
                           work_validator_on_validate,
                           work_result);

  event_dispatcher_notify_work_submitted (event_dispatcher, work_result);
}

static gboolean
serve_work (gpointer user_data)
{
  WorkRequest *work_request;
  JsonNode *work_item;
  JsonObject *obj;

  if (! pool_server_need_work (pool_server) ||
      ! upstream_service_has_work (upstream_service))
    {
      return FALSE;
    }

  work_request = pool_server_get_work_request (pool_server);
  work_item = upstream_service_get_work (upstream_service);

  /* set easy target */
  obj = json_node_get_object (work_item);
  json_object_set_string_member (obj, "target", EASY_TARGET);

  if (pool_server_send_work_item (pool_server, work_request, work_item))
    {
      event_dispatcher_notify_work_sent (event_dispatcher,
                                         work_request,
                                         work_item);

      work_validator_track_work_sent (work_validator,
                                      work_request,
                                      work_item,
                                      upstream_service);
    }

  json_node_free (work_item);
  work_request_unref (work_request);

  return pool_server_need_work (pool_server) &&
    upstream_service_has_work (upstream_service);
}

static void
upstream_service_on_has_work (UpstreamService *upstream_service,
                              JsonNode        *work,
                              gpointer         user_data)
{
  evd_timeout_add (NULL,
                   0,
                   G_PRIORITY_DEFAULT,
                   serve_work,
                   NULL);
}

static void
pool_server_on_getwork (PoolServer  *self,
                        WorkRequest *work_request,
                        gpointer     user_data)
{
  event_dispatcher_notify_work_requested (event_dispatcher, work_request);

  evd_timeout_add (NULL,
                   0,
                   G_PRIORITY_DEFAULT,
                   serve_work,
                   NULL);
}

static void
block_monitor_on_block_change (BlockMonitor *block_monitor,
                               guint         block,
                               gpointer      user_data)
{
  current_block = block;

  upstream_service_notify_new_block (upstream_service, block);
  pool_server_notify_new_block (pool_server, block);
  work_validator_notify_new_block (work_validator, block);

  event_dispatcher_notify_current_block (event_dispatcher, block);
}

/*
static gboolean
force_new_block (gpointer user_data)
{
  event_dispatcher_notify_block_found (event_dispatcher,
                                       current_block,
                                       NULL);

  //  block_monitor_on_block_change (block_monitor, 197554, NULL);

  return TRUE;
}
*/

static void
load_global_config (GKeyFile *config)
{
  /* log file */
  log_file_name = g_key_file_get_string (config,
                                         CONFIG_GROUP_NAME,
                                         "log-file",
                                         NULL);
  if (log_file_name == NULL || log_file_name[0] == '\0')
    log_file_name = g_strdup (DEFAULT_LOG_FILENAME);

  /* log level */
  log_level = g_key_file_get_integer (config,
                                      CONFIG_GROUP_NAME,
                                      "log-file",
                                      NULL);

  /* pid file */
  pid_file_name = g_key_file_get_string (config,
                                         CONFIG_GROUP_NAME,
                                         "pid-file",
                                         NULL);
  if (pid_file_name == NULL || pid_file_name[0] == '\0')
    pid_file_name = g_strdup (DEFAULT_PID_FILENAME);

  /* run as user */
  run_as_user = g_key_file_get_string (config,
                                       CONFIG_GROUP_NAME,
                                       "user",
                                       NULL);

  /* run as group */
  run_as_group = g_key_file_get_string (config,
                                        CONFIG_GROUP_NAME,
                                        "group",
                                        NULL);
}

static gboolean
drop_privileges (gpointer user_data)
{
  GError *error = NULL;
  EvdDaemon *evd_daemon = EVD_DAEMON (user_data);

  if (! evd_daemon_set_user (evd_daemon, run_as_user, &error))
    {
      g_print ("ERROR dropping privileges: %s\n", error->message);

      /* @TODO: report this through the log file */
      g_error_free (error);

      /* this is critical, halt */
      evd_daemon_quit (evd_daemon, -1);
    }

  return FALSE;
}

gint
main (gint argc, gchar *argv[])
{
  EvdDaemon *evd_daemon = NULL;
  gint exit_code = 0;
  GOptionContext *context = NULL;
  GKeyFile *config = NULL;

  g_type_init ();
  evd_tls_init (NULL);

  /* parse command line */
  context = g_option_context_new ("- Lightweight and memory efficient Bitcoin mining pool");
  g_option_context_add_main_entries (context, option_entries, NULL);
  if (! g_option_context_parse (context, &argc, &argv, &error))
    {
      g_print ("ERROR parsing commandline options: %s\n", error->message);
      goto out;
    }

  if (config_file_name == NULL)
    config_file_name = g_strdup (DEFAULT_CONFIG_FILENAME);

  /* load and parse configuration file */
  config = g_key_file_new ();
  if (! g_key_file_load_from_file (config,
                                   config_file_name,
                                   G_KEY_FILE_NONE,
                                   &error))
    {
      g_print ("ERROR loading configuration: %s\n", error->message);
      goto out;
    }

  load_global_config (config);

  /* upstream service */
  upstream_service = upstream_service_new (config,
                                           upstream_service_on_has_work,
                                           NULL,
                                           &error);
  if (upstream_service == NULL)
    {
      g_print ("ERROR creating upstream service: %s\n", error->message);
      goto out;
    }

  /* block monitor */
  block_monitor =
    block_monitor_new (config,
                       upstream_service_get_rpc (upstream_service),
                       block_monitor_on_block_change,
                       NULL);

  /* pool server */
  pool_server = pool_server_new (config,
                                 pool_server_on_getwork,
                                 pool_server_on_putwork,
                                 NULL);

  /* work validator */
  work_validator = work_validator_new (upstream_service_get_rpc (upstream_service));
  work_validator_set_target (work_validator, EASY_TARGET);

  /* event dispatcher */
  event_dispatcher = event_dispatcher_new (log_file_name, &error);
  if (event_dispatcher == NULL)
    {
      g_print ("ERROR creating event dispatcher: %s\n", error->message);
      goto out;
    }

  /* round manager */
  round_manager = round_manager_new (config, event_dispatcher);

  //  g_timeout_add (2000, force_new_block, NULL);

  /* daemon */
  evd_daemon = evd_daemon_get_default (&argc, &argv);
  evd_daemon_set_pid_file (evd_daemon, pid_file_name);

  /* start the show */
  if (daemonize && ! evd_daemon_daemonize (evd_daemon, &error))
    {
      g_print ("ERROR detaching daemon: %s\n", error->message);
      goto out;
    }

  if (! round_manager_start (round_manager, &error))
    {
      g_print ("ERROR starting round manager: %s\n", error->message);
      goto out;
    }

  block_monitor_start (block_monitor);
  pool_server_start (pool_server);

  /* drop privileges */
  if (run_as_user != NULL)
    evd_timeout_add (NULL,
                     0,
                     G_PRIORITY_HIGH,
                     drop_privileges,
                     evd_daemon);

  /* event loop */
  exit_code = evd_daemon_run (evd_daemon, &error);

  /* end the show */
  block_monitor_stop (block_monitor);

 out:
  /* free stuff */
  g_option_context_free (context);
  g_free (config_file_name);
  g_free (log_file_name);
  g_free (pid_file_name);
  g_free (run_as_user);
  g_free (run_as_group);
  if (config != NULL)
    g_key_file_free (config);

  if (evd_daemon != NULL)
    g_object_unref (evd_daemon);

  upstream_service_free (upstream_service);
  block_monitor_free (block_monitor);
  pool_server_free (pool_server);
  work_validator_free (work_validator);
  event_dispatcher_free (event_dispatcher);
  round_manager_free (round_manager);

  evd_tls_deinit ();

  /* exit */
  if (error != NULL)
    {
      exit_code = -1;
      g_print ("\npool-dance: exit with error: %s\n", error->message);
      g_error_free (error);
    }
  else
    {
      g_print ("\npool-dance: clean exit :)\n");
    }

  return exit_code;
}
