#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <libosso.h>
#include <connui/connui-utils.h>
#include <connui/connui-log.h>
#include <libxml/xpath.h>

#include <string.h>

#include "context.h"

#include "connui-cellular-sim.h"

#include "sim.h"

typedef struct _sim_data
{
  connui_cell_context *ctx;
  OrgOfonoSimManager *proxy;
  gchar *path;

  gboolean present;
  guint mcc;
  guint mnc;
  gchar *imsi;
  gchar *spn;
  connui_sim_security_code_type pin_required;
  gchar *pin_required_s;

  gulong properties_changed_id;
  guint idle_status_id;
  guint idle_security_code_id;
}
sim_data;

#define DATA "connui_cell_sim_data"

typedef struct _code_query_callback_data
{
  cell_sec_code_query_cb_callback cb;
  gpointer user_data;
} code_query_callback_data;

static connui_sim_status _get_status(sim_data *sd);
static void verify_required_pin(sim_data *sd);

static sim_data *
_sim_data_get(const char *path, GError **error)
{
  connui_cell_context *ctx = connui_cell_context_get(error);
  OrgOfonoModem *modem;

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
_sim_data_destroy(gpointer data)
{
  sim_data *sd = data;
  connui_sim_status status = CONNUI_SIM_STATUS_UNKNOWN;

  g_debug("Removing ofono sim manager for %s", sd->path);

  g_free(sd->imsi);
  g_free(sd->spn);
  g_free(sd->pin_required_s);

  if (sd->idle_status_id)
    g_source_remove(sd->idle_status_id);

  if (sd->idle_security_code_id)
    g_source_remove(sd->idle_security_code_id);

  g_signal_handler_disconnect(sd->proxy, sd->properties_changed_id);
  g_object_unref(sd->proxy);

  connui_utils_notify_notify(sd->ctx->sim_status_cbs, sd->path, &status, NULL);

  g_free(sd->path);
  g_free(sd);
}

static sim_data *
_sim_data_create(OrgOfonoSimManager *proxy, const gchar *path,
                 connui_cell_context *ctx)
{
  sim_data *sd = g_new0(sim_data, 1);
  OrgOfonoModem *modem = g_hash_table_lookup(ctx->modems, path);

  g_assert(modem);

  g_object_set_data_full(G_OBJECT(modem), DATA, sd, _sim_data_destroy);

  sd->path = g_strdup(path);
  sd->proxy = proxy;
  sd->ctx = ctx;
  sd->pin_required = CONNUI_SIM_SECURITY_CODE_UNKNOWN;

  return sd;
}

static gboolean
_idle_notify_status(gpointer user_data)
{
  sim_data *sd = user_data;
  guint status = _get_status(sd);

  sd->idle_status_id = 0;

  connui_utils_notify_notify(sd->ctx->sim_status_cbs, sd->path, &status, NULL);

  return G_SOURCE_REMOVE;
}

static void
_notify_status(sim_data *sd)
{
  if (sd && !sd->idle_status_id)
    sd->idle_status_id = g_idle_add(_idle_notify_status, sd);
}

static void
_notify_status_all(connui_cell_context *ctx)
{
  GHashTableIter iter;
  gpointer modem;

  g_hash_table_iter_init (&iter, ctx->modems);

  while (g_hash_table_iter_next (&iter, NULL, &modem))
    _notify_status(g_object_get_data(G_OBJECT(modem), DATA));
}

static gboolean
_idle_notify_security_code(gpointer user_data)
{
  sim_data *sd = user_data;
  guint status = _get_status(sd);

  sd->idle_security_code_id = 0;

  if (status == CONNUI_SIM_STATUS_OK_PIN_REQUIRED ||
      status == CONNUI_SIM_STATUS_OK_PUK_REQUIRED)
  {
    verify_required_pin(sd);
  }

  return G_SOURCE_REMOVE;
}

static void
_notify_security_code(sim_data *sd)
{
  if (sd && !sd->idle_security_code_id)
    sd->idle_security_code_id = g_idle_add(_idle_notify_security_code, sd);
}

static void
_notify_security_code_all(connui_cell_context *ctx)
{
  GHashTableIter iter;
  gpointer modem;

  g_hash_table_iter_init (&iter, ctx->modems);

  while (g_hash_table_iter_next (&iter, NULL, &modem))
    _notify_security_code(g_object_get_data(G_OBJECT(modem), DATA));
}

void
_get_pin_required(sim_data *sd, GVariant *value)
{
  const gchar *pin_required;

  if (!value)
    return;

  pin_required = g_variant_get_string(value, NULL);
  g_free(sd->pin_required_s);
  sd->pin_required_s = g_strdup(pin_required);

  if (!strcmp(pin_required, "none"))
    sd->pin_required = CONNUI_SIM_SECURITY_CODE_NONE;
  else if (!strcmp(pin_required, "pin"))
    sd->pin_required = CONNUI_SIM_SECURITY_CODE_PIN;
  else if (!strcmp(pin_required, "puk"))
    sd->pin_required = CONNUI_SIM_SECURITY_CODE_PUK;
  else if (!strcmp(pin_required, "pin2"))
    sd->pin_required = CONNUI_SIM_SECURITY_CODE_PIN2;
  else if (!strcmp(pin_required, "puk2"))
    sd->pin_required = CONNUI_SIM_SECURITY_CODE_PUK2;
  else
    sd->pin_required = CONNUI_SIM_SECURITY_CODE_UNSUPPORTED;
}

static gboolean
_parse_property(sim_data *sd, const gchar *name, GVariant *value)
{
  gboolean notify = FALSE;

  g_debug("SIM %s parsing property %s, type %s", sd->path, name,
          g_variant_get_type_string(value));

  if (!strcmp(name, OFONO_SIMMGR_PROPERTY_PRESENT))
  {
    sd->present = g_variant_get_boolean(value);
    notify = TRUE;
  }
  else if (!strcmp(name, OFONO_SIMMGR_PROPERTY_MCC))
    sd->mcc = atoi(g_variant_get_string(value, NULL));
  else if (!strcmp(name, OFONO_SIMMGR_PROPERTY_MNC))
    sd->mnc = atoi(g_variant_get_string(value, NULL));
  else if (!strcmp(name, OFONO_SIMMGR_PROPERTY_IMSI))
  {
    g_free(sd->imsi);
    sd->imsi = g_strdup((g_variant_get_string(value, NULL)));
  }
  else if (!strcmp(name, OFONO_SIMMGR_PROPERTY_SPN))
  {
    g_free(sd->spn);
    sd->spn = g_strdup((g_variant_get_string(value, NULL)));
  }
  else if (!strcmp(name, OFONO_SIMMGR_PROPERTY_PIN_REQUIRED))
  {
    _get_pin_required(sd, value);
    notify = TRUE;
  }

  return notify;
}

static void
_property_changed_cb(OrgOfonoSimManager *proxy, const gchar *name,
                     GVariant *value, gpointer user_data)
{
  sim_data *sd = user_data;
  GVariant *v = g_variant_get_variant(value);

  g_debug("Modem %s sim property %s changed", sd->path, name);

  if (_parse_property(sd, name, v))
  {
    _notify_status(sd);
    _notify_security_code(sd);
  }

  g_variant_unref(v);
}

__attribute__((visibility("hidden"))) void
connui_cell_modem_add_simmgr(connui_cell_context *ctx, const char *path)
{
  OrgOfonoSimManager *proxy;
  GError *error = NULL;

  g_debug("Adding ofono sim manager for %s", path);

  proxy = org_ofono_sim_manager_proxy_new_for_bus_sync(
        OFONO_BUS_TYPE, G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
        OFONO_SERVICE, path, NULL, &error);

  if (proxy)
  {
    sim_data *sd = _sim_data_create(proxy, path, ctx);
    GVariant *props;

    if (org_ofono_sim_manager_call_get_properties_sync(proxy, &props, NULL,
                                                       &error))
    {
      GVariantIter i;
      gchar *name;
      GVariant *v;

      g_variant_iter_init(&i, props);

      while (g_variant_iter_loop(&i, "{&sv}", &name, &v))
        _parse_property(sd, name, v);

      g_variant_unref(props);
    }
    else
    {
      CONNUI_ERR("Unable to get modem [%s] sim manager properties: %s",
                 path, error->message);
      g_error_free(error);
    }

    sd->properties_changed_id =
        g_signal_connect(proxy, "property-changed",
                         G_CALLBACK(_property_changed_cb), sd);

    _notify_status_all(ctx);
    _notify_security_code_all(ctx);
  }
  else
  {
    CONNUI_ERR("Error creating OFONO sim manager proxy for %s [%s]", path,
               error->message);
    g_error_free(error);
  }
}

__attribute__((visibility("hidden"))) void
connui_cell_modem_remove_simmgr(OrgOfonoModem *modem)
{
  g_object_set_data(G_OBJECT(modem), DATA, NULL);
}

static connui_sim_status
_get_status(sim_data *sd)
{
  guint status = CONNUI_SIM_STATUS_NO_SIM;

  if (sd->present)
  {
    status = CONNUI_SIM_STATUS_OK;

    switch (sd->pin_required)
    {
      case CONNUI_SIM_SECURITY_CODE_UNKNOWN:
        status = CONNUI_SIM_STATUS_UNKNOWN;
        break;
      case CONNUI_SIM_SECURITY_CODE_NONE:
      case CONNUI_SIM_SECURITY_CODE_PIN2:
        break;
      case CONNUI_SIM_SECURITY_CODE_PIN:
        status = CONNUI_SIM_STATUS_OK_PIN_REQUIRED;
        break;
      case CONNUI_SIM_SECURITY_CODE_PUK:
        status = CONNUI_SIM_STATUS_OK_PUK_REQUIRED;
        break;
      case CONNUI_SIM_SECURITY_CODE_PUK2:
        status = CONNUI_SIM_STATUS_OK_PUK2_REQUIRED;
        break;
      case CONNUI_SIM_SECURITY_CODE_UNSUPPORTED:
        status = CONNUI_SIM_STATE_LOCKED;
        break;
    }
  }

  return status;
}

connui_sim_status
connui_cell_sim_get_status(const char *modem_id, GError **error)
{
  sim_data *sd = _sim_data_get(modem_id, error);

  g_return_val_if_fail(sd != NULL, CONNUI_SIM_STATUS_UNKNOWN);

  return _get_status(sd);
}

static GVariant *
_get_property(sim_data *sd, const char *name, GError **error)
{
  GVariant *prop, *v = NULL;

  if (org_ofono_sim_manager_call_get_properties_sync(
        sd->proxy, &prop, NULL, error))
  {
    v = g_variant_lookup_value(prop, name, NULL);
    g_variant_unref(prop);
  }

  return v;
}

static gboolean
sec_code_query(connui_cell_context *ctx, const char *modem_id,
               connui_sim_security_code_type code_type,
               gchar **old_code,
               gchar **new_code,
               code_query_callback_data *cbd)
{
  g_return_val_if_fail(ctx != NULL && old_code != NULL && cbd != NULL, FALSE);

  connui_utils_notify_notify(
        ctx->sec_code_cbs, (gpointer)modem_id, &code_type, &old_code, &new_code,
        &(cbd->cb), &(cbd->user_data), NULL);

  return *old_code != 0;
}

static void
sec_code_cb(code_query_callback_data *cbd, const char *modem_id,
            connui_sim_security_code_type code_type, gboolean ok, GError *error)
{
  if (cbd->cb)
    cbd->cb(modem_id, code_type, ok, cbd->user_data, error);
}

static gboolean
verify_code(sim_data *sd, connui_sim_security_code_type code_type,
            gchar *old_code, gchar *new_code, code_query_callback_data *cbd,
            GError **error)
{
  gboolean ok = FALSE;
  const gchar *type;
  GError *local_error = NULL;
  GVariant *v;

  switch (code_type)
  {
    case CONNUI_SIM_SECURITY_CODE_PIN:
      type = "pin";
      break;
    case CONNUI_SIM_SECURITY_CODE_PUK:
      type = "puk";
      break;
    case CONNUI_SIM_SECURITY_CODE_PIN2:
      type = "pin2";
      break;
    case CONNUI_SIM_SECURITY_CODE_PUK2:
      type = "puk2";
      break;
    default:
      g_assert_not_reached();
  }

  /* check how pin reset works */
  if (!new_code)
  {
      ok = org_ofono_sim_manager_call_enter_pin_sync(
            sd->proxy, type, old_code, NULL, &local_error);
  }
   else
  {
      ok = org_ofono_sim_manager_call_change_pin_sync(
            sd->proxy, type, old_code, new_code, NULL, &local_error);
  }

  v = _get_property(sd, OFONO_SIMMGR_PROPERTY_PIN_REQUIRED, NULL);
  _get_pin_required(sd, v);

  sec_code_cb(cbd, sd->path, code_type, ok, local_error);

  if (local_error)
  {
    if (error)
      *error = g_error_copy(local_error);

    g_error_free(local_error);
  }

  return ok;
}

static void
verify_required_pin(sim_data *sd)
{
  code_query_callback_data cbd = {NULL, NULL};
  gchar *new = NULL;
  gchar *old = NULL;
  gboolean code_queried = FALSE;
  connui_sim_security_code_type type = sd->pin_required;

  g_assert(type != CONNUI_SIM_SECURITY_CODE_UNKNOWN &&
           type != CONNUI_SIM_SECURITY_CODE_NONE &&
           type != CONNUI_SIM_SECURITY_CODE_UNSUPPORTED);

  if (type == CONNUI_SIM_SECURITY_CODE_PUK ||
      type == CONNUI_SIM_SECURITY_CODE_PUK2)
  {
    if (sec_code_query(sd->ctx, sd->path, type, &old, &new, &cbd))
      code_queried = TRUE;
  }

  if (!code_queried)
  {
    if (!sec_code_query(sd->ctx, sd->path, type, &old, NULL, &cbd))
    {
      g_free(old);
      return;
    }
    else
      code_queried = TRUE;
  }

  if (code_queried)
    verify_code(sd, type, old, new, &cbd, NULL);

  g_free(old);
  g_free(new);
}

void
connui_cell_sim_status_close(cell_sim_status_cb cb)
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);

  g_debug("Close sim status callback %p", cb);

  g_return_if_fail(ctx != NULL);

  ctx->sim_status_cbs = connui_utils_notify_remove(ctx->sim_status_cbs, cb);

  connui_cell_context_destroy(ctx);
}

