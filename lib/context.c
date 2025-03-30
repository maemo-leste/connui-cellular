#include <connui/connui-log.h>

#include "connui-cell-marshal.h"
#include "context.h"

#include "modem.h"

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

static void
_add_modem(connui_cell_context *ctx, const gchar *path, GVariant *properties)
{
  GError *error = NULL;
  ConnuiCellModem *modem = connui_cell_modem_proxy_new_for_bus_sync(
        OFONO_BUS_TYPE, G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
        OFONO_SERVICE, path, NULL, &error);

  g_debug("Adding modem %s", path);

  if (modem)
  {
    g_hash_table_insert(ctx->modems, g_strdup(path), modem);
    connui_cell_modem_add(ctx, modem, path, properties);
  }
  else
  {
    CONNUI_ERR("Error creating OFONO modem %s proxy [%s]", path,
               error->message);
    g_error_free(error);
  }
}

static void
_modem_added_cb(ConnuiCellManager *manager, const gchar *path,
                GVariant *properties, gpointer user_data)
{
  _add_modem(user_data, path, properties);
}

static void
_destroy_modem(gpointer modem)
{
  connui_cell_modem_remove(modem);
  g_object_unref(modem);
}

static void
_modem_removed_cb(ConnuiCellManager *manager, const gchar *path,
                  gpointer user_data)
{
  connui_cell_context *ctx = user_data;
  gpointer key;
  gpointer value;

  /* keep it alive in case callbacks call connui_cell_context_destroy() */
  g_object_ref(manager);

  /* we can't use g_hash_table_remove() here, as _destroy_modem() may call
   * connui_cell_context_destroy(), because of the registered callbacks */
  if (g_hash_table_steal_extended(ctx->modems, path, &key, &value))
  {
    /* that should call all the _xxx_data_destroy() functions */
    g_free(key);
    _destroy_modem(value);
  }

  g_object_unref(manager);
}

static gboolean
create_proxies(connui_cell_context *ctx)
{
  GError *error = NULL;
  GVariant *modems, *properties;
  gchar *path;
  GVariantIter iter;

  ctx->manager = connui_cell_manager_proxy_new_for_bus_sync(
        G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
        "org.ofono", "/", NULL, &error);

  if (!ctx->manager)
  {
    CONNUI_ERR("Error creating OFONO manager [%s]", error->message);
    g_error_free(error);
    return FALSE;
  }

  ctx->modems = g_hash_table_new_full(
        g_str_hash, g_str_equal, g_free, _destroy_modem);

  if (!connui_cell_manager_call_get_modems_sync(
        ctx->manager, &modems, NULL, &error))
  {
    CONNUI_ERR("Error getting OFONO modems [%s]", error->message);
    g_error_free(error);
    g_hash_table_unref(ctx->modems);
    g_object_unref(ctx->manager);
    return FALSE;
  }

  g_variant_iter_init(&iter, modems);

  while (g_variant_iter_loop(&iter, "(&o@a{sv})", &path, &properties))
    _add_modem(ctx, path, properties);

  g_variant_unref(modems);

  ctx->modem_added_id = g_signal_connect(ctx->manager, "modem-added",
                                         G_CALLBACK(_modem_added_cb), ctx);
  ctx->modem_removed_id = g_signal_connect(ctx->manager, "modem-removed",
                                           G_CALLBACK(_modem_removed_cb), ctx);

  return TRUE;
}

__attribute__((visibility("hidden"))) connui_cell_context *
connui_cell_context_get(GError **error)
{
  static connui_cell_context context;

  if (context.initialized)
    return &context;

  g_debug("Init context");

  register_marshallers();

  if (!create_proxies(&context))
    return NULL;

  context.modem_cbs = NULL;

  context.sim_status_cbs = NULL;
  context.sec_code_cbs = NULL;
  context.net_status_cbs = NULL;
  context.conn_status_cbs = NULL;
  context.net_list_cbs = NULL;
  context.net_select_cbs = NULL;
  context.call_status_cbs = NULL;

  context.initialized = TRUE;

  return &context;
}

