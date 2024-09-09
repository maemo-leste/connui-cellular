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

typedef struct _OperatorNameCBSHomeItem OperatorNameCBSHomeItem;
typedef struct _OperatorNameCBSHomeItemClass OperatorNameCBSHomeItemClass;
typedef struct _OperatorNameCBSHomeItemPrivate OperatorNameCBSHomeItemPrivate;

struct _OperatorNameCBSHomeItem
{
  HDHomePluginItem hitem;
};

struct _OperatorNameCBSHomeItemClass
{
  HDHomePluginItemClass parent;
};

GType operator_name_cbs_home_item_get_type(void);

#define IS_EMPTY(s) (!(s) || !(s)[0])
#define CLEAR(s) s = (g_free(s), NULL)
#define MODEM_REGISTERED(v) ((v) == CONNUI_NET_REG_STATUS_HOME || \
  (v) == CONNUI_NET_REG_STATUS_ROAMING)

#define PRIVATE(obj) \
  operator_name_cbs_home_item_get_instance_private(\
  (OperatorNameCBSHomeItem *)(obj));

static void get_operator_name(OperatorNameCBSHomeItem *item,
                              const char *modem_id,
                              const cell_network_state *state);

typedef struct
{
  gchar *operator_name;
  gchar *service_provider_name;
  gboolean show_service_provider;
  connui_net_registration_status reg_status;
  connui_net_radio_access_tech rat_name;
}
home_item_modem;

struct _OperatorNameCBSHomeItemPrivate
{
  GHashTable *modems;
  GtkWidget *label;
  guint flightmode;
};

HD_DEFINE_PLUGIN_MODULE_WITH_PRIVATE(OperatorNameCBSHomeItem,
                                     operator_name_cbs_home_item,
                                     HD_TYPE_HOME_PLUGIN_ITEM);

static void
update_widget(OperatorNameCBSHomeItemPrivate *priv)
{
  GList *modems = connui_cell_modem_get_modems();
  GList *l;
  GString *s = g_string_new(NULL);
  gchar *display_name;
  int count = 0;

  /* support only 2 modems in the status area */
  for (l = modems; l && count < 2; l = l->next)
  {
    home_item_modem *modem = g_hash_table_lookup(priv->modems, l->data);
    const gchar *op = NULL;

    if (modem)
    {
      if (!IS_EMPTY(modem->operator_name))
        op = modem->operator_name;
      else if (!IS_EMPTY(modem->service_provider_name))
        op = modem->service_provider_name;
    }

    if (!op)
      op = "";

    if (count)
      g_string_append_c(s, '\n');

    g_string_append(s, op);

    count++;
  }

  g_list_free_full(modems, g_free);
  display_name = g_string_free(s, FALSE);

  gtk_label_set_text(GTK_LABEL(priv->label), display_name);
  g_free(display_name);

  gtk_widget_queue_draw(priv->label);
}

static void
destroy_modem(home_item_modem *modem)
{
  g_free(modem->service_provider_name);
  g_free(modem->operator_name);
  g_free(modem);
}

static void
widget_flightmode_cb(guint offline, gpointer user_data)
{
  OperatorNameCBSHomeItemPrivate *priv = PRIVATE(user_data);

  if (offline)
  {
    g_hash_table_remove_all(priv->modems);

    update_widget(priv);
  }

  priv->flightmode = offline;
}

static void
modem_remove(OperatorNameCBSHomeItem *item, const char *modem_id)
{
  OperatorNameCBSHomeItemPrivate *priv = PRIVATE(item);

  g_hash_table_remove(priv->modems, modem_id);
}

static home_item_modem *
modem_create(OperatorNameCBSHomeItem *item, const char *modem_id)
{
  OperatorNameCBSHomeItemPrivate *priv = PRIVATE(item);
  home_item_modem *modem = g_new0(home_item_modem, 1);

  modem->reg_status = CONNUI_NET_REG_STATUS_UNKNOWN;
  modem->rat_name = CONNUI_NET_RAT_UNKNOWN;

  g_hash_table_insert(priv->modems, g_strdup(modem_id), modem);

  return modem;
}

static home_item_modem *
modem_get(OperatorNameCBSHomeItem *item, const char *modem_id)
{
  OperatorNameCBSHomeItemPrivate *priv = PRIVATE(item);

  return g_hash_table_lookup(priv->modems, modem_id);
}

static void
widget_net_status_cb(const char *modem_id, const cell_network_state *state,
                     gpointer user_data)
{
  OperatorNameCBSHomeItem *item = user_data;
  OperatorNameCBSHomeItemPrivate *priv = PRIVATE(item);
  home_item_modem *modem;

  if (priv->flightmode)
    return;

  if (state->reg_status == CONNUI_NET_REG_STATUS_UNKNOWN)
  {
    modem_remove(item, modem_id);
    update_widget(priv);
    return;
  }

  modem = modem_get(item, modem_id);

  if (!modem)
    modem = modem_create(item, modem_id);

  if (state->reg_status != modem->reg_status)
  {
    modem->reg_status = state->reg_status;
    modem->rat_name = CONNUI_NET_RAT_UNKNOWN;

    update_widget(priv);
  }

  if (state->network)
  {
    if (modem->reg_status == CONNUI_NET_REG_STATUS_UNKNOWN)
    {
      modem->rat_name = CONNUI_NET_RAT_UNKNOWN;

      update_widget(priv);
    }

    if (state->operator_name && MODEM_REGISTERED(modem->reg_status))
    {
      get_operator_name(item, modem_id, state);
      modem->rat_name = CONNUI_NET_RAT_UNKNOWN;

      update_widget(priv);
    }
  }

  if (state->rat_name != modem->rat_name && MODEM_REGISTERED(modem->reg_status))
  {
    modem->rat_name = state->rat_name;

    update_widget(priv);
  }
}

