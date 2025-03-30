/*
 * cellular-settings-net.c
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

#include "config.h"

#include <connui/connui-log.h>

#include "cellular-settings.h"
#include "cellular-settings-net.h"
#include "cellular-settings-utils.h"

#include <glib/gi18n-lib.h>

static GtkWidget *
_roaming_widget_create()
{
  return cellular_settings_create_touch_selector(
        0,
        _("conn_va_phone_network_roam_ask"),
        _("conn_va_phone_network_roam_allow"),
        NULL);
}

static void
_roaming_set(cellular_settings *cs, gboolean ask)
{
  GObject *object = G_OBJECT(cs->network.data.roam);

  g_object_set_data(object, "guard", GINT_TO_POINTER(TRUE));
  hildon_picker_button_set_active(HILDON_PICKER_BUTTON(object), ask ? 0 : 1);
  g_object_set_data(object, "guard", GINT_TO_POINTER(FALSE));
}

static void
_roaming_value_changed_cb(HildonPickerButton *button, gpointer user_data)
{
  cellular_settings *cs = user_data;
  gboolean ask;
  GError *error = NULL;

  if (g_object_get_data(G_OBJECT(button), "guard"))
    return;

  ask = hildon_picker_button_get_active(button) == 0;

  if (!connui_cell_connection_set_roaming_allowed(
        cellular_settings_get_current_modem_id(cs), !ask, &error))
  {
    CONNUI_ERR("Error while setting roaming allowed: %s",
               error ? error->message : "unknown error");

    _roaming_set(cs, ask ? 1 : 0);
  }
}

void
_net_widgets_create(cellular_settings *cs, GtkWidget *parent,
                    GtkSizeGroup *size_group)
{
/*  GtkWidget *button;

  cs->network_select = create_widget(parent, size_group,
                                     _("conn_fi_phone_network_sel"),
                                     network_select_create);
  g_signal_connect(G_OBJECT(cs->network_select), "value-changed",
                   (GCallback)network_select_value_changed_cb, settings);

  cs->network_mode = create_widget(parent, size_group,
                                   _("conn_fi_phone_network_mode"),
                                   network_mode_create);*/
  cs->network.data.roam = cellular_settings_create_widget(
        parent, size_group, _("conn_fi_phone_network_data_roam"),
        _roaming_widget_create);
  g_signal_connect(G_OBJECT(cs->network.data.roam), "value-changed",
                   G_CALLBACK(_roaming_value_changed_cb), cs);

/*
  button = create_button(
        parent, size_group, _("conn_bd_phone_network_data_counter"));
  g_signal_connect(G_OBJECT(button), "clicked",
                   (GCallback)network_data_counter_clicked_cb, settings);

  button = create_button(
        parent, size_group, _("conn_bd_phone_roaming_network_data_counter"));

  g_signal_connect(G_OBJECT(button), "clicked",
                   (GCallback)roaming_network_data_counter_clicked_cb,
                   settings);*/
}

static void
_disable_widgets(cellular_settings *cs)
{
  gtk_widget_set_sensitive(cs->network.data.roam, FALSE);
}

void
_net_show(cellular_settings *cs, const gchar *modem_id)
{
  const cell_connection_status *conn_status;

  _disable_widgets(cs);

  conn_status = connui_cell_connection_get_status(modem_id, NULL);

  if (conn_status)
  {
    _roaming_set(cs, !conn_status->roaming_allowed);
    gtk_widget_set_sensitive(cs->network.data.roam, TRUE);
  }
}
