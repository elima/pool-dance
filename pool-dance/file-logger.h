/*
 * file-logger.h
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

#ifndef __FILE_LOGGER_H__
#define __FILE_LOGGER_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct _FileLogger FileLogger;

FileLogger *        file_logger_new                      (const gchar  *file_name,
                                                          gint          priority,
                                                          GError      **error);
FileLogger *        file_logger_new_from_stream          (GFileOutputStream *stream,
                                                          gint               priority);

void                file_logger_free                     (FileLogger *self);

void                file_logger_log                      (FileLogger  *self,
                                                          const gchar *entry);

GFileOutputStream * file_logger_get_stream               (FileLogger *self);

void                file_logger_flush                    (FileLogger          *self,
                                                          GCancellable        *cancellable,
                                                          GAsyncReadyCallback  callback,
                                                          gpointer             user_data);
gboolean            file_logger_flush_finish             (GAsyncResult  *result,
                                                          GError       **error);

void                file_logger_freeze                   (FileLogger *self);
void                file_logger_thaw                     (FileLogger *self);

void                file_logger_copy_and_truncate        (FileLogger          *self,
                                                          const gchar         *copy_file_name,
                                                          guint                timeout_before_truncate,
                                                          GCancellable        *cancellable,
                                                          GAsyncReadyCallback  callback,
                                                          gpointer             user_data);
gboolean            file_logger_copy_and_truncate_finish (GAsyncResult  *result,
                                                          GError       **error);

G_END_DECLS

#endif /* __FILE_LOGGER_H__ */
