#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <libosso.h>
#include <connui/connui-utils.h>
#include <connui/connui-log.h>
#include <connui/connui-dbus.h>
#include <telepathy-glib/telepathy-glib.h>
#include <gio/gio.h>

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

static void ofono_signal_strength_change(guchar signals_bar);
static void ofono_rat_change(gchar* technology);
static void ofono_reg_status_change(gchar* status);
static void ofono_countrycode_change(gchar* country_code);
static void ofono_operatorcode_change(gchar* operator_code);
static void ofono_reg_operatorname_change(gchar* operator_name);
static void ofono_cellid_change(guint cellid);
static void ofono_lac_change(guint cellid);

void debug_call_all(const char* name, GVariant* value) {
    /* XXX: just testing, this is a mess :-) */

    if (!strcmp(name, "Strength")) {
        CONNUI_ERR("netreg_propchange strength");
        guchar strength = 0;
        g_variant_get(value, "y", &strength);
        ofono_signal_strength_change(strength);
    } else if (!strcmp(name, "Technology")) {
        CONNUI_ERR("netreg_propchange technology");
        gchar* str = NULL;
        g_variant_get(value, "s", &str);
        ofono_rat_change(str);
        g_free(str);
    } else if (!strcmp(name, "Status")) {
        CONNUI_ERR("netreg_propchange status");
        gchar* str = NULL;
        g_variant_get(value, "s", &str);
        ofono_reg_status_change(str);
        g_free(str);
    } else if (!strcmp(name, "MobileCountryCode")) {
        CONNUI_ERR("netreg_propchange countrycode");
        gchar* str = NULL;
        g_variant_get(value, "s", &str);
        ofono_countrycode_change(str);
        g_free(str);
    } else if (!strcmp(name, "MobileNetworkCode")) {
        CONNUI_ERR("netreg_propchange networkcode");
        gchar* str = NULL;
        g_variant_get(value, "s", &str);
        ofono_operatorcode_change(str);
        g_free(str);
    } else if (!strcmp(name, "CellId")) {
        CONNUI_ERR("netreg_propchange cellid");
        guint cellid = 0;
        g_variant_get(value, "u", &cellid);
        ofono_cellid_change(cellid);
    } else if (!strcmp(name, "LocationAreaCode")) {
        CONNUI_ERR("netreg_propchange lac");
        guint16 lac = 0;
        g_variant_get(value, "q", &lac);
        ofono_lav_change(lac);
    } else if (!strcmp(name, "Name")) {
        CONNUI_ERR("netreg_propchange name");
        gchar* str = NULL;
        g_variant_get(value, "s", &str);
        ofono_reg_operatorname_change(str);
        g_free(str);
    }
}

/* TODO: example of getter, rather than property changed handler */
void debug_netreg(OfonoNetReg* netreg) {
	CONNUI_ERR("debug_netreg");
	guint i;
    OfonoObject* obj = ofono_netreg_object(netreg);

    /* XXX: Shouldn't these keys be freed ? */
    GPtrArray* keys = ofono_object_get_property_keys(obj);
	CONNUI_ERR("debug_netreg keys len %d", keys->len);
    for (i=0; i<keys->len; i++) {
        const char* key = keys->pdata[i];
        if (1) {
            /* XXX: shouldn't these gvariants be freed / dereffed? */
            GVariant* v = ofono_object_get_property(obj, key, NULL);
            gchar* text = g_variant_print(v, FALSE);
            CONNUI_ERR("%s: %s\n", key, text);
            g_free(text);

            debug_call_all(key, v);
        } else {
            CONNUI_ERR("%s\n", key);
        }
    }
	CONNUI_ERR("debug_netreg_done");
}

static void netreg_propchange(OfonoNetReg *sender, const char* name, GVariant *value, connui_cell_context *ctx) {
    gchar* text = g_variant_print(value, FALSE);
    CONNUI_ERR("netreg_propchange: %s = %s\n", name, text);
    g_free(text);

    debug_call_all(name, value);

    debug_netreg(ctx->ofono_netreg); // XXX remove
    //g_variant_unref(value); // ???
}

void set_netreg(connui_cell_context* ctx) {
    CONNUI_ERR("set_netreg");
    debug_netreg(ctx->ofono_netreg); // XXX: move or remove this

    // TODO: Create function to fill struct without doing callbacks, and then do
    // one callback.

    ctx->ofono_netreg_property_changed_id = ofono_netreg_add_property_changed_handler(ctx->ofono_netreg,
            (OfonoNetRegPropertyHandler)netreg_propchange,
            NULL /* name NULL = all properties? */,
            (void*)ctx);

    return;
}

