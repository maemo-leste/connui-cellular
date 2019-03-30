#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <libosso.h>
#include <connui/connui-utils.h>
#include <connui/connui-log.h>

#include "context.h"

static void
connui_cell_call_status_change_cb(DBusGProxy *proxy, gboolean call,
                                  gboolean unk, connui_cell_context *ctx)
{
  g_return_if_fail(ctx != NULL);

  if (proxy)
    connui_utils_notify_notify_BOOLEAN(ctx->call_status_cbs, call);
}

void
connui_cell_call_status_close(cell_call_status_cb cb)
{
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_if_fail(ctx != NULL);

  ctx->call_status_cbs  = connui_utils_notify_remove(ctx->call_status_cbs, cb);

  if (!ctx->call_status_cbs)
  {
    dbus_g_proxy_disconnect_signal(ctx->csd_call_proxy,
                                   "ServerStatus",
                                   (GCallback)connui_cell_call_status_change_cb,
                                   ctx);
  }

  connui_cell_context_destroy(ctx);
}

static void
conn_cell_get_call_instances_cb(DBusGProxy *proxy, GPtrArray *instances,
                                GError *error, gpointer user_data)
{
  connui_cell_context *ctx = user_data;

  g_return_if_fail(ctx != NULL);

  gboolean calls = FALSE;

  if (error)
  {
    CONNUI_ERR("Error: %s", error->message);
    g_clear_error(&error);
    return;
  }

  if (instances )
  {
    int i;

    for (i = 0; i < instances->len; i++)
    {
      GValueArray *val_array = instances->pdata[i];

      if (val_array->n_values > 1 && g_value_get_uint(&val_array->values[i]))
        calls = TRUE;
    }

    g_ptr_array_free(instances, TRUE);
  }

  if (proxy)
    connui_utils_notify_notify_BOOLEAN(ctx->call_status_cbs, calls);

}

typedef void (*call_instances_cb_f)(DBusGProxy *, GPtrArray *, GError *, gpointer);

static void
get_call_instances_cb(DBusGProxy *proxy, DBusGProxyCall *call, void *user_data)
{
  GType g_value_array_type =
      dbus_g_type_get_struct("GValueArray", dbus_g_object_path_get_g_type(),
                             G_TYPE_UINT, G_TYPE_INVALID);
  GType g_ptr_array_type =
      dbus_g_type_get_collection("GPtrArray", g_value_array_type);
  sim_status_data *data = user_data;
  GError *error = NULL;
  GPtrArray *instances;

  dbus_g_proxy_end_call(proxy, call, &error,
                        g_ptr_array_type, &instances,
                        G_TYPE_INVALID);
  ((call_instances_cb_f)data->cb)(proxy, instances, error, data->data);
}

gboolean
connui_cell_call_status_register(cell_call_status_cb cb, gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get();
  sim_status_data *data;

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (!ctx->call_status_cbs)
  {
    dbus_g_proxy_connect_signal(ctx->csd_call_proxy, "ServerStatus",
                                (GCallback)connui_cell_call_status_change_cb,
                                ctx, NULL);
  }

  ctx->call_status_cbs =
      connui_utils_notify_add(ctx->call_status_cbs, cb, user_data);

  data = g_slice_new(sim_status_data);
  data->cb = (GCallback)conn_cell_get_call_instances_cb;
  data->data = ctx;

  dbus_g_proxy_begin_call(ctx->csd_call_proxy, "GetCallInstances",
                          get_call_instances_cb, data,
                          destroy_sim_status_data, G_TYPE_INVALID);

  connui_cell_context_destroy(ctx);

  return TRUE;
}
