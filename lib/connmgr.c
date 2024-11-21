/*
 * connmgr.c
 *
 * Copyright (C) 2024 Ivaylo Dimitrov <ivo.g.dimitrov.75@gmail.com>
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <libosso.h>
#include <connui/connui-log.h>
#include <connui/connui-utils.h>

#include "context.h"

#include "connmgr.h"

#define DATA "connui_cell_connmgr_data"

typedef struct _cm_data
{
  connui_cell_context *ctx;
  OrgOfonoConnectionManager *proxy;
  gchar *path;

  cell_connection_status status;

  guint idle_id;
  gulong changed_id;
}
cm_data;

static gboolean
_idle_notify(gpointer user_data)
{
  cm_data *cmd = user_data;
  cmd->idle_id = 0;

  connui_utils_notify_notify(cmd->ctx->conn_status_cbs, cmd->path, &cmd->status,
                             NULL);

  return G_SOURCE_REMOVE;
}

static void
_notify(cm_data *cmd)
{
  if (cmd && !cmd->idle_id)
    cmd->idle_id = g_idle_add(_idle_notify, cmd);
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

static void
_cm_data_destroy(gpointer data)
{
  cm_data *cmd = data;

  g_debug("Removing ofono connection manager for %s", cmd->path);

  if (cmd->idle_id)
    g_source_remove(cmd->idle_id);

  g_signal_handler_disconnect(cmd->proxy, cmd->changed_id);

  if (cmd->proxy)
    g_object_unref(cmd->proxy);

  g_free(cmd->path);
  g_free(cmd);
}

static cm_data *
_cm_data_create(OrgOfonoConnectionManager *proxy, const gchar *path,
                connui_cell_context *ctx)
{
  cm_data *cmd = g_new0(cm_data, 1);
  OrgOfonoModem *modem = g_hash_table_lookup(ctx->modems, path);

  g_assert(modem);
  g_assert(g_object_get_data(G_OBJECT(modem), DATA) == NULL);

  g_object_set_data_full(G_OBJECT(modem), DATA, cmd, _cm_data_destroy);

  cmd->path = g_strdup(path);
  cmd->proxy = proxy;
  cmd->ctx = ctx;
  cmd->status.bearer = CONNUI_CONNMGR_BEARER_UNKNOWN;

  return cmd;
}

static cm_data *
_cm_data_get(const char *path, connui_cell_context *ctx, GError **error)
{
  OrgOfonoModem *modem;
  cm_data *cmd;

  g_return_val_if_fail(path != NULL, NULL);

  modem = g_hash_table_lookup(ctx->modems, path);

  if (!modem)
  {
    CONNUI_ERR("Invalid modem path %s", path);
    g_set_error(error, CONNUI_ERROR, CONNUI_ERROR_NOT_FOUND,
                "No such modem [%s]", path);
    return NULL;
  }

  if (!(cmd = g_object_get_data(G_OBJECT(modem), DATA)))
  {
    g_set_error(error, CONNUI_ERROR, CONNUI_ERROR_NOT_FOUND,
                "Modem [%s] has no connection manager interface", path);
  }

  return cmd;
}

static connui_connmgr_bearer
_bearer(const gchar *bearer)
{
  if (!strcmp(bearer, "none"))
    return CONNUI_CONNMGR_BEARER_NONE;
  else if (!strcmp(bearer, "gprs"))
    return CONNUI_CONNMGR_BEARER_GPRS;
  else if (!strcmp(bearer, "edge"))
    return CONNUI_CONNMGR_BEARER_EDGE;
  else if (!strcmp(bearer, "umts"))
    return CONNUI_CONNMGR_BEARER_UMTS;
  else if (!strcmp(bearer, "hsdpa"))
    return CONNUI_CONNMGR_BEARER_HSPDA;
  else if (!strcmp(bearer, "hsupa"))
    return CONNUI_CONNMGR_BEARER_HSUPA;
  else if (!strcmp(bearer, "hspa"))
    return CONNUI_CONNMGR_BEARER_HSPA;
  else if (!strcmp(bearer, "lte"))
    return CONNUI_CONNMGR_BEARER_LTE;
  else
    return CONNUI_CONNMGR_BEARER_UNKNOWN;
}

static void
_parse_property(cm_data *cmd, const gchar *name, GVariant *value)
{
  cell_connection_status *status = &cmd->status;

  g_debug("CONNMGR %s parsing property %s, type %s", cmd->path, name,
          g_variant_get_type_string(value));

  if (!strcmp(name, OFONO_CONNMGR_PROPERTY_ATTACHED))
    status->attached = g_variant_get_boolean(value);
  else if (!strcmp(name, OFONO_CONNMGR_PROPERTY_POWERED))
    status->powered = g_variant_get_boolean(value);
  else if (!strcmp(name, OFONO_CONNMGR_PROPERTY_SUSPENDED))
    status->suspended = g_variant_get_boolean(value);
  else if (!strcmp(name, OFONO_CONNMGR_PROPERTY_ROAMING_ALLOWED))
    status->roaming_allowed = g_variant_get_boolean(value);
  else if (!strcmp(name, OFONO_CONNMGR_PROPERTY_BEARER))
    status->bearer = _bearer(g_variant_get_string(value, NULL));
}

static void
_property_changed_cb(OrgOfonoConnectionManager *proxy, const gchar *name,
                    GVariant *value, gpointer user_data)
{
  cm_data *cmd = user_data;
  GVariant *v = g_variant_get_variant(value);

  g_debug("Modem %s connection manager property %s changed", cmd->path, name);

  _parse_property(cmd, name, v);
  _notify(cmd);

  g_variant_unref(v);
}

__attribute__((visibility("hidden"))) void
connui_cell_modem_add_connection_manager(connui_cell_context *ctx,
                                         const char *path)
{
  OrgOfonoConnectionManager *proxy;
  GError *error = NULL;

  g_debug("Adding ofono connection manager for %s", path);

  proxy = org_ofono_connection_manager_proxy_new_for_bus_sync(
        OFONO_BUS_TYPE, G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
        OFONO_SERVICE, path, NULL, &error);

  if (proxy)
  {
    cm_data *cmd = _cm_data_create(proxy, path, ctx);
    GVariant *props;

    if (org_ofono_connection_manager_call_get_properties_sync(proxy, &props,
                                                              NULL, &error))
    {
      GVariantIter i;
      gchar *name;
      GVariant *v;

      g_variant_iter_init(&i, props);

      while (g_variant_iter_loop(&i, "{&sv}", &name, &v))
        _parse_property(cmd, name, v);

      g_variant_unref(props);
    }
    else
    {
      CONNUI_ERR("Unable to get modem [%s] network registration properties: %s",
                 path, error->message);
      g_error_free(error);
    }

    cmd->changed_id = g_signal_connect(proxy, "property-changed",
                                       G_CALLBACK(_property_changed_cb), cmd);

    _notify_all(ctx);
  }
  else
  {
    CONNUI_ERR("Error creating OFONO connection manager proxy for %s [%s]",
               path, error->message);
    g_error_free(error);
  }
}

__attribute__((visibility("hidden"))) void
connui_cell_modem_remove_connection_manager(OrgOfonoModem *modem)
{
  g_object_set_data(G_OBJECT(modem), DATA, NULL);
}

gboolean
connui_cell_connmgr_status_register(cell_connection_status_cb cb,
                                    gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);

  g_return_val_if_fail(ctx != NULL, FALSE);

  ctx->conn_status_cbs = connui_utils_notify_add(ctx->conn_status_cbs,
                                                 (connui_utils_notify)cb,
                                                 user_data);

  _notify_all(ctx);

  return TRUE;
}

void
connui_cell_connmgr_status_close(cell_connection_status_cb cb)
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);

  g_return_if_fail(ctx != NULL);

  ctx->conn_status_cbs = connui_utils_notify_remove(ctx->conn_status_cbs, cb);

  connui_cell_context_destroy(ctx);
}

static gboolean
_set_property(const char *modem_id,const gchar *name, GVariant *value,
              GError **error)
{
  connui_cell_context *ctx = connui_cell_context_get(error);
  cm_data *cmd;

  gboolean rv = FALSE;

  g_return_val_if_fail(ctx != NULL, rv);

  cmd = _cm_data_get(modem_id, ctx, error);

  if (cmd)
  {
    rv = org_ofono_connection_manager_call_set_property_sync(
          cmd->proxy, name, g_variant_new_variant(value), NULL, error);
  }

  connui_cell_context_destroy(ctx);

  return rv;
}

gboolean
connui_cell_connection_set_roaming_allowed(const char *modem_id,
                                           gboolean allowed,
                                           GError **error)
{
  return _set_property(modem_id, OFONO_CONNMGR_PROPERTY_ROAMING_ALLOWED,
                       g_variant_new_boolean(allowed), error);
}

gboolean
connui_cell_connection_set_powered(const char *modem_id, gboolean powered,
                                   GError **error)
{
  return _set_property(modem_id, OFONO_CONNMGR_PROPERTY_POWERED,
                       g_variant_new_boolean(powered),  error);
}

const cell_connection_status *
connui_cell_connection_get_status(const char *modem_id, GError **error)
{
  connui_cell_context *ctx = connui_cell_context_get(error);
  cell_connection_status *status = NULL;
  cm_data *cmd;

  g_return_val_if_fail(ctx != NULL, NULL);

  cmd = _cm_data_get(modem_id, ctx, error);

  if (cmd)
    status = &cmd->status;

  connui_cell_context_destroy(ctx);

  return status;
}