gboolean
connui_cell_sim_status_register(cell_sim_status_cb cb, gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);

  g_debug("Register new sim status callback %p", cb);

  g_return_val_if_fail(ctx != NULL, FALSE);

  ctx->sim_status_cbs =
      connui_utils_notify_add(ctx->sim_status_cbs, cb, user_data);

  _notify_status_all(ctx);

  connui_cell_context_destroy(ctx);

  return TRUE;
}

gboolean
connui_cell_sim_is_network_in_service_provider_info(guint mnc, guint mcc)
{
  xmlDocPtr doc = xmlParseFile(MBPI_DATABASE);
  gboolean rv = FALSE;

  if (doc)
  {
    /* Create xpath evaluation context */
    xmlXPathContextPtr ctx = xmlXPathNewContext(doc);

    if (ctx)
    {
      gchar *xpath = g_strdup_printf(
            "//network-id[@mcc='%03u' and @mnc='%02u']", mcc, mnc);
      xmlXPathObjectPtr obj = xmlXPathEvalExpression(BAD_CAST xpath, ctx);

      if (obj)
      {
        if (obj->nodesetval)
          rv = TRUE;

        xmlXPathFreeObject(obj);
      }

      g_free(xpath);
      xmlXPathFreeContext(ctx);
    }
    else
      CONNUI_ERR("Unable to create new XPath context");

    xmlFreeDoc(doc);
  }
  else
    CONNUI_ERR("Unable to parse '" MBPI_DATABASE "'");

  return rv;
}

