#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <libosso.h>
#include <connui/connui-utils.h>
#include <connui/connui-log.h>

#include "context.h"

static void
net_signal_strength_change_notify(connui_cell_context *ctx)
{
  connui_utils_notify_notify_POINTER(ctx->net_status_cbs, &ctx->state);
}

static void
net_reg_status_change_cb(DBusGProxy *proxy, uint8_t reg_status,
                         uint16_t current_lac, uint32_t current_cell_id,
                         uint32_t operator_code, uint32_t country_code,
                         uint8_t type, uint8_t supported_services,
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
net_signal_strength_change_cb(DBusGProxy *proxy, uint8_t signals_bar,
                              uint8_t rssi_in_dbm, connui_cell_context *ctx)
{
  g_return_if_fail(ctx != NULL);

  ctx->state.network_signals_bar = signals_bar;

  if (proxy)
      net_signal_strength_change_notify(ctx);
}

static void
net_rat_change_cb(DBusGProxy *proxy, uint8_t rat_name, connui_cell_context *ctx)
{
  g_return_if_fail(ctx != NULL);

  ctx->state.rat_name = rat_name;

  if (proxy)
    net_signal_strength_change_notify(ctx);
}

static void
net_radio_info_change_cb(DBusGProxy *proxy, uint8_t network_radio_state,
                         uint8_t network_hsdpa_allocated,
                         uint8_t network_hsupa_allocated,
                         connui_cell_context *ctx)
{
  g_return_if_fail(ctx != NULL);

  ctx->state.network_hsdpa_allocated = network_hsdpa_allocated;

  if (proxy)
    net_signal_strength_change_notify(ctx);
}

static void
net_cell_info_change_cb(DBusGProxy *proxy, uint8_t network_cell_type,
                        uint16_t network_current_lac,
                        uint32_t network_current_cell_id,
                        uint32_t network_operator_code,
                        uint32_t network_country_code,
                        uint8_t network_service_status,
                        uint8_t network_type, connui_cell_context *ctx)
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
operator_name_change_cb(DBusGProxy *proxy, uint8_t operator_name_type,
                        const char *operator_name,
                        const char *alternative_operator_name,
                        uint32_t network_operator_code,
                        uint32_t network_country_code, connui_cell_context *ctx)
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

typedef void (*net_get_rat_cb_f)(DBusGProxy *, uint8_t, int32_t, GError *,
                                 connui_cell_context *);

static void
get_radio_access_technology_cb(DBusGProxy *proxy, DBusGProxyCall *call_id,
                               void *user_data)
{
  sim_status_data *data = (sim_status_data *)user_data;
  GError *error = NULL;
  int32_t error_value;
  uint8_t network_rat_name;

  dbus_g_proxy_end_call(proxy, call_id, &error,
                        G_TYPE_UCHAR, &network_rat_name,
                        G_TYPE_INT, &error_value,
                        G_TYPE_INVALID);

  ((net_get_rat_cb_f)data->cb)(proxy, network_rat_name, error_value, error,
                               data->data);
}

static void
net_get_rat_cb(DBusGProxy *proxy, uint8_t network_rat_name, int32_t error_value,
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

typedef void (*net_get_reg_status_cb_f)(DBusGProxy *, uint8_t, uint32_t,
                                         uint32_t, uint32_t, uint32_t, uint8_t,
                                         uint8_t, int32_t, GError *,
                                         connui_cell_context *);
static void
get_registration_status_cb(DBusGProxy *proxy, DBusGProxyCall *call_id,
                           void *user_data)
{
  sim_status_data *data = (sim_status_data *)user_data;
  int32_t error_value;
  uint32_t network_country_code;
  uint32_t network_operator_code;
  uint32_t network_current_cell_id;
  uint32_t network_current_lac;
  uint8_t network_supported_services;
  uint8_t network_type;
  uint8_t network_reg_status;
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
net_get_signal_strength_cb(DBusGProxy *proxy, uint8_t network_signals_bar,
                           uint8_t network_rssi_in_dbm, int32_t error_value,
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

typedef void (*net_get_signal_strength_cb_f)(DBusGProxy *, uint8_t, uint8_t,
                                             int32_t, GError *,
                                             connui_cell_context *);

static void
get_signal_strength_cb(DBusGProxy *proxy, DBusGProxyCall *call_id,
                       void *user_data)
{
  sim_status_data *data = (sim_status_data *)user_data;
  GError *error = NULL;
  uint8_t network_rssi_in_dbm;
  uint8_t network_signals_bar;
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
net_get_reg_status_cb(DBusGProxy *proxy, uint8_t reg_status,
                      uint16_t current_lac, uint32_t current_cell_id,
                      uint32_t operator_code, uint32_t country_code,
                      uint8_t type, uint8_t supported_services,
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
