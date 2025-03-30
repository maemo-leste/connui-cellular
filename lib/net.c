#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <libosso.h>
#include <connui/connui-utils.h>
#include <connui/connui-log.h>
#include <connui/connui-dbus.h>
#include <telepathy-glib/telepathy-glib.h>
#include <gio/gio.h>
#include <libxml/xpath.h>

#include <string.h>

#include "context.h"
#include "service-call.h"

#include "net.h"

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

typedef struct _net_data
{
  connui_cell_context *ctx;
  ConnuiCellNetworkRegistration *proxy;
  gchar *path;

  cell_network_state state;
  connui_net_selection_mode selection_mode;

  guint idle_id;
  gulong properties_changed_id;
}
net_data;

#define DATA "connui_cell_net_data"

static net_data *
_net_data_get(const char *path, GError **error)
{
  connui_cell_context *ctx = connui_cell_context_get(error);
  ConnuiCellModem *modem;

  g_return_val_if_fail(path != NULL, NULL);

  modem = g_hash_table_lookup(ctx->modems, path);

  if (!modem)
  {
    CONNUI_ERR("Invalid modem path %s", path);
    g_set_error(error, CONNUI_ERROR, CONNUI_ERROR_NOT_FOUND,
                "No such modem [%s]", path);
    return NULL;
  }

  return g_object_get_data(G_OBJECT(modem), DATA);
}

static void
init_net_state(cell_network_state *state)
{
  state->reg_status = CONNUI_NET_REG_STATUS_UNKNOWN;
  state->lac = 0;
  state->cell_id = 0;
  state->supported_services = 0;
  state->network_signals_bar = 0;
  state->rat_name = CONNUI_NET_RAT_UNKNOWN;
  state->network_hsdpa_allocated = FALSE;
}

static void
_net_data_destroy(gpointer data)
{
  net_data *nd = data;
  cell_network_state *state = &nd->state;

  if (nd->idle_id)
    g_source_remove(nd->idle_id);

  g_signal_handler_disconnect(nd->proxy, nd->properties_changed_id);
  g_object_unref(nd->proxy);

  connui_cell_network_free(state->network);
  state->network = NULL;
  g_free(state->operator_name);
  state->operator_name = NULL;
  init_net_state(state);

  connui_utils_notify_notify(nd->ctx->net_status_cbs, nd->path, state, NULL);

  g_free(nd->path);
  g_free(nd);
}

static net_data *
_net_data_create(ConnuiCellNetworkRegistration *proxy, const gchar *path,
                 connui_cell_context *ctx)
{
  net_data *nd = g_new0(net_data, 1);
  ConnuiCellModem *modem = g_hash_table_lookup(ctx->modems, path);

  g_assert(modem);
  g_assert(g_object_get_data(G_OBJECT(modem), DATA) == NULL);

  g_object_set_data_full(G_OBJECT(modem), DATA, nd, _net_data_destroy);

  nd->path = g_strdup(path);
  nd->proxy = proxy;
  nd->ctx = ctx;

  nd->state.network = g_new0(cell_network, 1);
  nd->selection_mode = CONNUI_NET_SELECT_MODE_UNKNOWN;

  init_net_state(&nd->state);

  return nd;
}

static gboolean
_idle_notify(gpointer user_data)
{
  net_data *nd = user_data;
  nd->idle_id = 0;

  connui_utils_notify_notify(nd->ctx->net_status_cbs, nd->path, &nd->state,
                             NULL);

  return G_SOURCE_REMOVE;
}

static void
_notify(net_data *nd)
{
  if (nd && !nd->idle_id)
    nd->idle_id = g_idle_add(_idle_notify, nd);
}

static void
_notify_all(connui_cell_context *ctx)
{
  GHashTableIter iter;
  gpointer modem;

  g_hash_table_iter_init (&iter, ctx->modems);

  while (g_hash_table_iter_next(&iter, NULL, &modem))
    _notify(g_object_get_data(G_OBJECT(modem), DATA));
}

