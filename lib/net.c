#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <libosso.h>
#include <connui/connui-utils.h>
#include <connui/connui-log.h>

#include <string.h>

#include "context.h"

struct _service_call
{
  DBusGProxy *proxy;
  DBusGProxyCall *proxy_call;
  service_call_cb_f cb;
  gpointer data;
  int unk;
};

typedef struct _service_call service_call;

// Cellular system states
#define NETWORK_CS_INACTIVE         0x00
#define NETWORK_CS_ACTIVE           0x01
#define NETWORK_CS_STATE_UNKNOWN    0x02

// Cellular system operation modes
#define NETWORK_CS_OP_MODE_NORMAL    0x00   // CS is in normal operation mode
#define NETWORK_CS_OP_MODE_GAN_ONLY  0x01   // CS is in GAN only operation mode
#define NETWORK_CS_OP_MODE_UNKNOWN   0x02

static void
net_signal_strength_change_notify(connui_cell_context *ctx)
{
  connui_utils_notify_notify_POINTER(ctx->net_status_cbs, &ctx->state);
}

static void
net_reg_status_change_cb(DBusGProxy *proxy, guchar reg_status,
                         gushort current_lac, guint current_cell_id,
                         guint operator_code, guint country_code,
                         guchar type, guchar supported_services,
                         connui_cell_context *ctx)
{
  g_return_if_fail(ctx != NULL);

  if (reg_status != -1)
    ctx->state.reg_status = reg_status;

  if (supported_services != -1)
    ctx->state.supported_services = supported_services;

  ctx->state.lac = current_lac;
  ctx->state.cell_id = current_cell_id;

  if (!ctx->state.network)
    ctx->state.network = g_new0(cell_network, 1);

  ctx->state.network->country_code = g_strdup_printf("%d", country_code);
  ctx->state.network->network_type = type;
  ctx->state.network->operator_code = g_strdup_printf("%d", operator_code);

  if (proxy)
    net_signal_strength_change_notify(ctx);
}

static void
net_signal_strength_change_cb(DBusGProxy *proxy, guchar signals_bar,
                              guchar rssi_in_dbm, connui_cell_context *ctx)
{
  g_return_if_fail(ctx != NULL);

  ctx->state.network_signals_bar = signals_bar;

  if (proxy)
      net_signal_strength_change_notify(ctx);
}

static void
net_rat_change_cb(DBusGProxy *proxy, guchar rat_name, connui_cell_context *ctx)
{
  g_return_if_fail(ctx != NULL);

  ctx->state.rat_name = rat_name;

  if (proxy)
    net_signal_strength_change_notify(ctx);
}

static void
net_radio_info_change_cb(DBusGProxy *proxy, guchar network_radio_state,
                         guchar network_hsdpa_allocated,
                         guchar network_hsupa_allocated,
                         connui_cell_context *ctx)
{
  g_return_if_fail(ctx != NULL);

  ctx->state.network_hsdpa_allocated = network_hsdpa_allocated;

  if (proxy)
    net_signal_strength_change_notify(ctx);
}

static void
net_cell_info_change_cb(DBusGProxy *proxy, guchar network_cell_type,
                        gushort network_current_lac,
                        guint network_current_cell_id,
                        guint network_operator_code,
                        guint network_country_code,
                        guchar network_service_status,
                        guchar network_type, connui_cell_context *ctx)
{
  g_return_if_fail(ctx != NULL);

  /* FIXME - what about NETWORK_REG_STATUS_ROAM_BLINK? */
  if (ctx->state.reg_status == NETWORK_REG_STATUS_HOME ||
      ctx->state.reg_status == NETWORK_REG_STATUS_ROAM)
  {
    net_reg_status_change_cb(NULL, -1, network_current_lac,
                             network_current_cell_id, network_operator_code,
                             network_country_code, network_type, -1, ctx);
    ctx->state.network->service_status = network_service_status;

    if (proxy)
      net_signal_strength_change_notify(ctx);
  }
}

static void
operator_name_change_cb(DBusGProxy *proxy, guchar operator_name_type,
                        const char *operator_name,
                        const char *alternative_operator_name,
                        guint network_operator_code,
                        guint network_country_code, connui_cell_context *ctx)
{
  g_return_if_fail(ctx != NULL);

  ctx->state.operator_name_type = operator_name_type;
  g_free(ctx->state.operator_name);
  g_free(ctx->state.alternative_operator_name);
  ctx->state.operator_name = g_strdup(operator_name);
  ctx->state.alternative_operator_name = g_strdup(alternative_operator_name);

  if (proxy)
    net_signal_strength_change_notify(ctx);
}

void
connui_cell_net_status_close(cell_network_state_cb cb)
{
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_if_fail(ctx != NULL);

  ctx->net_status_cbs = connui_utils_notify_remove(ctx->net_status_cbs, cb);

  if (!ctx->net_status_cbs)
  {
    dbus_g_proxy_disconnect_signal(ctx->phone_net_proxy,
                                   "registration_status_change",
                                   (GCallback)net_reg_status_change_cb, ctx);
    dbus_g_proxy_disconnect_signal(ctx->phone_net_proxy,
                                   "signal_strength_change",
                                   (GCallback)net_signal_strength_change_cb,
                                   ctx);
    dbus_g_proxy_disconnect_signal(ctx->phone_net_proxy,
                                   "radio_access_technology_change",
                                   (GCallback)net_rat_change_cb, ctx);
    dbus_g_proxy_disconnect_signal(ctx->phone_net_proxy, "radio_info_change",
                                   (GCallback)net_radio_info_change_cb, ctx);
    dbus_g_proxy_disconnect_signal(ctx->phone_net_proxy, "cell_info_change",
                                   (GCallback)net_cell_info_change_cb, ctx);
    dbus_g_proxy_disconnect_signal(ctx->phone_net_proxy, "operator_name_change",
                                   (GCallback)operator_name_change_cb, ctx);
  }

  connui_cell_context_destroy(ctx);
}

typedef void (*net_get_rat_cb_f)(DBusGProxy *, guchar, int32_t, GError *,
                                 connui_cell_context *);

