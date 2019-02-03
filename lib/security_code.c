#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <libosso.h>
#include <connui/connui-utils.h>
#include <connui/connui-log.h>

#include "context.h"

void
connui_cell_security_code_close(void (*cb)())
{
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_if_fail(ctx != NULL);

  ctx->sec_code_cbs = connui_utils_notify_remove(ctx->sec_code_cbs, cb);

  if (!ctx->sec_code_cbs)
  {
    dbus_g_proxy_disconnect_signal(ctx->phone_sim_security_proxy,
                                   "verify_code_requested",
                                   (GCallback)verify_code_requested_cb, ctx);
  }

  connui_cell_context_destroy(ctx);
}
