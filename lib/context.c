#include <dbus/dbus-glib.h>
#include <connui/connui-log.h>

#include "connui-cell-marshal.h"
#include "context.h"

__attribute__((visibility("hidden"))) void
destroy_sim_status_data(gpointer mem_block)
{
  g_slice_free(sim_status_data, mem_block);
}

static void
register_marshallers()
{
  static gboolean marshallers_registered;

  if (marshallers_registered)
    return;

  dbus_g_object_register_marshaller(
        connui_cell_VOID__UCHAR_UINT_UINT_UINT_UINT_UCHAR_UCHAR, G_TYPE_NONE,
        G_TYPE_UCHAR, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
        G_TYPE_UCHAR, G_TYPE_UCHAR, G_TYPE_INVALID);
  dbus_g_object_register_marshaller(connui_cell_VOID__UCHAR_UCHAR,
                                    G_TYPE_NONE, G_TYPE_UCHAR, G_TYPE_UCHAR,
                                    G_TYPE_INVALID);
  dbus_g_object_register_marshaller(connui_cell_VOID__UCHAR_UCHAR_UCHAR,
                                    G_TYPE_NONE, G_TYPE_UCHAR, G_TYPE_UCHAR,
                                    G_TYPE_UCHAR, G_TYPE_INVALID);
  dbus_g_object_register_marshaller(
        connui_cell_VOID__INT_INT_INT_INT_INT_INT_INT_INT, G_TYPE_NONE,
        G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT,
        G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INVALID);
  dbus_g_object_register_marshaller(connui_cell_VOID__BOOLEAN_BOOLEAN,
                                    G_TYPE_NONE, G_TYPE_BOOLEAN,
                                    G_TYPE_BOOLEAN, G_TYPE_INVALID);
  dbus_g_object_register_marshaller(
        connui_cell_VOID__UCHAR_STRING_STRING_UINT_UINT, G_TYPE_NONE,
        G_TYPE_UCHAR, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_UINT,
        G_TYPE_INVALID);

  marshallers_registered = TRUE;
}


static gboolean
create_proxies(connui_cell_context *ctx)
{
  GError *error = NULL;
  DBusGConnection *bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);

  if (error)
  {
    CONNUI_ERR("Initializing DBUS failed: %s", error->message);
    g_clear_error(&error);
    return FALSE;
  }

  ctx->phone_net_proxy = dbus_g_proxy_new_for_name(bus,
                                                   "com.nokia.phone.net",
                                                   "/com/nokia/phone/net",
                                                   "Phone.Net");
  ctx->phone_sim_proxy = dbus_g_proxy_new_for_name(bus,
                                                   "com.nokia.phone.SIM",
                                                   "/com/nokia/phone/SIM",
                                                   "Phone.Sim");
  ctx->phone_sim_security_proxy = dbus_g_proxy_new_for_name(
                                    bus,
                                    "com.nokia.phone.SIM",
                                    "/com/nokia/phone/SIM/security",
                                    "Phone.Sim.Security");
  ctx->csd_ss_proxy = dbus_g_proxy_new_for_name(bus,
                                                "com.nokia.csd",
                                                "/com/nokia/csd/ss",
                                                "com.nokia.csd.SS");
  ctx->csd_call_proxy = dbus_g_proxy_new_for_name(bus,
                                                  "com.nokia.csd.Call",
                                                  "/com/nokia/csd/call",
                                                  "com.nokia.csd.Call");
  ctx->phone_ssc_proxy = dbus_g_proxy_new_for_name(bus,
                                                   "com.nokia.phone.SSC",
                                                   "/com/nokia/phone/SSC",
                                                   "com.nokia.phone.SSC");
  dbus_g_connection_unref(bus);

  return TRUE;
}

