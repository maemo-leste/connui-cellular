#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <libosso.h>
#include <connui/connui-utils.h>
#include <connui/connui-log.h>

#include "context.h"

struct _service_call
{
  DBusGProxy *proxy;
  DBusGProxyCall *proxy_call;
  GCallback cb;
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
  sim_status_data *data = (sim_status_data *)user_data;
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
  sim_status_data *data = (sim_status_data *)user_data;
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
  sim_status_data *data = (sim_status_data *)user_data;
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
