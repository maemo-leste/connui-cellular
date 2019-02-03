#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <libosso.h>
#include <connui/connui-utils.h>
#include <connui/connui-log.h>

#include "context.h"

static gboolean
sec_code_query(connui_cell_context *ctx, gint code_type, gchar **old_code,
               gchar **new_code, sim_status_data *verification_data)
{
  g_return_val_if_fail(
        ctx != NULL && old_code != NULL && verification_data != NULL, FALSE);

  connui_utils_notify_notify_INT_POINTER_POINTER_POINTER_POINTER(
        ctx->sec_code_cbs, code_type, old_code, new_code, verification_data->cb,
        verification_data->data);

  return *old_code != 0;
}

typedef void (*verify_code_cb_f)(guint, gint, connui_cell_context *);

static gboolean
verify_code(connui_cell_context *ctx, gint code_type, gchar *old_code,
            gchar *new_code, gint *error_value, sim_status_data *data)
{
  GError *error = NULL;
  gint err;

  if ( !new_code )
    new_code = "";

  if (dbus_g_proxy_call(ctx->phone_sim_security_proxy, "verify_code", &error,
                        G_TYPE_UINT, code_type,
                        G_TYPE_STRING, old_code,
                        G_TYPE_STRING, new_code,
                        G_TYPE_INVALID,
                        G_TYPE_INT, &err,
                        G_TYPE_INVALID))
  {
    if (err)
      CONNUI_ERR("verify code returned error: %d", err);
  }
  else
  {
    CONNUI_ERR("Error with DBUS: %s", error->message);
    g_clear_error(&error);
    err = 1;
  }

  if (data && data->cb)
    ((verify_code_cb_f)data->cb)(code_type, err, data->data);


  if (error_value)
    *error_value = err;

  return err == 0;
}

static void
verify_code_requested_cb(DBusGProxy *proxy, int status,
                         connui_cell_context *ctx)
{
  sim_status_data verification_data = {NULL, NULL};
  gchar *new_code = NULL;
  gchar *old_code = NULL;
  gboolean code_queried = FALSE;

  if (status == 5 || status == 3 || status == 8)
  {
    if (sec_code_query(ctx, status, &old_code, &new_code, &verification_data))
      code_queried = TRUE;
  }

  if (!code_queried)
  {
    if (!sec_code_query(ctx, status, &old_code, NULL, &verification_data))
    {
      g_free(old_code);
      return;
    }
    else
      code_queried = TRUE;
  }

  if (code_queried)
    verify_code(ctx, status, old_code, new_code, NULL, &verification_data);

  g_free(old_code);
  g_free(new_code);
}

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
