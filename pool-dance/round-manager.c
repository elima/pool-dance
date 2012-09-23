/*
 * round-manager.c
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

#include <time.h>

#include "round-manager.h"

#include "file-logger.h"

#define CONFIG_GROUP_NAME "round-manager"

#define DEFAULT_ROUND_FILE "/var/lib/pool-dance/round"

struct _RoundManager
{
  FileLogger *logger;
  gchar *log_file_name;
  GFile *log_file;

  EventDispatcherVTable vtable;
};

static gboolean    init_log_file     (RoundManager  *self,
                                      const gchar   *log_file_name,
                                      GError       **error);

static void        on_work_validated (EventDispatcher *self,
                                      guint            result_code,
                                      const gchar     *user,
                                      const gchar     *passw,
                                      gpointer         user_data);
static void        on_block_found    (EventDispatcher *event_dispatcher,
                                      guint            block,
                                      gchar           *user,
                                      gchar           *passw,
                                      gpointer         user_data);

RoundManager *
round_manager_new (GKeyFile *config, EventDispatcher *event_dispatcher)
{
  RoundManager *self;

  self = g_slice_new0 (RoundManager);

  self->log_file_name = g_key_file_get_string (config,
                                               CONFIG_GROUP_NAME,
                                               "round-file",
                                               NULL);
  if (self->log_file_name == NULL || self->log_file_name[0] == '\0')
    self->log_file_name = g_strdup (DEFAULT_ROUND_FILE);

  self->log_file = g_file_new_for_path (self->log_file_name);

  event_dispatcher_set_vtable (event_dispatcher,
                               &self->vtable,
                               self,
                               NULL);

  self->vtable.work_validated = on_work_validated;
  self->vtable.block_found = on_block_found;

  return self;
}

void
round_manager_free (RoundManager *self)
{
  if (self == NULL)
    return;

  g_object_unref (self->log_file);

  if (self->logger != NULL)
    file_logger_free (self->logger);
  g_free (self->log_file_name);

  g_slice_free (RoundManager, self);
}

gboolean
round_manager_start (RoundManager *self, GError **error)
{
  return init_log_file (self, self->log_file_name, error);
}

static void
log_started (RoundManager *self)
{
  gchar *entry;

  entry = g_strdup_printf ("%lu\t%s", time (NULL), "STARTED");
  file_logger_log (self->logger, entry);
  g_free (entry);
}

static void
log_resume (RoundManager *self)
{
  gchar *entry;

  entry = g_strdup_printf ("%lu\t%s", time (NULL), "RESUMED");
  file_logger_log (self->logger, entry);
  g_free (entry);
}

static gboolean
init_log_file (RoundManager *self, const gchar *log_file_name, GError **error)
{
  GFileOutputStream *stream;
  GError *_error = NULL;
  gboolean result = TRUE;

  stream = g_file_create (self->log_file,
                          G_FILE_CREATE_PRIVATE,
                          NULL,
                          &_error);

  if (stream == NULL)
    {
      if (_error->code == G_IO_ERROR_EXISTS)
        {
          /* file already existed */
          self->logger = file_logger_new (log_file_name,
                                          G_PRIORITY_HIGH,
                                          error);
          if (self->logger == NULL)
            result = FALSE;
          else
            log_resume (self);

          g_error_free (_error);
        }
      else
        {
          /* other error, fail */
          g_propagate_error (error, _error);
          result = FALSE;
        }
    }
  else
    {
      /* file created */
      self->logger = file_logger_new_from_stream (stream, G_PRIORITY_HIGH);

      log_started (self);
    }

  return result;
}

static void
on_work_validated (EventDispatcher *event_dispatcher,
                   guint            result_code,
                   const gchar     *user,
                   const gchar     *passw,
                   gpointer         user_data)
{
  RoundManager *self = user_data;
  gchar *entry;

  entry = g_strdup_printf ("%lu\t%s\t%u\t\"%s\"\t\"%s\"",
                           time (NULL),
                           "SHARE",
                           result_code,
                           user,
                           passw);
  file_logger_log (self->logger, entry);
  g_free (entry);
}

static void
on_copy_and_truncate (GObject      *obj,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  RoundManager *self = user_data;
  GError *error = NULL;

  if (! file_logger_copy_and_truncate_finish (res, &error))
    {
      g_warning ("Failed to truncate round log file: %s\n", error->message);
      g_error_free (error);
      return;
    }

  log_started (self);
}

static void
on_block_found (EventDispatcher *event_dispatcher,
                guint            block,
                gchar           *user,
                gchar           *passw,
                gpointer         user_data)
{
  RoundManager *self = user_data;
  gchar *entry;
  gchar *file_name;

  entry = g_strdup_printf ("%lu\t%s\t%u\t\"%s\"\t\"%s\"",
                           time (NULL),
                           "BLOCK",
                           block,
                           user,
                           passw);
  file_logger_log (self->logger, entry);
  g_free (entry);

  file_name = g_strdup_printf ("%s.%u", self->log_file_name, block);

  file_logger_copy_and_truncate (self->logger,
                                 file_name,
                                 1000,
                                 NULL,
                                 on_copy_and_truncate,
                                 self);
  g_free (file_name);
}