__attribute__((visibility("hidden"))) void
connui_cell_context_destroy(connui_cell_context *ctx)
{
  if (!ctx->initialized)
    return;

  if (ctx->modem_cbs)
    return;

  if (ctx->sim_status_cbs || ctx->sec_code_cbs || ctx->conn_status_cbs ||
      ctx->net_status_cbs || ctx->net_list_cbs || ctx->net_select_cbs ||
      ctx->service_calls)
  {
    return;
  }

  g_debug("Destroy context");

  g_signal_handler_disconnect(G_OBJECT(ctx->manager), ctx->modem_added_id);
  g_signal_handler_disconnect(G_OBJECT(ctx->manager), ctx->modem_removed_id);
  g_hash_table_unref(ctx->modems);
  g_object_unref(G_OBJECT(ctx->manager));

  ctx->initialized = FALSE;
}

#define CONNUI_ERROR_(error) OFONO_SERVICE ".Error." error

static const GDBusErrorEntry connui_errors[] = {
    {CONNUI_ERROR_INVALID_ARGS,          CONNUI_ERROR_("InvalidArguments")},
    {CONNUI_ERROR_INVALID_FORMAT,        CONNUI_ERROR_("InvalidFormat")},
    {CONNUI_ERROR_NOT_IMPLEMENTED,       CONNUI_ERROR_("NotImplemented")},
    {CONNUI_ERROR_FAILED,                CONNUI_ERROR_("Failed")},
    {CONNUI_ERROR_BUSY,                  CONNUI_ERROR_("InProgress")},
    {CONNUI_ERROR_NOT_FOUND,             CONNUI_ERROR_("NotFound")},
    {CONNUI_ERROR_NOT_ACTIVE,            CONNUI_ERROR_("NotActive")},
    {CONNUI_ERROR_NOT_SUPPORTED,         CONNUI_ERROR_("NotSupported")},
    {CONNUI_ERROR_NOT_AVAILABLE,         CONNUI_ERROR_("NotAvailable")},
    {CONNUI_ERROR_TIMED_OUT,             CONNUI_ERROR_("Timedout")},
    {CONNUI_ERROR_SIM_NOT_READY,         CONNUI_ERROR_("SimNotReady")},
    {CONNUI_ERROR_IN_USE,                CONNUI_ERROR_("InUse")},
    {CONNUI_ERROR_NOT_ATTACHED,          CONNUI_ERROR_("NotAttached")},
    {CONNUI_ERROR_ATTACH_IN_PROGRESS,    CONNUI_ERROR_("AttachInProgress")},
    {CONNUI_ERROR_NOT_REGISTERED,        CONNUI_ERROR_("NotRegistered")},
    {CONNUI_ERROR_CANCELED,              CONNUI_ERROR_("Canceled")},
    {CONNUI_ERROR_ACCESS_DENIED,         CONNUI_ERROR_("AccessDenied")},
    {CONNUI_ERROR_EMERGENCY_ACTIVE,      CONNUI_ERROR_("EmergencyActive")},
    {CONNUI_ERROR_INCORRECT_PASSWORD,    CONNUI_ERROR_("IncorrectPassword")},
    {CONNUI_ERROR_NOT_ALLOWED,           CONNUI_ERROR_("NotAllowed")},
    {CONNUI_ERROR_NOT_RECOGNIZED,        CONNUI_ERROR_("NotRecognized")},
    {CONNUI_ERROR_NETWORK_TERMINATED,    CONNUI_ERROR_("Terminated")}
};

G_STATIC_ASSERT(G_N_ELEMENTS(connui_errors) == CONNUI_NUM_ERRORS);

GQuark
connui_error_quark()
{
    static volatile gsize connui_error_quark_value = 0;
    g_dbus_error_register_error_domain("connui-error-quark",
        &connui_error_quark_value, connui_errors, G_N_ELEMENTS(connui_errors));
    return (GQuark)connui_error_quark_value;
}
