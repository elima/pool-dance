/*
 * upstream-service.c
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

#include "upstream-service.h"

#define CONFIG_GROUP_NAME "upstream-service"

#define DEFAULT_URL "http://127.0.0.1:8332/"
#define DEFAULT_WORK_CACHE_SIZE  10

struct _UpstreamService
{
  EvdJsonrpcHttpClient *rpc;

  UpstreamServiceHasWorkCb has_work_cb;
  gpointer user_data;

  SoupMessageHeaders *headers;

  GQueue *work_queue;
  guint work_queue_min;
  guint work_requests;
};

static void rpc_on_getwork (GObject      *obj,
                            GAsyncResult *result,
                            gpointer      user_data);

UpstreamService *
upstream_service_new (GKeyFile                  *config,
                      UpstreamServiceHasWorkCb   has_work_callback,
                      gpointer                   user_data,
                      GError                   **error)
{
  UpstreamService *self = NULL;
  EvdHttpRequest *http_req;

  gchar *url = NULL;
  gchar *user = NULL;
  gchar *passw = NULL;

  url = g_key_file_get_string (config, CONFIG_GROUP_NAME, "url", NULL);
  if (url == NULL || url[0] == '\0')
    url = g_strdup (DEFAULT_URL);

  user = g_key_file_get_string (config, CONFIG_GROUP_NAME, "user", NULL);
  if (user == NULL || user[0] == '\0')
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_ARGUMENT,
                   "No RPC user specified");
      goto out;
    }

  passw = g_key_file_get_string (config, CONFIG_GROUP_NAME, "password", NULL);
  if (passw == NULL || passw[0] == '\0')
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_ARGUMENT,
                   "No RPC password specified");
      goto out;
    }

  self = g_slice_new0 (UpstreamService);

  /* JSON-RPC HTTP client */
  self->rpc = evd_jsonrpc_http_client_new (url);
  http_req = evd_jsonrpc_http_client_get_http_request (self->rpc);
  evd_http_request_set_basic_auth_credentials (http_req,
                                               user,
                                               passw);

  self->work_queue_min = g_key_file_get_integer (config,
                                                 CONFIG_GROUP_NAME,
                                                 "work-cache-size",
                                                 NULL);
  if (self->work_queue_min == 0)
    self->work_queue_min = DEFAULT_WORK_CACHE_SIZE;

  self->has_work_cb = has_work_callback;
  self->user_data = user_data;

 out:
  g_free (url);
  g_free (user);
  g_free (passw);

  return self;
}

void
upstream_service_free (UpstreamService *self)
{
  if (self == NULL)
    return;

  if (self->work_queue != NULL)
    g_queue_free_full (self->work_queue, (GDestroyNotify) json_node_free);
  g_object_unref (self->rpc);

  g_slice_free (UpstreamService, self);
}

static void
fill_work_queue (UpstreamService *self)
{
  while (self->work_requests +
         g_queue_get_length (self->work_queue) < self->work_queue_min)
    {
      self->work_requests++;
      evd_jsonrpc_http_client_call_method (self->rpc,
                                           "getwork",
                                           NULL,
                                           NULL,
                                           rpc_on_getwork,
                                           self);
    }
}

static void
rpc_on_getwork (GObject      *obj,
                GAsyncResult *result,
                gpointer      user_data)
{
  UpstreamService *self = user_data;
  JsonNode *json_result;
  JsonNode *json_error;
  GError *error = NULL;

  if (! evd_jsonrpc_http_client_call_method_finish (EVD_JSONRPC_HTTP_CLIENT (obj),
                                                    result,
                                                    &json_result,
                                                    &json_error,
                                                    &error))
    {
      g_print ("Getwork failed: %s\n", error->message);
      g_error_free (error);
      return;
    }
  else
    {
      g_queue_push_head (self->work_queue, json_result);

      self->has_work_cb (self, json_result, self->user_data);

      json_node_free (json_error);
    }

  self->work_requests--;

  fill_work_queue (self);
}

void
upstream_service_notify_new_block (UpstreamService *self, guint block)
{
  if (self->work_queue != NULL)
    g_queue_free_full (self->work_queue, (GDestroyNotify) json_node_free);

  self->work_queue = g_queue_new ();
  self->work_requests = 0;

  fill_work_queue (self);
}

EvdJsonrpcHttpClient *
upstream_service_get_rpc (UpstreamService *self)
{
  return self->rpc;
}

gboolean
upstream_service_has_work (UpstreamService *self)
{
  return g_queue_get_length (self->work_queue) > 0;
}

JsonNode *
upstream_service_get_work (UpstreamService *self)
{
  JsonNode *work;

  work = g_queue_pop_head (self->work_queue);

  fill_work_queue (self);

  return work;
}
