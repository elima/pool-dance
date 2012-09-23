/*
 * work-validator.c
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

#include "work-validator.h"

#define TRACK_NONCE_MAX 16

struct _WorkValidator
{
  GMainContext *context;
  EvdJsonrpcHttpClient *rpc;

  GThreadPool *thread_pool;

  GHashTable *work_by_merkle_root;
  GHashTable *work_by_merkle_root_prev;

  guint block_num;
  gchar *block_hash;
  gchar *block_hash_prev;

  guint8 target[32];
};

typedef struct
{
  UpstreamService *upstream_service;
  gchar *user;

  guchar version[8];
  guchar timestamp[8];

  guint nonce_count;
  guint32 nonces[TRACK_NONCE_MAX];

  gboolean possible_stale;
} TrackedWork;


static void resolve_current_block_hash     (WorkValidator *self);
static void validate_work_result_in_thread (GSimpleAsyncResult *res,
                                            WorkValidator      *self);

static void
tracked_work_free (TrackedWork *data)
{
  g_free (data->user);

  g_slice_free (TrackedWork, data);
}

static gboolean
on_work_validated (gpointer user_data)
{
  GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);

  g_simple_async_result_complete_in_idle (res);
  g_object_unref (res);

  return FALSE;
}

static void
swap_hex_bytes2 (gchar *hex, goffset from, gsize len)
{
  gint i;
  gchar t[2];
  goffset head = from * 2;
  goffset tail = head + len * 2 - 2;

  for (i=0; i<len/2; i++)
    {
      memcpy (t, hex + head + i*2, 2);
      memcpy (hex + head + i*2, hex + tail - i*2, 2);
      memcpy (hex + tail - i*2, t, 2);
    }
}

static gboolean
hex_to_bin (const gchar *hex, gsize hex_size, guint8 *bin, GError **error)
{
  gchar byte[3] = {0, };
  gint i;
  gchar *endptr;

  for (i=0; i<hex_size/2; i++)
    {
      memcpy (byte, hex + i*2, 2);
      bin[i] = strtoul (byte, &endptr, 16);
      if (endptr != byte + 2)
        {
          g_set_error (error,
                       WORK_VALIDATOR_ERROR,
                       WORK_VALIDATOR_ERROR_INVALID,
                       "Invalid hex string");
          return FALSE;
        }
    }

  return TRUE;
}

WorkValidator *
work_validator_new (EvdJsonrpcHttpClient *rpc)
{
  WorkValidator *self;

  self = g_slice_new0 (WorkValidator);

  self->context = g_main_context_get_thread_default ();

  self->rpc = rpc;
  g_object_ref (rpc);

  self->thread_pool = g_thread_pool_new ((GFunc) validate_work_result_in_thread,
                                         self,
                                         4,
                                         FALSE,
                                         NULL);

  self->work_by_merkle_root =
    g_hash_table_new_full (g_str_hash,
                           g_str_equal,
                           g_free,
                           (GDestroyNotify) tracked_work_free);

  return self;
}

void
work_validator_free (WorkValidator *self)
{
  if (self == NULL)
    return;

  g_object_unref (self->rpc);
  g_thread_pool_free (self->thread_pool, TRUE, FALSE);
  g_hash_table_unref (self->work_by_merkle_root);
  if (self->work_by_merkle_root_prev != NULL)
    g_hash_table_unref (self->work_by_merkle_root_prev);
  g_free (self->block_hash);

  g_slice_free (WorkValidator, self);
}

static gchar *
work_item_get_data_hex (JsonNode *work_item)
{
  const gchar *data;

  if (JSON_NODE_HOLDS_OBJECT (work_item))
    {
      JsonObject *obj;

      obj = json_node_get_object (work_item);
      data = json_object_get_string_member (obj, "data");
    }
  else
    {
      JsonArray *arr;

      arr = json_node_get_array (work_item);
      data = json_array_get_string_element (arr, 0);
    }

  return g_strdup (data);
}

static gchar *
work_data_get_merkle_root_hex (const gchar *data)
{
  gchar merkle_root[65] = {0, };

  memcpy (merkle_root, data + 72, 64);

  return g_strdup (merkle_root);
}

static guint32
work_data_get_nonce (const gchar *data)
{
  gchar nonce_str[9] = {0, };
  guint32 nonce;

  memcpy (nonce_str, data + 152, 8);
  nonce = (guint32) g_ascii_strtoull (nonce_str, NULL, 16);

  return GUINT32_SWAP_LE_BE (nonce);
}

static TrackedWork *
get_tracked_work_by_data (WorkValidator  *self,
                          const gchar    *data,
                          GError        **error)
{
  TrackedWork *tracked_work;
  gchar *merkle_root;

  merkle_root = work_data_get_merkle_root_hex (data);

  tracked_work = g_hash_table_lookup (self->work_by_merkle_root, merkle_root);
  if (tracked_work == NULL)
    {
      /* check if it belongs to a previously tracked work */
      tracked_work = g_hash_table_lookup (self->work_by_merkle_root_prev,
                                          merkle_root);
      if (tracked_work == NULL)
        g_set_error (error,
                     WORK_VALIDATOR_ERROR,
                     WORK_VALIDATOR_ERROR_INVALID,
                     "Work result for an unknown work item");
      else
        tracked_work->possible_stale = TRUE;
    }

  g_free (merkle_root);

  return tracked_work;
}

