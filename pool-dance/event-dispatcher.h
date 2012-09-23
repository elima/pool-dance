/*
 * event-dispatcher.h
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

#ifndef __EVENT_DISPATCHER_H__
#define __EVENT_DISPATCHER_H__

#include <evd.h>

#include "work-request.h"
#include "work-result.h"

G_BEGIN_DECLS

typedef struct _EventDispatcher EventDispatcher;

typedef void (* EventDispatcherOnWorkValidated) (EventDispatcher *self,
                                                 guint            result_code,
                                                 const gchar     *user,
                                                 const gchar     *passw,
                                                 gpointer         user_data);
typedef void (* EventDispatcherOnBlockFound) (EventDispatcher *self,
                                              guint            block,
                                              gchar           *user,
                                              gchar           *passw,
                                              gpointer         user_data);

typedef struct
{
  EventDispatcherOnWorkValidated work_validated;
  EventDispatcherOnBlockFound block_found;
} EventDispatcherVTable;


EventDispatcher * event_dispatcher_new                   (const gchar  *log_file_name,
                                                          GError      **error);
void              event_dispatcher_free                  (EventDispatcher *self);

void              event_dispatcher_notify_work_validated (EventDispatcher *self,
                                                          WorkResult      *work_result,
                                                          guint            error_code,
                                                          const gchar     *reason);

void              event_dispatcher_notify_work_sent      (EventDispatcher *self,
                                                          WorkRequest     *work_request,
                                                          JsonNode        *work_item);

void              event_dispatcher_notify_work_requested (EventDispatcher *self,
                                                          WorkRequest     *work_request);

void              event_dispatcher_notify_work_submitted (EventDispatcher *self,
                                                          WorkResult      *work_result);

void              event_dispatcher_notify_current_block  (EventDispatcher *self,
                                                          guint            block);

void              event_dispatcher_notify_block_found    (EventDispatcher *self,
                                                          guint            block,
                                                          WorkResult      *work_result);

void              event_dispatcher_set_vtable            (EventDispatcher       *self,
                                                          EventDispatcherVTable *vtable,
                                                          gpointer               user_data,
                                                          GDestroyNotify         user_data_free_func);

G_END_DECLS

#endif /* __EVENT_DISPATCHER_H__ */
