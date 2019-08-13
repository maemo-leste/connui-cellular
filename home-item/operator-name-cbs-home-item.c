/*
 *	operator-name-cbs-home-item (operator name item)
 *	Copyright (C) 2011 Nicolai Hess/Jonathan Wilson
 *	Copyright (C) 2012 Pali Rohár <pali.rohar@gmail.com>
 *	
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *	
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *	GNU General Public License for more details.
 *	
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <hildon/hildon.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <gconf/gconf-client.h>
#include <connui/connui.h>

#include "connui-cellular.h"

#include "operator-name-cbs-home-item.h"
#include "smsutil.h"

#define PHONE_NET_SERVICE "com.nokia.phone.net"
#define PHONE_NET_PATH "/com/nokia/phone/net"
#define PHONE_NET_IFACE "Phone.Net"
#define PHONE_SIM_SERVICE "com.nokia.phone.SIM"
#define PHONE_SIM_PATH "/com/nokia/phone/SIM"
#define PHONE_SIM_IFACE "Phone.Sim"

#define GET_OPERATOR_NAME "get_operator_name"
#define GET_REGISTRATION_STATUS "get_registration_status"
#define GET_SERVICE_PROVIDER_NAME "get_service_provider_name"
#define GET_SERVICE_PROVIDER_INFO "get_service_provider_info"

#define OPERATOR_NAME_PATH "/apps/connui-cellular"
#define OPERATOR_NAME_CBSMS_ENABLED OPERATOR_NAME_PATH "/cbsms_enabled"
#define OPERATOR_NAME_CBSMS_CHANNEL OPERATOR_NAME_PATH "/cbsms_channel"
#define OPERATOR_NAME_CUSTOM_ENABLED OPERATOR_NAME_PATH "/custom_enabled"
#define OPERATOR_NAME_CUSTOM_NAME OPERATOR_NAME_PATH "/custom_name"
#define OPERATOR_NAME_LOGGING_ENABLED OPERATOR_NAME_PATH "/logging_enabled"
#define OPERATOR_NAME_NAME_LOGGING_ENABLED OPERATOR_NAME_PATH "/name_logging_enabled"

#define OPERATOR_NAME_CBS_HOME_ITEM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE(obj, TYPE_OPERATOR_NAME_CBS_HOME_ITEM, OperatorNameCBSHomeItemPrivate))
gboolean cbsms = TRUE;
gboolean custom = FALSE;
gint channel = 50;

static void get_operator_name(OperatorNameCBSHomeItemPrivate *priv,
                              const cell_network_state *state);

struct _OperatorNameCBSHomeItemPrivate
{
  GtkWidget* label;
  gchar* custom_name;
  gchar* display_name;
  gchar* operator_name;
  gchar* service_provider_name;
  gchar* cell_name;
  gboolean show_service_provider;
  gchar reg_status;
  gchar rat_name;
  gchar *operator_state_name;
  int status;
  guint flightmode;
  guint32 service_provider_name_type;
  guint32 cell_id;
  guint gconfnotify_id;
  DBusGConnection* dbus_conn;
  GConfClient *gconf_client;
};

HD_DEFINE_PLUGIN_MODULE(OperatorNameCBSHomeItem, operator_name_cbs_home_item,
                        HD_TYPE_HOME_PLUGIN_ITEM);

static void
update_widget(OperatorNameCBSHomeItemPrivate *priv)
{
  const char *display_name = NULL;
  const char *service_name = NULL;
  const char *cell_name = NULL;
  char *name = NULL;
  int len = 0;

  if (priv->status < 0)
    display_name = NULL;
  else if (priv->custom_name && custom)
    display_name = priv->custom_name;
  else if (priv->display_name && priv->display_name[0])
    display_name = priv->display_name;
  else if (priv->operator_name && priv->operator_name[0])
    display_name = priv->operator_name;
  else if (priv->service_provider_name && priv->service_provider_name[0])
    display_name = priv->service_provider_name;

  if (priv->status < 0 || priv->service_provider_name == display_name)
    service_name = NULL;
  else if (priv->show_service_provider && priv->service_provider_name[0])
    service_name = priv->service_provider_name;

  if (display_name && service_name && !strcasecmp(display_name, service_name))
    service_name = NULL;

  if (priv->status < 0)
    cell_name = NULL;
  else if (priv->cell_name && priv->cell_name[0] && cbsms)
    cell_name = priv->cell_name;

  if (display_name)
    len += strlen(display_name);

  if (service_name)
    len += strlen(service_name);

  if (cell_name)
    len += strlen(cell_name);

  name = calloc(1, len+6);

  if (!name)
    return;

  len = 0;

  if (display_name)
  {
    if (len) { name[len] = ' '; name[len+1] = ' '; len += 2; }
    len += sprintf(name+len, "%s", display_name);
    while (len && name[len-1] == ' ') --len;
    name[len] = 0;
  }

  if (service_name)
  {
    if (len) { name[len] = ' '; name[len+1] = ' '; len += 2; }
    len += sprintf(name+len, "%s", service_name);
    while (len && name[len-1] == ' ') --len;
    name[len] = 0;
  }

  if (cell_name)
  {
    if (len) {
      name[len] = ' ';
      name[len+1] = ' ';
      len += 2;
    }

    len += sprintf(name+len, "%s", cell_name);

    while (len && name[len-1] == ' ')
      --len;

    name[len] = 0;
  }

  gtk_label_set_text(GTK_LABEL(priv->label), name);
  gtk_widget_queue_draw(priv->label);

  free(name);
}

static DBusHandlerResult
_dbus_message_filter_func(DBusConnection* connection, DBusMessage* message,
                          void* user_data)
{
  OperatorNameCBSHomeItem* home_item = OPERATOR_NAME_CBS_HOME_ITEM(user_data);

  if(home_item)
  {
    OperatorNameCBSHomeItemPrivate* priv =
        OPERATOR_NAME_CBS_HOME_ITEM_GET_PRIVATE(home_item);

    if(priv && !priv->flightmode)
    {
      if(dbus_message_is_signal(message,"Phone.SMS","IncomingCBS"))
      {
        char *data;
        int count;
        int serial;
        int message_id;
        int coding;
        int page;
        int info_length;
        struct cbs cbs;
        GSList *l = NULL;
        char lang[3];
        char *utf8 = NULL;
        unsigned char pdu[88];

        dbus_message_get_args(message, NULL,
                              DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE, &data, &count,
                              DBUS_TYPE_UINT32, &serial,
                              DBUS_TYPE_UINT32, &message_id,
                              DBUS_TYPE_BYTE, &coding,
                              DBUS_TYPE_BYTE, &page,
                              DBUS_TYPE_BYTE, &info_length,
                              DBUS_TYPE_INVALID);

        pdu[0] = (unsigned char)((serial >> 8) & 0xFF);
        pdu[1] = (unsigned char)(serial & 0xFF);
        pdu[2] = (unsigned char)((message_id >> 8) & 0xFF);
        pdu[3] = (unsigned char)(message_id & 0xFF);
        pdu[4] = (unsigned char)coding;
        pdu[5] = (unsigned char)page;

        memset(&pdu[6], 0, 82);
        memcpy(&pdu[6], data, count);
        cbs_decode(pdu, 88, &cbs);

        if (cbs.ud[0])
          l = g_slist_append(NULL, &cbs);

        if (l)
          utf8 = cbs_decode_text(l, lang);

        if (cbs.message_identifier == channel && utf8 &&
            !strncmp(utf8, "@@@@@", 5))
        {
          DBusGProxy *dbus_g_proxy =
              dbus_g_proxy_new_for_name(priv->dbus_conn, PHONE_NET_SERVICE,
                                        PHONE_NET_PATH, PHONE_NET_IFACE);

          if(dbus_g_proxy)
          {
            gint32 net_err = -1;
            guchar status;
            guint16 lac;
            guint16 network_type;
            guint16 services;
            guint32 cellid;
            guint32 operator_code;
            guint32 country_code;

            if(dbus_g_proxy_call(dbus_g_proxy, GET_REGISTRATION_STATUS, NULL,
                                 G_TYPE_INVALID,
                                 G_TYPE_UCHAR, &status,
                                 G_TYPE_UINT, &lac,
                                 G_TYPE_UINT, &cellid,
                                 G_TYPE_UINT, &operator_code,
                                 G_TYPE_UINT, &country_code,
                                 G_TYPE_UCHAR, &network_type,
                                 G_TYPE_UCHAR, &services,
                                 G_TYPE_INT, &net_err,
                                 G_TYPE_INVALID) && net_err == 0)
            {
              if (priv->cell_id != cellid)
                priv->cell_id = cellid;
            }
          }

          g_free(priv->cell_name);
          priv->cell_name = g_strdup(utf8);
          update_widget(priv);
        }

        g_free(utf8);
        g_slist_free(l);
      }
    }
  }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
widget_flightmode_cb(guint offline, gpointer user_data)
{
  OperatorNameCBSHomeItem* home_item = OPERATOR_NAME_CBS_HOME_ITEM(user_data);

  if(home_item)
  {
    OperatorNameCBSHomeItemPrivate* priv =
        OPERATOR_NAME_CBS_HOME_ITEM_GET_PRIVATE(home_item);

    if (!priv)
      return;

    if (offline)
    {
      g_free(priv->cell_name);
      priv->cell_name = g_strdup("");
      g_free(priv->display_name);
      priv->display_name = g_strdup("");
      priv->reg_status = -1;
      g_free(priv->operator_state_name);
      priv->operator_state_name = g_strdup("NULL");
      priv->status = -1;
      priv->rat_name = -1;
      priv->cell_id = -1;
      update_widget(priv);
    }

    priv->flightmode = offline;
  }
}

static gboolean
name_handler(gpointer user_data)
{
  OperatorNameCBSHomeItem* home_item = OPERATOR_NAME_CBS_HOME_ITEM(user_data);

  if(home_item)
  {
    OperatorNameCBSHomeItemPrivate* priv =
        OPERATOR_NAME_CBS_HOME_ITEM_GET_PRIVATE(home_item);

    if(priv)
    {
      g_free(priv->cell_name);
      priv->cell_name = g_strdup("");
      update_widget(priv);
    }
  }

  return FALSE;
}

static void
widget_net_status_cb(const cell_network_state *state, gpointer user_data)
{
  OperatorNameCBSHomeItem* home_item = OPERATOR_NAME_CBS_HOME_ITEM(user_data);
  if(home_item)
  {
    OperatorNameCBSHomeItemPrivate* priv = OPERATOR_NAME_CBS_HOME_ITEM_GET_PRIVATE(home_item);
    if(priv && !priv->flightmode)
    {
      if (state->reg_status != priv->reg_status)
      {
        priv->reg_status = state->reg_status;
        g_free(priv->cell_name);
        priv->cell_name = g_strdup("");
        g_free(priv->display_name);
        priv->display_name = g_strdup("");
        g_free(priv->operator_state_name);
        priv->operator_state_name = g_strdup("NULL");
        priv->status = -1;
        priv->rat_name = -1;
        priv->cell_id = -1;
        update_widget(priv);
      }
      if (state->network)
      {
        if (state->network->service_status != priv->status &&
            priv->reg_status < NETWORK_REG_STATUS_NOSERV)
        {
          priv->status = state->network->service_status;
          g_free(priv->cell_name);
          priv->cell_name = g_strdup("");
          priv->cell_id = -1;
          priv->rat_name = -1;
          update_widget(priv);
        }

        if (state->operator_name &&
            strcmp(state->operator_name, priv->operator_state_name) &&
            priv->reg_status < NETWORK_REG_STATUS_NOSERV && priv->status < 2)
        {
          g_free(priv->operator_state_name);
          priv->operator_state_name = g_strdup(state->operator_name);
          get_operator_name(priv, state);
          g_free(priv->cell_name);
          priv->cell_name = g_strdup("");
          priv->cell_id = -1;
          priv->rat_name = -1;
          update_widget(priv);
        }
      }

      if (state->rat_name != priv->rat_name &&
          priv->reg_status < NETWORK_REG_STATUS_NOSERV && priv->status < 2)
      {
        priv->rat_name = state->rat_name;
        g_free(priv->cell_name);
        priv->cell_name = g_strdup("");
        priv->cell_id = -1;
        update_widget(priv);
      }

      if (priv->cell_id != state->cell_id &&
          priv->reg_status < NETWORK_REG_STATUS_NOSERV && priv->status < 2)
      {
        priv->cell_id = state->cell_id;
        g_timeout_add_seconds(5, name_handler, home_item);
      }
    }
  }
}

static void
_disable_dbus_filter_func(DBusConnection* conn, OperatorNameCBSHomeItem* plugin)
{
  if(conn)
    dbus_connection_remove_filter(conn, _dbus_message_filter_func, NULL);
}

static void
_set_dbus_filter_func(DBusConnection* conn, OperatorNameCBSHomeItem* plugin)
{
  if(conn)
  {
    dbus_bus_add_match(conn,"type='signal',interface='Phone.SMS'", NULL);
    dbus_connection_add_filter(conn, _dbus_message_filter_func, plugin, NULL);
  }
}

static void
gconf_changed_func(GConfClient *gconf_client, guint cnxn_id, GConfEntry *entry,
                   OperatorNameCBSHomeItem* home_item)
{
  if (gconf_entry_get_value (entry) != NULL &&
      gconf_entry_get_value (entry)->type == GCONF_VALUE_BOOL)
  {
    gboolean new_setting = gconf_value_get_bool(gconf_entry_get_value(entry));

    if (!strcmp(gconf_entry_get_key(entry),OPERATOR_NAME_CBSMS_ENABLED))
    {
      cbsms = new_setting;
      update_widget(home_item->priv);
    }
    else if (!strcmp(gconf_entry_get_key(entry),OPERATOR_NAME_CUSTOM_ENABLED))
    {
      custom = new_setting;
      update_widget(home_item->priv);
    }
  }
  else if (gconf_entry_get_value (entry) != NULL &&
           gconf_entry_get_value (entry)->type == GCONF_VALUE_INT)
  {
    if (!strcmp(gconf_entry_get_key(entry), OPERATOR_NAME_CBSMS_CHANNEL))
    {
      channel = gconf_value_get_int(gconf_entry_get_value(entry));

      if (channel <= 0)
        channel = 50;

      g_free(home_item->priv->cell_name);
      home_item->priv->cell_name = g_strdup("");
      update_widget(home_item->priv);
    }
  }
  else if (gconf_entry_get_value (entry) != NULL &&
           gconf_entry_get_value (entry)->type == GCONF_VALUE_STRING)
  {
    if (!strcmp(gconf_entry_get_key(entry), OPERATOR_NAME_CUSTOM_NAME))
    {
      const gchar *name = gconf_value_get_string(gconf_entry_get_value(entry));

      g_free(home_item->priv->custom_name);
      home_item->priv->custom_name = g_strdup(name);
      update_widget(home_item->priv);
    }
  }
}

static void
get_operator_name(OperatorNameCBSHomeItemPrivate *priv,
                  const cell_network_state *state)
{
  if (state->operator_name)
  {
    if (!priv->operator_name ||
        strcmp(priv->operator_name, state->operator_name))
    {
      g_free(priv->operator_name);
      priv->operator_name = g_strdup(state->operator_name);
      update_widget(priv);
    }
  }

  // Ensure that these properties are available, otherwise, bail
  if ((!state->network->country_code) || (!state->network->operator_code))
      return;

  gint error;
  gchar *operator =
      connui_cell_net_get_operator_name(state->network, TRUE, &error);

  guchar code[3];
  /* WTF is going on here? BCD? */
  code[0] = (state->network->country_code[0] - 0x30) |
      ((state->network->country_code[1] - 0x30) << 4);
  code[1] = (strlen(state->network->operator_code) == 2) ? 0xF0 : ((state->network->operator_code[2] << 4) & 0xF0) | (state->network->country_code[2] - 0x30);
  code[2] = (state->network->operator_code[0] - 0x30) | ((state->network->operator_code[1] - 0x30) << 4);

  /* we can't have error set and operator name != NULL */
  if (error == 0 && operator)
  {
    if (!priv->operator_name || !priv->operator_name[0])
    {
      g_free(priv->operator_name);
      priv->operator_name = g_strdup(operator);

      update_widget(priv);
    }
    else
      g_free(operator);
  }

  if (!priv->service_provider_name)
  {
    priv->service_provider_name =
        connui_cell_sim_get_service_provider(&priv->service_provider_name_type,
                                             &error);
    update_widget(priv);
  }

  if (priv->service_provider_name && priv->service_provider_name[0] &&
      ((priv->reg_status == 0) ||
       (connui_cell_sim_is_network_in_service_provider_info(&error, code))))
  {
    if (!(priv->service_provider_name_type & 1))
    {
      g_free(priv->operator_name);
      priv->operator_name = g_strdup(priv->service_provider_name);
    }
    else if (priv->operator_name)
    {
      if (strcasecmp(priv->operator_name, priv->service_provider_name))
      {
        priv->show_service_provider = TRUE;
        update_widget(priv);
      }
    }
  }
  else if (priv->service_provider_name)
  {
    if (priv->service_provider_name_type & 2 && priv->service_provider_name &&
        !strcasecmp(priv->operator_name, priv->service_provider_name))
    {
      return;
    }

    priv->show_service_provider = TRUE;
    update_widget(priv);
  }
}