void release_netreg(connui_cell_context* ctx) {
    CONNUI_ERR("release_netreg");

    if ((ctx->ofono_netreg) && (ctx->ofono_netreg_property_changed_id)) {
        ofono_netreg_remove_handler(ctx->ofono_netreg, ctx->ofono_netreg_property_changed_id);
        ctx->ofono_netreg_property_changed_id = 0;
    }

    return;
}

static void
net_signal_strength_change_notify(connui_cell_context *ctx)
{
  connui_utils_notify_notify_POINTER(ctx->net_status_cbs, &ctx->state);
}


static void ofono_signal_strength_change(guchar signals_bar) {
    connui_cell_context *ctx = connui_cell_context_get();
    g_return_if_fail(ctx != NULL);

    ctx->state.network_signals_bar = signals_bar;

    net_signal_strength_change_notify(ctx);

    connui_cell_context_destroy(ctx);
}

#if 0
/* TODO: make sure all of this is implemented */
#define NETWORK_RAT_NAME_UNKNOWN         0x00
#define NETWORK_GSM_RAT                  0x01
#define NETWORK_UMTS_RAT                 0x02
#define NETWORK_MASK_GPRS_SUPPORT   0x01
#define NETWORK_MASK_CS_SERVICES    0x02
#define NETWORK_MASK_EGPRS_SUPPORT  0x04
#define NETWORK_MASK_HSDPA_AVAIL    0x08
#define NETWORK_MASK_HSUPA_AVAIL    0x10
};
#endif

static void ofono_rat_change(gchar* technology) {
    connui_cell_context *ctx = connui_cell_context_get();
    g_return_if_fail(ctx != NULL);

    /* TODO: name thing / map */

    if (!strcmp("lte", technology)) {
        ctx->state.rat_name = NETWORK_LTE_RAT;
    } else if (!strcmp("hspa", technology)) {
        // XXX
        ctx->state.rat_name = NETWORK_UMTS_RAT;
        ctx->state.network_hsdpa_allocated = 1;
    } else if (!strcmp("umts", technology)) {
        ctx->state.rat_name = NETWORK_UMTS_RAT;
    } else if (!strcmp("edge", technology)) {
        ctx->state.rat_name = NETWORK_GSM_RAT;
        // XXX: Maybe add this bit to supported_services ?
        //if (priv->state.supported_services & NETWORK_MASK_EGPRS_SUPPORT)
    } else if (!strcmp("gsm", technology)) {
        ctx->state.rat_name = NETWORK_GSM_RAT;
    }

    net_signal_strength_change_notify(ctx);

    connui_cell_context_destroy(ctx);
}

#if 0
    enum net_registration_status
{
    NETWORK_REG_STATUS_HOME = 0x00, // CS is registered to home network
    NETWORK_REG_STATUS_ROAM,        // CS is registered to some other network than home network
    NETWORK_REG_STATUS_ROAM_BLINK,  // CS is registered to non-home system in a non-home area ('a' or 'b' area)
    NETWORK_REG_STATUS_NOSERV,      // CS is not in service
    NETWORK_REG_STATUS_NOSERV_SEARCHING,    // CS is not in service, but is currently searching for service
    NETWORK_REG_STATUS_NOSERV_NOTSEARCHING, // CS is not in service and it is not currently searching for service
    NETWORK_REG_STATUS_NOSERV_NOSIM,        // CS is not in service due to missing SIM or missing subscription
    NETWORK_REG_STATUS_POWER_OFF = 0x08,    // CS is in power off state
    NETWORK_REG_STATUS_NSPS,                // CS is in No Service Power Save State (currently not listening to any cell)
    NETWORK_REG_STATUS_NSPS_NO_COVERAGE,    // CS is in No Service Power Save State (CS is entered to this state because there is no network coverage)
    NETWORK_REG_STATUS_NOSERV_SIM_REJECTED_BY_NW // CS is not in service due to missing subscription
};
#endif

/* TODO: This only implements basic status changes, it lacks all the power
 * saving modes above */
