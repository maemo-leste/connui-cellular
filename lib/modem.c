/*
 * modem.c
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

/**
 * SECTION:connui-cell-modem
 * @short_description: methods for accessing cellular modems.
 *
 * TBD
 */

#include "config.h"

#include <dbus/dbus.h>
#include <libosso.h>

#include <connui/connui-utils.h>
#include <connui/connui-log.h>

#include "context.h"
#include "net.h"
#include "sim.h"
#include "sups.h"
#include "connmgr.h"

#include "org.ofono.VoiceCallManager.h"

#include "connui-cellular-modem.h"

#define DATA "connui_cell_modem_data"

typedef struct _modem_data
{
  connui_cell_context *ctx;
  ConnuiCellModem *proxy;
  gchar *path;
  GHashTable *interfaces;

  gboolean online;
  gboolean powered;
  gchar *manufacturer;
  gchar *model;
  gchar *revision;
  gchar *serial;
  ConnuiCellVoiceCallManager *vcm;

  gulong properties_changed_id;
  guint notify_id;
}
modem_data;

static void
_modem_data_destroy(gpointer data)
{
  modem_data *md = data;
  connui_modem_status status = CONNUI_MODEM_STATUS_REMOVED;

  if (md->notify_id)
    g_source_remove(md->notify_id);

  g_signal_handler_disconnect(md->proxy, md->properties_changed_id);

  g_free(md->manufacturer);
  g_free(md->model);
  g_free(md->revision);
  g_free(md->serial);

  g_hash_table_unref(md->interfaces);

  connui_utils_notify_notify(md->ctx->modem_cbs, md->path, &status, NULL);

  g_free(md->path);
  g_free(md);
}

static modem_data *
_modem_data_create(ConnuiCellModem *proxy, const gchar *path,
                   connui_cell_context *ctx)
{
  modem_data *md = g_new0(modem_data, 1);

  md->ctx = ctx;
  md->interfaces = g_hash_table_new_full(g_str_hash, g_str_equal,
                                         g_free, NULL);
  md->proxy = proxy;
  md->path = g_strdup(path);

  return md;
}

static modem_data *
_modem_get_data(const char *path, GError **error)
{
  connui_cell_context *ctx = connui_cell_context_get(error);
  ConnuiCellModem *proxy;
  modem_data *md;

  g_return_val_if_fail(path != NULL, NULL);

  proxy = g_hash_table_lookup(ctx->modems, path);

  if (!proxy)
  {
    CONNUI_ERR("Invalid modem path %s", path);
    g_set_error(error, CONNUI_ERROR, CONNUI_ERROR_NOT_FOUND,
                "No such modem [%s]", path);
    return NULL;
  }

  md = g_object_get_data(G_OBJECT(proxy), DATA);

  g_assert(md);

  return md;
}

static connui_modem_status
_modem_get_status(modem_data *md)
{
  connui_modem_status status;

  if (md->online)
    status = CONNUI_MODEM_STATUS_ONLINE;
  else if (md->powered)
    status = CONNUI_MODEM_STATUS_POWERED;
  else
    status = CONNUI_MODEM_STATUS_OK;

  return status;
}

static gboolean
_idle_notify(gpointer user_data)
{
  modem_data *md = user_data;
  connui_modem_status status = _modem_get_status(md);

  md->notify_id = 0;
  connui_utils_notify_notify(md->ctx->modem_cbs, md->path, &status, NULL);

  return G_SOURCE_REMOVE;
}

static void
_notify(modem_data *md)
{
  if (!md->notify_id)
    md->notify_id = g_idle_add(_idle_notify, md);
}

gboolean
connui_cell_modem_status_register(cell_modem_status_cb cb, gpointer user_data)
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);
  GHashTableIter iter;
  ConnuiCellModem *modem;

  g_return_val_if_fail(ctx != NULL, FALSE);

  ctx->modem_cbs = connui_utils_notify_add(ctx->modem_cbs, cb, user_data);

  g_hash_table_iter_init (&iter, ctx->modems);

  while (g_hash_table_iter_next(&iter, NULL, (gpointer *)&modem))
    _notify(g_object_get_data(G_OBJECT(modem), DATA));

  return TRUE;
}

void
connui_cell_modem_status_close(cell_modem_status_cb cb)
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);

  g_return_if_fail(ctx != NULL);

  ctx->modem_cbs = connui_utils_notify_remove(ctx->modem_cbs, cb);

  connui_cell_context_destroy(ctx);
}

