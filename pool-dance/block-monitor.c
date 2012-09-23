/*
 * block-monitor.c
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

#include "block-monitor.h"

#define CONFIG_GROUP_NAME "block-monitor"

#define DEFAULT_LATENCY 250

struct _BlockMonitor
{
  guint currentBlock;

  gboolean started;
  gint src_id;
  guint latency;

  EvdJsonrpcHttpClient *rpc;

  BlockMonitorChangeCb block_change_cb;
  BlockMonitorChangeCb block_change_cb_user_data;
};


static gboolean checkBlock (gpointer user_data);


BlockMonitor *
block_monitor_new (GKeyFile             *config,
                   EvdJsonrpcHttpClient *rpc_client,
                   BlockMonitorChangeCb  callback,
                   gpointer              user_data)
{
  BlockMonitor *self;

  self = g_slice_new0 (BlockMonitor);

  self->rpc = rpc_client;
  g_object_ref (self->rpc);

  self->block_change_cb = callback;
  self->block_change_cb_user_data = user_data;

  self->latency = g_key_file_get_integer (config,
                                          CONFIG_GROUP_NAME,
                                          "latency",
                                          NULL);
  if (self->latency == 0)
    self->latency = DEFAULT_LATENCY;

  return self;
}

void
block_monitor_free (BlockMonitor *self)
{
  if (self == NULL)
    return;

  block_monitor_stop (self);

  g_object_unref (self->rpc);

  g_slice_free (BlockMonitor, self);
}

static void
on_block_count (GObject      *obj,
                GAsyncResult *result,
                gpointer      user_data)
{
  BlockMonitor *self = user_data;
  JsonNode *json_result;
  JsonNode *json_error;
  GError *error = NULL;

  if (! evd_jsonrpc_http_client_call_method_finish (EVD_JSONRPC_HTTP_CLIENT (obj),
                                                    result,
                                                    &json_result,
                                                    &json_error,
                                                    &error))
    {
      g_print ("Get block count failed: %s\n", error->message);
      g_error_free (error);
    }
  else
    {
      guint block;

      block = json_node_get_int (json_result);
      if (block > self->currentBlock)
        {
          self->currentBlock = block;

          if (self->started)
            self->block_change_cb (self,
                                   self->currentBlock,
                                   self->block_change_cb_user_data);
        }

      json_node_free (json_result);
      json_node_free (json_error);
    }

  if (self->started)
    self->src_id = evd_timeout_add (NULL,
                                    self->latency,
                                    G_PRIORITY_HIGH,
                                    checkBlock,
                                    self);
}

static gboolean
checkBlock (gpointer user_data)
{
  BlockMonitor *self = user_data;

  self->src_id = 0;

  evd_jsonrpc_http_client_call_method (self->rpc,
                                       "getblockcount",
                                       NULL,
                                       NULL,
                                       on_block_count,
                                       self);
  return FALSE;
}

void
block_monitor_start (BlockMonitor *self)
{
  if (self->started)
    return;

  if (self->src_id != 0)
    g_source_remove (self->src_id);

  self->started = TRUE;
  checkBlock (self);
}

void
block_monitor_stop (BlockMonitor *self)
{
  if (! self->started)
    return;

  self->started = FALSE;

  if (self->src_id > 0)
    {
      g_source_remove (self->src_id);
      self->src_id = 0;
    }
}
