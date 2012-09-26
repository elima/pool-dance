/*
 * file-logger.c
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

#include <gio/gio.h>
#include <string.h>
#include <evd.h>

#include "file-logger.h"

struct _FileLogger
{
  GFile *file;
  GFileOutputStream *stream;

  GCancellable *cancellable;

  GQueue *queue;
  gint priority;

  gboolean flushing;
  GSimpleAsyncResult *async_result;

  gboolean frozen;

  GString *write_all_string;
};

typedef struct
{
  FileLogger *self;
  gchar *copy_file_name;
  guint timeout_before_truncate;
  GSimpleAsyncResult *result;
} TruncateData;

static void write_all (FileLogger *self);

static void
on_flush (GObject      *obj,
          GAsyncResult *res,
          gpointer      user_data)
{
  FileLogger *self = user_data;
  GError *error = NULL;

  if (! g_output_stream_flush_finish (G_OUTPUT_STREAM (obj),
                                      res,
                                      &error))
    {
      g_print ("Error flushing file logger: %s\n", error->message);
      g_error_free (error);
    }

  if (self->async_result != NULL)
    {
      GSimpleAsyncResult *res;

      res = self->async_result;
      self->async_result = NULL;

      g_simple_async_result_complete (res);
      g_object_unref (res);
    }
}

static void
on_log_entry_written (GObject      *obj,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  FileLogger *self = user_data;
  GError *error = NULL;
  gssize size;

  size = g_output_stream_write_finish (G_OUTPUT_STREAM (obj),
                                       result,
                                       &error);
  if (size < 0)
    {
      g_print ("Failed to write to log file: %s", error->message);
      g_error_free (error);
    }
  else if (size > 0)
    {
      g_string_erase (self->write_all_string, 0, size);
    }

  if (self->flushing)
    {
      self->flushing = FALSE;

      if (self->write_all_string->len > 0)
        {
          write_all (self);
          return;
        }
    }

  if (self->async_result != NULL)
    {
      g_output_stream_flush_async (G_OUTPUT_STREAM (self->stream),
                                   G_PRIORITY_DEFAULT,
                                   self->cancellable,
                                   on_flush,
                                   self);
      return;
    }

  write_all (self);
}

static void
write_all (FileLogger *self)
{
  if (g_output_stream_has_pending (G_OUTPUT_STREAM (self->stream)))
    return;

  if (! self->frozen && ! self->flushing && self->async_result == NULL)
    {
      while (g_queue_get_length (self->queue) > 0)
        {
          gchar *entry;

          entry = g_queue_pop_head (self->queue);
          g_string_append (self->write_all_string, entry);
          g_free (entry);
        }
    }

  if (self->write_all_string->len > 0)
    g_output_stream_write_async (G_OUTPUT_STREAM (self->stream),
                                 self->write_all_string->str,
                                 self->write_all_string->len,
                                 self->priority,
                                 self->cancellable,
                                 on_log_entry_written,
                                 self);
}

static gboolean
copy_and_truncate_on_timeout (gpointer user_data)
{
  TruncateData *data = user_data;
  FileLogger *self = data->self;
  GError *error = NULL;
  GFile *target;

  /* these operations are blocking on-purpose */

  /* make a copy of current round file */
  target = g_file_new_for_path (data->copy_file_name);

  if (! g_file_copy (self->file,
                     target,
                     G_FILE_COPY_BACKUP | G_FILE_COPY_OVERWRITE,
                     NULL,
                     NULL,
                     NULL,
                     &error))
    {
      g_simple_async_result_set_from_error (data->result, error);
      g_error_free (error);
    }
  else
    {
      /* truncate file */
      if (! g_seekable_truncate (G_SEEKABLE (self->stream),
                                 0,
                                 NULL,
                                 &error))
        {
          g_simple_async_result_set_from_error (data->result, error);
          g_error_free (error);
        }
    }

  g_simple_async_result_complete (data->result);

  g_object_unref (target);

  g_object_unref (data->result);
  g_free (data->copy_file_name);
  g_slice_free (TruncateData, data);

  file_logger_thaw (self);

  return FALSE;
}