static gboolean
check_merkle_root_and_nonce_is_unique (const gchar  *data,
                                       TrackedWork  *tracked_work,
                                       GError      **error)
{
  guint32 nonce;
  gint i;

  nonce = work_data_get_nonce (data);

  for (i=0; i<tracked_work->nonce_count; i++)
    if (nonce == tracked_work->nonces[i])
      {
        g_set_error (error,
                     WORK_VALIDATOR_ERROR,
                     WORK_VALIDATOR_ERROR_DUPLICATED,
                     "Duplicate work result");
        return FALSE;
      }

  tracked_work->nonces[tracked_work->nonce_count] = nonce;
  tracked_work->nonce_count++;

  return TRUE;
}

static gboolean
check_previous_block_hash_matches (const gchar  *data,
                                   const gchar  *block_hash,
                                   GError      **error)
{
  gchar prev_block_hash[65] = {0, };

  memcpy (prev_block_hash, data + 8, 64);

  if (g_strcmp0 (prev_block_hash, block_hash) != 0)
    {
      g_set_error (error,
                   WORK_VALIDATOR_ERROR,
                   WORK_VALIDATOR_ERROR_INVALID,
                   "Previous block hash mismatch");
      return FALSE;
    }

  return TRUE;
}

static gboolean
prevalidate_work_result (WorkValidator *self, WorkResult *work_result, GError **error)
{
  TrackedWork *tracked_work;
  gchar *data = NULL;
  gchar *user = NULL;

  data = work_item_get_data_hex (work_result_get_json_node (work_result));

  /* check length */
  if (strlen (data) != 256)
    {
      g_set_error (error,
                   WORK_VALIDATOR_ERROR,
                   WORK_VALIDATOR_ERROR_INVALID,
                   "Work data is invalid, incorrect length");
      goto out;
    }

  /* check if the merkle root has ever been sent to a miner */
  tracked_work = get_tracked_work_by_data (self, data, error);
  if (tracked_work == NULL)
    goto out;
  else if (tracked_work->possible_stale)
    work_result_mark_stale (work_result);

  /* compare version */
  if (memcmp (tracked_work->version, data + 0, 8) != 0)
    {
      g_set_error (error,
                   WORK_VALIDATOR_ERROR,
                   WORK_VALIDATOR_ERROR_INVALID,
                   "Version mismatch");
      goto out;
    }

  /* compare timestamp */
  if (memcmp (tracked_work->timestamp, data + 136, 8) != 0)
    {
      g_set_error (error,
                   WORK_VALIDATOR_ERROR,
                   WORK_VALIDATOR_ERROR_INVALID,
                   "Timestamp mismatch");
      goto out;
    }

  /* check that merkle-root + nonce is not repeated */
  if (! check_merkle_root_and_nonce_is_unique (data, tracked_work, error))
    goto out;

  /* compare users */
  work_result_get_client_info (work_result, &user, NULL, NULL, NULL);
  if (g_strcmp0 (tracked_work->user, user) != 0)
    {
      g_set_error (error,
                   WORK_VALIDATOR_ERROR,
                   WORK_VALIDATOR_ERROR_INVALID,
                   "User mismatch");
      goto out;
    }

  /* check previous block hash matches */
  if ( (! tracked_work->possible_stale &&
        ! check_previous_block_hash_matches (data, self->block_hash, error)) ||
       (tracked_work->possible_stale &&
        ! check_previous_block_hash_matches (data, self->block_hash_prev, error)) )
    {
      goto out;
    }

 out:
  g_free (user);
  g_free (data);

  return (*error) != NULL;
}

