/*
 * event-dispatcher.c
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

#include "event-dispatcher.h"

#include "work-validator.h"
#include "file-logger.h"

static const gchar *work_validator_error_names[__WORK_VALIDATOR_ERROR_LAST__] =
  {
    "SUCCESS",
    "INVALID",
    "STALLED",
    "DUPLICATED"
  };

struct _EventDispatcher
{
  FileLogger *logger;

  EventDispatcherVTable *vtable;
  gpointer vtable_user_data;
  GDestroyNotify vtable_user_data_free_func;
};

EventDispatcher *
event_dispatcher_new (const gchar *log_file_name, GError **error)
{
  EventDispatcher *self;

  self = g_slice_new0 (EventDispatcher);

  if (log_file_name != NULL)
    {
      self->logger = file_logger_new (log_file_name, G_PRIORITY_DEFAULT, error);
      if (self->logger == NULL)
        {
          event_dispatcher_free (self);
          return NULL;
        }
    }

  return self;
}

void
event_dispatcher_free (EventDispatcher *self)
{
  if (self == NULL)
    return;

  if (self->logger != NULL)
    file_logger_free (self->logger);

  g_slice_free (EventDispatcher, self);
}

static gchar *
get_timestamp_str (void)
{
  GDateTime *date;
  GDateTime *date_utc;
  gchar *date_str;

  date = g_date_time_new_now_local ();
  date_utc = g_date_time_to_utc (date);
  g_date_time_unref (date);

  date_str = g_date_time_format (date_utc, "%d/%b/%Y:%H:%M:%S %z");
  g_date_time_unref (date_utc);

  return date_str;
}

typedef struct
{
  gchar *user;
  gchar *passw;
  gchar *remote_addr;
  gchar *user_agent;
} ClientInfo;

static ClientInfo *
get_client_info (WorkRequest *request)
{
  ClientInfo *info;

  info = g_slice_new0 (ClientInfo);

  work_request_get_client_info (request,
                                &info->user,
                                &info->passw,
                                &info->remote_addr,
                                &info->user_agent);

  return info;
}

static void
client_info_free (ClientInfo *info)
{
  g_free (info->user);
  g_free (info->passw);
  g_free (info->remote_addr);
  g_free (info->user_agent);

  g_slice_free (ClientInfo, info);
}

void
event_dispatcher_notify_work_validated (EventDispatcher *self,
                                        WorkResult      *work_result,
                                        guint            error_code,
                                        const gchar     *reason)
{
  ClientInfo *info;

  info = get_client_info ((WorkRequest *) work_result);

  if (self->vtable != NULL)
    {
      self->vtable->work_validated (self,
                                    error_code,
                                    info->user,
                                    info->passw,
                                    self->vtable_user_data);
    }

  if (self->logger != NULL)
    {
      gboolean accepted;
      gchar *date_str;
      gchar *entry;

      date_str = get_timestamp_str ();

      accepted = (error_code == WORK_VALIDATOR_ERROR_SUCCESS);

      if (accepted)
        entry = g_strdup_printf ("[%s]\t%s\t\"%s\"\t\"%s\"\t%s\t\"%s\"",
                                 date_str,
                                 "WORK-ACCEPTED",
                                 info->user,
                                 info->passw,
                                 info->remote_addr,
                                 info->user_agent);
      else
        entry = g_strdup_printf ("[%s]\t%s\t\"%s\"\t\"%s\"\t%s\t\"%s\"\t%s\t\"%s\"",
                                 date_str,
                                 "WORK-REJECTED",
                                 info->user,
                                 info->passw,
                                 info->remote_addr,
                                 info->user_agent,
                                 work_validator_error_names[error_code],
                                 reason);
      g_free (date_str);

      file_logger_log (self->logger, entry);
      g_free (entry);
    }

  client_info_free (info);
}

void
event_dispatcher_notify_work_sent (EventDispatcher *self,
                                   WorkRequest     *work_request,
                                   JsonNode        *work_item)
{
  ClientInfo *info;

  info = get_client_info (work_request);

  /* @TODO: call virtual method */

  if (self->logger != NULL)
    {
      gchar *entry;
      gchar *date_str;

      date_str = get_timestamp_str ();
      entry = g_strdup_printf ("[%s]\tWORK-SERVED\t\"%s\"\t\"%s\"\t%s\t\"%s\"",
                               date_str,
                               info->user,
                               info->passw,
                               info->remote_addr,
                               info->user_agent);
      g_free (date_str);

      file_logger_log (self->logger, entry);
      g_free (entry);
    }

  client_info_free (info);
}

