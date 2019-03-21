#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <libosso.h>
#include <connui/connui-utils.h>
#include <connui/connui-log.h>

#include <string.h>

#include "context.h"

static void
sim_status_changed_cb(DBusGProxy *proxy, guint status,
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
sim_status_cb(DBusGProxy *proxy, guint sim_status, gint error_value,
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

typedef void (*get_sim_status_cb_f)(DBusGProxy *, guint, gint, GError *,
                                    connui_cell_context *);
__attribute__((visibility("hidden"))) void
get_sim_status_cb(DBusGProxy *proxy, DBusGProxyCall *call_id, void *user_data)
{
  sim_status_data *data = (sim_status_data *)user_data;
  gint error_value;
  guint sim_status;
  GError *error = NULL;

  dbus_g_proxy_end_call(proxy, call_id, &error,
                        G_TYPE_UINT, &sim_status,
                        G_TYPE_INT, &error_value,
                        G_TYPE_INVALID);
  ((get_sim_status_cb_f)data->cb)(proxy, sim_status, error_value, error,
                                  data->data);
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
    data->data = ctx;
    ctx->get_sim_status_call =
        dbus_g_proxy_begin_call(ctx->phone_sim_proxy, "get_sim_status",
                                (DBusGProxyCallNotify)get_sim_status_cb, data,
                                destroy_sim_status_data, G_TYPE_INVALID);
  }

  connui_cell_context_destroy(ctx);

  return ctx->get_sim_status_call != NULL;
}

gboolean
connui_cell_sim_is_network_in_service_provider_info(gint *error_value,
                                                    guchar *code)
{
  gboolean rv = FALSE;
  GArray *provider_info = NULL;
  GError *error = NULL;
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (dbus_g_proxy_call(
        ctx->phone_sim_proxy, "get_service_provider_info", &error,
        G_TYPE_INVALID,
        dbus_g_type_get_collection("GArray", G_TYPE_UCHAR), &provider_info,
        G_TYPE_INT, error_value,
        G_TYPE_INVALID))
  {
    if (provider_info)
    {
      int i;

      for (i = 0; i < provider_info->len; i += 4)
      {
        if (!memcmp(&provider_info->data[i], code, 3))
        {
          rv = TRUE;
          break;
        }
      }

      g_array_free(provider_info, TRUE);
    }

    connui_cell_context_destroy(ctx);
  }
  else
  {
    CONNUI_ERR("Error with DBUS in: %s", error->message);
    g_clear_error(&error);

    if (error_value )
      *error_value = 1;

    connui_cell_context_destroy(ctx);
  }

  return rv;
}

gchar *
connui_cell_sim_get_service_provider(guint *name_type, gint *error_value)
{
  GError *error = NULL;
  guint unk2;
  guint unk1;
  gchar *service_provider_name = NULL;
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (dbus_g_proxy_call(
        ctx->phone_sim_proxy, "get_service_provider_name", &error,
        G_TYPE_INVALID,
        G_TYPE_STRING, &service_provider_name,
        G_TYPE_UINT, &unk1,
        G_TYPE_UINT, &unk2,
        G_TYPE_INT, error_value,
        G_TYPE_INVALID))
  {
    *name_type = unk1 | 2 * unk2;
    connui_cell_context_destroy(ctx);
    return service_provider_name;
  }

  CONNUI_ERR("Error with DBUS: %s", error->message);
  g_clear_error(&error);

  if (error_value)
    *error_value = 1;

  connui_cell_context_destroy(ctx);

  return NULL;
}

gboolean
connui_cell_sim_is_locked(gboolean *has_error)
{
  connui_cell_context *ctx = connui_cell_context_get();
  gboolean rv;
  GError *error = NULL;
  gint status = 0;

  g_return_val_if_fail(ctx != NULL, FALSE);

  rv = dbus_g_proxy_call(ctx->phone_sim_proxy, "read_simlock_status", &error,
                         G_TYPE_INVALID,
                         G_TYPE_INT, &status,
                         G_TYPE_INVALID);

  if (rv)
  {
    if (has_error)
      *has_error = status == 0 || status == 8 || status == 7 || status == 5;

    rv = (status == 2 || status == 3 || status == 4);
  }
  else
  {
    CONNUI_ERR("Error with DBUS: %s", error->message);
    g_clear_error(&error);

    if (has_error)
      *has_error = TRUE;
  }

  connui_cell_context_destroy(ctx);

  return rv;
}

gboolean
connui_cell_sim_deactivate_lock(const gchar *pin_code, gint *error_value)
{
  connui_cell_context *ctx = connui_cell_context_get();
  gboolean rv = FALSE;
  GError *error = NULL;
  gint err_val = 0;

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (dbus_g_proxy_call(ctx->phone_sim_security_proxy, "deactivate_simlock",
                        &error,
                        G_TYPE_UCHAR, 7, /* no idea what is this */
                        G_TYPE_STRING, pin_code,
                        G_TYPE_INVALID,
                        G_TYPE_INT, &err_val,
                        G_TYPE_INVALID))
  {
    if (error_value)
      *error_value = err_val;

    rv = err_val == 0;
  }
  else
  {
    CONNUI_ERR("Error with DBUS: %s", error->message);
    g_clear_error(&error);

    if (error_value)
      *error_value = 1;
  }

  connui_cell_context_destroy(ctx);

  return rv;
}

guint
connui_cell_sim_verify_attempts_left(guint code_type, gint *error_value)
{
  connui_cell_context *ctx = connui_cell_context_get();
  GError *error = NULL;
  gint err_val;
  guint attempts_left;

  g_return_val_if_fail(ctx != NULL, 0);

  if (!dbus_g_proxy_call(ctx->phone_sim_security_proxy, "verify_attempts_left",
                         &error,
                         G_TYPE_UINT, code_type,
                         G_TYPE_INVALID,
                         G_TYPE_UINT, &attempts_left,
                         G_TYPE_INT, &err_val,
                         G_TYPE_INVALID))
  {
    CONNUI_ERR("Error with DBUS: %s", error->message);
    g_clear_error(&error);
    err_val = 1;
    attempts_left = 0;
  }

  if (error_value)
    *error_value = err_val;

  connui_cell_context_destroy(ctx);

  return attempts_left;
}