static void
get_operator_name(OperatorNameCBSHomeItem *item, const char *modem_id,
                  const cell_network_state *state)
{
  OperatorNameCBSHomeItemPrivate *priv = PRIVATE(item);
  gchar *name;
  cell_network *net;
  home_item_modem *modem = modem_get(item, modem_id);

  if (state->operator_name)
  {
    if (!modem->operator_name ||
        strcmp(modem->operator_name, state->operator_name))
    {
      g_free(modem->operator_name);
      modem->operator_name = g_strdup(state->operator_name);
      update_widget(priv);
    }
  }

  net = state->network;
  // Ensure that these properties are available, otherwise, bail
  if ((!net->country_code) || (!net->operator_code))
      return;

  name = connui_cell_net_get_operator_name(net, NULL);

  if (name)
  {
    if (IS_EMPTY(modem->operator_name))
    {
      g_free(modem->operator_name);
      modem->operator_name = name;

      update_widget(priv);
    }
    else
      g_free(name);
  }

  if (!modem->service_provider_name)
  {
    modem->service_provider_name =
        connui_cell_sim_get_service_provider(modem_id, NULL);
    update_widget(priv);
  }

  if (!IS_EMPTY(modem->service_provider_name) &&
      ((modem->reg_status == CONNUI_NET_REG_STATUS_HOME) ||
       (connui_cell_sim_is_network_in_service_provider_info(
          atoi(net->country_code), atoi(net->operator_code)))))
  {
    if (modem->operator_name)
    {
      if (strcasecmp(modem->operator_name, modem->service_provider_name))
      {
        modem->show_service_provider = TRUE;
        update_widget(priv);
      }
    }
  }
  else if (modem->service_provider_name)
  {
    modem->show_service_provider = TRUE;
    update_widget(priv);
  }
}

static void
widget_modem_status_cb(const char *modem_id, const connui_modem_status *status,
                       gpointer user_data)
{
  OperatorNameCBSHomeItem *item = user_data;
  OperatorNameCBSHomeItemPrivate *priv = PRIVATE(item);

  if (priv->flightmode)
    return;

  update_widget(priv);
}

static gboolean
operator_name_cbs_home_item_expose_event(GtkWidget *widget,
                                         GdkEventExpose *event)
{
  OperatorNameCBSHomeItemPrivate *priv = PRIVATE(widget);
  PangoLayout *layout = gtk_label_get_layout(GTK_LABEL(priv->label));
  cairo_t *cr = gdk_cairo_create(GDK_DRAWABLE(widget->window));

  pango_layout_set_line_spacing(layout, .85);

  gdk_cairo_region(cr, event->region);
  cairo_clip(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
  cairo_paint(cr);
  cairo_destroy(cr);

  return GTK_WIDGET_CLASS(operator_name_cbs_home_item_parent_class)->
      expose_event(widget, event);
}

static void
operator_name_cbs_home_item_realize(GtkWidget *widget)
{
  GdkScreen *screen = gtk_widget_get_screen(widget);

  gtk_widget_set_colormap(widget, gdk_screen_get_rgba_colormap(screen));
  gtk_widget_set_app_paintable(widget, TRUE);
  GTK_WIDGET_CLASS(operator_name_cbs_home_item_parent_class)->realize(widget);
}

static void
operator_name_cbs_home_item_init(OperatorNameCBSHomeItem *home_item)
{
  OperatorNameCBSHomeItemPrivate *priv = PRIVATE(home_item);
  priv->modems =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                            (GDestroyNotify)destroy_modem);

  priv->flightmode = FALSE;

  priv->label = gtk_label_new(NULL);
  hildon_helper_set_logical_font(priv->label, "SystemFont");

  gtk_widget_set_size_request(priv->label, -1, 56);
  gtk_misc_set_alignment(GTK_MISC(priv->label), 0.0f, 0.5f);
  gtk_misc_set_padding(GTK_MISC(priv->label), 0, 0);
  gtk_widget_show(priv->label);

  gtk_container_add(GTK_CONTAINER(home_item), priv->label);

  connui_cell_net_status_register(widget_net_status_cb, home_item);
  connui_cell_modem_status_register(widget_modem_status_cb, home_item);
  connui_flightmode_status(widget_flightmode_cb, home_item);
}

static void
operator_name_cbs_home_item_finalize(GObject* object)
{
  connui_cell_net_status_close(widget_net_status_cb);
  connui_cell_modem_status_close(widget_modem_status_cb);
  connui_flightmode_close(widget_flightmode_cb);

  G_OBJECT_CLASS(operator_name_cbs_home_item_parent_class)->finalize(object);
}

static void
operator_name_cbs_home_item_class_finalize(OperatorNameCBSHomeItemClass *klass)
{
}

static void
operator_name_cbs_home_item_class_init(OperatorNameCBSHomeItemClass* klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

  widget_class->realize = operator_name_cbs_home_item_realize;
  widget_class->expose_event = operator_name_cbs_home_item_expose_event;

  G_OBJECT_CLASS(klass)->finalize = operator_name_cbs_home_item_finalize;
}