static void
get_radio_access_technology_cb(DBusGProxy *proxy, DBusGProxyCall *call_id,
                               void *user_data)
{
  sim_status_data *data = user_data;
  GError *error = NULL;
  int32_t error_value;
  guchar network_rat_name;

  dbus_g_proxy_end_call(proxy, call_id, &error,
                        G_TYPE_UCHAR, &network_rat_name,
                        G_TYPE_INT, &error_value,
                        G_TYPE_INVALID);

  ((net_get_rat_cb_f)data->cb)(proxy, network_rat_name, error_value, error,
                               data->data);
}

static void
net_get_rat_cb(DBusGProxy *proxy, guchar network_rat_name, int32_t error_value,
               GError *error, connui_cell_context *ctx)
{
  g_return_if_fail(ctx != NULL);

  ctx->get_registration_status_call = NULL;

  if (error)
  {
    CONNUI_ERR("%s", error->message);
    g_clear_error(&error);
  }
  else
  {
    if (error_value)
      CONNUI_ERR("Error in method return: %d", error_value);

    net_rat_change_cb(NULL, network_rat_name, ctx);
    net_signal_strength_change_notify(ctx);
  }
}

typedef void (*net_get_reg_status_cb_f)(DBusGProxy *, guchar, guint,
                                         guint, guint, guint, guchar,
                                         guchar, int32_t, GError *,
                                         connui_cell_context *);
static void
get_registration_status_cb(DBusGProxy *proxy, DBusGProxyCall *call_id,
                           void *user_data)
{
  sim_status_data *data = user_data;
  int32_t error_value;
  guint network_country_code;
  guint network_operator_code;
  guint network_current_cell_id;
  guint network_current_lac;
  guchar network_supported_services;
  guchar network_type;
  guchar network_reg_status;
  GError *error = NULL;

  dbus_g_proxy_end_call(proxy, call_id, &error,
                        G_TYPE_UCHAR, &network_reg_status,
                        G_TYPE_UINT, &network_current_lac,
                        G_TYPE_UINT, &network_current_cell_id,
                        G_TYPE_UINT, &network_operator_code,
                        G_TYPE_UINT, &network_country_code,
                        G_TYPE_UCHAR, &network_type,
                        G_TYPE_UCHAR, &network_supported_services,
                        G_TYPE_INT, &error_value,
                        G_TYPE_INVALID);

  ((net_get_reg_status_cb_f)data->cb)(proxy, network_reg_status,
                                      network_current_lac,
                                      network_current_cell_id,
                                      network_operator_code,
                                      network_country_code, network_type,
                                      network_supported_services, error_value,
                                      error, data->data);
}

static void
net_get_signal_strength_cb(DBusGProxy *proxy, guchar network_signals_bar,
                           guchar network_rssi_in_dbm, int32_t error_value,
                           GError *error, connui_cell_context *ctx)
{
  g_return_if_fail(ctx != NULL);

  ctx->get_registration_status_call = NULL;

  if (error)
  {
    CONNUI_ERR("%s", error->message);
    g_clear_error(&error);
    return;
  }

  if (error_value)
    CONNUI_ERR("Error in method return: %d", error_value);

  net_signal_strength_change_cb(NULL, network_signals_bar, network_rssi_in_dbm,
                                ctx);

  if (!ctx->state.rat_name)
  {
    sim_status_data *data = g_slice_new(sim_status_data);

    data->cb = (GCallback)net_get_rat_cb;
    data->data = ctx;
    ctx->get_registration_status_call =
        dbus_g_proxy_begin_call(ctx->phone_net_proxy,
                                "get_radio_access_technology",
                                get_radio_access_technology_cb, data,
                                destroy_sim_status_data, G_TYPE_INVALID);
  }
  else
    net_signal_strength_change_notify(ctx);
}

typedef void (*net_get_signal_strength_cb_f)(DBusGProxy *, guchar, guchar,
                                             int32_t, GError *,
                                             connui_cell_context *);

static void
get_signal_strength_cb(DBusGProxy *proxy, DBusGProxyCall *call_id,
                       void *user_data)
{
  sim_status_data *data = user_data;
  GError *error = NULL;
  guchar network_rssi_in_dbm;
  guchar network_signals_bar;
  int32_t error_value;

  dbus_g_proxy_end_call(proxy, call_id, &error,
                        G_TYPE_UCHAR, &network_signals_bar,
                        G_TYPE_UCHAR, &network_rssi_in_dbm,
                        G_TYPE_INT, &error_value, G_TYPE_INVALID);

  ((net_get_signal_strength_cb_f)data->cb)(proxy, network_signals_bar,
                                           network_rssi_in_dbm, error_value,
                                           error, data->data);
}

static void
net_get_reg_status_cb(DBusGProxy *proxy, guchar reg_status,
                      gushort current_lac, guint current_cell_id,
                      guint operator_code, guint country_code,
                      guchar type, guchar supported_services,
                      int32_t error_value, GError *error,
                      connui_cell_context *ctx)
{
  g_return_if_fail(ctx != NULL);

  ctx->get_registration_status_call = NULL;

  if (error)
  {
    CONNUI_ERR("%s", error->message);
    g_clear_error(&error);
    return;
  }

  if (error_value)
    CONNUI_ERR("Error in method return: %d", error_value);

  net_reg_status_change_cb(NULL, reg_status, current_lac, current_cell_id,
                           operator_code, country_code, type,
                           supported_services, ctx);

  if (!ctx->state.network_signals_bar || !ctx->state.rat_name)
  {
    sim_status_data *data = g_slice_new(sim_status_data);
    GCallback cb;
    DBusGProxyCallNotify notify;
    const char *method;

    if (!ctx->state.network_signals_bar)
    {
      cb = (GCallback)net_get_signal_strength_cb;
      notify = get_signal_strength_cb;
      method = "get_signal_strength";
    }
    else
    {
      cb = (GCallback)net_get_rat_cb;
      notify = get_radio_access_technology_cb;
      method = "get_radio_access_technology";
    }

    data->cb = cb;
    data->data = ctx;
    ctx->get_registration_status_call =
        dbus_g_proxy_begin_call(ctx->phone_net_proxy, method, notify,
                                data, destroy_sim_status_data, G_TYPE_INVALID);
  }
  else
    net_signal_strength_change_notify(ctx);
}

