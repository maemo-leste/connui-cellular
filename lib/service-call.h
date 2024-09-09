/*
 * service-call.h
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

#ifndef __CONNUI_INTERNAL_SERVICE_CALL_H_INCLUDED__
#define __CONNUI_INTERNAL_SERVICE_CALL_H_INCLUDED__

typedef struct _service_call_data
{
  guint id;
  gchar *path;

  GCallback callback;
  gpointer user_data;

  GAsyncReadyCallback async_cb;
  gpointer async_data;

  GCancellable *cancellable;
  GError *error;
  guint timeout_id;
}
service_call_data;

guint
service_call_next_id(connui_cell_context *ctx);

service_call_data *
service_call_add(connui_cell_context *ctx, guint id, GCallback cb,
                 gpointer user_data);
void
service_call_destroy(service_call_data *scd);
void
service_call_remove(connui_cell_context *ctx, guint id);
service_call_data *
service_call_take(connui_cell_context *ctx, guint id);

#endif /* __CONNUI_INTERNAL_SERVICE_CALL_H_INCLUDED__ */