static void
add_dbus_signals(connui_cell_context *ctx)
{
  dbus_g_proxy_add_signal(ctx->phone_net_proxy, "registration_status_change",
                          G_TYPE_UCHAR, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
                          G_TYPE_UINT, G_TYPE_UCHAR, G_TYPE_UCHAR,
                          G_TYPE_INVALID);
  dbus_g_proxy_add_signal(ctx->phone_net_proxy, "signal_strength_change",
                          G_TYPE_UCHAR, G_TYPE_UCHAR, G_TYPE_INVALID);
  dbus_g_proxy_add_signal(ctx->phone_net_proxy, "network_time_info_change",
                          G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT,
                          G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT,
                          G_TYPE_INVALID);
  dbus_g_proxy_add_signal(ctx->phone_net_proxy, "cellular_system_state_change",
                          G_TYPE_UCHAR, G_TYPE_UCHAR, G_TYPE_UCHAR,
                          G_TYPE_INVALID);
  dbus_g_proxy_add_signal(ctx->phone_net_proxy,
                          "radio_access_technology_change", G_TYPE_UCHAR,
                          G_TYPE_INVALID);
  dbus_g_proxy_add_signal(ctx->phone_net_proxy, "radio_info_change",
                          G_TYPE_UCHAR, G_TYPE_UCHAR, G_TYPE_UCHAR,
                          G_TYPE_INVALID);
  dbus_g_proxy_add_signal(ctx->phone_net_proxy, "cell_info_change",
                          G_TYPE_UCHAR, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
                          G_TYPE_UINT, G_TYPE_UCHAR, G_TYPE_UCHAR,
                          G_TYPE_INVALID);
  dbus_g_proxy_add_signal(ctx->phone_net_proxy, "operator_name_change",
                          G_TYPE_UCHAR);
  dbus_g_proxy_add_signal(ctx->phone_sim_proxy, "status", G_TYPE_UINT,
                          G_TYPE_INVALID);
  dbus_g_proxy_add_signal(ctx->phone_sim_security_proxy,
                          "verify_code_requested", G_TYPE_UINT, G_TYPE_INVALID);
  dbus_g_proxy_add_signal(ctx->csd_call_proxy, "ServerStatus", G_TYPE_BOOLEAN,
                          G_TYPE_BOOLEAN, G_TYPE_INVALID);
  dbus_g_proxy_add_signal(ctx->phone_ssc_proxy, "modem_state_changed_ind",
                          G_TYPE_STRING, G_TYPE_INVALID);
}

__attribute__((visibility("hidden"))) connui_cell_context *
connui_cell_context_get()
{
  static connui_cell_context context;

  if (context.initialized)
    return &context;

  context.state.cell_id = 0;
  context.state.network = NULL;
  context.state.reg_status = 0;
  context.state.supported_services = 0;
  context.state.lac = 0;
  context.state.operator_name_type = 0;
  context.state.operator_name = NULL;
  context.state.alternative_operator_name = NULL;

  register_marshallers();

  register_ofono(&context);

  if (!create_proxies(&context))
    return NULL;

  add_dbus_signals(&context);

  context.ssc_state_cbs = NULL;
  context.sim_status_cbs = NULL;
  context.sec_code_cbs = NULL;
  context.net_status_cbs = NULL;
  context.net_list_cbs = NULL;
  context.net_select_cbs = NULL;
  context.cs_status_cbs = NULL;
  context.call_status_cbs = NULL;
  context.initialized = TRUE;

  return &context;
}

__attribute__((visibility("hidden"))) void
connui_cell_context_destroy(connui_cell_context *ctx)
{
  if (ctx->sim_status_cbs || ctx->sec_code_cbs ||
      ctx->net_status_cbs || ctx->net_list_cbs || ctx->net_select_cbs ||
      ctx->cs_status_cbs || ctx->service_calls || ctx->clir_cb ||
      ctx->ssc_state_cbs)
  {
    return;
  }

  if (ctx->get_sim_status_call)
  {
    dbus_g_proxy_cancel_call(ctx->phone_sim_proxy, ctx->get_sim_status_call);
    ctx->get_sim_status_call = NULL;
  }

  if (ctx->get_sim_status_call_1)
  {
    dbus_g_proxy_cancel_call(ctx->phone_sim_proxy, ctx->get_sim_status_call_1);
    ctx->get_sim_status_call_1 = NULL;
  }

  if (ctx->get_registration_status_call)
  {
    dbus_g_proxy_cancel_call(ctx->phone_net_proxy,
                             ctx->get_registration_status_call);
    ctx->get_registration_status_call = NULL;
  }

  if (ctx->get_available_network_call)
  {
    dbus_g_proxy_cancel_call(ctx->phone_net_proxy,
                             ctx->get_available_network_call);
    ctx->get_available_network_call = NULL;
  }

  if (ctx->select_network_call)
  {
    dbus_g_proxy_cancel_call(ctx->phone_net_proxy, ctx->select_network_call);
    ctx->select_network_call = NULL;
  }

  if (ctx->state.network)
  {
    connui_cell_network_free(ctx->state.network);
    ctx->state.network = NULL;
  }

  g_free(ctx->state.operator_name);
  ctx->state.operator_name = NULL;

  g_free(ctx->state.alternative_operator_name);
  ctx->state.alternative_operator_name = NULL;

  g_object_unref(G_OBJECT(ctx->phone_net_proxy));
  g_object_unref(G_OBJECT(ctx->phone_sim_proxy));
  g_object_unref(G_OBJECT(ctx->phone_sim_security_proxy));
  g_object_unref(G_OBJECT(ctx->csd_ss_proxy));
  g_object_unref(G_OBJECT(ctx->csd_call_proxy));
  g_object_unref(G_OBJECT(ctx->phone_ssc_proxy));

  unregister_ofono(ctx);

  ctx->initialized = FALSE;
}