gchar *
connui_cell_sim_get_service_provider(const char *modem_id, GError **error)
{
  connui_cell_context *ctx = connui_cell_context_get(error);
  sim_data *sd;
  gchar *spn = NULL;

  g_return_val_if_fail(ctx != NULL, NULL);

  sd = _sim_data_get(modem_id, error);

  if (sd && sd->spn)
    spn = g_strdup(sd->spn);

  connui_cell_context_destroy(ctx);

  return spn;
}

gboolean
connui_cell_sim_needs_pin(const char *modem_id, GError **error)
{
  connui_cell_context *ctx = connui_cell_context_get(error);
  gboolean needed = FALSE;
  sim_data *sd;

  g_return_val_if_fail(ctx != NULL, FALSE);

  sd = _sim_data_get(modem_id, error);

  if (sd)
  {
    if (sd->pin_required != CONNUI_SIM_SECURITY_CODE_UNKNOWN)
      needed = sd->pin_required != CONNUI_SIM_SECURITY_CODE_NONE;
  }

  connui_cell_context_destroy(ctx);

  return needed;
}

gboolean
connui_cell_sim_is_locked(const char *modem_id, GError **error)
{
  connui_cell_context *ctx = connui_cell_context_get(error);
  sim_data *sd;
  gboolean locked = FALSE;

  g_return_val_if_fail(ctx != NULL, FALSE);

  sd = _sim_data_get(modem_id, error);

  if (sd)
  {
    GVariant *v = _get_property(sd, "LockedPins", error);

    if (v)
    {
      GHashTable *pins = g_hash_table_new(g_str_hash, g_str_equal);
      GVariantIter i;
      char *locked_pin;

      g_variant_iter_init(&i, v);

      g_hash_table_add(pins, "phone");
      g_hash_table_add(pins, "firstphone");
      g_hash_table_add(pins, "network");
      g_hash_table_add(pins, "netsub");
      g_hash_table_add(pins, "service");
      g_hash_table_add(pins, "corp");
      g_hash_table_add(pins, "firstphonepuk");
      g_hash_table_add(pins, "networkpuk");
      g_hash_table_add(pins, "netsubpuk");
      g_hash_table_add(pins, "servicepuk");
      g_hash_table_add(pins, "corppuk");

      while (g_variant_iter_loop(&i, "s", &locked_pin))
      {
        if (g_hash_table_contains(pins, locked_pin))
        {
          locked = TRUE;
          g_free(locked_pin);
          break;
        }
      }

      g_hash_table_unref(pins);
    }
    else
    {
      g_set_error(error, CONNUI_ERROR, CONNUI_ERROR_NOT_FOUND,
                  "No LockedPins property");
    }
  }

  connui_cell_context_destroy(ctx);

  return locked;
}

