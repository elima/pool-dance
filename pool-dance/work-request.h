/*
 * work-request.h
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

#ifndef __WORK_REQUEST_H__
#define __WORK_REQUEST_H__

#include <evd.h>

G_BEGIN_DECLS

typedef struct _WorkRequest WorkRequest;

WorkRequest *       work_request_ref                        (WorkRequest *self);
void                work_request_unref                      (WorkRequest *self);

guint               work_request_get_invocation_id          (WorkRequest *self);
EvdHttpConnection * work_request_get_connection             (WorkRequest *self);

void                work_request_get_client_info            (WorkRequest  *self,
                                                             gchar       **user,
                                                             gchar       **password,
                                                             gchar       **remote_addr,
                                                             gchar       **user_agent);

G_END_DECLS

#endif /* __WORK_REQUEST_H__ */