static gboolean
operator_name_cbs_home_item_expose_event(GtkWidget *widget,
                                         GdkEventExpose *event)
{
  cairo_t *cr = gdk_cairo_create (GDK_DRAWABLE (widget->window));

  gdk_cairo_region (cr, event->region);
  cairo_clip (cr);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.0);
  cairo_paint (cr);
  cairo_destroy (cr);

  return GTK_WIDGET_CLASS(operator_name_cbs_home_item_parent_class)->
      expose_event (widget, event);
}

static void
operator_name_cbs_home_item_realize(GtkWidget *widget)
{
  GdkScreen *screen = gtk_widget_get_screen (widget);

  gtk_widget_set_colormap (widget, gdk_screen_get_rgba_colormap (screen));
  gtk_widget_set_app_paintable (widget, TRUE);
  GTK_WIDGET_CLASS(operator_name_cbs_home_item_parent_class)->realize (widget);
}

static void
operator_name_cbs_home_item_init(OperatorNameCBSHomeItem* home_item)
{
  home_item->priv = OPERATOR_NAME_CBS_HOME_ITEM_GET_PRIVATE(home_item);
  home_item->priv->cell_name = g_strdup("");
  home_item->priv->reg_status = -1;
  home_item->priv->operator_state_name = g_strdup("NULL");
  home_item->priv->status = -1;
  home_item->priv->rat_name = -1;
  home_item->priv->cell_id = -1;
  home_item->priv->dbus_conn =
      hd_home_plugin_item_get_dbus_g_connection(&home_item->hitem,
                                                DBUS_BUS_SYSTEM, NULL);
  home_item->priv->label = gtk_label_new(NULL);
  home_item->priv->flightmode = FALSE;
  gtk_widget_set_name(home_item->priv->label, "HomeCellularLabel");
  hildon_helper_set_logical_font(home_item->priv->label, "SystemFont");
  gtk_label_set_text(GTK_LABEL(home_item->priv->label), "");
  gtk_misc_set_alignment(GTK_MISC(home_item->priv->label), 0.0f, 1.0f);
  gtk_misc_set_padding(GTK_MISC(home_item->priv->label), 0, 12);
  gtk_widget_show(GTK_WIDGET(home_item->priv->label));
  gtk_container_add(GTK_CONTAINER(home_item),
                    GTK_WIDGET(home_item->priv->label));

  if (home_item->priv->gconf_client == NULL)
    home_item->priv->gconf_client = gconf_client_get_default();

  if (home_item->priv->gconfnotify_id == 0)
  {
    gconf_client_add_dir(home_item->priv->gconf_client, OPERATOR_NAME_PATH,
                         GCONF_CLIENT_PRELOAD_NONE, NULL);
    home_item->priv->gconfnotify_id =
        gconf_client_notify_add(home_item->priv->gconf_client,
                                OPERATOR_NAME_PATH,
                                (GConfClientNotifyFunc) gconf_changed_func,
                                home_item, NULL, NULL);
  }

  GConfValue *val = gconf_client_get(home_item->priv->gconf_client,
                                     OPERATOR_NAME_CBSMS_ENABLED, NULL);

  if (val && val->type == GCONF_VALUE_BOOL)
    cbsms = gconf_value_get_bool(val);

  channel = gconf_client_get_int(home_item->priv->gconf_client,
                                 OPERATOR_NAME_CBSMS_CHANNEL, NULL);
  custom = gconf_client_get_bool(home_item->priv->gconf_client,
                                 OPERATOR_NAME_CUSTOM_ENABLED, NULL);
  home_item->priv->custom_name = 0;
  gchar *on = gconf_client_get_string(home_item->priv->gconf_client,
                                      OPERATOR_NAME_CUSTOM_NAME,NULL);

  if (on)
    home_item->priv->custom_name = g_strdup(on);

  if (channel <= 0)
    channel = 50;

  _set_dbus_filter_func(
        dbus_g_connection_get_connection(home_item->priv->dbus_conn),
        home_item);
  connui_cell_net_status_register(widget_net_status_cb, home_item);
  connui_flightmode_status(widget_flightmode_cb, home_item);
}