static void ofono_reg_status_change(gchar* status) {
    connui_cell_context *ctx = connui_cell_context_get();
    g_return_if_fail(ctx != NULL);

    // TODO: also use sim status here? (no service because of no sim, etc)

    if (!strcmp("unregistered", status)) {
        ctx->state.reg_status = NETWORK_REG_STATUS_NOSERV;
    } else if (!strcmp("registered", status)) {
        ctx->state.reg_status = NETWORK_REG_STATUS_HOME;
    } else if (!strcmp("searching", status)) {
        ctx->state.reg_status = NETWORK_REG_STATUS_NOSERV_SEARCHING;
    } else if (!strcmp("denied", status)) {
        // XXX: might be wrong
        ctx->state.reg_status = NETWORK_REG_STATUS_NOSERV_SIM_REJECTED_BY_NW;
    } else if (!strcmp("unknown", status)) {
        // TODO: this is not correct
        ctx->state.reg_status = NETWORK_REG_STATUS_NOSERV;
        //ctx->state.reg_status = ...
    } else if (!strcmp("roaming", status)) {
        ctx->state.reg_status = NETWORK_REG_STATUS_ROAM;
        // TODO: NETWORK_REG_STATUS_ROAM_BLINK
    }

    net_signal_strength_change_notify(ctx);
    connui_cell_context_destroy(ctx);
}

static void ofono_cellid_change(guint cellid) {
    connui_cell_context *ctx = connui_cell_context_get();
    g_return_if_fail(ctx != NULL);

    ctx->state.cell_id = cellid;

    connui_cell_context_destroy(ctx);
    net_signal_strength_change_notify(ctx);
}

static void ofono_lac_change(guint16 lac) {
    connui_cell_context *ctx = connui_cell_context_get();
    g_return_if_fail(ctx != NULL);

    ctx->state.lac = lac;

    connui_cell_context_destroy(ctx);
    net_signal_strength_change_notify(ctx);
}

static void ofono_reg_operatorname_change(gchar* operator_name) {
    connui_cell_context *ctx = connui_cell_context_get();
    g_return_if_fail(ctx != NULL);

    // XXX: move elsewhere, dedup
    if (!ctx->state.network)
        ctx->state.network = g_new0(cell_network, 1);

    // TODO: might not be the right place to free this.
    if (ctx->state.network->operator_name) {
        g_free(ctx->state.network->operator_name);
        ctx->state.network->operator_name = NULL;
    }
    ctx->state.network->operator_name = g_strdup(operator_name);

    /* XXX: We also set operator_name on cell_network_state here for now, as
     * it's not clear what the difference is.
     * Probably want this elsewhere? (Also, with alternative operator name?) */
    if (ctx->state.operator_name) {
        g_free(ctx->state.operator_name);
        ctx->state.operator_name = NULL;
    }
    ctx->state.operator_name = g_strdup(operator_name);

    connui_cell_context_destroy(ctx);
    net_signal_strength_change_notify(ctx);
}

static void ofono_countrycode_change(gchar* country_code) {
    connui_cell_context *ctx = connui_cell_context_get();
    g_return_if_fail(ctx != NULL);

    // XXX: move elsewhere, dedup
    if (!ctx->state.network)
        ctx->state.network = g_new0(cell_network, 1);

    // TODO: might not be the right place to free this.
    if (ctx->state.network->country_code) {
        g_free(ctx->state.network->country_code);
        ctx->state.network->country_code = NULL;
    }
    ctx->state.network->country_code = g_strdup(country_code);

    connui_cell_context_destroy(ctx);
    net_signal_strength_change_notify(ctx);
}

