/*
 * call-settings.c
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
#include <connui/connui-utils.h>
#include <connui/connui-log.h>
#include <connui/connui-dbus.h>
#include <telepathy-glib/telepathy-glib.h>
#include <gio/gio.h>

#include <string.h>

#include "context.h"

#include "sups.h"
#include "service-call.h"

#define DATA "connui_cell_sups_data"

typedef struct _sups_data
{
  connui_cell_context *ctx;
  OrgOfonoSupplementaryServices *proxy;
  gchar *path;

  GHashTable *pending;
}
sups_data;

static void _call_waiting_get_cb(GObject *object, GAsyncResult* res,
                                 gpointer data);
static void _call_waiting_set_cb(GObject *object, GAsyncResult* res,
                                 gpointer data);

static void _forwarding_query_cb(GObject *object, GAsyncResult *res,
                                 gpointer data);
static void _do_call(sups_data *sd, service_call_data *scd);

static void
_sups_data_destroy(gpointer data)
{
  sups_data *sd = data;

  /* FIXME - cancel pending calls */
  g_debug("Removing ofono call settings for %s", sd->path);

  if (sd->proxy)
    g_object_unref(sd->proxy);

  g_free(sd->path);
  g_free(sd);
}

static sups_data *
_sups_data_get(const char *path, connui_cell_context *ctx, GError **error)
{
  OrgOfonoModem *modem;
  sups_data *sd;

  modem = g_hash_table_lookup(ctx->modems, path);

  if (!modem)
  {
    CONNUI_ERR("Invalid modem path %s", path);
    g_set_error(error, CONNUI_ERROR, CONNUI_ERROR_NOT_FOUND,
                "No such modem [%s]", path);
    return NULL;
  }

  if (!(sd = g_object_get_data(G_OBJECT(modem), DATA)))
  {
    sd = g_new0(sups_data, 1);
    g_object_set_data_full(G_OBJECT(modem), DATA, sd,
                           _sups_data_destroy);
    sd->path = g_strdup(path);
    sd->ctx = ctx;
    sd->pending = g_hash_table_new(g_direct_hash, g_direct_equal);
  }

  return sd;
}

__attribute__((visibility("hidden"))) void
connui_cell_modem_add_supplementary_services(connui_cell_context *ctx,
                                             const char *path)
{
  sups_data *sd = _sups_data_get(path, ctx, NULL);
  OrgOfonoSupplementaryServices *proxy;
  GError *error = NULL;

  g_assert(sd->proxy == NULL);

  g_debug("Adding ofono supplementary services for %s", path);

  proxy = org_ofono_supplementary_services_proxy_new_for_bus_sync(
        OFONO_BUS_TYPE, G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
        OFONO_SERVICE, path, NULL, &error);

  if (proxy)
  {
    sd->proxy = g_object_ref(proxy);

    GHashTableIter iter;
    service_call_data *scd;

    g_hash_table_iter_init(&iter, sd->pending);

    while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&scd))
      _do_call(sd, scd);

    g_hash_table_unref(sd->pending);
    sd->pending = NULL;
  }
  else
  {
    CONNUI_ERR("Error creating OFONO supplementary services proxy for %s [%s]",
               path, error->message);
    g_error_free(error);
  }
}

__attribute__((visibility("hidden"))) void
connui_cell_modem_remove_supplementary_services(OrgOfonoModem *modem)
{
  g_object_set_data(G_OBJECT(modem), DATA, NULL);
}

static void
_do_call(sups_data *sd, service_call_data *scd)
{
  if (!sd->proxy)
  {
    g_hash_table_insert(sd->pending, GUINT_TO_POINTER(scd->id), scd);
    return;
  }

  scd->cancellable = g_cancellable_new();

  if (scd->async_cb == _call_waiting_get_cb)
  {
    org_ofono_supplementary_services_call_initiate(
          sd->proxy, "*#43#", scd->cancellable, scd->async_cb, scd);
  }
  else if (scd->async_cb == _call_waiting_set_cb)
  {
    gboolean enable = GPOINTER_TO_UINT(scd->data);
    const char *cmd = enable ? "*43#" : "#43#";

    org_ofono_supplementary_services_call_initiate(
          sd->proxy, cmd, scd->cancellable, scd->async_cb, scd);
  }
  else if (scd->async_cb == _forwarding_query_cb)
  {
    org_ofono_supplementary_services_call_initiate(
          sd->proxy, "*#004**11#", scd->cancellable, scd->async_cb, scd);
  }
  else
    g_assert_not_reached();
}