static connui_net_registration_status
_reg_status(const gchar *status)
{
  if (!strcmp(status, "unregistered"))
    return CONNUI_NET_REG_STATUS_UNREGISTERED;
  else if (!strcmp(status, "registered"))
    return CONNUI_NET_REG_STATUS_HOME;
  else if (!strcmp(status, "searching"))
    return CONNUI_NET_REG_STATUS_SEARCHING;
  else if (!strcmp(status, "denied"))
    return CONNUI_NET_REG_STATUS_DENIED;
  else if (!strcmp(status, "roaming"))
    return CONNUI_NET_REG_STATUS_ROAMING;
  else
    return CONNUI_NET_REG_STATUS_UNKNOWN;
}

static guint
_rat_name(const gchar *tech)
{
  if (!strcmp(tech, "gsm") || !strcmp(tech, "edge"))
    return CONNUI_NET_RAT_GSM;
  else if (!strcmp(tech, "umts") || !strcmp(tech, "hspa"))
    return CONNUI_NET_RAT_UMTS;
  else if (!strcmp(tech, "lte"))
    return CONNUI_NET_RAT_LTE;
  else if (!strcmp(tech, "nr"))
    return CONNUI_NET_RAT_NR;
  else
    return CONNUI_NET_RAT_UNKNOWN;
}

static gboolean
_hspda(const gchar *tech)
{
  return !strcmp(tech, "hspa");
}

static gboolean
_parse_property(net_data *nd, const gchar *name, GVariant *value)
{
  gboolean notify = FALSE;
  cell_network_state *state = &nd->state;
  cell_network *network = state->network;

  g_debug("NET %s parsing property %s, type %s", nd->path, name,
          g_variant_get_type_string(value));

  if (!strcmp(name, OFONO_NETREG_PROPERTY_STRENGTH))
  {
    nd->state.network_signals_bar = g_variant_get_byte(value);

    if (state->rat_name)
      notify = TRUE;
  }
  else if (!strcmp(name, OFONO_NETREG_PROPERTY_NAME))
  {
    g_free(state->operator_name);
    state->operator_name = g_strdup(g_variant_get_string(value, NULL));
    g_free(network->operator_name);
    network->operator_name = g_strdup(state->operator_name);
    notify = TRUE;
  }
  else if (!strcmp(name, OFONO_NETREG_PROPERTY_MCC))
  {
    g_free(network->country_code);
    network->country_code = g_strdup(g_variant_get_string(value, NULL));
    notify = TRUE;
  }
  else if (!strcmp(name, OFONO_NETREG_PROPERTY_MNC))
  {
    g_free(network->operator_code);
    network->operator_code = g_strdup(g_variant_get_string(value, NULL));
    notify = TRUE;
  }
  else if (!strcmp(name, OFONO_NETREG_PROPERTY_LOCATION_AREA_CODE))
  {
    state->lac = g_variant_get_uint16(value);
    notify = TRUE;
  }
  else if (!strcmp(name, OFONO_NETREG_PROPERTY_CELL_ID))
  {
    state->cell_id = g_variant_get_uint32(value);
    notify = TRUE;
  }
  else if (!strcmp(name, OFONO_NETREG_PROPERTY_STATUS))
  {
    state->reg_status = _reg_status(g_variant_get_string(value, NULL));
    notify = TRUE;
  }
  else if (!strcmp(name, OFONO_NETREG_PROPERTY_TECHNOLOGY))
  {
    const gchar *tech = g_variant_get_string(value, NULL);

    state->rat_name = _rat_name(tech);
    state->network_hsdpa_allocated = _hspda(tech);
    notify = TRUE;
  }
  else if (!strcmp(name, OFONO_NETREG_PROPERTY_MODE))
  {
    const gchar *mode = g_variant_get_string(value, NULL);

    if (!strcmp(mode, "manual"))
      nd->selection_mode = CONNUI_NET_SELECT_MODE_MANUAL;
    else if (!strcmp(mode, "auto"))
      nd->selection_mode = CONNUI_NET_SELECT_MODE_AUTO;
    else if (!strcmp(mode, "auto-only"))
      nd->selection_mode = CONNUI_NET_SELECT_MODE_AUTO_ONLY;
    else
      nd->selection_mode = CONNUI_NET_SELECT_MODE_UNKNOWN;

    notify = TRUE;
  }

  return notify;
}