gboolean
connui_cell_net_status_register(cell_network_state_cb cb, gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (!ctx->net_status_cbs)
  {
    dbus_g_proxy_connect_signal(ctx->phone_net_proxy,
                                "registration_status_change",
                                (GCallback)net_reg_status_change_cb, ctx, NULL);
    dbus_g_proxy_connect_signal(ctx->phone_net_proxy,
                                "signal_strength_change",
                                (GCallback)net_signal_strength_change_cb, ctx,
                                NULL);
    dbus_g_proxy_connect_signal(ctx->phone_net_proxy,
                                "radio_access_technology_change",
                                (GCallback)net_rat_change_cb, ctx, NULL);
    dbus_g_proxy_connect_signal(ctx->phone_net_proxy, "radio_info_change",
                                (GCallback)net_radio_info_change_cb, ctx,
                                NULL);
    dbus_g_proxy_connect_signal(ctx->phone_net_proxy, "cell_info_change",
                                (GCallback)net_cell_info_change_cb, ctx, NULL);
    dbus_g_proxy_connect_signal(ctx->phone_net_proxy, "operator_name_change",
                                (GCallback)operator_name_change_cb, ctx, NULL);
  }

  ctx->net_status_cbs = connui_utils_notify_add(ctx->net_status_cbs,
                                                (connui_utils_notify)cb,
                                                user_data);
  if (!ctx->get_registration_status_call)
  {
    sim_status_data *data = g_slice_new(sim_status_data);

    data->cb = (GCallback)net_get_reg_status_cb;
    data->data = ctx;
    ctx->get_registration_status_call =
        dbus_g_proxy_begin_call(ctx->phone_net_proxy, "get_registration_status",
                                get_registration_status_cb, data,
                                destroy_sim_status_data, G_TYPE_INVALID);
  }

  connui_cell_context_destroy(ctx);

  return ctx->get_registration_status_call != NULL;
}

gchar *
connui_cell_net_get_operator_name(cell_network *network, gboolean long_name,
                                  gint *error_value)
{
  guchar network_oper_name_type;
  gchar *network_display_tag = NULL;
  GError *error = NULL;
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_val_if_fail(ctx != NULL, NULL);

  if (!network)
  {
    CONNUI_ERR("Network is null");
    goto err_out;
  }

  if (long_name)
    network_oper_name_type = NETWORK_NITZ_FULL_OPER_NAME;
  else
    network_oper_name_type = NETWORK_NITZ_SHORT_OPER_NAME;

  if (!dbus_g_proxy_call(
        ctx->phone_net_proxy, "get_operator_name", &error,
        G_TYPE_UCHAR, network_oper_name_type,
        G_TYPE_UINT, (guint)g_ascii_strtod(network->operator_code, NULL),
        G_TYPE_UINT, (guint)g_ascii_strtod(network->country_code, NULL),
        G_TYPE_INVALID,
        G_TYPE_STRING, &network_display_tag,
        G_TYPE_INT, error_value,
        G_TYPE_INVALID))
  {
    CONNUI_ERR("Error with DBUS: %s", error->message);
    g_clear_error(&error);
    goto err_out;
  }

  if (network_display_tag && *network_display_tag)
  {
    connui_cell_context_destroy(ctx);
    return network_display_tag;
  }

  g_free(network_display_tag);

  network_display_tag = NULL;

  if (!dbus_g_proxy_call(
        ctx->phone_net_proxy, "get_operator_name", &error,
        G_TYPE_UCHAR, NETWORK_HARDCODED_LATIN_OPER_NAME,
        G_TYPE_UINT, (guint)g_ascii_strtod(network->operator_code, NULL),
        G_TYPE_UINT, (guint)g_ascii_strtod(network->country_code, NULL),
        G_TYPE_INVALID,
        G_TYPE_STRING, &network_display_tag,
        G_TYPE_INT, error_value,
        G_TYPE_INVALID))
  {
    CONNUI_ERR("Error with DBUS: %s", error->message);
    g_clear_error(&error);
    goto err_out;
  }

  if (network_display_tag && !*network_display_tag)
  {
    g_free(network_display_tag);
    network_display_tag = NULL;
  }

  connui_cell_context_destroy(ctx);

  return network_display_tag;

err_out:

  if (error_value)
    *error_value = 1;

  connui_cell_context_destroy(ctx);

  return NULL;
}

guchar
connui_cell_net_get_network_selection_mode(gint *error_value)
{
  GError *error = NULL;
  gint error_val = 1;
  guchar network_selection_mode = NETWORK_SELECT_MODE_UNKNOWN;
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_val_if_fail(ctx != NULL, NETWORK_SELECT_MODE_UNKNOWN);

  if (!dbus_g_proxy_call(
        ctx->phone_net_proxy, "get_network_selection_mode", &error,
        G_TYPE_INVALID,
        G_TYPE_UCHAR, &network_selection_mode,
        G_TYPE_INT, &error_val,
        G_TYPE_INVALID))
  {
    CONNUI_ERR("Error with DBUS %s", error->message);
    g_clear_error(&error);
  }

  if (error_value)
    *error_value = error_val;

  connui_cell_context_destroy(ctx);

  return network_selection_mode;
}

guchar
connui_cell_net_get_radio_access_mode(gint *error_value)
{
  GError *error = NULL;
  gint error_val = 1;
  guchar selected_rat = NET_GSS_UNKNOWN_SELECTED_RAT;
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_val_if_fail(ctx != NULL, NET_GSS_UNKNOWN_SELECTED_RAT);

  if (!dbus_g_proxy_call(
        ctx->phone_net_proxy, "get_selected_radio_access_technology", &error,
         G_TYPE_INVALID,
         G_TYPE_UCHAR, &selected_rat,
         G_TYPE_INT, &error_val,
         G_TYPE_INVALID))
  {
    CONNUI_ERR("Error with DBUS: %s",error->message);
    g_clear_error(&error);
    error_val = 1;
  }

  if (error_value)
    *error_value = error_val;

  connui_cell_context_destroy(ctx);

  return selected_rat;
}

