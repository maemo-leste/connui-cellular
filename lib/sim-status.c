#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <libosso.h>
#include <connui/connui-utils.h>
#include <connui/connui-log.h>

#include "context.h"

static void
sim_status_changed_cb(DBusGProxy *proxy, uint32_t status,
                      connui_cell_context *ctx)
{
  g_return_if_fail(ctx != NULL && ctx->sim_status_cbs != NULL);

  connui_utils_notify_notify_UINT(ctx->sim_status_cbs, status);
}

void
connui_cell_sim_status_close(cell_sim_status_cb cb)
{
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_if_fail(ctx != NULL);

  ctx->sim_status_cbs = connui_utils_notify_remove(ctx->sim_status_cbs, cb);

  if (!ctx->sim_status_cbs)
    dbus_g_proxy_disconnect_signal(ctx->phone_sim_proxy, "status",
                                   (GCallback)sim_status_changed_cb, ctx);

  connui_cell_context_destroy(ctx);
}

static void
sim_status_cb(DBusGProxy *proxy, uint32_t sim_status, int error_value,
              GError *error, connui_cell_context *ctx)
{
  ctx->get_sim_status_call = NULL;

  if (error)
  {
    sim_status_changed_cb(proxy, 0, ctx);
    CONNUI_ERR("%s", error->message);
    g_clear_error(&error);
  }
  else
  {
    if (error_value)
      CONNUI_ERR("Error in method return: %d", error_value);

    sim_status_changed_cb(proxy, sim_status, ctx);
  }
}

typedef void (*get_sim_status_cb_f)(DBusGProxy *, uint32_t, int32_t, GError *,
                                    connui_cell_context *);
static void
get_sim_status_cb(DBusGProxy *proxy, DBusGProxyCall *call_id, void *user_data)
{
  sim_status_data *data = (sim_status_data *)user_data;
  int32_t error_value;
  uint32_t sim_status;
  GError *error = NULL;

  dbus_g_proxy_end_call(proxy, call_id, &error,
                        G_TYPE_UINT, &sim_status,
                        G_TYPE_INT, &error_value,
                        G_TYPE_INVALID);
  ((get_sim_status_cb_f)data->cb)(proxy, sim_status, error_value, error,
                                  data->ctx);
}

gboolean
connui_cell_sim_status_register(cell_sim_status_cb cb, gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (!ctx->sim_status_cbs)
    dbus_g_proxy_connect_signal(ctx->phone_sim_proxy, "status",
                                (GCallback)sim_status_changed_cb, ctx, NULL);

  ctx->sim_status_cbs =
      connui_utils_notify_add(ctx->sim_status_cbs, cb, user_data);

  if (!ctx->get_sim_status_call)
  {

    sim_status_data *data = g_slice_new(sim_status_data);

    data->cb = (GCallback)sim_status_cb;
    data->ctx = ctx;
    ctx->get_sim_status_call =
        dbus_g_proxy_begin_call(ctx->phone_sim_proxy, "get_sim_status",
                                (DBusGProxyCallNotify)get_sim_status_cb, data,
                                destroy_sim_status_data, G_TYPE_INVALID);
  }

  connui_cell_context_destroy(ctx);

  return ctx->get_sim_status_call != NULL;
}