static void
_property_changed_cb(ConnuiCellNetworkRegistration *proxy, const gchar *name,
                    GVariant *value, gpointer user_data)
{
  net_data *nd = user_data;
  GVariant *v = g_variant_get_variant(value);

  g_debug("Modem %s network registration property %s changed", nd->path, name);

  if (_parse_property(nd, name, v))
    _notify(nd);

  g_variant_unref(v);
}

__attribute__((visibility("hidden"))) void
connui_cell_modem_add_netreg(connui_cell_context *ctx, const char *path)
{
  ConnuiCellNetworkRegistration *proxy;
  GError *error = NULL;

  proxy = connui_cell_network_registration_proxy_new_for_bus_sync(
        OFONO_BUS_TYPE, G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
        OFONO_SERVICE, path, NULL, &error);

  if (proxy)
  {
    net_data *nd = _net_data_create(proxy, path, ctx);
    GVariant *props;

    if (connui_cell_network_registration_call_get_properties_sync(proxy, &props,
                                                                NULL, &error))
    {
      GVariantIter i;
      gchar *name;
      GVariant *v;

      g_variant_iter_init(&i, props);

      while (g_variant_iter_loop(&i, "{&sv}", &name, &v))
        _parse_property(nd, name, v);

      g_variant_unref(props);
    }
    else
    {
      CONNUI_ERR("Unable to get modem [%s] network registration properties: %s",
                 path, error->message);
      g_error_free(error);
    }

    nd->properties_changed_id =
        g_signal_connect(proxy, "property-changed",
                         G_CALLBACK(_property_changed_cb), nd);

    _notify_all(ctx);
  }
  else
  {
    CONNUI_ERR("Error creating OFONO network registration proxy for %s [%s]",
               path, error->message);
    g_error_free(error);
  }
}

__attribute__((visibility("hidden"))) void
connui_cell_modem_remove_netreg(ConnuiCellModem *modem)
{
  g_object_set_data(G_OBJECT(modem), DATA, NULL);
}

#if 0
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
#endif

void
connui_cell_net_status_close(cell_network_state_cb cb)
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);

  g_return_if_fail(ctx != NULL);

  ctx->net_status_cbs = connui_utils_notify_remove(ctx->net_status_cbs, cb);

  connui_cell_context_destroy(ctx);
}

gboolean
connui_cell_net_status_register(cell_network_state_cb cb, gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);

  g_return_val_if_fail(ctx != NULL, FALSE);

  ctx->net_status_cbs = connui_utils_notify_add(ctx->net_status_cbs,
                                                (connui_utils_notify)cb,
                                                user_data);

  _notify_all(ctx);

  return TRUE;
}

static gchar *
_mbpi_get_name(guint mcc, guint mnc)
{
  xmlDocPtr doc;
  gchar *name = NULL;

  doc = xmlParseFile(MBPI_DATABASE);

  if (doc)
  {
    /* Create xpath evaluation context */
    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);

    if (ctx)
    {
      gchar *xpath = g_strdup_printf(
        "//network-id[@mcc='%03u' and @mnc='%02u']/../../name/text()", mcc,
        mnc);
      xmlXPathObjectPtr obj = xmlXPathEvalExpression(BAD_CAST xpath, ctx);

      if (obj && obj->nodesetval)
      {
        xmlNodeSetPtr nodes = obj->nodesetval;

        if (nodes->nodeNr)
        {
          xmlChar *content = xmlNodeGetContent(nodes->nodeTab[0]);

          name = g_strdup((const gchar *)content);

          xmlFree(content);
        }

        xmlXPathFreeObject(obj);
      }
      else
        CONNUI_ERR("Unable to evaluate xpath expression '%s'", xpath);

      g_free(xpath);
      xmlXPathFreeContext(ctx);
    }
    else
      CONNUI_ERR("Unable to create new XPath context");

    xmlFreeDoc(doc);
  }
  else
    CONNUI_ERR("Unable to parse '" MBPI_DATABASE "'");

  return name;
}

