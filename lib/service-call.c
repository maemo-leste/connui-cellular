/*
 * service-call.c
 *
 * Copyright (C) 2024 Ivaylo Dimitrov <ivo.g.dimitrov.75@gmail.com>
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <connui/connui-log.h>

#include "context.h"
#include "service-call.h"

__attribute__((visibility("hidden"))) guint
service_call_next_id(connui_cell_context *ctx)
{
  ++ctx->service_call_id;

  return ctx->service_call_id;
}

__attribute__((visibility("hidden"))) service_call_data *
service_call_add(connui_cell_context *ctx, guint id, GCallback cb,
                 gpointer user_data)
{
  service_call_data *scd = g_new0(service_call_data, 1);

  scd->callback = cb;
  scd->user_data = user_data;
  scd->id = id;

  if (!ctx->service_calls)
    ctx->service_calls = g_hash_table_new(g_direct_hash, g_direct_equal);

  g_hash_table_insert(ctx->service_calls, GUINT_TO_POINTER(id), scd);

  return scd;
}

static service_call_data *
service_call_find(connui_cell_context *ctx, guint id)
{
  service_call_data *call =
      g_hash_table_lookup(ctx->service_calls, GUINT_TO_POINTER(id));

  if (!call)
    CONNUI_ERR("Unable to find call ID %d", id);

  return call;
}

__attribute__((visibility("hidden"))) void
service_call_destroy(service_call_data *scd)
{
  if (!scd)
    return;

  if (scd->error)
    g_error_free(scd->error);

  if (scd->cancellable)
    g_object_unref(scd->cancellable);

  if (scd->timeout_id)
  {
    g_source_remove(scd->timeout_id);
    scd->timeout_id = 0;
  }

  g_free(scd);
}

__attribute__((visibility("hidden"))) void
service_call_remove(connui_cell_context *ctx, guint id)
{
  service_call_data *scd = service_call_take(ctx, id);

  if (scd)
    service_call_destroy(scd);
}

__attribute__((visibility("hidden"))) service_call_data *
service_call_take(connui_cell_context *ctx, guint id)
{
  service_call_data *scd;

  if (!ctx->service_calls)
  {
    CONNUI_ERR("Unable to find any service calls to take");
    return NULL;
  }

  scd = service_call_find(ctx, id);

  if (scd)
  {
    g_hash_table_remove(ctx->service_calls, GUINT_TO_POINTER(id));

    if (!g_hash_table_size(ctx->service_calls))
    {
      g_hash_table_destroy(ctx->service_calls);
      ctx->service_call_id = 0;
      ctx->service_calls = NULL;
    }
  }

  return scd;
}