static service_call *
add_service_call(connui_cell_context *ctx, guint call_id, DBusGProxy *proxy,
                 DBusGProxyCall *proxy_call, service_call_cb_f cb,
                 gpointer user_data)
{
  service_call *call = g_new0(service_call, 1);

  call->proxy = proxy;
  call->proxy_call = proxy_call;
  call->cb = cb;
  call->data = user_data;

  if (!ctx->service_calls)
    ctx->service_calls = g_hash_table_new(g_direct_hash, g_direct_equal);

  g_hash_table_insert(ctx->service_calls, GUINT_TO_POINTER(call_id), call);

  return call;
}

static service_call *
find_service_call_for_removal(connui_cell_context *ctx, guint call_id,
                              gboolean reset)
{
  service_call *call =
      g_hash_table_lookup(ctx->service_calls, GUINT_TO_POINTER(call_id));

  if (call)
  {
    if (reset)
    {
      call->unk = 0;
      call->proxy = NULL;
      call->proxy_call = NULL;
    }
  }
  else
    CONNUI_ERR("Unable to find call ID %d for removal", call_id);

  return call;
}

static void
remove_service_call(connui_cell_context *ctx, guint call_id)
{
  service_call *call;

  if (!ctx->service_calls)
  {
    CONNUI_ERR("Unable to find any service calls for removal");
    return;
  }

  call = find_service_call_for_removal(ctx, call_id, FALSE);

  if (call)
  {
    if (call->proxy && call->proxy_call)
      dbus_g_proxy_cancel_call(call->proxy, call->proxy_call);

    if (call->unk)
    {
      g_source_remove(call->unk);
      call->unk = 0;
    }

    g_hash_table_remove(ctx->service_calls, GUINT_TO_POINTER(call_id));
    g_free(call);

    if (!g_hash_table_size(ctx->service_calls))
    {
      g_hash_table_destroy(ctx->service_calls);
      ctx->service_call_id = 0;
      ctx->service_calls = NULL;
    }
  }
}

void
connui_cell_net_cancel_service_call(guint call_id)
{
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_if_fail(ctx != NULL);

  if (ctx->service_calls && call_id)
    remove_service_call(ctx, call_id);

  connui_cell_context_destroy(ctx);
}

static void
connui_cell_cs_status_change_cb(DBusGProxy *proxy, guchar network_cs_state,
                                guchar network_cs_type,
                                guchar network_cs_operation_mode,
                                connui_cell_context *ctx)
{
  gboolean active = FALSE;

  g_return_if_fail(ctx != NULL && ctx->cs_status_cbs != NULL);

  if (network_cs_state == NETWORK_CS_ACTIVE)
    active = TRUE;

  connui_utils_notify_notify_BOOLEAN(ctx->cs_status_cbs, active);
}

gboolean
connui_cell_cs_status_register(cell_cs_status_cb cb, gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (!ctx->cs_status_cbs)
  {
    dbus_g_proxy_connect_signal(ctx->phone_net_proxy,
                                "cellular_system_state_change",
                                (GCallback)connui_cell_cs_status_change_cb,
                                ctx, NULL);
  }

  ctx->cs_status_cbs =
      connui_utils_notify_add(ctx->cs_status_cbs, cb, user_data);
  connui_cell_context_destroy(ctx);

  return TRUE;
}

void
connui_cell_cs_status_close(cell_cs_status_cb cb)
{
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_if_fail(ctx != NULL);

  ctx->cs_status_cbs = connui_utils_notify_remove(ctx->cs_status_cbs, cb);

  if (!ctx->cs_status_cbs)
  {
    dbus_g_proxy_disconnect_signal(ctx->phone_net_proxy,
                                   "cellular_system_state_change",
                                   (GCallback)connui_cell_cs_status_change_cb,
                                   ctx);
  }

  connui_cell_context_destroy(ctx);
}

gboolean
connui_cell_net_is_activated(gint *error_value)
{
  connui_cell_context *ctx = connui_cell_context_get();
  GError *error = NULL;
  guchar network_cs_operation_mode = NETWORK_CS_OP_MODE_UNKNOWN;
  guchar network_cs_state = NETWORK_CS_STATE_UNKNOWN;
  gboolean rv = FALSE;

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (!dbus_g_proxy_call(ctx->phone_net_proxy, "get_cs_state", &error,
         G_TYPE_INVALID,
         G_TYPE_UCHAR, &network_cs_state,
         G_TYPE_UCHAR, &network_cs_operation_mode,
         G_TYPE_INT, NULL,
         G_TYPE_INVALID))
  {
    CONNUI_ERR("Error with DBUS: %s", error->message);
    g_clear_error(&error);

    if (error_value)
      *error_value = 1;
  }
  else
  {
    if (error_value)
      *error_value = 0;

    if (network_cs_state == NETWORK_CS_ACTIVE)
      rv = TRUE;
  }

  connui_cell_context_destroy(ctx);

  return rv;
}

static guint
get_next_call_id(connui_cell_context *ctx)
{
  ctx->service_call_id++;

  return ctx->service_call_id;
}

typedef void (*net_divert_reply_f)(DBusGProxy *, GError *, gpointer);

static void
divert_activate_cb(DBusGProxy *proxy, DBusGProxyCall *call_id, void *user_data)
{
  sim_status_data *data = user_data;
  GError *error = NULL;

  dbus_g_proxy_end_call(proxy, call_id, &error, G_TYPE_INVALID);
  ((net_divert_reply_f)data->cb)(proxy, error, data->data);
}