gchar *
connui_cell_net_get_operator_name(cell_network *network, GError **error)
{
  gchar *name = NULL;
  connui_cell_context *ctx;
  GHashTableIter iter;
  gpointer modem;
  guint mcc;
  guint mnc;

  g_return_val_if_fail(network != NULL, NULL);

  ctx = connui_cell_context_get(error);
  g_return_val_if_fail(ctx != NULL, NULL);

  mcc = g_ascii_strtoll(network->country_code, NULL, 10);
  mnc = g_ascii_strtoll(network->operator_code, NULL, 10);

  g_hash_table_iter_init (&iter, ctx->modems);

  while (!name && g_hash_table_iter_next(&iter, NULL, &modem))
  {
    net_data *nd = g_object_get_data(G_OBJECT(modem), DATA);

    if (nd)
    {
      GVariant *operators;

      if (connui_cell_network_registration_call_get_operators_sync(
            nd->proxy, &operators, NULL, NULL))
      {
        GVariantIter i;
        GVariant *dict;

        g_variant_iter_init(&i, operators);

        while (!name && g_variant_iter_next (&i, "(&o@a{sv})", NULL, &dict))
        {
          const gchar *s;

          if (g_variant_lookup(dict, "MobileCountryCode", "&s", &s) &&
              mcc == g_ascii_strtoll(s, NULL, 10) &&
              g_variant_lookup(dict, "MobileNetworkCode", "&s", &s) &&
              mnc == g_ascii_strtoll(s, NULL, 10) &&
              g_variant_lookup(dict, "Name", "&s", &s))
          {
            name = g_strdup(s);
          }
        }

        g_variant_unref(operators);
      }
    }
  }

  if (name && *name)
  {
    connui_cell_context_destroy(ctx);
    return name;
  }

  g_free(name);

  name = _mbpi_get_name(mcc, mnc);

  if (name && !*name)
  {
    g_free(name);
    name = NULL;
  }

  connui_cell_context_destroy(ctx);

  return name;
}

connui_net_selection_mode
connui_cell_net_get_network_selection_mode(const gchar *modem_id,
                                           GError **error)
{
  connui_net_selection_mode mode = CONNUI_NET_SELECT_MODE_UNKNOWN;
  connui_cell_context *ctx;

  net_data *nd;

  g_return_val_if_fail(modem_id != NULL, mode);

  ctx = connui_cell_context_get(error);
  g_return_val_if_fail(ctx != NULL, mode);

  nd = _net_data_get(modem_id, error);

  if (nd)
    mode = nd->selection_mode;

  connui_cell_context_destroy(ctx);

  return mode;
}

connui_net_radio_access_tech
connui_cell_net_get_radio_access_mode(const char *modem_id,
                                      GError **error)
{
  connui_net_radio_access_tech rat = CONNUI_NET_RAT_UNKNOWN;
  connui_cell_context *ctx;

  net_data *nd;

  g_return_val_if_fail(modem_id != NULL, rat);

  ctx = connui_cell_context_get(error);
  g_return_val_if_fail(ctx != NULL, rat);

  nd = _net_data_get(modem_id, error);

  if (nd)
    rat = nd->state.rat_name;

  connui_cell_context_destroy(ctx);

  return rat;
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

static guint
get_next_call_id(connui_cell_context *ctx)
{
  ctx->service_call_id++;

  return ctx->service_call_id;
}

typedef void (*net_divert_reply_f)(DBusGProxy *, GError *, gpointer);

#if 0
static void
divert_activate_cb(DBusGProxy *proxy, DBusGProxyCall *call_id, void *user_data)
{
  sim_status_data *data = user_data;
  GError *error = NULL;

  dbus_g_proxy_end_call(proxy, call_id, &error, G_TYPE_INVALID);
  ((net_divert_reply_f)data->cb)(proxy, error, data->data);
}
#endif

#if 0
static void
connui_cell_net_divert_activate_reply(DBusGProxy *proxy, GError *error,
                                      const void *user_data)
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);
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
#endif