/**
  * connui_cell_modem_get_modems
  * @error: return location for any error that might have occurent. If not NULL,
  * it must initialize it to NULL GError *.
  *
  * Get list of ids of all modems currently present.
  *
  * Returns:(transfer full): #GList of modems. You must free the result with
  *                          g_list_free_full(list, g_free):
  */
GList *
connui_cell_modem_get_modems(GError **error)
{
  GList *modems, *l;
  connui_cell_context *ctx = connui_cell_context_get(error);

  g_return_val_if_fail(ctx != NULL, NULL);

  modems = g_hash_table_get_keys(ctx->modems);

  for (l = modems; l; l = l->next)
    l->data = g_strdup(l->data);

  connui_cell_context_destroy(ctx);

  return modems;
}

#define GET(x, type, default) \
  connui_cell_context *ctx; \
  modem_data *md; \
  type x = default; \
\
  g_return_val_if_fail(modem_id != NULL, x); \
  ctx = connui_cell_context_get(error); \
  g_return_val_if_fail(ctx != NULL, x); \
\
  if ((md = _modem_get_data(modem_id, error))) \
    x = md->x; \
\
  connui_cell_context_destroy(ctx); \
\
  return x;

gboolean
connui_cell_modem_is_online(const char *modem_id, GError **error)
{
  GET(online, gboolean, FALSE)
}

gboolean
connui_cell_modem_is_powered(const char *modem_id, GError **error)
{
  GET(powered, gboolean, FALSE)
}

const gchar *
connui_cell_modem_get_model(const char *modem_id, GError **error)
{
  GET(model, const gchar *, NULL)
}

const gchar *
connui_cell_modem_get_serial(const char *modem_id, GError **error)
{
  GET(serial, const gchar *, NULL)
}

const gchar *
connui_cell_modem_get_revision(const char *modem_id, GError **error)
{
  GET(revision, const gchar *, NULL)
}

const gchar *
connui_cell_modem_get_manufacturer(const char *modem_id, GError **error)
{
  GET(manufacturer, const gchar *, NULL)
}

static void
_parse_interfaces(modem_data *md, GVariant *value)
{
  GHashTable *interfaces = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                 g_free, NULL);
  GVariantIter vi;
  gchar *iface;
  GHashTableIter hi;

  g_variant_iter_init(&vi, value);

  while (g_variant_iter_loop(&vi, "s", &iface))
    g_hash_table_add(interfaces, g_strdup(iface));

  g_hash_table_iter_init (&hi, interfaces);

  while (g_hash_table_iter_next (&hi, (gpointer *)&iface, NULL))
  {
    if (g_hash_table_lookup(md->interfaces, iface))
      continue;

    g_debug("Modem %s new interface %s", md->path, iface);

    if (!strcmp(iface, OFONO_SIMMGR_INTERFACE_NAME))
      connui_cell_modem_add_simmgr(md->ctx, md->path);
    else if (!strcmp(iface, OFONO_NETREG_INTERFACE_NAME))
      connui_cell_modem_add_netreg(md->ctx, md->path);
    else if (!strcmp(iface, OFONO_SUPPLSVCS_INTERFACE_NAME))
      connui_cell_modem_add_supplementary_services(md->ctx, md->path);
    else if (!strcmp(iface, OFONO_CONNMGR_INTERFACE_NAME))
      connui_cell_modem_add_connection_manager(md->ctx, md->path);
    else if (!strcmp(iface, OFONO_VOICECALL_MANAGER_INTERFACE_NAME))
    {
      GError *error = NULL;

      md->vcm = connui_cell_voice_call_manager_proxy_new_for_bus_sync(
            OFONO_BUS_TYPE, G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
            OFONO_SERVICE, md->path, NULL, &error);

      if (error)
      {
        CONNUI_ERR(
              "Error creating OFONO voice call manager proxy for %s [%s]",
              md->path, error->message);
        g_error_free(error);
      }
    }
  }

  g_hash_table_iter_init (&hi, md->interfaces);

  while (g_hash_table_iter_next (&hi, (gpointer *)&iface, NULL))
  {
    if (g_hash_table_lookup(interfaces, iface))
      continue;

    g_debug("Modem %s interface %s removed", md->path, iface);

    if (!strcmp(iface, OFONO_SIMMGR_INTERFACE_NAME))
      connui_cell_modem_remove_simmgr(md->proxy);
    else if (!strcmp(iface, OFONO_NETREG_INTERFACE_NAME))
      connui_cell_modem_remove_netreg(md->proxy);
    else if (!strcmp(iface, OFONO_SUPPLSVCS_INTERFACE_NAME))
      connui_cell_modem_remove_supplementary_services(md->proxy);
    else if (!strcmp(iface, OFONO_CONNMGR_INTERFACE_NAME))
      connui_cell_modem_remove_connection_manager(md->proxy);
    else if (!strcmp(iface, OFONO_VOICECALL_MANAGER_INTERFACE_NAME))
    {
      g_object_unref(md->vcm);
      md->vcm = NULL;
    }
  }

  g_hash_table_unref(md->interfaces);
  md->interfaces = interfaces;
}

