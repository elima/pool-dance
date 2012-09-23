/*
 * work-validator.h
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

#ifndef __WORK_VALIDATOR_H__
#define __WORK_VALIDATOR_H__

#include <evd.h>

#include "work-request.h"
#include "work-result.h"
#include "upstream-service.h"

G_BEGIN_DECLS

#define WORK_VALIDATOR_ERROR_DOMAIN "es.bitcoin.pool.WorkValidator.ErrorDomain"
#define WORK_VALIDATOR_ERROR        g_quark_from_string (WORK_VALIDATOR_ERROR_DOMAIN)

typedef enum
{
  WORK_VALIDATOR_ERROR_SUCCESS    = 0,
  WORK_VALIDATOR_ERROR_INVALID    = 1,
  WORK_VALIDATOR_ERROR_STALE      = 2,
  WORK_VALIDATOR_ERROR_DUPLICATED = 3,

  __WORK_VALIDATOR_ERROR_LAST__
} WorkValidatorError;

typedef struct _WorkValidator WorkValidator;

WorkValidator * work_validator_new              (EvdJsonrpcHttpClient *rpc);
void            work_validator_free             (WorkValidator *self);

void            work_validator_validate         (WorkValidator       *self,
                                                 WorkResult          *work_result,
                                                 GCancellable        *cancellable,
                                                 GAsyncReadyCallback  callback,
                                                 gpointer             user_data);
gboolean        work_validator_validate_finish  (WorkValidator  *self,
                                                 GAsyncResult   *result,
                                                 GError        **error);

void            work_validator_track_work_sent  (WorkValidator   *self,
                                                 WorkRequest     *work_request,
                                                 JsonNode        *work_item,
                                                 UpstreamService *upstream_service);

void            work_validator_notify_new_block (WorkValidator *self,
                                                 guint          block);

void            work_validator_set_target       (WorkValidator *self,
                                                 const gchar   *target);

G_END_DECLS

#endif /* __WORK_VALIDATOR_H__ */