static gint
compare_inverted_hashes (guint8 *hash1, guint8 *hash2)
{
  gint i;

  for (i=0; i<32; i++)
    if (hash1[32-i-1] < hash2[32-i-1])
      return -1;
    else if (hash1[32-i-1] > hash2[32-i-1])
      return 1;

  return 0;
}

static void
validate_work_result_in_thread (GSimpleAsyncResult *res, WorkValidator *self)
{
  WorkResult *work_result;
  gchar *data = NULL;
  GError *error = NULL;
  guint8 data_bin[80];
  gint i;

  GChecksum *chksum;
  gsize hash_len = 32;
  guint8 hash1[32];
  guint8 hash2[32];

  work_result = g_simple_async_result_get_op_res_gpointer (res);

  /* do the blocking part of the validation */

  data = work_item_get_data_hex (work_result_get_json_node (work_result));

  /* remove data padding */
  data[160] = '\0';

  /* swap each 32 bits words */
  for (i=0; i<80; i+=4)
    swap_hex_bytes2 (data, i, 4);

  /* convert data to binary */
  if (! hex_to_bin (data, 160, data_bin, &error))
    goto out;

  /* calculate SHA256(SHA256(data_bin)) */
  chksum = g_checksum_new (G_CHECKSUM_SHA256);

  g_checksum_update (chksum, data_bin, sizeof (data_bin));
  g_checksum_get_digest (chksum, hash1, &hash_len);

  g_checksum_reset (chksum);

  g_checksum_update (chksum, (guchar *) hash1, 32);
  g_checksum_get_digest (chksum, hash2, &hash_len);

  g_checksum_free (chksum);

  /* compare hash with target */
  if (compare_inverted_hashes (hash2, self->target) > 0)
    {
      g_set_error (&error,
                   WORK_VALIDATOR_ERROR,
                   WORK_VALIDATOR_ERROR_INVALID,
                   "Block hash is not less than target");
      goto out;
    }

  /* check is work result is marked as staled */
  if (work_result_is_stale (work_result))
    {
      g_set_error (&error,
                   WORK_VALIDATOR_ERROR,
                   WORK_VALIDATOR_ERROR_STALE,
                   "Block hash belongs to previous block. Stale!");
    }

 out:
  if (error != NULL)
    {
      g_simple_async_result_set_from_error (res, error);
      g_error_free (error);
    }

  evd_timeout_add (self->context,
                   0,
                   G_PRIORITY_DEFAULT,
                   on_work_validated,
                   res);

  g_free (data);
}

void
work_validator_validate (WorkValidator       *self,
                         WorkResult          *work_result,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
  GSimpleAsyncResult *res;
  GError *error = NULL;

  res = g_simple_async_result_new (NULL,
                                   callback,
                                   user_data,
                                   work_validator_validate);

  work_result_ref (work_result);
  g_simple_async_result_set_op_res_gpointer (res,
                                            work_result,
                                            (GDestroyNotify) work_result_unref);

  /* do a quick, non-blocking pre-validation */
  if (! prevalidate_work_result (self, work_result, &error))
    goto out;

 out:
  if (error != NULL)
    {
      g_simple_async_result_set_from_error (res, error);
      g_error_free (error);

      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);
    }
  else
    {
      /* do the blocking part of the validation in a thread */
      g_thread_pool_push (self->thread_pool, res, NULL);
    }
}

