/*
 * pool-server.c
 *
 * pool-dance: Simple, light-weight and efficient Bitcoin mining pool
 *             <https://github.com/elima/pool-dance>
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

#include "pool-server.h"

#define CONFIG_GROUP_NAME "pool-server"

#define SERVER_NAME "pool-dance/" VERSION

#define DEFAULT_LISTEN_ADDR "0.0.0.0"
#define DEFAULT_LISTEN_PORT 8335

#define LP_PATH "/lp"

struct _PoolServer
{
  EvdWebService *web_service;
  EvdJsonrpcHttpServer *rpc;
  gchar *listen_addr;

  PoolServerGetworkCb getwork_callback;
  PoolServerPutworkCb putwork_callback;
  gpointer user_data;

  SoupMessageHeaders *headers;

  GList *lp_conns;

  GQueue *getwork_queue;
};

struct _WorkRequest
{
  gint ref_count;
  EvdHttpConnection *conn;
  EvdHttpRequest *req;
  PoolServer *self;
  guint invocation_id;
  gboolean from_lp;
};

static void getwork_connection_on_close (EvdConnection *conn, gpointer user_data);

WorkRequest *
work_request_new (PoolServer        *self,
                  guint              invocation_id,
                  EvdHttpConnection *conn,
                  gboolean           from_lp)
{
  WorkRequest *data;

  data = g_slice_new0 (WorkRequest);
  data->ref_count = 1;

  data->self = self;

  data->invocation_id = invocation_id;

  data->conn = conn;
  g_object_ref (conn);

  data->req = evd_http_connection_get_current_request (conn);

  data->from_lp = from_lp;

  g_signal_connect (conn,
                    "close",
                    G_CALLBACK (getwork_connection_on_close),
                    data);

  return data;
}

static void
work_request_free (WorkRequest *self)
{
  g_signal_handlers_disconnect_by_func (self->conn,
                                        getwork_connection_on_close,
                                        self);
  g_object_unref (self->conn);

  g_slice_free (WorkRequest, self);
}

WorkRequest *
work_request_ref (WorkRequest *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->ref_count > 0, NULL);

  g_atomic_int_add (&self->ref_count, 1);

  return self;
}

void
work_request_unref (WorkRequest *self)
{
  gint old_ref;

  if (self == NULL)
    return;

  g_return_if_fail (self != NULL);
  g_return_if_fail (self->ref_count > 0);

  old_ref = g_atomic_int_get (&self->ref_count);
  if (old_ref > 1)
    g_atomic_int_compare_and_exchange (&self->ref_count, old_ref, old_ref - 1);
  else
    work_request_free (self);
}

void
work_request_get_client_info (WorkRequest  *self,
                              gchar       **user,
                              gchar       **password,
                              gchar       **remote_addr,
                              gchar       **user_agent)
{
  /* get user */
  if (user != NULL)
    evd_http_request_get_basic_auth_credentials (self->req, user, password);

  /* get remote address */
  if (remote_addr != NULL)
    {
      *remote_addr = evd_socket_get_remote_address_str
        (evd_connection_get_socket (EVD_CONNECTION (self->conn)), NULL, NULL);
    }

  /* get user agent */
  if (user_agent != NULL)
    {
      SoupMessageHeaders *headers;

      headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (self->req));
      *user_agent = g_strdup (soup_message_headers_get_one (headers, "User-Agent"));
    }
}