gboolean
connui_cell_sim_deactivate_lock(const char *modem_id, const gchar *pin_code,
                                GError **error)
{
  connui_cell_context *ctx = connui_cell_context_get(error);
  sim_data *sd;
  gboolean ok = FALSE;

  g_return_val_if_fail(ctx != NULL, FALSE);

  sd = _sim_data_get(modem_id, error);

  if (sd)
  {
    ok = org_ofono_sim_manager_call_unlock_pin_sync(
          sd->proxy, sd->pin_required_s, pin_code, NULL, error);
  }

  connui_cell_context_destroy(ctx);

  return ok;
}

guint
connui_cell_sim_verify_attempts_left(const char *modem_id,
                                     connui_sim_security_code_type code_type,
                                     GError **error)
{
  connui_cell_context *ctx = connui_cell_context_get(error);
  sim_data *sd;
  guchar attempts_left = 0;

  g_return_val_if_fail(ctx != NULL, 0);

  sd = _sim_data_get(modem_id, error);

  if (sd)
  {
    GVariant *v = _get_property(sd, "Retries", error);

    if (v)
    {
      GVariant *v2;

      switch (code_type)
      {
        case CONNUI_SIM_SECURITY_CODE_PIN:
          v2 = g_variant_lookup_value(v, "pin", G_VARIANT_TYPE_BYTE);
          break;
        case CONNUI_SIM_SECURITY_CODE_PUK:
          v2 = g_variant_lookup_value(v, "puk", G_VARIANT_TYPE_BYTE);
          break;
        case CONNUI_SIM_SECURITY_CODE_PIN2:
          v2 = g_variant_lookup_value(v, "pin2", G_VARIANT_TYPE_BYTE);
          break;
        case CONNUI_SIM_SECURITY_CODE_PUK2:
          v2 = g_variant_lookup_value(v, "puk2", G_VARIANT_TYPE_BYTE);
          break;
        default:
          g_set_error(error, CONNUI_ERROR, CONNUI_ERROR_INVALID_ARGS,
                      "Invalid code_type %d", code_type);
          goto cleanup;
      }

      g_variant_get(v2, "y", &attempts_left);
      g_variant_unref(v2);
    }
    else
    {
      g_set_error(error, CONNUI_ERROR, CONNUI_ERROR_NOT_FOUND,
                  "No Retries property");
    }
  }

cleanup:
  connui_cell_context_destroy(ctx);

  return (guint)attempts_left;
}