static void
copy_and_truncate_on_logger_flushed (GObject      *obj,
                                     GAsyncResult *res,
                                     gpointer      user_data)
{
  TruncateData *data = user_data;
  GError *error = NULL;

  if (! file_logger_flush_finish (res, &error))
    {
      g_simple_async_result_set_from_error (data->result, error);
      g_error_free (error);

      g_simple_async_result_complete (data->result);

      g_object_unref (data->result);
      g_free (data->copy_file_name);
      g_slice_free (TruncateData, data);
    }
  else
    {
      evd_timeout_add (NULL,
                       data->timeout_before_truncate,
                       G_PRIORITY_HIGH,
                       copy_and_truncate_on_timeout,
                       data);
    }
}

/* public methods */

FileLogger *
file_logger_new_from_stream (GFileOutputStream *stream, gint priority)
{
  FileLogger *self;

  self = g_slice_new0 (FileLogger);

  self->queue = g_queue_new ();
  self->priority = priority;

  self->stream = stream;
  g_object_ref (stream);

  self->cancellable = g_cancellable_new ();

  self->write_all_string = g_string_new ("");

  return self;
}

FileLogger *
file_logger_new (const gchar *file_name, gint priority, GError **error)
{
  FileLogger *self = NULL;
  GFile *file;
  GFileOutputStream *stream;

  file = g_file_new_for_path (file_name);

  stream = g_file_append_to (file,
                             G_FILE_CREATE_NONE,
                             NULL,
                             error);
  if (stream != NULL)
    {
      self = file_logger_new_from_stream (stream, priority);
      g_object_unref (stream);

      self->file = file;
    }
  else
    {
      g_object_unref (file);
    }

  return self;
}

void
file_logger_free (FileLogger *self)
{
  if (self->file != NULL)
    g_object_unref (self->file);

  g_queue_free_full (self->queue, g_free);

  if (self->stream != NULL)
    g_object_unref (self->stream);

  g_object_unref (self->cancellable);

  g_string_free (self->write_all_string, TRUE);

  g_slice_free (FileLogger, self);
}

void
file_logger_log (FileLogger *self, const gchar *entry)
{
  gchar *copy;

  copy = g_strdup_printf ("%s\n", entry);

  g_queue_push_tail (self->queue, copy);

  if (! self->frozen &&
      ! self->flushing &&
      self->async_result == NULL)
    {
      write_all (self);
    }
}

GFileOutputStream *
file_logger_get_stream (FileLogger *self)
{
  return self->stream;
}

void
file_logger_flush (FileLogger          *self,
                   GCancellable        *cancellable,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
  GSimpleAsyncResult *res;

  res = g_simple_async_result_new (NULL,
                                   callback,
                                   user_data,
                                   file_logger_flush);

  if (self->async_result != NULL)
    {
      g_simple_async_result_set_error (res,
                                       G_IO_ERROR,
                                       G_IO_ERROR_PENDING,
                                       "Operation pending");
      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);
      return;
    }

  self->async_result = res;
  self->flushing = TRUE;

  if (g_output_stream_has_pending (G_OUTPUT_STREAM (self->stream)))
    return;

  self->flushing = FALSE;
  write_all (self);
}

gboolean
file_logger_flush_finish (GAsyncResult *result, GError **error)
{
  return
    ! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                             error);
}

void
file_logger_freeze (FileLogger *self)
{
  self->frozen = TRUE;
}

void
file_logger_thaw (FileLogger *self)
{
  self->frozen = FALSE;

  if (! self->frozen &&
      (self->write_all_string->len > 0 ||
       g_queue_get_length (self->queue)) > 0)
    {
      write_all (self);
    }
}

void
file_logger_copy_and_truncate (FileLogger          *self,
                               const gchar         *copy_file_name,
                               guint                timeout_before_truncate,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
  TruncateData *data;
  GSimpleAsyncResult *res;

  res = g_simple_async_result_new (NULL,
                                   callback,
                                   user_data,
                                   file_logger_copy_and_truncate);

  if (self->file == NULL)
    {
      g_simple_async_result_set_error (res,
                                       G_IO_ERROR,
                                       G_IO_ERROR_INVALID_ARGUMENT,
                                       "Can't truncate, no reference to the file");
      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);
      return;
    }

  data = g_slice_new0 (TruncateData);
  data->self = self;
  data->copy_file_name = g_strdup (copy_file_name);
  data->timeout_before_truncate = timeout_before_truncate;
  data->result = res;

  file_logger_freeze (self);

  file_logger_flush (self,
                     cancellable,
                     copy_and_truncate_on_logger_flushed,
                     data);
}

gboolean
file_logger_copy_and_truncate_finish (GAsyncResult *result, GError **error)
{
  return
    ! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                             error);
}
