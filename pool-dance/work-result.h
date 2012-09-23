/*
 * work-result.h
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

#ifndef __WORK_RESULT_H__
#define __WORK_RESULT_H__

#include <evd.h>

G_BEGIN_DECLS

typedef struct _WorkResult WorkResult;

WorkResult *        work_result_new                        (JsonNode          *work,
                                                            guint              invocation_id,
                                                            EvdHttpConnection *conn);
WorkResult *        work_result_ref                        (WorkResult *self);
void                work_result_unref                      (WorkResult *self);

JsonNode *          work_result_get_json_node              (WorkResult *self);
guint               work_result_get_invocation_id          (WorkResult *self);
EvdHttpConnection * work_result_get_connection             (WorkResult *self);

void                work_result_get_client_info            (WorkResult  *self,
                                                            gchar      **user,
                                                            gchar      **password,
                                                            gchar      **remote_addr,
                                                            gchar      **user_agent);

void                work_result_mark_stale                  (WorkResult *self);
gboolean            work_result_is_stale                    (WorkResult *self);

G_END_DECLS

#endif /* __WORK_RESULT_H__ */