#if 0
static void
divert_cancel_cb(DBusGProxy *proxy, DBusGProxyCall *call_id, void *user_data)
{
  sim_status_data *data = user_data;
  GError *error = NULL;

  dbus_g_proxy_end_call(proxy, call_id, &error, G_TYPE_INVALID);
  ((net_divert_reply_f)data->cb)(proxy, error, data->data);
}
#endif

#if 0
static void
connui_cell_net_divert_cancel_reply(DBusGProxy *proxy, GError *error,
                                    const void *user_data)
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);
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
#endif

guint
connui_cell_net_set_call_forwarding_enabled(gboolean enabled,
                                            const gchar *phone_number,
                                            service_call_cb_f cb,
                                            gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);
  DBusGProxy *proxy;
  DBusGProxyCall *proxy_call;
  guint call_id;
  sim_status_data *data;

  return 0;

  if (!phone_number)
    phone_number = "";

  g_return_val_if_fail(ctx != NULL, 0);

  call_id = get_next_call_id(ctx);
  data = g_slice_new(sim_status_data);
  data->data = GUINT_TO_POINTER(call_id);

  if (enabled)
  {
#if 0
    proxy = ctx->csd_ss_proxy;
    data->cb = (GCallback)connui_cell_net_divert_activate_reply;
    proxy_call = dbus_g_proxy_begin_call(proxy, "DivertActivate",
                                         divert_activate_cb,
                                         data,
                                         destroy_sim_status_data,
                                         G_TYPE_UINT, 5,
                                         G_TYPE_STRING, phone_number,
                                         G_TYPE_UINT, 20, G_TYPE_INVALID);
#endif
  }
  else
  {
#if 0
    proxy = ctx->csd_ss_proxy;
    data->cb = (GCallback)connui_cell_net_divert_cancel_reply;
    proxy_call = dbus_g_proxy_begin_call(proxy, "DivertCancel",
                                         divert_cancel_cb, data,
                                         destroy_sim_status_data,
                                         G_TYPE_UINT, 5, G_TYPE_INVALID);
#endif
  }

  add_service_call(ctx, call_id, proxy, proxy_call, cb, user_data);

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
  connui_cell_context *ctx = connui_cell_context_get(NULL);
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
/*
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
*/
void
connui_cell_net_cancel_select(cell_net_select_cb cb)
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);

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
/*
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
*/
gboolean
connui_cell_net_select(cell_network *network, cell_net_select_cb cb,
                       gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);
  gboolean rv = FALSE;
  //sim_status_data *data;
  //net_data *nd;

  g_return_val_if_fail(ctx != NULL, FALSE);

  //nd = _net_data_get(modem_id, NULL);

  g_assert(0);

  if (ctx->select_network_call)
    connui_cell_net_cancel_select(NULL);

  if (network && !ctx->select_network_call)
  {
/*    data = g_slice_new(sim_status_data);

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
          G_TYPE_INVALID);*/
  }
  else
  {
/*    if (network || ctx->select_network_call)
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
                                G_TYPE_INVALID);*/
  }

  if (ctx->select_network_call)
    rv = TRUE;

  ctx->net_select_cbs =
      connui_utils_notify_add(ctx->net_select_cbs, cb, user_data);

  connui_cell_context_destroy(ctx);

  return rv;
}
/*
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
*/
void
connui_cell_reset_network()
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);
  g_assert(0);
  g_return_if_fail(ctx != NULL);

  if (ctx->select_network_call)
    connui_cell_net_cancel_select(NULL);

  if (!ctx->select_network_call)
  {
/*    sim_status_data *data = g_slice_new(sim_status_data);

    data->cb = (GCallback)net_reset_cb;
    data->data = ctx;
    ctx->select_network_call = dbus_g_proxy_begin_call_with_timeout(
          ctx->phone_net_proxy, "select_network", select_network_cb, data,
          destroy_sim_status_data, 500000,
          G_TYPE_UCHAR, NETWORK_SELECT_MODE_NO_SELECTION,
          G_TYPE_UCHAR, NETWORK_RAT_NAME_UNKNOWN,
          G_TYPE_STRING, NULL, G_TYPE_STRING, NULL, G_TYPE_INVALID);*/
  }

  connui_cell_context_destroy(ctx);
}

