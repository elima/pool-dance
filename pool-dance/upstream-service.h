/*
 * upstream-service.h
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

#ifndef __UPSTREAM_SERVICE_H__
#define __UPSTREAM_SERVICE_H__

#include <evd.h>

G_BEGIN_DECLS

typedef struct _UpstreamService UpstreamService;

typedef void (* UpstreamServiceHasWorkCb) (UpstreamService  *self,
                                           JsonNode         *work,
                                           gpointer          user_data);

UpstreamService *      upstream_service_new              (GKeyFile                  *config,
                                                          UpstreamServiceHasWorkCb   has_work_callback,
                                                          gpointer                   user_data,
                                                          GError                   **error);
void                   upstream_service_free             (UpstreamService *self);

EvdJsonrpcHttpClient * upstream_service_get_rpc          (UpstreamService *self);

void                   upstream_service_notify_new_block (UpstreamService *self,
                                                          guint            block);

gboolean               upstream_service_has_work         (UpstreamService *self);
JsonNode *             upstream_service_get_work         (UpstreamService *self);

G_END_DECLS

#endif /* __UPSTREAM_SERVICE_H__ */