static void
operator_name_cbs_home_item_finalize(GObject* object)
{
  OperatorNameCBSHomeItem* home_item = OPERATOR_NAME_CBS_HOME_ITEM(object);

  connui_cell_net_status_close(widget_net_status_cb);
  connui_flightmode_close(widget_flightmode_cb);

  if (home_item->priv->gconfnotify_id)
  {
    gconf_client_notify_remove(home_item->priv->gconf_client, home_item->priv->gconfnotify_id);
    gconf_client_remove_dir(home_item->priv->gconf_client, OPERATOR_NAME_PATH, NULL);
    home_item->priv->gconfnotify_id = 0;
  }

  if (home_item->priv->gconf_client)
  {
    gconf_client_clear_cache(home_item->priv->gconf_client);
    g_object_unref (G_OBJECT(home_item->priv->gconf_client));
    home_item->priv->gconf_client = NULL;
  }

  _disable_dbus_filter_func(
        dbus_g_connection_get_connection(home_item->priv->dbus_conn),
        home_item);
  G_OBJECT_CLASS(operator_name_cbs_home_item_parent_class)->finalize(object);
}

static void
operator_name_cbs_home_item_class_finalize(OperatorNameCBSHomeItemClass* klass)
{
}

static void
operator_name_cbs_home_item_class_init(OperatorNameCBSHomeItemClass* klass)
{
  g_type_class_add_private(klass, sizeof(OperatorNameCBSHomeItemPrivate));
  GTK_WIDGET_CLASS(klass)->realize = operator_name_cbs_home_item_realize;
  GTK_WIDGET_CLASS(klass)->expose_event = operator_name_cbs_home_item_expose_event;
  G_OBJECT_CLASS(klass)->finalize = (GObjectFinalizeFunc)operator_name_cbs_home_item_finalize;
  gtk_rc_parse_string("style \"HomeCellularLabel\" = \"osso-color-themeing\" { fg[NORMAL] = \"#FFFFFF\" engine \"sapwood\" { shadowcolor = \"#000000\" }} widget \"*.HomeCellularLabel\" style \"HomeCellularLabel\"");
}