static void
rpc_on_method_call (EvdJsonrpcHttpServer *rpc,
                    const gchar          *method_name,
                    JsonNode             *params,
                    guint                 invocation_id,
                    EvdHttpConnection    *conn,
                    EvdHttpRequest       *request,
                    gpointer              user_data)
{
  PoolServer *self = user_data;
  JsonArray *params_arr;

  g_assert (self != NULL);
  g_assert (EVD_IS_HTTP_CONNECTION (conn));

  if (g_strcmp0 (method_name, "getwork") != 0)
    {
      JsonNode *json_node;

      json_node = json_node_new (JSON_NODE_VALUE);
      json_node_set_string (json_node, "Method not supported");

      evd_jsonrpc_http_server_respond_error (rpc,
                                             invocation_id,
                                             json_node,
                                             NULL);
      json_node_free (json_node);

      return;
    }

  params_arr = json_node_get_array (params);
  if (json_array_get_length (params_arr) == 0)
    {
      WorkRequest *getwork;

      /* getwork */

      /* create new getwork item */
      getwork = work_request_new (self, invocation_id, conn, FALSE);

      /* enqueue getwork */
      g_queue_push_tail (self->getwork_queue, getwork);

      /* notify of getwork request */
      self->getwork_callback (self, getwork, self->user_data);
    }
  else
    {
      WorkResult *work_result;
      JsonNode *work;

      /* putwork */

      work = json_node_copy (params);

      /* create new work result */
      work_result = work_result_new (work, invocation_id, conn);

      /* notify of putwork request */
      self->putwork_callback (self, work_result, self->user_data);
    }
}

static void
getwork_connection_on_close (EvdConnection *conn, gpointer user_data)
{
  WorkRequest *data = user_data;

  g_signal_handlers_disconnect_by_func (conn,
                                        getwork_connection_on_close,
                                        data);

  g_queue_remove (data->self->getwork_queue, data);
  work_request_unref (data);
}

static void
lp_connection_on_close (EvdConnection *conn, gpointer user_data)
{
  PoolServer *self = user_data;

  g_signal_handlers_disconnect_by_func (conn,
                                        lp_connection_on_close,
                                        NULL);
  self->lp_conns = g_list_remove (self->lp_conns, conn);
  g_object_unref (conn);
}

static void
on_request_headers (EvdWebService     *service,
                    EvdHttpConnection *conn,
                    EvdHttpRequest    *req,
                    gpointer           user_data)
{
  PoolServer *self = user_data;
  SoupURI *uri;

  uri = evd_http_request_get_uri (req);

  if (g_strcmp0 (uri->path, LP_PATH) != 0)
    {
      evd_web_service_add_connection_with_request (EVD_WEB_SERVICE (self->rpc),
                                                   conn,
                                                   req,
                                                   EVD_SERVICE (service));
    }
  else
    {
      /* long polling request */

      self->lp_conns = g_list_append (self->lp_conns, conn);

      g_object_ref (conn);
      g_signal_connect (conn,
                        "close",
                        G_CALLBACK (lp_connection_on_close),
                        self);
    }
}

static void
web_service_on_listen (GObject      *obj,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  GError *error = NULL;

  if (! evd_service_listen_finish (EVD_SERVICE (obj),
                                   result,
                                   &error))
    {
      g_print ("POOL-SERVER: %s\n", error->message);
      g_error_free (error);
    }
  else
    {
      g_print ("POOL-SERVER: Listening...\n");
    }
}

PoolServer *
pool_server_new (GKeyFile            *config,
                 PoolServerGetworkCb  getwork_callback,
                 PoolServerPutworkCb  putwork_callback,
                 gpointer             user_data)
{
  PoolServer *self;
  gchar *addr;
  guint port;

  self = g_slice_new0 (PoolServer);

  self->getwork_callback = getwork_callback;
  self->putwork_callback = putwork_callback;
  self->user_data = user_data;

  addr = g_key_file_get_string (config,
                                CONFIG_GROUP_NAME,
                                "listen-addr",
                                NULL);
  if (addr == NULL || addr[0] == '\0')
    addr = g_strdup (DEFAULT_LISTEN_ADDR);

  port = g_key_file_get_integer (config,
                                 CONFIG_GROUP_NAME,
                                 "listen-port",
                                 NULL);
  if (port == 0)
    port = DEFAULT_LISTEN_PORT;

  self->listen_addr = g_strdup_printf ("%s:%u", addr, port);
  g_free (addr);

  /* getwork queue */
  self->getwork_queue = g_queue_new ();

  /* JSON-RPC HTTP server */
  self->rpc = evd_jsonrpc_http_server_new ();
  evd_jsonrpc_http_server_set_method_call_callback (self->rpc,
                                                    rpc_on_method_call,
                                                    self,
                                                    NULL);

  self->headers = evd_jsonrpc_http_server_get_response_headers (self->rpc);
  soup_message_headers_replace (self->headers, "Server", SERVER_NAME);
  soup_message_headers_replace (self->headers, "X-Long-Polling", LP_PATH);

  /* main Web service */
  self->web_service = evd_web_service_new ();
  g_signal_connect (self->web_service,
                    "request-headers",
                    G_CALLBACK (on_request_headers),
                    self);

  return self;
}