gboolean
connui_cell_security_code_register(cell_sec_code_query_cb cb,
                                   gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);

  g_debug("Register new sim security code callback %p", cb);

  g_return_val_if_fail(ctx != NULL, FALSE);

  ctx->sec_code_cbs = connui_utils_notify_add(ctx->sec_code_cbs, cb, user_data);

  _notify_security_code_all(ctx);

  connui_cell_context_destroy(ctx);

  return TRUE;
}

void
connui_cell_security_code_close(cell_sec_code_query_cb cb)
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);

  g_debug("Close sim security code callback %p", cb);

  g_return_if_fail(ctx != NULL);

  ctx->sec_code_cbs = connui_utils_notify_remove(ctx->sec_code_cbs, cb);

  connui_cell_context_destroy(ctx);
}

connui_sim_security_code_type
connui_cell_security_code_get_active(const char *modem_id, GError **error)
{
  connui_cell_context *ctx = connui_cell_context_get(error);
  connui_sim_security_code_type rv = CONNUI_SIM_SECURITY_CODE_UNKNOWN;
  sim_data *sd;

  g_return_val_if_fail(ctx != NULL, CONNUI_SIM_SECURITY_CODE_UNKNOWN);

  sd = _sim_data_get(modem_id, error);

  if (sd)
    rv = sd->pin_required;

  connui_cell_context_destroy(ctx);

  return rv;
}