static void
_remove_call(service_call_data *scd)
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);

  g_assert(ctx);

  service_call_remove(ctx, scd->id);
  connui_cell_context_destroy(ctx);
}

#if 0
static gboolean
_sups_data_error(service_call_data *scd)
{
  if (scd->async_cb == _call_waiting_get_cb)
  {
    ((call_waiting_get_cb)scd->callback)(
          FALSE, scd->error, scd->user_data);
  }
  else if (scd->async_cb == _noreply_query_cb ||
           scd->async_cb == _unreachable_query_cb ||
           scd->async_cb == _busy_query_cb)
  {
    ((call_forwarding_get_cb)scd->callback)(
          FALSE, NULL, scd->user_data, scd->error);
  }
  else
    g_assert_not_reached();

  return G_SOURCE_REMOVE;
}

/* takes ownership of the error */
static void
_sups_data_error_idle(gpointer fn, gpointer cb, gpointer user_data,
                      GError *error)
{
  service_call_data *scd = g_new0(service_call_data, 1);

  scd->error = error;
  scd->callback = cb;
  scd->user_data = user_data;
  scd->async_cb = fn;

  g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, (GSourceFunc)_sups_data_error,
                  scd, (GDestroyNotify)service_call_destroy);
}
#endif

static guint
_sups_service_call(const char *modem_id,
                   GAsyncReadyCallback async_cb,
                   gpointer cb,
                   gpointer data,
                   gpointer user_data)
{
  GError *error = NULL;
  connui_cell_context *ctx;
  guint id;
  service_call_data *scd;
  sups_data *sd;

  if (!(ctx = connui_cell_context_get(&error)))
    return 0;

  if (!(sd = _sups_data_get(modem_id, ctx, &error)))
  {
    connui_cell_context_destroy(ctx);
    return 0;
  }

  id = service_call_next_id(ctx);
  scd = service_call_add(ctx, id, (GCallback)cb, user_data);
  scd->data = data;
  scd->async_cb = async_cb;
  scd->async_data = sd;

  _do_call(sd, scd);

  connui_cell_context_destroy(ctx);

  return id;
}

static void
_call_waiting_get_cb(GObject *object, GAsyncResult* res, gpointer data)
{
  OrgOfonoSupplementaryServices *proxy =
      ORG_OFONO_SUPPLEMENTARY_SERVICES(object);
  gchar *result_name;
  GVariant *value;
  service_call_data *scd = data;
  sups_data *sd = scd->async_data;
  gboolean enabled = FALSE;

  if (org_ofono_supplementary_services_call_initiate_finish(
        proxy, &result_name, &value, res, &scd->error))
  {
    if (strcmp(result_name, "CallWaiting"))
    {
      CONNUI_ERR("Unexpected result %s, ignoring", result_name);
      g_set_error(&scd->error, CONNUI_ERROR, CONNUI_ERROR_NOT_RECOGNIZED,
                  "%s", result_name);
    }
    else
    {
      GVariant *cw;
      GVariant *dict;
      GVariant *v;

      g_variant_get(value, "v", &cw);
      g_variant_get(cw, "(&s@a{sv})", NULL, &dict);
      v = g_variant_lookup_value(dict, "VoiceCallWaiting", NULL);

      if (v)
      {
        enabled = !strcmp(g_variant_get_string(v, NULL), "enabled");
        g_variant_unref(v);
      }

      g_variant_unref(cw);
    }

    g_free(result_name);
    g_variant_unref(value);
  }

  ((call_waiting_get_cb)scd->callback)(
        sd->path, enabled, scd->error, scd->user_data);

  _remove_call(scd);
}

guint
connui_cell_sups_get_call_waiting_enabled(const char *modem_id,
                                          call_waiting_get_cb cb,
                                          gpointer user_data)
{
  return _sups_service_call(modem_id, _call_waiting_get_cb,
                            cb, NULL, user_data);
}

static void
_call_waiting_set_cb(GObject *object, GAsyncResult* res, gpointer data)
{
  OrgOfonoSupplementaryServices *proxy =
      ORG_OFONO_SUPPLEMENTARY_SERVICES(object);
  gchar *result_name;
  GVariant *value;
  service_call_data *scd = data;
  sups_data *sd = scd->async_data;

  if (org_ofono_supplementary_services_call_initiate_finish(
        proxy, &result_name, &value, res, &scd->error))
  {
    if (strcmp(result_name, "CallWaiting"))
    {
      CONNUI_ERR("Unexpected result %s, ignoring", result_name);
      g_set_error(&scd->error, CONNUI_ERROR, CONNUI_ERROR_NOT_RECOGNIZED,
                  "%s", result_name);
    }

    g_free(result_name);
    g_variant_unref(value);
  }

  ((call_waiting_set_cb)scd->callback)(sd->path, scd->error, scd->user_data);

  _remove_call(scd);
}