void
event_dispatcher_notify_work_requested (EventDispatcher *self,
                                        WorkRequest     *work_request)
{
  ClientInfo *info;

  info = get_client_info (work_request);

  /* @TODO: call virtual method */

  if (self->logger != NULL)
    {
      gchar *entry;
      gchar *date_str;

      date_str = get_timestamp_str ();

      entry = g_strdup_printf ("[%s]\tWORK-REQUESTED\t\"%s\"\t\"%s\"\t%s\t\"%s\"",
                               date_str,
                               info->user,
                               info->passw,
                               info->remote_addr,
                               info->user_agent);
      g_free (date_str);

      file_logger_log (self->logger, entry);
      g_free (entry);
    }

  client_info_free (info);
}

void
event_dispatcher_notify_work_submitted (EventDispatcher *self,
                                        WorkResult      *work_result)
{
  ClientInfo *info;

  info = get_client_info ((WorkRequest *) work_result);

  /* @TODO: call virtual method */

  if (self->logger != NULL)
    {
      gchar *entry;
      gchar *date_str;

      date_str = get_timestamp_str ();

      entry = g_strdup_printf ("[%s]\tWORK-SUBMITTED\t\"%s\"\t\"%s\"\t%s\t\"%s\"",
                               date_str,
                               info->user,
                               info->passw,
                               info->remote_addr,
                               info->user_agent);
      g_free (date_str);

      file_logger_log (self->logger, entry);
      g_free (entry);
    }

  client_info_free (info);
}

void
event_dispatcher_notify_current_block (EventDispatcher *self, guint block)
{
  /* @TODO: call virtual method */

  if (self->logger != NULL)
    {
      gchar *entry;
      gchar *date_str;

      date_str = get_timestamp_str ();

      entry = g_strdup_printf ("[%s]\tCURRENT-BLOCK\t%u",
                               date_str,
                               block);
      g_free (date_str);

      file_logger_log (self->logger, entry);
      g_free (entry);
    }
}

void
event_dispatcher_notify_block_found (EventDispatcher *self,
                                     guint            block,
                                     WorkResult      *work_result)
{
  ClientInfo *info;

  info = get_client_info ((WorkRequest *) work_result);

  /*
  info = g_slice_new0 (ClientInfo);
  info->user = g_strdup ("pepe");
  info->passw = g_strdup ("lolo");
  info->remote_addr = g_strdup ("127.0.0.1");
  info->user_agent = g_strdup ("tester");
  */

  if (self->vtable->block_found != NULL)
    {
      self->vtable->block_found (self,
                                 block,
                                 info->user,
                                 info->passw,
                                 self->vtable_user_data);
    }

  if (self->logger != NULL)
    {
      gchar *entry;
      gchar *date_str;

      date_str = get_timestamp_str ();

      entry = g_strdup_printf ("[%s]\tBLOCK-FOUND\t%u\t\"%s\"\t\"%s\"",
                               date_str,
                               block,
                               info->user,
                               info->passw);
      g_free (date_str);

      file_logger_log (self->logger, entry);
      g_free (entry);
    }

  client_info_free (info);
}

void
event_dispatcher_set_vtable (EventDispatcher       *self,
                             EventDispatcherVTable *vtable,
                             gpointer               user_data,
                             GDestroyNotify         user_data_free_func)
{
  self->vtable = vtable;
  self->vtable_user_data = user_data;
  self->vtable_user_data_free_func = user_data_free_func;
}
