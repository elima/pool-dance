/*
 * round-manager.h
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

#ifndef __ROUND_MANAGER_H__
#define __ROUND_MANAGER_H__

#include <glib.h>

#include "event-dispatcher.h"

G_BEGIN_DECLS

typedef struct _RoundManager RoundManager;

RoundManager * round_manager_new                   (GKeyFile         *config,
                                                    EventDispatcher  *event_dispatcher);
void           round_manager_free                  (RoundManager *self);

gboolean       round_manager_start                 (RoundManager  *self,
                                                    GError       **error);

G_END_DECLS

#endif /* __ROUND_MANAGER_H__ */