guint
connui_cell_sups_set_call_waiting_enabled(const char *modem_id,
                                          gboolean enabled,
                                          call_waiting_set_cb cb,
                                          gpointer user_data)
{
  return _sups_service_call(modem_id, _call_waiting_set_cb, cb,
                            GINT_TO_POINTER(enabled), user_data);
}

static void
_forwarding_query_cb(GObject *object, GAsyncResult *res, gpointer data)
{
  OrgOfonoSupplementaryServices *proxy =
      ORG_OFONO_SUPPLEMENTARY_SERVICES(object);
  gchar *result_name;
  GVariant *value;
  service_call_data *scd = data;
  sups_data *sd = scd->async_data;
  GVariant *busy = NULL;
  GVariant *nr = NULL;
  GVariant *ur = NULL;
  connui_sups_call_forward scf;
  const connui_sups_call_forward *pscf = NULL;

  if (org_ofono_supplementary_services_call_initiate_finish(
        proxy, &result_name, &value, res, &scd->error))
  {
    if (strcmp(result_name, "CallForwarding"))
    {
      CONNUI_ERR("Unexpected result %s, ignoring", result_name);
      g_set_error(&scd->error, CONNUI_ERROR, CONNUI_ERROR_NOT_RECOGNIZED,
                  "%s", result_name);
    }
    else
    {
      GVariant *cf;
      GVariant *dict;
      const char *s;

      g_variant_get(value, "v", &cf);
      g_variant_get(cf, "(&s&s@a{sv})", NULL, NULL, &dict);

      if((busy = g_variant_lookup_value(dict, "VoiceBusy", NULL)) &&
         (s = g_variant_get_string(busy, NULL)) && *s)
      {
        scf.cond.busy.number = s;
      }
      else
        scf.cond.busy.number = NULL;

      if ((nr = g_variant_lookup_value(dict, "VoiceNoReply", NULL)) &&
          (s = g_variant_get_string(nr, NULL)) && *s)
      {
        scf.cond.no_reply.number = s;
      }
      else
        scf.cond.no_reply.number = NULL;

      if ((ur = g_variant_lookup_value(dict, "VoiceNotReachable", NULL)) &&
          (s = g_variant_get_string(ur, NULL)) && *s)
      {
        scf.cond.unreachable.number = s;
      }
      else
        scf.cond.unreachable.number = NULL;

      g_variant_unref(cf);

      /* OFONO API is broken, no way to get disabled phone */
      scf.cond.busy.enabled = !!scf.cond.busy.number;
      scf.cond.no_reply.enabled = !!scf.cond.no_reply.number;
      scf.cond.unreachable.enabled = !!scf.cond.unreachable.number;
      pscf = &scf;
    }

    g_free(result_name);
    g_variant_unref(value);
  }

  ((call_forwarding_get_cb)scd->callback)(
        sd->path, pscf, scd->user_data, scd->error);

  if (busy)
    g_variant_unref(busy);

  if (nr)
    g_variant_unref(nr);

  if(ur)
    g_variant_unref(ur);

  _remove_call(scd);
}

guint
connui_cell_sups_get_call_forwarding_enabled(const char *modem_id,
                                             call_forwarding_get_cb cb,
                                             gpointer user_data)
{
  return
      _sups_service_call(modem_id, _forwarding_query_cb, cb, NULL, user_data);
}

void
connui_cell_sups_cancel_service_call(guint id)
{
  connui_cell_context *ctx = connui_cell_context_get(NULL);

  g_return_if_fail(ctx != NULL);

  if (ctx->service_calls && id)
  {
    service_call_data *scd = service_call_find(ctx, id);

    if (scd)
    {
      if (scd->cancellable)
        g_cancellable_cancel(scd->cancellable);
      else
      {
        sups_data *sd = scd->async_data;
        service_call_take(ctx, id);

        if (sd && sd->pending)
          g_hash_table_remove(sd->pending, GUINT_TO_POINTER(id));

        service_call_destroy(scd);
      }
    }
  }

  connui_cell_context_destroy(ctx);
}
