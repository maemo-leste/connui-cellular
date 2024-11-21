/*
 * cellular-settings-sim.c
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
#include "cellular-settings-sim.h"
#include "cellular-settings-utils.h"

#include <glib/gi18n-lib.h>

#include "connui-cellular.h"

static void
_sim_pin_request_set(cellular_settings *cs, gboolean active)
{
  GObject *object = G_OBJECT(cs->sim.pin_request);

  g_object_set_data(object, "guard", GINT_TO_POINTER(TRUE));
  hildon_check_button_set_active(HILDON_CHECK_BUTTON(object), active);
  g_object_set_data(object, "guard", GINT_TO_POINTER(FALSE));
}

static void
_sim_pin_init(cellular_settings *cs)
{
  GObject *object = G_OBJECT(cs->sim.pin);

  g_object_set_data(object, "guard", GINT_TO_POINTER(TRUE));
  hildon_entry_set_text(HILDON_ENTRY(object), "1234");
  g_object_set_data(object, "guard", GINT_TO_POINTER(FALSE));
}

static void
_sim_pin_request_toggled_cb(HildonCheckButton *button, cellular_settings *cs)
{
  gboolean active ;

  if (g_object_get_data(G_OBJECT(button), "guard"))
    return;

  active = hildon_check_button_get_active(button);

  if (!connui_cell_code_ui_set_current_code_active(active))
  {
    active = !active;
    _sim_pin_request_set(cs, active);
  }

  gtk_widget_set_sensitive(cs->sim.pin, active);
}

static void
_sim_pin_changed_cb(cellular_settings *cs)
{
  if (g_object_get_data(G_OBJECT(cs->sim.pin), "guard"))
    return;

  connui_cell_code_ui_change_code(CONNUI_SIM_SECURITY_CODE_PIN);
  _sim_pin_init(cs);
}

static void
_disable_widgets(cellular_settings *cs)
{
  gtk_widget_set_sensitive(cs->sim.pin_request, FALSE);
  gtk_widget_set_sensitive(cs->sim.pin, FALSE);
}

void
_sim_widgets_create(cellular_settings *cs, GtkWidget *parent,
                    GtkSizeGroup *size_group)
{
  cs->sim.pin_request = cellular_settings_create_widget(
        parent, size_group, _("conn_fi_phone_network_pin_request"),
        cellular_settings_create_check_button);
  g_signal_connect(G_OBJECT(cs->sim.pin_request), "toggled",
                   G_CALLBACK(_sim_pin_request_toggled_cb), cs);
  cs->sim.pin = cellular_settings_create_widget(
        parent, size_group, _("conn_fi_phone_network_pin"),
        cellular_settings_create_entry);
  gtk_entry_set_visibility(GTK_ENTRY(cs->sim.pin), FALSE);
  g_signal_connect_swapped(G_OBJECT(cs->sim.pin), "changed",
                           G_CALLBACK(_sim_pin_changed_cb), cs);
  g_signal_connect_swapped(G_OBJECT(cs->sim.pin), "button-press-event",
                           G_CALLBACK(_sim_pin_changed_cb), cs);
  _disable_widgets(cs);
}

void
_sim_show(cellular_settings *cs, const gchar *modem_id)
{
  GError *error = NULL;

  _sim_pin_init(cs);

  cs->sim.code_active = connui_cell_security_code_get_enabled(
        modem_id, CONNUI_SIM_SECURITY_CODE_PIN, &error);

  if (error)
  {
    CONNUI_ERR("Error while fetching security code enabled: %s",
               error->message);
    g_clear_error(&error);
  }

  _sim_pin_request_set(cs, cs->sim.code_active);

  gtk_widget_set_sensitive(cs->sim.pin_request, TRUE);
  gtk_widget_set_sensitive(cs->sim.pin, cs->sim.code_active);
}

void
_sim_apply(cellular_settings *cs, const gchar *modem_id)
{

}

void
_sim_cancel(cellular_settings *cs)
{

}