gboolean
connui_cell_security_code_get_enabled(const char *modem_id, GError **error)
{
  return connui_cell_security_code_get_active(modem_id, error) ==
      CONNUI_SIM_SECURITY_CODE_PIN;
}

gboolean
connui_cell_security_code_set_active(const char *modem_id,
                                     connui_sim_security_code_type code_type,
                                     GError **error)
{
  connui_cell_context *ctx = connui_cell_context_get(error);
  code_query_callback_data cbd = {NULL, NULL};
  gboolean rv = FALSE;
  gchar *code = NULL;
  sim_data *sd;
  GError *local_error = NULL;

  g_return_val_if_fail(ctx != NULL, FALSE);

  g_return_val_if_fail(ctx->sec_code_cbs != NULL,
                       (connui_cell_context_destroy(ctx),
                        g_set_error(error, CONNUI_ERROR, CONNUI_ERROR_NOT_ACTIVE,
                                    "No security callback registered"),
                        FALSE));
  g_return_val_if_fail(code_type == CONNUI_SIM_SECURITY_CODE_PIN ||
                       code_type == CONNUI_SIM_SECURITY_CODE_PIN2,
                       (connui_cell_context_destroy(ctx),
                        g_set_error(error, CONNUI_ERROR,
                                    CONNUI_ERROR_INVALID_ARGS,
                                    "Invalid PIN type"),
                        FALSE));

  sd = _sim_data_get(modem_id, error);

  if (!sd)
  {
    connui_cell_context_destroy(ctx);
    return FALSE;
  }

  if (sec_code_query(sd->ctx, modem_id, code_type, &code, NULL, &cbd))
  {
    rv = org_ofono_sim_manager_call_lock_pin_sync(
          sd->proxy, code_type == CONNUI_SIM_SECURITY_CODE_PIN ?
            "pin" : "pin2", code, NULL,
          &local_error);
  }
  else
  {
    g_set_error(&local_error, CONNUI_ERROR, CONNUI_ERROR_CANCELED,
                "PIN entry cancelled");
  }

  sec_code_cb(&cbd, modem_id, code_type, rv, local_error);

  if (local_error)
  {
    if (error)
      *error = g_error_copy(local_error);

    g_error_free(local_error);
  }

  g_free(code);
  connui_cell_context_destroy(ctx);

  return rv;
}


