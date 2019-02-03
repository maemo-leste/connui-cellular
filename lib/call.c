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