static void
connui_cell_net_divert_activate_reply(DBusGProxy *proxy, GError *error,
                                      const void *user_data)
{
  connui_cell_context *ctx = connui_cell_context_get();
  guint call_id = GPOINTER_TO_UINT(user_data);
  service_call *service_call;

  g_return_if_fail(ctx != NULL);

  if (!(service_call = find_service_call_for_removal(ctx, call_id, TRUE)))
  {
     CONNUI_ERR("Unable to get call ID %u", call_id);
     return;
  }

  if ( error )
  {
    const char *err_msg = error->message;
    int error_value;

    CONNUI_ERR("Error in call: %s", err_msg);

    if (err_msg &&
        (!strcmp(err_msg, "CALL BARRED") || !strcmp(err_msg, "DATA MISSING") ||
         !strcmp(err_msg, "Invalid Parameter") ||
         !strcmp(err_msg, "UNEXPECTED DATA VALUE") ||
         !strcmp(err_msg, "UNSPECIFIED REASON")))
    {
      error_value = 10001;
    }
    else
      error_value = 1;

    if (service_call->cb)
      service_call->cb(FALSE, error_value, NULL, service_call->data);
  }
  else
  {
    if (service_call->cb)
      service_call->cb(TRUE, 0, NULL, service_call->data);

  }

  remove_service_call(ctx, (guint)call_id);
  connui_cell_context_destroy(ctx);
}

static void
divert_cancel_cb(DBusGProxy *proxy, DBusGProxyCall *call_id, void *user_data)
{
  sim_status_data *data = user_data;
  GError *error = NULL;

  dbus_g_proxy_end_call(proxy, call_id, &error, G_TYPE_INVALID);
  ((net_divert_reply_f)data->cb)(proxy, error, data->data);
}

static void
connui_cell_net_divert_cancel_reply(DBusGProxy *proxy, GError *error,
                                    const void *user_data)
{
  connui_cell_context *ctx = connui_cell_context_get();
  guint call_id = GPOINTER_TO_UINT(user_data);
  service_call *service_call;

  g_return_if_fail(ctx != NULL);

  if (!(service_call = find_service_call_for_removal(ctx, call_id, TRUE)))
  {
     CONNUI_ERR("Unable to get call ID %u", call_id);
     return;
  }

  if (error)
  {
    CONNUI_ERR("Error in call: %s", error->message);

    if (service_call->cb)
      service_call->cb(FALSE, 1, NULL, service_call->data);
  }
  else
  {
    if (service_call->cb)
      service_call->cb(FALSE, 0, NULL, service_call->data);
  }

  remove_service_call(ctx, call_id);
  connui_cell_context_destroy(ctx);
}

guint
connui_cell_net_set_call_forwarding_enabled(gboolean enabled,
                                            const gchar *phone_number,
                                            service_call_cb_f cb,
                                            gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get();
  DBusGProxy *proxy;
  DBusGProxyCall *proxy_call;
  guint call_id;
  sim_status_data *data;

  if (!phone_number)
    phone_number = "";

  g_return_val_if_fail(ctx != NULL, 0);

  call_id = get_next_call_id(ctx);
  data = g_slice_new(sim_status_data);
  data->data = GUINT_TO_POINTER(call_id);

  if (enabled)
  {
    proxy = ctx->csd_ss_proxy;
    data->cb = (GCallback)connui_cell_net_divert_activate_reply;
    proxy_call = dbus_g_proxy_begin_call(proxy, "DivertActivate",
                                         divert_activate_cb,
                                         data,
                                         destroy_sim_status_data,
                                         G_TYPE_UINT, 5,
                                         G_TYPE_STRING, phone_number,
                                         G_TYPE_UINT, 20, G_TYPE_INVALID);
  }
  else
  {
    proxy = ctx->csd_ss_proxy;
    data->cb = (GCallback)connui_cell_net_divert_cancel_reply;
    proxy_call = dbus_g_proxy_begin_call(proxy, "DivertCancel",
                                         divert_cancel_cb, data,
                                         destroy_sim_status_data,
                                         G_TYPE_UINT, 5, G_TYPE_INVALID);
  }

  add_service_call(ctx, call_id, proxy, proxy_call, cb, user_data);

  connui_cell_context_destroy(ctx);

  return call_id;
}

typedef void (*divert_check_reply_cb_f)(DBusGProxy *, gboolean, gchar *, GError *, gpointer);

static void
divert_check_reply_cb(DBusGProxy *proxy, DBusGProxyCall *call, void *user_data)
{
  sim_status_data *data = user_data;
  gchar *phone_number;
  gboolean enabled;
  GError *error = NULL;

  dbus_g_proxy_end_call(proxy, call, &error,
                        G_TYPE_BOOLEAN, &enabled,
                        G_TYPE_STRING, &phone_number,
                        G_TYPE_INVALID);
  ((divert_check_reply_cb_f)(data->cb))(proxy, enabled, phone_number, error,
                                        data->data);

  g_free(phone_number);
}

static void
connui_cell_net_divert_check_reply(DBusGProxy *proxy, gboolean enabled,
                                   const gchar *phone_number, GError *error,
                                   gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get();
  guint call_id = GPOINTER_TO_UINT(user_data);
  service_call *service_call;

  g_return_if_fail(ctx != NULL);

  if (!(service_call = find_service_call_for_removal(ctx, call_id, TRUE)))
  {
    CONNUI_ERR("Unable to get call ID %d", call_id);
    return;
  }

  if (error)
  {
    CONNUI_ERR("Error in call: %s", error->message);

    if (service_call->cb)
      service_call->cb(FALSE, 1, NULL, service_call->data);
  }
  else
  {
    if (phone_number && !*phone_number)
        phone_number = NULL;

    if (service_call->cb)
      service_call->cb(enabled, 0, phone_number, service_call->data);
  }

  remove_service_call(ctx, (guint)call_id);
  connui_cell_context_destroy(ctx);
}

/* see https://wiki.maemo.org/Phone_control for values of type */
guint
connui_cell_net_get_call_forwarding_enabled(guint type, service_call_cb_f cb,
                                            gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get();
  guint call_id;
  DBusGProxy *proxy;
  sim_status_data *data;
  DBusGProxyCall *proxy_call;

  g_return_val_if_fail(ctx != NULL, 0);

  call_id = get_next_call_id(ctx);
  proxy = ctx->csd_ss_proxy;
  data = g_slice_new(sim_status_data);
  data->data = GUINT_TO_POINTER(call_id);
  data->cb = (GCallback)connui_cell_net_divert_check_reply;

  proxy_call = dbus_g_proxy_begin_call(proxy, "DivertCheck",
                                       divert_check_reply_cb, data,
                                       destroy_sim_status_data,
                                       G_TYPE_UINT, type, G_TYPE_INVALID);
  add_service_call(ctx, call_id, proxy, proxy_call, cb, user_data);
  connui_cell_context_destroy(ctx);

  return call_id;
}