static void
_parse_property(modem_data *md, const gchar *name, GVariant *value)
{
  g_debug("Modem %s parsing property %s, type %s", md->path, name,
          g_variant_get_type_string(value));

  if (!strcmp(name, OFONO_MODEM_PROPERTY_POWERED))
    md->powered = g_variant_get_boolean(value);
  else if (!strcmp(name, OFONO_MODEM_PROPERTY_ONLINE))
    md->online = g_variant_get_boolean(value);
  else if (!strcmp(name, OFONO_MODEM_PROPERTY_MANUFACTURER))
    md->manufacturer = g_strdup(g_variant_get_string(value, NULL));
  else if (!strcmp(name, OFONO_MODEM_PROPERTY_MODEL))
    md->model = g_strdup(g_variant_get_string(value, NULL));
  else if (!strcmp(name, OFONO_MODEM_PROPERTY_REVISION))
    md->revision = g_strdup(g_variant_get_string(value, NULL));
  else if (!strcmp(name, OFONO_MODEM_PROPERTY_SERIAL))
    md->serial = g_strdup(g_variant_get_string(value, NULL));
  else if (!strcmp(name, OFONO_MODEM_PROPERTY_INTERFACES))
    _parse_interfaces(md, value);
}

static void
_property_changed_cb(ConnuiCellModem *proxy, const gchar *name,
                    GVariant *value, gpointer user_data)
{
  modem_data *md = user_data;
  GVariant *v = g_variant_get_variant(value);

  g_debug("Modem %s property %s changed", md->path, name);

  _parse_property(md, name, v);
  _notify(md);

  g_variant_unref(v);
}

__attribute__((visibility("hidden"))) void
connui_cell_modem_add(connui_cell_context *ctx, ConnuiCellModem *proxy,
                      const gchar *path, GVariant *properties)
{
  modem_data *md = _modem_data_create(proxy, path, ctx);
  GVariantIter i;
  gchar *name;
  GVariant *v;

  g_debug("New ofono modem %s", path);

  g_variant_iter_init(&i, properties);

  while (g_variant_iter_loop(&i, "{&sv}", &name, &v))
    _parse_property(md, name, v);

  md->properties_changed_id =
      g_signal_connect(proxy, "property-changed",
                       G_CALLBACK(_property_changed_cb), md);
  g_object_set_data_full(G_OBJECT(proxy), DATA, md, _modem_data_destroy);

  _notify(md);
}

__attribute__((visibility("hidden"))) void
connui_cell_modem_remove(ConnuiCellModem *proxy)
{
  gchar *path = NULL;

  g_object_get(proxy, "g-object-path", &path, NULL);
  g_debug("Removing ofono modem %s", path);
  g_free(path);
  g_object_set_data(G_OBJECT(proxy), DATA, NULL);
}

GStrv
connui_cell_emergency_get_numbers(const char *modem_id, GError **error)
{
  connui_cell_context *ctx;
  GStrv numbers = NULL;
  modem_data *md;

  g_return_val_if_fail(modem_id != NULL, NULL);

  ctx = connui_cell_context_get(error);
  g_return_val_if_fail(ctx != NULL, NULL);

  md = _modem_get_data(modem_id, error);

  if (md)
  {
    GVariant *props;

    if (connui_cell_voice_call_manager_call_get_properties_sync(
          md->vcm, &props, NULL, error))
    {
      gchar *name;
      GVariant *v;
      GVariantIter i;

      g_variant_iter_init(&i, props);

      while (g_variant_iter_loop(&i, "{&sv}", &name, &v))
      {
        if (!strcmp(name, "EmergencyNumbers"))
        {
          numbers = g_variant_dup_strv(v, NULL);
          g_variant_unref(v);
          break;
        }
      }

      g_variant_unref(props);
    }
  }

  connui_cell_context_destroy(ctx);

  return numbers;
}