void
pool_server_free (PoolServer *self)
{
  if (self == NULL)
    return;

  g_free (self->listen_addr);

  g_queue_free_full (self->getwork_queue, (GDestroyNotify) work_request_unref);
  g_list_free (self->lp_conns);

  g_object_unref (self->rpc);
  g_object_unref (self->web_service);

  g_slice_free (PoolServer, self);
}

void
pool_server_start (PoolServer *self)
{
  evd_service_listen (EVD_SERVICE (self->web_service),
                      self->listen_addr,
                      NULL,
                      web_service_on_listen,
                      self);
}

EvdWebService *
pool_server_get_web_service (PoolServer *self)
{
  return EVD_WEB_SERVICE (self->rpc);
}

static void
long_polling_conn_to_getwork (EvdHttpConnection *conn, PoolServer *self)
{
  WorkRequest *data;

  g_signal_handlers_disconnect_by_func (conn,
                                        lp_connection_on_close,
                                        self);

  data = work_request_new (self, 0, conn, TRUE);

  g_object_unref (conn);

  g_queue_push_tail (self->getwork_queue, data);
}

void
pool_server_notify_new_block (PoolServer *self, guint block)
{
  gchar *block_st;

  block_st = g_strdup_printf ("%u", block);
  soup_message_headers_replace (self->headers, "X-Blocknum", block_st);
  g_free (block_st);

  /* flush getwork cache */
  g_list_foreach (self->lp_conns, (GFunc) long_polling_conn_to_getwork, self);
  g_list_free (self->lp_conns);
  self->lp_conns = NULL;
}

gboolean
pool_server_need_work (PoolServer *self)
{
  return g_queue_get_length (self->getwork_queue) > 0;
}

WorkRequest *
pool_server_get_work_request (PoolServer *self)
{
  return g_queue_pop_head (self->getwork_queue);
}

gboolean
pool_server_send_work_item (PoolServer  *self,
                            WorkRequest *work_request,
                            JsonNode    *work_item)
{
  GError *error = NULL;
  gboolean result = TRUE;

  if (! work_request->from_lp)
    {
      if (! evd_jsonrpc_http_server_respond (self->rpc,
                                             work_request->invocation_id,
                                             work_item,
                                             &error))
        {
          g_print ("Failed to send work: %s\n", error->message);
          g_error_free (error);

          result = FALSE;
        }
    }
  else
    {
      JsonGenerator *gen;
      gchar *json;
      gchar *msg;

      gen = json_generator_new ();
      json_generator_set_root (gen, work_item);

      json = json_generator_to_data (gen, NULL);
      g_object_unref (gen);

      msg = g_strdup_printf ("{\"result\": %s, \"id\": \"0\", \"error\": null}", json);
      g_free (json);

      if (! evd_web_service_respond (self->web_service,
                                     work_request->conn,
                                     SOUP_STATUS_OK,
                                     self->headers,
                                     msg,
                                     strlen (msg),
                                     &error))
        {
          g_print ("Failed to send work to long-polling: %s\n", error->message);
          g_error_free (error);

          result = FALSE;
        }

      g_free (msg);
    }

  return result;
}

void
pool_server_respond_putwork (PoolServer  *self,
                             WorkResult  *work_result,
                             gboolean     accepted,
                             const gchar *reason)
{
  guint invocation_id;
  JsonNode *result;
  GError *error = NULL;

  invocation_id = work_result_get_invocation_id (work_result);

  result = json_node_new (JSON_NODE_VALUE);
  json_node_set_boolean (result, accepted);

  if (! evd_jsonrpc_http_server_respond (self->rpc,
                                         invocation_id,
                                         result,
                                         &error))
    {
      g_print ("Failed to respond putwork: %s\n", error->message);
      g_error_free (error);
    }

  json_node_free (result);
}