typedef void (*net_waiting_reply_f)(DBusGProxy *, GError *, gpointer);

static void
waiting_activate_cb(DBusGProxy *proxy, DBusGProxyCall *call_id, void *user_data)
{
  sim_status_data *data = user_data;
  GError *error = NULL;

  dbus_g_proxy_end_call(proxy, call_id, &error, G_TYPE_INVALID);
  ((net_waiting_reply_f)data->cb)(proxy, error, data->data);
}

static void
connui_cell_net_waiting_activate_reply(DBusGProxy *proxy, GError *error,
                                       gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get();
  guint call_id = GPOINTER_TO_UINT(user_data);
  service_call *service_call;

  g_return_if_fail(ctx != NULL);

  if (!(service_call = find_service_call_for_removal(ctx, call_id, TRUE)))
  {
    CONNUI_ERR("Unable to get call ID %d", call_id);
    return;
  }

  if (error)
  {
    CONNUI_ERR("Error in call: %s", error->message);

    if (service_call->cb)
      service_call->cb(FALSE, 1, NULL, service_call->data);
  }
  else
  {
    if (service_call->cb)
      service_call->cb(TRUE, 0, NULL, service_call->data);
  }

  remove_service_call(ctx, call_id);
  connui_cell_context_destroy(ctx);
}

static void
waiting_cancel_cb(DBusGProxy *proxy, DBusGProxyCall *call_id, void *user_data)
{
  sim_status_data *data = user_data;
  GError *error = NULL;

  dbus_g_proxy_end_call(proxy, call_id, &error, G_TYPE_INVALID);
  ((net_waiting_reply_f)data->cb)(proxy, error, data->data);
}

static void
connui_cell_net_waiting_cancel_reply(DBusGProxy *proxy, GError *error,
                                     gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get();
  guint call_id = GPOINTER_TO_UINT(user_data);
  service_call *service_call;

  g_return_if_fail(ctx != NULL);

  if (!(service_call = find_service_call_for_removal(ctx, call_id, TRUE)))
  {
    CONNUI_ERR("Unable to get call ID %d", call_id);
    return;
  }

  if (error)
  {
    CONNUI_ERR("Error in call: %s", error->message);

    if (service_call->cb)
      service_call->cb(FALSE, 1, NULL, service_call->data);
  }
  else
  {
    if (service_call->cb)
      service_call->cb(FALSE, 0, NULL, service_call->data);
  }

  remove_service_call(ctx, call_id);
  connui_cell_context_destroy(ctx);
}

guint
connui_cell_net_set_call_waiting_enabled(gboolean enabled, service_call_cb_f cb,
                                         gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get();
  DBusGProxy *proxy = ctx->csd_ss_proxy;
  guint call_id;
  DBusGProxyCall *proxy_call;
  sim_status_data *data;

  g_return_val_if_fail(ctx != NULL, 0);

  call_id = get_next_call_id(ctx);

  data = g_slice_new(sim_status_data);
  data->data = GUINT_TO_POINTER(call_id);

  if (enabled)
  {
    data->cb = (GCallback)connui_cell_net_waiting_activate_reply;
    proxy_call = dbus_g_proxy_begin_call(proxy, "WaitingActivate",
                                         waiting_activate_cb, data,
                                         destroy_sim_status_data,
                                         G_TYPE_INVALID);
  }
  else
  {
    data->cb = (GCallback)connui_cell_net_waiting_cancel_reply;
    proxy_call = dbus_g_proxy_begin_call(proxy, "WaitingCancel",
                                         waiting_cancel_cb, data,
                                         destroy_sim_status_data,
                                         G_TYPE_INVALID);
  }

  add_service_call(ctx, call_id, proxy, proxy_call, cb, user_data);
  connui_cell_context_destroy(ctx);

  return call_id;
}

typedef void (*net_waiting_check_reply_f)(DBusGProxy *, gboolean, GError *, gpointer);

static void
waiting_check_cb(DBusGProxy *proxy, DBusGProxyCall *call_id, void *user_data)
{
  sim_status_data *data = user_data;
  gboolean enabled;
  GError *error = NULL;

  dbus_g_proxy_end_call(proxy, call_id, &error, 0x14u, &enabled, 0);
  ((net_waiting_check_reply_f)data->cb)(proxy, enabled, error, data->data);
}

static void
connui_cell_net_waiting_check_reply(DBusGProxy *proxy, gboolean enabled,
                                    GError *error, gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get();
  guint call_id = GPOINTER_TO_UINT(user_data);
  service_call *service_call;

  g_return_if_fail(ctx != NULL);

  if (!(service_call = find_service_call_for_removal(ctx, call_id, TRUE)))
  {
    CONNUI_ERR("Unable to get call ID %d", call_id);
    return;
  }

  if ( error )
  {
    CONNUI_ERR("Error in call: %s", error->message);

    if (service_call->cb)
      service_call->cb(FALSE, 1, NULL, service_call->data);
  }
  else
  {
    if (service_call->cb)
      service_call->cb(enabled, 0, NULL, service_call->data);
  }

  remove_service_call(ctx, call_id);
  connui_cell_context_destroy(ctx);
}

guint
connui_cell_net_get_call_waiting_enabled(service_call_cb_f cb,
                                         gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get();
  guint call_id;
  DBusGProxy *proxy;
  sim_status_data *data;
  DBusGProxyCall *call;

  g_return_val_if_fail(ctx != NULL, 0);

  call_id = get_next_call_id(ctx);
  proxy = ctx->csd_ss_proxy;

  data = g_slice_new(sim_status_data);
  data->data = GUINT_TO_POINTER(call_id);
  data->cb = (GCallback)connui_cell_net_waiting_check_reply;

  call = dbus_g_proxy_begin_call(proxy, "WaitingCheck", waiting_check_cb, data,
                                 destroy_sim_status_data, G_TYPE_INVALID);
  add_service_call(ctx, call_id, proxy, call, cb, user_data);
  connui_cell_context_destroy(ctx);

  return call_id;
}