gboolean
work_validator_validate_finish (WorkValidator  *self,
                                GAsyncResult   *result,
                                GError        **error)
{
  GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (result);

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                                        NULL,
                                                        work_validator_validate),
                        FALSE);

  return ! g_simple_async_result_propagate_error (res, error);
}

void
work_validator_track_work_sent (WorkValidator   *self,
                                WorkRequest     *work_request,
                                JsonNode        *work_item,
                                UpstreamService *upstream_service)
{
  gchar *data;
  TrackedWork *tracked_work;
  gchar *user;

  gchar *merkle_root;

  data = work_item_get_data_hex (work_item);
  g_assert (strlen (data) == 256);

  merkle_root = work_data_get_merkle_root_hex (data);

  work_request_get_client_info (work_request, &user, NULL, NULL, NULL);

  tracked_work = g_slice_new (TrackedWork);
  tracked_work->upstream_service = upstream_service;
  tracked_work->user = user;
  memcpy (tracked_work->version, data + 0, 8);
  memcpy (tracked_work->timestamp, data + 136, 8);

  memset (tracked_work->nonces, 0, TRACK_NONCE_MAX);
  tracked_work->nonce_count = 0;

  g_hash_table_insert (self->work_by_merkle_root, merkle_root, tracked_work);

  g_free (data);
}

static void
on_block_hash (GObject      *obj,
               GAsyncResult *result,
               gpointer      user_data)
{
  WorkValidator *self = user_data;
  JsonNode *json_result;
  JsonNode *json_error;
  GError *error = NULL;

  if (! evd_jsonrpc_http_client_call_method_finish (EVD_JSONRPC_HTTP_CLIENT (obj),
                                                    result,
                                                    &json_result,
                                                    &json_error,
                                                    &error))
    {
      g_print ("Get block hash failed: %s\n", error->message);
      g_error_free (error);

      /* try again */
      resolve_current_block_hash (self);
    }
  else
    {
      const gchar *block_hash;
      gint i;

      block_hash = json_node_get_string (json_result);

      self->block_hash = g_new0 (gchar, 65);

      /* change uint32 order to ease comparison during validation */
      for (i=0; i<64; i=i+8)
        memcpy (self->block_hash + i, block_hash + 64 - 8 - i, 8);

      json_node_free (json_result);
      json_node_free (json_error);
    }
}

static void
resolve_current_block_hash (WorkValidator *self)
{
  JsonNode *params;
  JsonArray *arr;

  params = json_node_new (JSON_NODE_ARRAY);
  arr = json_array_new ();
  json_node_set_array (params, arr);
  json_array_add_int_element (arr, self->block_num);

  evd_jsonrpc_http_client_call_method (self->rpc,
                                       "getblockhash",
                                       params,
                                       NULL,
                                       on_block_hash,
                                       self);

  json_array_unref (arr);
  json_node_free (params);
}

void
work_validator_notify_new_block (WorkValidator *self, guint block)
{
  self->block_num = block;

  g_free (self->block_hash_prev);
  self->block_hash_prev = self->block_hash;

  if (self->work_by_merkle_root_prev != NULL)
    g_hash_table_unref (self->work_by_merkle_root_prev);

  self->work_by_merkle_root_prev = self->work_by_merkle_root;

  self->work_by_merkle_root =
    g_hash_table_new_full (g_str_hash,
                           g_str_equal,
                           g_free,
                           (GDestroyNotify) tracked_work_free);

  resolve_current_block_hash (self);
}

void
work_validator_set_target (WorkValidator *self, const gchar *target)
{
  hex_to_bin (target, 64, self->target, NULL);
}
