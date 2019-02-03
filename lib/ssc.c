#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <libosso.h>
#include <connui/connui-utils.h>
#include <connui/connui-log.h>

#include <string.h>

#include "context.h"

static void
ssc_state_changed_cb(DBusGProxy *proxy, gchar *modem_state,
                     connui_cell_context *ctx)
{
  g_return_if_fail(ctx != NULL && ctx->ssc_state_cbs != NULL);

  connui_utils_notify_notify_POINTER(ctx->ssc_state_cbs, &modem_state);
}

gboolean
connui_cell_ssc_state_register(cell_ssc_state_cb cb, gpointer user_data)
{
  gboolean rv;
  gchar *modem_state = NULL;
  GError *error = NULL;
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (!ctx->ssc_state_cbs)
  {
    dbus_g_proxy_connect_signal(ctx->phone_ssc_proxy, "modem_state_changed_ind",
                                (GCallback)ssc_state_changed_cb, ctx, NULL);
  }

  ctx->ssc_state_cbs =
      connui_utils_notify_add(ctx->ssc_state_cbs, cb, user_data);

  rv = dbus_g_proxy_call(ctx->phone_ssc_proxy, "get_modem_state", &error,
                         G_TYPE_INVALID,
                         G_TYPE_STRING, &modem_state,
                         G_TYPE_INVALID);

  if (rv)
    ssc_state_changed_cb(ctx->phone_ssc_proxy, modem_state, ctx);
  else
    CONNUI_ERR("%s", error->message);

  g_free(modem_state);
  connui_cell_context_destroy(ctx);

  return rv;
}

void
connui_cell_ssc_state_close(cell_ssc_state_cb cb)
{
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_if_fail(ctx != NULL);

  ctx->ssc_state_cbs = connui_utils_notify_remove(ctx->ssc_state_cbs, cb);

  if (!ctx->ssc_state_cbs)
  {
    dbus_g_proxy_disconnect_signal(ctx->phone_ssc_proxy,
                                   "modem_state_changed_ind",
                                   (GCallback)ssc_state_changed_cb, NULL);
  }

  connui_cell_context_destroy(ctx);
}
