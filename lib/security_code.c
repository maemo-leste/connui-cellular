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
  gboolean ok = FALSE;
  CONNUI_ERR("verify_code");
  if ( !new_code ) {
      CONNUI_ERR("calling ofono_simmgr_enter_pin");
      /* TODO: map code_type to pin or puk */
      ok = ofono_simmgr_enter_pin(ctx->ofono_sim_manager, "pin", old_code);


      if (data && data->cb)
        ((verify_code_cb_f)data->cb)(code_type, !ok, data->data);

      if (error_value)
          *error_value = !ok;
  } else {
      /* What do we do here, change pin? */
      return ok;
  }

  return ok;
#if 0
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
#endif
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

static gboolean
sec_code_status_cb(gpointer data)
{
  connui_cell_context *ctx = data;
  guint sim_status = connui_cell_sim_get_status();
#if 0
  ctx->get_sim_status_call_1 = NULL;
#endif
  guint sec_code_status;
  sec_code_status = sim_status;

#if 0
  if (error)
  {
    CONNUI_ERR("DBUS error: %s\n", error->message);
    g_clear_error(&error);
  }
  else
  {
    if (error_value)
      CONNUI_ERR("Error in method return: %d\n", error_value);
#endif

    if (sec_code_status == 7)
      verify_code_requested_cb(ctx->phone_sim_security_proxy, 2, ctx);
    else if (sec_code_status == 8)
      verify_code_requested_cb(ctx->phone_sim_security_proxy, 3, ctx);
  }
}

/* in sim.c */
void get_sim_status_cb(DBusGProxy *proxy, DBusGProxyCall *call_id, void *user_data);

gboolean
connui_cell_security_code_register(cell_sec_code_query_cb cb,
                                   gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get();
  gboolean rv = FALSE;

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (!ctx->sec_code_cbs)
  {
    dbus_g_proxy_connect_signal(ctx->phone_sim_security_proxy,
                                "verify_code_requested",
                                (GCallback)verify_code_requested_cb, ctx, NULL);
  }

  ctx->sec_code_cbs = connui_utils_notify_add(ctx->sec_code_cbs, cb, user_data);

  if (!ctx->get_sim_status_call_1)
  {
    sim_status_data *data = g_slice_new(sim_status_data);

    data->cb = (GCallback)sec_code_status_cb;
    data->data = ctx;

    ctx->get_sim_status_call_1 =
        dbus_g_proxy_begin_call(ctx->phone_sim_proxy, "get_sim_status",
                                get_sim_status_cb, data,
                                destroy_sim_status_data, G_TYPE_INVALID);
  }

  if (ctx->get_sim_status_call_1)
    rv = TRUE;

  connui_cell_context_destroy(ctx);

  return rv;
}

void
connui_cell_security_code_close(cell_sec_code_query_cb cb)
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

guint
connui_cell_security_code_get_active(gint *error_value)
{
  connui_cell_context *ctx = connui_cell_context_get();
  guint rv = SIM_SECURITY_CODE_PIN;
  gint err_val;
  GError *error = NULL;

  g_return_val_if_fail(ctx != NULL, SIM_SECURITY_CODE_PIN);

  if (!dbus_g_proxy_call(ctx->phone_sim_security_proxy, "get_active_pin",
                         &error,
                         G_TYPE_INVALID,
                         G_TYPE_UINT, &rv,
                         G_TYPE_INT, &err_val,
                         G_TYPE_INVALID))
  {
    CONNUI_ERR("Error with DBUS in: %s", error->message);
    g_clear_error(&error);
    err_val = 1;
  }

  if (error_value)
    *error_value = err_val;

  connui_cell_context_destroy(ctx);

  return rv;
}

typedef void (*sc_pin_f)(guint, gint, gpointer);

static void
sec_code_fail(security_code_type code_type, gint *error_value,
              sim_status_data *data)
{
  CONNUI_ERR("PIN code entering failure");

  if (error_value)
    *error_value = 3;

  if (data->cb)
    ((sc_pin_f)data->cb)(code_type, 3, data->data);
}

gboolean
connui_cell_security_code_set_active(security_code_type code_type,
                                     gint *error_value)
{
  connui_cell_context *ctx = connui_cell_context_get();
  gboolean rv = FALSE;
  sim_status_data data = {NULL, NULL};
  gint err_val;
  GError *error = NULL;
  gchar *code = NULL;

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (error_value)
    *error_value = 3;

  g_return_val_if_fail(ctx->sec_code_cbs != NULL,
                       (connui_cell_context_destroy(ctx), FALSE));
  g_return_val_if_fail(code_type == SIM_SECURITY_CODE_PIN ||
                       code_type == SIM_SECURITY_CODE_UPIN,
                       (connui_cell_context_destroy(ctx), FALSE));

  if (error_value)
    *error_value = 0;

  if (sec_code_query(ctx, code_type, &code, NULL, &data))
  {
    if (!dbus_g_proxy_call(ctx->phone_sim_security_proxy, "set_active_pin",
                           &error,
           G_TYPE_UINT, (guint)code_type,
           G_TYPE_STRING, code,
           G_TYPE_INVALID,
           G_TYPE_INT, &err_val,
           G_TYPE_INVALID) )
    {
      CONNUI_ERR("Error with DBUS: %s", error->message);
      g_clear_error(&error);
      err_val = 1;
    }

    if (data.cb)
      ((sc_pin_f)data.cb)(code_type, err_val, data.data);

    if (error_value)
      *error_value = err_val;

    rv = err_val == 0;
  }
  else
    sec_code_fail(code_type, error_value, &data);

  g_free(code);
  connui_cell_context_destroy(ctx);

  return rv;
}