void
connui_cell_net_cancel_list(cell_net_list_cb cb)
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);

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
  connui_cell_context *ctx = connui_cell_context_get(NULL);

  g_return_val_if_fail(ctx != NULL, NULL);

  current_network.operator_code = ctx->network.operator_code;
  current_network.country_code = ctx->network.country_code;

  connui_cell_context_destroy(ctx);

  if (ctx->network.country_code && !ctx->select_network_call)
    return &current_network;

  return NULL;
}

gboolean
connui_cell_net_set_radio_access_mode(guchar selected_rat, gint *error_value)
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);
  GError *error = NULL;
  gint err_val = 1;

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (!dbus_g_proxy_call(ctx->phone_net_proxy,
                         "set_selected_radio_access_technology", &error,
                         G_TYPE_UCHAR, selected_rat, G_TYPE_INVALID,
                         G_TYPE_INT, &err_val, G_TYPE_INVALID))
  {
    CONNUI_ERR("Error with DBUS %s", error->message);
    g_clear_error(&error);
    err_val = 1;
  }

  if (error_value)
    *error_value = err_val;

  connui_cell_context_destroy(ctx);

  return err_val == 0;

}

static TpAccount *
find_ring_account(TpAccountManager *manager)
{
  GList *accounts, *l;
  TpAccount *rv = NULL;

  accounts = tp_account_manager_dup_valid_accounts(manager);

  for (l = accounts; l != NULL; l = l->next)
  {
    TpAccount *account = l->data;

    if (!strcmp(tp_account_get_cm_name(account), "ring"))
    {
      g_object_ref(account);
      rv = account;
      break;
    }
  }

  g_list_free_full(accounts, g_object_unref);

  return rv;
}

static void
_cid_get_cb(GObject *object, GAsyncResult *res, gpointer user_data)
{
  guint32 anonymity = 0;
  TpAccountManager *manager = (TpAccountManager *)object;
  service_call_data *scd = user_data;
  connui_cell_context *ctx = connui_cell_context_get(NULL);

  g_assert(ctx);

  if (tp_proxy_prepare_finish(object, res, &scd->error))
  {
    TpAccount *ring = find_ring_account(manager);

    if (ring)
    {
      const GHashTable *parameters = tp_account_get_parameters (ring);

      anonymity = tp_asv_get_uint32(
            parameters, TP_PROP_CONNECTION_INTERFACE_ANONYMITY_ANONYMITY_MODES,
            NULL);
      g_object_unref(ring);
    }
  }
  else
    CONNUI_ERR("Error preparing TpAccountManager: %s", scd->error->message);

  ((cell_get_anonymity_cb)scd->callback)(anonymity, scd->error, scd->user_data);

  service_call_remove(ctx, scd->id);
  connui_cell_context_destroy(ctx);
}