static void ofono_operatorcode_change(gchar* operator_code) {
    connui_cell_context *ctx = connui_cell_context_get();
    g_return_if_fail(ctx != NULL);

    // XXX: move elsewhere, dedup
    if (!ctx->state.network)
        ctx->state.network = g_new0(cell_network, 1);

    // TODO: might not be the right place to free this.
    if (ctx->state.network->operator_code) {
        g_free(ctx->state.network->operator_code);
        ctx->state.network->operator_code = NULL;
    }
    ctx->state.network->operator_code = g_strdup(operator_code);

    connui_cell_context_destroy(ctx);
    net_signal_strength_change_notify(ctx);
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

gboolean
connui_cell_net_set_radio_access_mode(guchar selected_rat, gint *error_value)
{
  connui_cell_context *ctx = connui_cell_context_get();
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

static gboolean
connui_cell_order_mc_accounts(GAsyncReadyCallback async_cb,
                              const gchar *presentation, cell_clir_cb cb,
                              gpointer cb_data)
{
  TpAccountManager *manager;
  connui_cell_context *ctx = connui_cell_context_get();

  g_return_val_if_fail(ctx != NULL, FALSE);

  manager = tp_account_manager_dup();

  if (!manager)
  {
    CONNUI_ERR("Can't create TpAccountManager");
    connui_cell_context_destroy(ctx);
    return FALSE;
  }

  ctx->clir_cb = cb;
  ctx->clir_cb_data = cb_data;

  tp_proxy_prepare_async (manager, NULL, async_cb, (gpointer)presentation);
  g_object_unref (manager);

  connui_cell_context_destroy(ctx);

  return TRUE;
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
connui_cell_net_get_caller_id_presentation_cb(GObject *object,
                                              GAsyncResult *res,
                                              gpointer user_data)
{
  const gchar *privacy = NULL;
  connui_cell_context *ctx;
  TpAccountManager *manager = (TpAccountManager *)object;
  GError *error = NULL;
  gboolean success = TRUE;

  if (!tp_proxy_prepare_finish(object, res, &error))
  {
    CONNUI_ERR("Error preparing TpAccountManager: %s", error->message);
    g_clear_error(&error);
    success = FALSE;
  }

  ctx = connui_cell_context_get();

  g_return_if_fail(ctx != NULL);

  if (success)
  {
    TpAccount *ring_account = find_ring_account(manager);

    if (ring_account)
    {
      privacy = tp_asv_get_string(
            tp_account_get_parameters(ring_account),
            "com.nokia.Telepathy.Connection.Interface.GSM.Privacy");
      g_object_unref(ring_account);
    }
  }

  if (!privacy)
    privacy = "";

  if (ctx->clir_cb)
  {
    ctx->clir_cb(TRUE, 0, privacy, ctx->clir_cb_data);
    ctx->clir_cb = NULL;
  }
  else
    CONNUI_ERR("CLIR callback is NULL");

  ctx->clir_cb_data = NULL;
  connui_cell_context_destroy(ctx);
}

gboolean
connui_cell_net_get_caller_id_presentation(cell_clir_cb cb,
                                           gpointer user_data)
{
  return connui_cell_order_mc_accounts(
        connui_cell_net_get_caller_id_presentation_cb, NULL, cb, user_data);
}

static void
connui_cell_net_get_caller_id_update_parameter_cb(GObject *object,
                                                  GAsyncResult *res,
                                                  gpointer user_data)
{
  connui_cell_context *ctx;
  TpAccount *account = (TpAccount *)object;
  GError *error = NULL;
  GStrv reconnect_required = NULL;

  if (!tp_account_update_parameters_finish(account, res, &reconnect_required,
                                           &error))
  {
    CONNUI_ERR("Error updating TpAccount parameter: %s", error->message);
    g_clear_error(&error);
  }

  g_strfreev(reconnect_required);

  ctx = connui_cell_context_get();

  g_return_if_fail(ctx != NULL);

  if (ctx->clir_cb)
  {
    ctx->clir_cb(FALSE, 0, NULL, ctx->clir_cb_data);
    ctx->clir_cb = NULL;
  }
  else
    CONNUI_ERR("CLIR callback is NULL");

  ctx->clir_cb_data = NULL;
  connui_cell_context_destroy(ctx);
}

static void
connui_cell_net_set_caller_id_presentation_cb(GObject *object,
                                              GAsyncResult *res,
                                              gpointer user_data)
{
  TpAccountManager *manager = (TpAccountManager *)object;
  GError *error = NULL;

  /* Failure should be handled better I guess, like calling the cb, etc. */
  if (!tp_proxy_prepare_finish(object, res, &error))
  {
    CONNUI_ERR("Error preparing TpAccountManager: %s", error->message);
    g_clear_error(&error);
    return;
  }

  TpAccount *ring_account = find_ring_account(manager);

  if (ring_account)
  {
    const gchar *unset[] = {NULL};
    GHashTable *set = tp_asv_new(
          "com.nokia.Telepathy.Connection.Interface.GSM.Privacy",
          G_TYPE_STRING, (const gchar *)user_data, NULL);

    tp_account_update_parameters_async(
          ring_account, set, unset,
          connui_cell_net_get_caller_id_update_parameter_cb, user_data);
    g_object_unref(ring_account);
  }
}

gboolean
connui_cell_net_set_caller_id_presentation(const gchar *presentation,
                                           cell_clir_cb cb, gpointer user_data)
{
  return connui_cell_order_mc_accounts(
        connui_cell_net_set_caller_id_presentation_cb, presentation, cb,
        user_data);
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
