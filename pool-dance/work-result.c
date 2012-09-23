/*
 * work-result.c
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

#include "work-result.h"

struct _WorkResult
{
  gint ref_count;
  EvdHttpConnection *conn;
  EvdHttpRequest *req;
  JsonNode *work;
  guint invocation_id;
  gboolean stale;
};

WorkResult *
work_result_new (JsonNode *work, guint invocation_id, EvdHttpConnection *conn)
{
  WorkResult *self;

  self = g_slice_new (WorkResult);
  self->ref_count = 1;

  self->work = work;
  self->invocation_id = invocation_id;

  self->conn = conn;
  g_object_ref (conn);

  self->req = evd_http_connection_get_current_request (conn);

  return self;
}

static void
work_result_free (WorkResult *self)
{
  json_node_free (self->work);
  g_object_unref (self->conn);

  g_slice_free (WorkResult, self);
}

WorkResult *
work_result_ref (WorkResult *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->ref_count > 0, NULL);

  g_atomic_int_add (&self->ref_count, 1);

  return self;
}

void
work_result_unref (WorkResult *self)
{
  gint old_ref;

  g_return_if_fail (self != NULL);
  g_return_if_fail (self->ref_count > 0);

  old_ref = g_atomic_int_get (&self->ref_count);
  if (old_ref > 1)
    g_atomic_int_compare_and_exchange (&self->ref_count, old_ref, old_ref - 1);
  else
    work_result_free (self);
}

JsonNode *
work_result_get_json_node (WorkResult *self)
{
  return self->work;
}

guint
work_result_get_invocation_id (WorkResult *self)
{
  return self->invocation_id;
}

EvdHttpConnection *
work_result_get_connection (WorkResult *self)
{
  return self->conn;
}

void
work_result_get_client_info (WorkResult  *self,
                             gchar      **user,
                             gchar      **password,
                             gchar      **remote_addr,
                             gchar      **user_agent)
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

void
work_result_mark_stale (WorkResult *self)
{
  self->stale = TRUE;
}

gboolean
work_result_is_stale (WorkResult *self)
{
  return self->stale;
}
