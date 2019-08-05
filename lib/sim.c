#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <libosso.h>
#include <connui/connui-utils.h>
#include <connui/connui-log.h>

#include <string.h>

#include "context.h"
#include "ofono-context.h"

/* TODO: example of getter, rather than property changed handler */
void debug_sim(OfonoSimMgr* sim) {
	CONNUI_ERR("debug_sim");
	guint i;
    OfonoObject* obj = ofono_simmgr_object(sim);

    /* XXX: Shouldn't these keys be freed ? */
    GPtrArray* keys = ofono_object_get_property_keys(obj);
	CONNUI_ERR("debug_sim keys len %d", keys->len);
    for (i=0; i<keys->len; i++) {
        const char* key = keys->pdata[i];
        if (1) {
            /* XXX: shouldn't these gvariants be freed / dereffed? */
            GVariant* v = ofono_object_get_property(obj, key, NULL);
            gchar* text = g_variant_print(v, FALSE);
            CONNUI_ERR("%s: %s\n", key, text);
            g_free(text);
        } else {
            CONNUI_ERR("%s\n", key);
        }
    }
	CONNUI_ERR("debug_sim_done");
}

void present_changed(OfonoSimMgr* sender, void* arg) {
    CONNUI_ERR("** present changed");

    connui_cell_context *ctx = arg;
    // XXX: free obj? deref obj? nothing?
    OfonoObject* obj = ofono_simmgr_object(ctx->ofono_sim_manager);

    // XXX: deref variant?
    GVariant* v = ofono_object_get_property(obj, "Present", NULL);
    gboolean present;
    g_variant_get(v, "b", &present);
    CONNUI_ERR("** present: %d", present);

    // XXX: yeah, ugly.
    guint present_status;
    if (present)
        present_status = 1;
    else
        present_status = 0;

    CONNUI_ERR("Sending present notification with %d", present_status);
    connui_utils_notify_notify_UINT(ctx->sim_status_cbs, present_status);
}

void set_sim(connui_cell_context *ctx) {
    CONNUI_ERR("set_sim");
    debug_sim(ctx->ofono_sim_manager);

    ctx->ofono_sim_present_changed_valid_id = ofono_simmgr_add_present_changed_handler(ctx->ofono_sim_manager,
            present_changed, ctx);

    /* XXX: first manual invoke */
    present_changed(ctx->ofono_sim_manager, ctx);

    return;
}

void release_sim(connui_cell_context *ctx) {
    CONNUI_ERR("release_sim");

    ofono_simmgr_remove_handler(ctx->ofono_sim_manager, ctx->ofono_sim_present_changed_valid_id);
    ctx->ofono_sim_present_changed_valid_id = 0;

    return;
}


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
  CONNUI_ERR("connui_cell_sim_status_register");

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
    CONNUI_ERR("connui_cell_sim_get_service_provider");
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_val_if_fail(ctx != NULL, FALSE);

  // XXX: free obj? deref obj? nothing?
  OfonoObject* obj = ofono_simmgr_object(ctx->ofono_sim_manager);
  // XXX: deref variant?
  // TODO: requires modem to be online (!) -- otherwise the variant is NULL
  GVariant* v = ofono_object_get_property(obj, "ServiceProviderName", NULL);

  gchar* name = NULL;
  if (!v) {
      CONNUI_ERR("Variant for ServiceProviderName is NULL");
  } else {
      g_variant_get(v, "s", &name);
      /* TODO: something/someone has to free this string */
  }

  if (!name)
      *error_value = 1;

  connui_cell_context_destroy(ctx);

  return name;
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
    /* TODO: Use `Retries` property for the right code type, see
     * include/connui-cellular.h for the SIM code types - SIM_SECURITY_CODE_PIN
     * and other security_code_type ; map to ofono types. */
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