gboolean
connui_cell_security_code_set_enabled(const char *modem_id, gboolean active,
                                      GError **error)
{
  connui_cell_context *ctx = connui_cell_context_get(error);
  code_query_callback_data cbd = {NULL, NULL};
  gchar *code = NULL;
  gboolean rv = FALSE;
  sim_data *sd;
  GError *local_error = NULL;
  const connui_sim_security_code_type type = CONNUI_SIM_SECURITY_CODE_PIN;

  g_return_val_if_fail(ctx != NULL, FALSE);

  sd = _sim_data_get(modem_id, error);

  if (!sd)
  {
    connui_cell_context_destroy(ctx);
    return FALSE;
  }

  if (sec_code_query(ctx, modem_id, type, &code, NULL, &cbd))
  {
    if (active)
    {
      rv = org_ofono_sim_manager_call_lock_pin_sync(
            sd->proxy, "pin", code, NULL, &local_error);
    }
    else
    {
      rv = org_ofono_sim_manager_call_unlock_pin_sync(
            sd->proxy, "pin", code, NULL, &local_error);
    }
  }
  else
  {
    g_set_error(&local_error, CONNUI_ERROR, CONNUI_ERROR_CANCELED,
                "PIN entry cancelled");
  }

  sec_code_cb(&cbd, modem_id, type, rv, local_error);

  if (local_error)
  {
    if (error)
      *error = g_error_copy(local_error);

    g_error_free(local_error);
  }

  g_free(code);
  connui_cell_context_destroy(ctx);

  return rv;
}

gboolean
connui_cell_security_code_change(const char *modem_id,
                                 connui_sim_security_code_type code_type,
                                 GError **error)
{
  connui_cell_context *ctx = connui_cell_context_get(error);
  gboolean rv = FALSE;
  code_query_callback_data cbd = {NULL, NULL};
  gchar *new = NULL;
  gchar *old = NULL;
  GError *local_error = NULL;
  sim_data *sd;

  g_return_val_if_fail(ctx != NULL, FALSE);

  g_return_val_if_fail(ctx->sec_code_cbs != NULL,
                       (connui_cell_context_destroy(ctx),
                        g_set_error(error, CONNUI_ERROR, CONNUI_ERROR_NOT_ACTIVE,
                                    "No security callback registered"),
                        FALSE));
  g_return_val_if_fail(code_type == CONNUI_SIM_SECURITY_CODE_PIN ||
                       code_type == CONNUI_SIM_SECURITY_CODE_PIN2,
                       (connui_cell_context_destroy(ctx),
                        g_set_error(error, CONNUI_ERROR,
                                    CONNUI_ERROR_INVALID_ARGS,
                                    "Invalid PIN type"),
                        FALSE));

  sd = _sim_data_get(modem_id, error);

  if (!sd)
  {
    connui_cell_context_destroy(ctx);
    return FALSE;
  }

  if (sec_code_query(ctx, modem_id, code_type, &old, &new, &cbd) && new && *new)
  {
    if (connui_cell_security_code_get_enabled(modem_id, &local_error) ||
        code_type != CONNUI_SIM_SECURITY_CODE_PIN)
    {
      rv = verify_code(sd, code_type, old, new, &cbd, &local_error);
    }
    else if (org_ofono_sim_manager_call_lock_pin_sync(sd->proxy, "pin", old,
                                                      NULL, &local_error))
    {
      rv = verify_code(sd, CONNUI_SIM_SECURITY_CODE_PIN, old, new,
                       &cbd, &local_error);

      if (rv)
      {
        rv = org_ofono_sim_manager_call_unlock_pin_sync(sd->proxy, "pin",
                                                        new, NULL,
                                                        &local_error);
      }
      else
      {
        rv = org_ofono_sim_manager_call_unlock_pin_sync(sd->proxy, "pin",
                                                        old, NULL,
                                                        &local_error);
      }
    }
  }
  else
  {
    g_set_error(&local_error, CONNUI_ERROR, CONNUI_ERROR_CANCELED,
                "PIN entry cancelled");
  }

  sec_code_cb(&cbd, modem_id, code_type, rv, local_error);

  if (local_error)
  {
    if (error)
      *error = g_error_copy(local_error);

    g_error_free(local_error);
  }

  g_free(old);
  g_free(new);
  connui_cell_context_destroy(ctx);

  return rv;
}