gboolean
connui_cell_net_get_caller_id_anonymity(cell_get_anonymity_cb cb,
                                        gpointer user_data)
{
  TpAccountManager *manager = tp_account_manager_dup();
  connui_cell_context *ctx = connui_cell_context_get(NULL);
  gboolean rv = FALSE;

  g_return_val_if_fail(ctx != NULL, FALSE);

  if (manager)
  {
    gint id = service_call_next_id(ctx);
    service_call_data *scd;

    scd = service_call_add(ctx, id, (GCallback)cb, user_data);
    tp_proxy_prepare_async(manager, NULL, _cid_get_cb, scd);
    g_object_unref(manager);
    rv = TRUE;
  }
  else
    CONNUI_ERR("Can't create TpAccountManager");

  connui_cell_context_destroy(ctx);

  return rv;
}

static void
_cid_update_parameter_cb(GObject *object, GAsyncResult *res, gpointer user_data)
{
  service_call_data *scd = user_data;
  TpAccount *account = (TpAccount *)object;
  GStrv reconnect_required = NULL;

  if (!tp_account_update_parameters_finish(account, res, &reconnect_required,
                                           &scd->error))
  {
    CONNUI_ERR("Error updating TpAccount parameter: %s", scd->error->message);
  }

  g_strfreev(reconnect_required);

  if (scd->callback)
    ((cell_set_cb)scd->callback)(scd->error, scd->user_data);

  service_call_destroy(scd);
}

static void
_cid_set_cb(GObject *object, GAsyncResult *res, gpointer user_data)
{
  service_call_data *scd = user_data;
  TpAccountManager *manager = (TpAccountManager *)object;

  if (tp_proxy_prepare_finish(object, res, &scd->error))
  {
    TpAccount *ring_account = find_ring_account(manager);

    if (ring_account)
    {
      guint anonymity = GPOINTER_TO_UINT(scd->async_data);
      const gchar *unset[] = {NULL};

      GHashTable *set = tp_asv_new(
            TP_PROP_CONNECTION_INTERFACE_ANONYMITY_ANONYMITY_MODES,
            G_TYPE_UINT, anonymity, NULL);

      tp_account_update_parameters_async(
            ring_account, set, unset, _cid_update_parameter_cb, user_data);
      g_object_unref(ring_account);
    }
  }
  else
  {
    CONNUI_ERR("Error preparing TpAccountManager: %s", scd->error->message);

    if (scd->callback)
      ((cell_set_cb)scd->callback)(scd->error, scd->user_data);

    service_call_destroy(scd);
  }
}

gboolean
connui_cell_net_set_caller_id_anonymity(guint anonimity, cell_set_cb cb,
                                        gpointer user_data)
{
  TpAccountManager *manager = tp_account_manager_dup();
  service_call_data *scd;

  if (!manager)
  {
    CONNUI_ERR("Can't create TpAccountManager");
    return FALSE;
  }

  scd = g_new0(service_call_data, 1);
  scd->callback = G_CALLBACK(cb);
  scd->user_data = user_data;
  scd->async_data = GUINT_TO_POINTER(anonimity);

  tp_proxy_prepare_async(manager, NULL, _cid_set_cb, scd);
  g_object_unref(manager);

  return TRUE;
}

void
connui_cell_net_set_caller_id_presentation_bluez(const gchar *caller_id)
{
  DBusMessage *msg;

  g_return_if_fail(caller_id != NULL);

  msg = connui_dbus_create_method_call("org.bluez",
                                       "/com/nokia/MaemoTelephony",
                                       "com.nokia.MaemoTelephony",
                                       "SetCallerId",
                                       DBUS_TYPE_STRING, &caller_id,
                                       DBUS_TYPE_INVALID);

  if (msg)
  {
    if (!connui_dbus_send_system_mcall(msg, -1, NULL, NULL, NULL))
      CONNUI_ERR("Unable to send SetCallerId to bluez");

    dbus_message_unref(msg);
  }
  else
    CONNUI_ERR("unable to create dbus message");

}
