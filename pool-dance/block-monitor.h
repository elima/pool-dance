/*
 * block-monitor.h
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

#ifndef __BLOCK_MONITOR_H__
#define __BLOCK_MONITOR_H__

#include <evd.h>

G_BEGIN_DECLS

typedef struct _BlockMonitor BlockMonitor;

typedef void (* BlockMonitorChangeCb) (BlockMonitor *self,
                                       guint         block,
                                       gpointer      user_data);

BlockMonitor *  block_monitor_new             (GKeyFile             *config,
                                               EvdJsonrpcHttpClient *rpc_client,
                                               BlockMonitorChangeCb  callback,
                                               gpointer              user_data);
void            block_monitor_free            (BlockMonitor *self);

void            block_monitor_start           (BlockMonitor *self);
void            block_monitor_stop            (BlockMonitor *self);

G_END_DECLS

#endif /* __BLOCK_MONITOR_H__ */
