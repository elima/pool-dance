/*
 * pool-server.h
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

#ifndef __POOL_SERVER_H__
#define __POOL_SERVER_H__

#include <evd.h>

#include "work-request.h"
#include "work-result.h"

G_BEGIN_DECLS

typedef struct _PoolServer PoolServer;

typedef void (* PoolServerGetworkCb) (PoolServer  *self,
                                      WorkRequest *work_request,
                                      gpointer     user_data);

typedef void (* PoolServerPutworkCb) (PoolServer *self,
                                      WorkResult *work_result,
                                      gpointer    user_data);

PoolServer *    pool_server_new              (GKeyFile            *config,
                                              PoolServerGetworkCb  getwork_callback,
                                              PoolServerPutworkCb  putwork_callback,
                                              gpointer             user_data);
void            pool_server_free             (PoolServer *self);

void            pool_server_start            (PoolServer *self);

EvdWebService * pool_server_get_web_service  (PoolServer *self);

void            pool_server_notify_new_block (PoolServer *self, guint block);

gboolean        pool_server_need_work        (PoolServer *self);
WorkRequest *   pool_server_get_work_request (PoolServer *self);
gboolean        pool_server_send_work_item   (PoolServer  *self,
                                              WorkRequest *work_request,
                                              JsonNode    *work_item);

void            pool_server_respond_putwork  (PoolServer  *self,
                                              WorkResult  *work_result,
                                              gboolean     accepted,
                                              const gchar *reason);

G_END_DECLS

#endif /* __POOL_SERVER_H__ */