static void
net_list_cb(DBusGProxy *proxy, GArray *status_array,
            GArray *operator_code_array, GArray *country_code_array,
            GArray *operator_name_array, GArray *network_type_array,
            GArray *umts_avail_array, int error_value, GError *error,
            void *user_data)
{
  connui_cell_context *ctx = user_data;
  int i;
  GSList *l = NULL;

  g_return_if_fail(ctx != NULL);

  ctx->get_available_network_call = NULL;

  if (error)
  {
    connui_utils_notify_notify_POINTER(ctx->net_list_cbs, l);
    CONNUI_ERR("Error %s\n", error->message);
    g_clear_error(&error);
    return;

  }

  if (error_value)
    CONNUI_ERR("Error in method return: %d\n", error_value);

  if (status_array && operator_code_array && country_code_array &&
      operator_name_array && network_type_array && umts_avail_array)
  {
    g_return_if_fail(status_array->len == umts_avail_array->len &&
                     status_array->len == network_type_array->len);

    for(i = 0; i < status_array->len; i++)
    {
      cell_network *net = g_new0(cell_network, 1);

      net->service_status = g_array_index(status_array, guchar, i);
      net->network_type = g_array_index(network_type_array, guchar, i);
      net->country_code = g_array_index(country_code_array, gchar *, i);
      net->operator_code = g_array_index(operator_code_array, gchar *, i);
      net->operator_name = g_array_index(operator_name_array, gchar *, i);
      net->umts_avail = g_array_index(umts_avail_array, guchar, i);
      l = g_slist_append(l, net);
    }

    g_array_free(status_array, TRUE);
    g_array_free(network_type_array, TRUE);
    g_array_free(umts_avail_array, TRUE);

    g_free(operator_code_array);
    g_free(country_code_array);
    g_free(operator_name_array);
  }

  connui_utils_notify_notify_POINTER(ctx->net_list_cbs, l);
  g_slist_foreach(ctx->net_list_cbs, (GFunc)g_free, NULL);
  g_slist_free(ctx->net_list_cbs);
  ctx->net_list_cbs = NULL;
}

typedef void (*available_network_cb_f)(DBusGProxy *, GArray *, GArray *, GArray *, GArray *, GArray *, GArray *, int, GError *, gpointer);

static void
get_available_network_cb(DBusGProxy *proxy, DBusGProxyCall *call, void *user_data)
{
  sim_status_data *data = user_data;
  GType g_array_uchar = dbus_g_type_get_collection("GArray", G_TYPE_UCHAR);
  GType g_array_str = G_TYPE_STRV;
  int error_value;
  GArray *umts_avail_array;
  GArray *network_type_array;
  GArray *operator_name_array;
  GArray *country_code_array;
  GArray *operator_code_array;
  GArray *network_status_array;
  GError *error = NULL;

  dbus_g_proxy_end_call(proxy, call, &error,
                        g_array_uchar, &network_status_array,
                        g_array_str, &operator_code_array,
                        g_array_str, &country_code_array,
                        g_array_str, &operator_name_array,
                        g_array_uchar, &network_type_array,
                        g_array_uchar, &umts_avail_array,
                        G_TYPE_INT, &error_value,
                        G_TYPE_INVALID);
  ((available_network_cb_f)data->cb)(proxy, network_status_array,
                                     operator_code_array, country_code_array,
                                     operator_name_array, network_type_array,
                                     umts_avail_array, error_value, error,
                                     data->data);
}

gboolean
connui_cell_net_list(cell_net_list_cb cb, gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get();
  gboolean rv = FALSE;

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (!ctx->get_available_network_call)
  {
    sim_status_data *data = g_slice_new(sim_status_data);

    data->cb = (GCallback)net_list_cb;
    data->data = ctx;
    ctx->get_available_network_call =
        dbus_g_proxy_begin_call_with_timeout(ctx->phone_net_proxy,
                                             "get_available_network",
                                             get_available_network_cb, data,
                                             destroy_sim_status_data, 500000,
                                             G_TYPE_INVALID);
  }

  ctx->net_list_cbs = connui_utils_notify_add(ctx->net_list_cbs, cb, user_data);

  if (ctx->get_available_network_call)
    rv = TRUE;

  connui_cell_context_destroy(ctx);

  return rv;
}

static void
net_select_cb(DBusGProxy *proxy, guchar network_reject_code, gint error_value,
              GError *error, gpointer user_data)
{
  connui_cell_context *ctx = user_data;

  g_return_if_fail(ctx != NULL);

  ctx->select_network_call = NULL;

  if (error)
  {
    CONNUI_ERR("Error %s\n", error->message);
    g_clear_error(&error);
    return;
  }

  if (error_value)
    CONNUI_ERR("Error in method return: %d\n", error_value);

  connui_utils_notify_notify_BOOLEAN_UINT(ctx->net_select_cbs, error_value == 0,
                                          network_reject_code);
  g_slist_foreach(ctx->net_select_cbs, (GFunc)&g_free, NULL);
  g_slist_free(ctx->net_select_cbs);
  ctx->net_select_cbs = NULL;
}

static void
net_select_mode_cb(DBusGProxy *proxy, gint error_value, GError *error,
                   gpointer user_data)
{
  net_select_cb(proxy, 0, error_value, error, user_data);
}

void
connui_cell_net_cancel_select(cell_net_select_cb cb)
{
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_if_fail(ctx != NULL);

  ctx->net_select_cbs = connui_utils_notify_remove(ctx->net_select_cbs, cb);

  if (!ctx->net_select_cbs && ctx->select_network_call)
  {
    GError *error = NULL;

    dbus_g_proxy_cancel_call(ctx->phone_net_proxy, ctx->select_network_call);
    ctx->select_network_call = NULL;

    if (!dbus_g_proxy_call(ctx->phone_net_proxy, "cancel_select_network",
                           &error, G_TYPE_INVALID,
                           G_TYPE_INT, NULL,
                           G_TYPE_INVALID))
    {
      CONNUI_ERR("Error %s", error->message);
      g_clear_error(&error);
    }
  }

  connui_cell_context_destroy(ctx);
}

