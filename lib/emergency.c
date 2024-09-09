#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <connui/connui-log.h>

#include "context.h"

gboolean
connui_cell_emergency_call()
{
  GError *error = NULL;
  connui_cell_context *ctx = connui_cell_context_get(NULL);
  gchar *op;

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (dbus_g_proxy_call(ctx->csd_call_proxy, "Create", &error,
                        G_TYPE_STRING, "urn:service:sos",
                        G_TYPE_INVALID,
                        DBUS_TYPE_G_OBJECT_PATH, &op,
                        G_TYPE_INVALID))
  {
    g_free(op);
    connui_cell_context_destroy(ctx);
    return TRUE;
  }

  CONNUI_ERR("Error with DBUS: %s", error->message);
  g_clear_error(&error);
  connui_cell_context_destroy(ctx);

  return FALSE;
}
