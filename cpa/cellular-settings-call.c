/*
 * cellular-settings-call.c
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
#include "cellular-settings-call.h"
#include "cellular-settings-utils.h"

#include <glib/gi18n-lib.h>

#include "connui-cellular.h"

static void
_call_divert_contact_clicked_cb(gpointer user_data);

static void
_call_forward_option_value_changed_cb(HildonPickerButton *button,
                                     gpointer user_data);

static GtkWidget *
_create_call_divert_widget()
{
  return cellular_settings_create_touch_selector(
        1, _("conn_fi_phone_call_divert_note"),
        dgettext("hildon-libs", "wdgt_bd_no"),
        NULL);
}

static GtkWidget *
_create_send_call_id_widget()
{
  HildonTouchSelector *selector;
  HildonTouchSelectorColumn *column;
  GtkWidget *button;
  GtkTreeIter iter;
  GtkListStore *list_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_UINT);

  gtk_list_store_insert_with_values(list_store, &iter, 0,
                                    0, dgettext("hildon-libs", "wdgt_bd_yes"),
                                    1, 2,
                                    -1);
  gtk_list_store_insert_with_values(list_store, &iter, 1,
                                    0, dgettext("hildon-libs", "wdgt_bd_no"),
                                    1, 1,
                                    -1);
  gtk_list_store_insert_with_values(list_store, &iter, 2,
                                    0, _("conn_va_phone_clir_network"),
                                    1, 0,
                                    -1);
  selector = HILDON_TOUCH_SELECTOR(hildon_touch_selector_new());

  column = hildon_touch_selector_append_text_column(
        selector, GTK_TREE_MODEL(list_store), TRUE);

  g_object_unref(G_OBJECT(list_store));
  g_object_set(G_OBJECT(column), "text-column", 0, NULL);

  button = hildon_picker_button_new(HILDON_SIZE_FINGER_HEIGHT,
                                    HILDON_BUTTON_ARRANGEMENT_VERTICAL);

  hildon_picker_button_set_selector(HILDON_PICKER_BUTTON(button), selector);
  hildon_picker_button_init(button, 0);

  return button;
}

static void
_call_divert_option_show_widgets(cellular_settings *cs, gboolean show)
{
  if (show)
  {
    gtk_widget_show_all(gtk_widget_get_parent(cs->call.forward.to));
    gtk_widget_show_all(cs->call.forward.contact);
  }
  else
  {
    gtk_widget_hide_all(gtk_widget_get_parent(cs->call.forward.to));
    gtk_widget_hide_all(cs->call.forward.contact);
  }
}

static void
_disable_widgets(cellular_settings *cs)
{
  gtk_widget_set_sensitive(cs->call.send_cid, FALSE);
  gtk_widget_set_sensitive(cs->call.waiting.button, FALSE);

  _call_divert_option_show_widgets(cs, FALSE);
  gtk_widget_set_sensitive(cs->call.forward.option, FALSE);
}

void
_call_widgets_create(cellular_settings *cs, GtkWidget *parent,
                     GtkSizeGroup *size_group)
{
  cs->call.send_cid = cellular_settings_create_widget(
        parent, size_group, _("conn_fi_phone_send_call_id"),
        _create_send_call_id_widget);

  cs->call.waiting.button = cellular_settings_create_widget(
        parent, size_group, _("conn_fi_phone_call_waiting"),
        cellular_settings_create_check_button);

  cs->call.forward.option = cellular_settings_create_widget(
        parent, size_group, _("conn_fi_phone_call_divert"),
        _create_call_divert_widget);

  g_signal_connect(G_OBJECT(cs->call.forward.option), "value-changed",
                   G_CALLBACK(_call_forward_option_value_changed_cb), cs);

  cs->call.forward.to = cellular_settings_create_widget(
        parent, size_group, _("conn_fi_phone_call_divert_to"),
        cellular_settings_create_entry);

  hildon_gtk_entry_set_input_mode(GTK_ENTRY(cs->call.forward.to),
                                  HILDON_GTK_INPUT_MODE_TELE);

  cs->call.forward.contact = cellular_settings_create_button(
        parent, size_group, _("conn_bd_phone_call_divert_contact"));
  g_signal_connect_swapped(G_OBJECT(cs->call.forward.contact), "clicked",
                   (GCallback)_call_divert_contact_clicked_cb, cs);
  _disable_widgets(cs);
}

static void
_call_divert_contact_clicked_cb(gpointer user_data)
{
  cellular_settings *cs = user_data;

  gtk_dialog_response(GTK_DIALOG(cs->dialog), GTK_RESPONSE_CONTACT);
}

static void
_call_forward_option_value_changed_cb(HildonPickerButton *button,
                                     gpointer user_data)
{
  cellular_settings *cs = user_data;
  gboolean divert = hildon_picker_button_get_active(button) == 0;

  _call_divert_option_show_widgets(cs, divert);
}

static void
_get_clir_cb(guint anonymity, GError *error, gpointer user_data)
{
  cellular_settings *cs = user_data;
  HildonPickerButton *button = HILDON_PICKER_BUTTON(cs->call.send_cid);
  HildonTouchSelector *selector = hildon_picker_button_get_selector(button);
  GtkTreeModel *model = hildon_touch_selector_get_model(selector, 0);
  GtkTreeIter iter;

  if (error)
    CONNUI_ERR("Error while fetching caller ID: %s", error->message);

  cs->call.anonimity = anonymity;

  if (gtk_tree_model_get_iter_first(model, &iter))
  {
    do
    {
      guint val;

      gtk_tree_model_get(model, &iter, 1, &val , -1);

      if (anonymity == val)
      {
        hildon_touch_selector_select_iter(selector, 0, &iter, FALSE);
        break;
      }
    }
    while (gtk_tree_model_iter_next(model, &iter));
  }

  gtk_widget_set_sensitive(cs->call.send_cid, TRUE);

  cellular_settings_stop_progress_indicator(cs);
}

static void
_get_call_forward_cb(const char *modem_id, const connui_sups_call_forward *cf,
                     gpointer user_data, GError *error)
{
  cellular_settings *cs = user_data;
  const char *number = NULL;
  gboolean enabled = FALSE;
  gint active;

  cs->call.svc_call_id = 0;

  if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
  {
    cs->pending--;
    return;
  }

  if (!error)
  {
    if ((enabled = cf->cond.busy.enabled))
      number = cf->cond.busy.number;
    else if ((enabled = cf->cond.no_reply.enabled))
      number = cf->cond.no_reply.number;
    else if ((enabled = cf->cond.unreachable.enabled))
      number = cf->cond.unreachable.number;
  }
  else
    CONNUI_ERR("Error in while fetching call forwarding: %s", error->message);

  active = enabled ? 0 : 1;

  cellular_settings_stop_progress_indicator(cs);
  cs->call.forward.enabled = enabled;

  hildon_picker_button_set_active(
        HILDON_PICKER_BUTTON(cs->call.forward.option), active);

  _call_divert_option_show_widgets(cs, enabled && !error);

  if (number)
  {
    g_object_set_data_full(G_OBJECT(cs->call.forward.to), "phone_number",
                           g_strdup(number), g_free);
    hildon_entry_set_text(HILDON_ENTRY(cs->call.forward.to), number);
  }
  else
    g_object_set_data(G_OBJECT(cs->call.forward.to), "phone_number", NULL);

  gtk_widget_set_sensitive(cs->call.forward.option, !error);
}

static void
_get_call_waiting_cb(const char *modem_id, gboolean enabled, GError *error,
                     gpointer user_data)
{
  cellular_settings *cs = user_data;

  cs->call.svc_call_id = 0;

  if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
  {
    cs->pending--;
    return;
  }

  if (error)
    CONNUI_ERR("Error while fetching call waiting: %s", error->message);

  cs->call.waiting.enabled = enabled;
  hildon_check_button_set_active(
        HILDON_CHECK_BUTTON(cs->call.waiting.button), enabled);

  if (!error)
    gtk_widget_set_sensitive(cs->call.waiting.button, TRUE);

  cs->call.svc_call_id = connui_cell_sups_get_call_forwarding_enabled(
        modem_id, _get_call_forward_cb, cs);

  if (!cs->call.svc_call_id)
    cellular_settings_stop_progress_indicator(cs);
}

void
_call_show(cellular_settings *cs, const gchar *modem_id)
{
  _disable_widgets(cs);

  cs->call.forward.enabled = FALSE;
  cs->call.waiting.enabled = FALSE;

  hildon_picker_button_set_active(
        HILDON_PICKER_BUTTON(cs->call.forward.option), 1);

  if (connui_cell_net_get_caller_id_anonymity(_get_clir_cb, cs))
    cs->pending++;

  cs->call.svc_call_id = connui_cell_sups_get_call_waiting_enabled(
        modem_id, _get_call_waiting_cb, cs);

  if (cs->call.svc_call_id)
    cs->pending++;
}

static void
_set_clir_cb(GError *error, gpointer user_data)
{
  cellular_settings *cs = user_data;

  cs->pending--;
}

static void
_call_apply_anonymity(cellular_settings *cs)
{
  guint anonimity = 0;
  HildonPickerButton *button = HILDON_PICKER_BUTTON(cs->call.send_cid);
  HildonTouchSelector *selector = hildon_picker_button_get_selector(button);
  GtkTreeModel *model = hildon_touch_selector_get_model(selector, 0);
  GtkTreeIter iter;

  if (hildon_touch_selector_get_selected(selector, 0, &iter))
    gtk_tree_model_get(model, &iter, 1, &anonimity, -1);

  if (anonimity != cs->call.anonimity)
  {
    if (connui_cell_net_set_caller_id_anonymity(anonimity, _set_clir_cb, cs))
      cs->pending++;

    while (cs->pending)
      g_main_context_iteration(NULL, TRUE);
/*
    if (anonimity == 2)
      cid_presentation_bluez = "allowed";
    else if (anonimity == 1))
      cid_presentation_bluez = "restricted";
    else if (anonimity == 0)
      cid_presentation_bluez = "none";
    else
      cid_presentation_bluez = NULL;

    connui_cell_net_set_caller_id_presentation_bluez(cid_presentation_bluez);
    */
  }
}

static void
_set_call_waiting_enabled_cb(const char *modem_id, GError *error,
                             gpointer user_data)
{
  cellular_settings *cs = user_data;

  cs->pending--;
}

static void
_call_apply_waiting(cellular_settings *cs, const gchar *modem_id)
{
  gboolean enabled = hildon_check_button_get_active(
        HILDON_CHECK_BUTTON(cs->call.waiting.button));

  if (enabled != cs->call.waiting.enabled)
  {
    if (connui_cell_sups_set_call_waiting_enabled(
          modem_id, enabled, _set_call_waiting_enabled_cb, cs))
    {
      cs->pending++;
    }
  }
}

void
_call_apply(cellular_settings *cs, const gchar *modem_id)
{
  _call_apply_anonymity(cs);
  _call_apply_waiting(cs, modem_id);
}

void
_call_cancel(cellular_settings *cs)
{
  if (cs->call.svc_call_id)
    connui_cell_cancel_service_call(cs->call.svc_call_id);
}