typedef void (*network_cb_f)(DBusGProxy *, guchar, gint, GError *, gpointer);

static void
select_network_cb(DBusGProxy *proxy, DBusGProxyCall *call_id, void *user_data)
{
  sim_status_data *data = user_data;
  gint error_value;
  GError *error = NULL;
  guchar reject_code;

  dbus_g_proxy_end_call(proxy, call_id, &error,
                        G_TYPE_UCHAR, &reject_code,
                        G_TYPE_INT, &error_value,
                        G_TYPE_INVALID);
  ((network_cb_f)data->cb)(proxy, reject_code, error_value, error,data->data);
}

typedef void (*network_mode_cb_f)(DBusGProxy *, int, GError *, gpointer);

static void
select_network_mode_cb(DBusGProxy *proxy, DBusGProxyCall *call_id,
                       void *user_data)
{
  sim_status_data *data = user_data;
  gint error_value;
  GError *error = NULL;

  dbus_g_proxy_end_call(proxy, call_id, &error,
                        G_TYPE_INT, &error_value,
                        G_TYPE_INVALID);
  ((network_mode_cb_f)data->cb)(proxy, error_value, error, data->data);
}

gboolean
connui_cell_net_select(cell_network *network, cell_net_select_cb cb,
                       gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get();
  gboolean rv = FALSE;
  sim_status_data *data;

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (ctx->select_network_call)
    connui_cell_net_cancel_select(NULL);

  if (network && !ctx->select_network_call)
  {
    data = g_slice_new(sim_status_data);

    ctx->network.operator_code = network->operator_code;
    ctx->network.country_code = network->country_code;
    data->cb = (GCallback)net_select_cb;
    data->data = ctx;
    ctx->select_network_call = dbus_g_proxy_begin_call_with_timeout(
          ctx->phone_net_proxy, "select_network", select_network_cb, data,
          destroy_sim_status_data, 500000,
          G_TYPE_UCHAR, NETWORK_SELECT_MODE_MANUAL,
          G_TYPE_UCHAR, NETWORK_RAT_NAME_UNKNOWN,
          G_TYPE_STRING, network->operator_code,
          G_TYPE_STRING, network->country_code,
          G_TYPE_INVALID);
  }
  else
  {
    if (network || ctx->select_network_call)
    {
      connui_cell_context_destroy(ctx);
      return FALSE;
    }

    ctx->network.operator_code = NULL;
    ctx->network.country_code = NULL;

    data = g_slice_new(sim_status_data);
    data->cb = (GCallback)net_select_mode_cb;
    data->data = ctx;
    ctx->select_network_call =
        dbus_g_proxy_begin_call(ctx->phone_net_proxy, "select_network_mode",
                                select_network_mode_cb, data,
                                destroy_sim_status_data,
                                G_TYPE_UCHAR, NETWORK_SELECT_MODE_AUTOMATIC,
                                G_TYPE_INVALID);
  }

  if (ctx->select_network_call)
    rv = TRUE;

  ctx->net_select_cbs =
      connui_utils_notify_add(ctx->net_select_cbs, cb, user_data);
  connui_cell_context_destroy(ctx);

  return rv;
}

static void
net_reset_cb(DBusGProxy *proxy, guchar network_reject_code, gint error_value,
             GError *error, gpointer user_data)
{
  connui_cell_context *ctx = user_data;

  g_return_if_fail(ctx != NULL);

  ctx->select_network_call = NULL;

  if (error)
  {
    CONNUI_ERR("Error %s\n", error->message);
    g_clear_error(&error);
  }
  else if (error_value)
    CONNUI_ERR("Error in method return %d\n", error_value);
}

void
connui_cell_reset_network()
{
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_if_fail(ctx != NULL);

  if (ctx->select_network_call)
    connui_cell_net_cancel_select(NULL);

  if (!ctx->select_network_call)
  {
    sim_status_data *data = g_slice_new(sim_status_data);

    data->cb = (GCallback)net_reset_cb;
    data->data = ctx;
    ctx->select_network_call = dbus_g_proxy_begin_call_with_timeout(
          ctx->phone_net_proxy, "select_network", select_network_cb, data,
          destroy_sim_status_data, 500000,
          G_TYPE_UCHAR, NETWORK_SELECT_MODE_NO_SELECTION,
          G_TYPE_UCHAR, NETWORK_RAT_NAME_UNKNOWN,
          G_TYPE_STRING, NULL, G_TYPE_STRING, NULL, G_TYPE_INVALID);
  }

  connui_cell_context_destroy(ctx);
}

void
connui_cell_net_cancel_list(cell_net_list_cb cb)
{
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_if_fail(ctx != NULL);

  ctx->net_list_cbs = connui_utils_notify_remove(ctx->net_list_cbs, cb);

  if (!ctx->net_list_cbs && ctx->get_available_network_call)
  {
    GError *error = NULL;

    dbus_g_proxy_cancel_call(ctx->phone_net_proxy,
                             ctx->get_available_network_call);
    ctx->get_available_network_call = NULL;

    if (!dbus_g_proxy_call(ctx->phone_net_proxy, "cancel_get_available_network",
                           &error, G_TYPE_INVALID, G_TYPE_INT, NULL,
                           G_TYPE_INVALID))
    {
      CONNUI_ERR("Error %s", error->message);
      g_clear_error(&error);
    }
  }

  connui_cell_context_destroy(ctx);
}

const cell_network *
connui_cell_net_get_current()
{
  static cell_network current_network;
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_val_if_fail(ctx != NULL, NULL);

  current_network.operator_code = ctx->network.operator_code;
  current_network.country_code = ctx->network.country_code;

  connui_cell_context_destroy(ctx);

  if (ctx->network.country_code && !ctx->select_network_call)
    return &current_network;

  return NULL;
}