gboolean
connui_cell_security_code_get_enabled(gint *error_value)
{
  connui_cell_context *ctx = connui_cell_context_get();
  gboolean rv;
  GError *error = NULL;
  guint enabled = 0;
  gint err_val;

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (!dbus_g_proxy_call(ctx->phone_sim_security_proxy, "get_code_state",
                         &error,
                         G_TYPE_UINT, SIM_SECURITY_CODE_PIN,
                         G_TYPE_INVALID,
                         G_TYPE_UINT, &enabled,
                         G_TYPE_INT, &err_val,
                         G_TYPE_INVALID))
  {
    CONNUI_ERR("Error with DBUS: %s", error->message);
    g_clear_error(&error);
    err_val = 1;
  }

  if (error_value)
    *error_value = err_val;

  rv = enabled == 1;

  connui_cell_context_destroy(ctx);

  return rv;
}

/* export if somebody outside this file needs it */
static gboolean
connui_cell_security_code_set_enabled_internal(connui_cell_context *ctx,
                                               guint active, gint *error_value,
                                               guint code_type,
                                               const gchar *code)
{
  GError *error = NULL;

  if (active)
    active = 1;

  if (!dbus_g_proxy_call(ctx->phone_sim_security_proxy, "set_code_state",
                        &error,
         G_TYPE_UINT, code_type,
         G_TYPE_UINT, active,
         G_TYPE_STRING, code,
         G_TYPE_INVALID,
         G_TYPE_INT, error_value,
         G_TYPE_INVALID))
  {
    CONNUI_ERR("Error with DBUS: %s", error->message);
    g_clear_error(&error);

    if (error_value)
      *error_value = 1;

    return FALSE;
  }

  return TRUE;
}

gboolean
connui_cell_security_code_change(security_code_type code_type,
                                 gint *error_value)
{
  connui_cell_context *ctx = connui_cell_context_get();
  gboolean rv = FALSE;
  sim_status_data data = {NULL, NULL};
  gchar *new_code = NULL;
  gchar *old_code = NULL;

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (error_value)
    *error_value = 3;

  g_return_val_if_fail(ctx->sec_code_cbs != NULL,
                       (connui_cell_context_destroy(ctx), FALSE));
  g_return_val_if_fail(code_type == SIM_SECURITY_CODE_PIN ||
                       code_type == SIM_SECURITY_CODE_PIN2 ||
                       code_type == SIM_SECURITY_CODE_UPIN,
                       (connui_cell_context_destroy(ctx), FALSE));

  if (error_value)
    *error_value = 0;

  if (sec_code_query(ctx, code_type, &old_code, &new_code, &data) &&
      new_code && *new_code)
  {
    if (connui_cell_security_code_get_enabled(error_value) ||
        code_type != SIM_SECURITY_CODE_PIN)
    {
      rv = verify_code(ctx, code_type, old_code, new_code, error_value, &data);
    }
    else if (connui_cell_security_code_set_enabled_internal(
               ctx, 1, error_value, SIM_SECURITY_CODE_PIN, old_code))
    {
      rv = verify_code(ctx, SIM_SECURITY_CODE_PIN, old_code, new_code,
                       error_value, &data);

      if (rv)
      {
        connui_cell_security_code_set_enabled_internal(ctx, 0, error_value,
                                                       SIM_SECURITY_CODE_PIN,
                                                       new_code);
      }
      else
      {
        connui_cell_security_code_set_enabled_internal(ctx, 0, error_value,
                                                       SIM_SECURITY_CODE_PIN,
                                                       old_code);
      }
    }
    else
    {
      if (data.cb)
        ((sc_pin_f)data.cb)(SIM_SECURITY_CODE_PIN, 1, data.data);

      rv = TRUE;
    }
  }
  else
    sec_code_fail(code_type, error_value, &data);

  g_free(old_code);
  g_free(new_code);
  connui_cell_context_destroy(ctx);

  return rv;
}

gboolean
connui_cell_security_code_set_enabled(gboolean active, gint *error_value)
{
  connui_cell_context *ctx = connui_cell_context_get();
  gboolean rv = FALSE;
  sim_status_data data = {NULL, NULL};
  gchar *code = NULL;

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (sec_code_query(ctx, SIM_SECURITY_CODE_PIN, &code, NULL, &data))
  {
    gint err_val;
    connui_cell_security_code_set_enabled_internal(ctx, active, &err_val,
                                                   SIM_SECURITY_CODE_PIN, code);

    if (data.cb)
      ((sc_pin_f)data.cb)(SIM_SECURITY_CODE_PIN, err_val, data.data);

    if (error_value)
      *error_value = err_val;

    rv = err_val == 0;
  }
  else
    sec_code_fail(SIM_SECURITY_CODE_PIN, error_value, &data);

  g_free(code);
  connui_cell_context_destroy(ctx);

  return rv;
}
