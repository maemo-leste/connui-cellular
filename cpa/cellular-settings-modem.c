/*
 * cellular-settings-modem.c
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

#include "cellular-settings.h"
#include "cellular-settings-modem.h"
#include "cellular-settings-sim.h"
#include "cellular-settings-call.h"
#include "cellular-settings-net.h"

#include <glib/gi18n-lib.h>

#include "connui-cellular.h"

enum
{
  MODEM_COLUMN_NAME,
  MODEM_COLUMN_ID
};

static void
_modem_selector_append_modem(GtkListStore *store, const gchar *id)
{
  gchar *model = connui_cell_modem_get_model(id, NULL);
  gchar *serial = connui_cell_modem_get_serial(id, NULL);

  if (model && serial)
  {
    gchar *text = g_strdup_printf("%s(%s)", model, serial);

    gtk_list_store_insert_with_values(store, NULL, -1,
                                      MODEM_COLUMN_NAME, text,
                                      MODEM_COLUMN_ID, id,
                                      -1);
    g_free(text);
  }

  g_free(serial);
  g_free(model);
}

static void
_modem_state_cb(const char *modem_id, const connui_modem_status *status,
                gpointer user_data)
{
  cellular_settings *cs = user_data;
  HildonTouchSelector *selector;
  GtkTreeModel *model;
  GtkTreeIter iter;
  gint i = 0;
  gint found = -1;

  selector = hildon_picker_button_get_selector(
        HILDON_PICKER_BUTTON(cs->modem_picker));
  model = hildon_touch_selector_get_model(selector, 0);

  if (gtk_tree_model_get_iter_first(model, &iter))
  {
    do
    {
      gchar *id;

      gtk_tree_model_get(model, &iter, MODEM_COLUMN_ID, &id , -1);

      if (!strcmp(modem_id, id))
      {
        if (*status == CONNUI_MODEM_STATUS_REMOVED)
        {
          if (i == hildon_touch_selector_get_active(selector, 0))
          {
            cellular_settings_cancel_service_calls(cs);
            g_object_set_data(G_OBJECT(selector), "modem_id", NULL);
          }

          gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
        }

        g_free(id);
        found = i;

        break;
      }

      g_free(id);
      i++;
    }
    while (gtk_tree_model_iter_next(model, &iter));
  }

  if (!gtk_tree_model_iter_n_children(model, NULL))
  {
    cellular_settings_destroy(cs);
    gtk_main_quit();
    return;
  }

  if (*status != CONNUI_MODEM_STATUS_REMOVED && found == -1)
    _modem_selector_append_modem(GTK_LIST_STORE(model), modem_id);

  gtk_widget_set_sensitive(cs->modem_picker,
                           gtk_tree_model_iter_n_children(model, NULL) > 1);
}

static void
_modem_show(const gchar *modem_id, cellular_settings *cs)
{
  PangoLayout *layout;

  connui_cell_code_ui_destroy();
  gtk_widget_hide(cs->pannable_area);
  gtk_widget_hide(cs->error_status);
  gtk_dialog_set_response_sensitive(GTK_DIALOG(cs->dialog),
                                    GTK_RESPONSE_OK, FALSE);

  if (connui_cell_code_ui_init(modem_id, GTK_WINDOW(cs->dialog), FALSE))
  {
    gtk_widget_show(cs->pannable_area);
    hildon_gtk_window_set_progress_indicator(
          GTK_WINDOW(cs->dialog), TRUE);

    _sim_show(cs, modem_id);
    _call_show(cs, modem_id);
    _net_show(cs, modem_id);
  }
  else
  {
    const char *note_type = "no_sim";
    gchar *note;

    switch (connui_cell_sim_get_status(modem_id, NULL))
    {
      case CONNUI_SIM_STATE_REJECTED:
        note_type = "sim_rejected";
        break;
      case CONNUI_SIM_STATE_LOCKED:
        note_type = "sim_locked";
        break;
      case CONNUI_SIM_STATUS_OK_PIN_REQUIRED:
      case CONNUI_SIM_STATUS_OK_PUK_REQUIRED:
      case CONNUI_SIM_STATUS_OK_PUK2_REQUIRED:
        note_type = "no_pin";
        break;
      default:
        note_type = "no_sim";
        break;
    }

    note = connui_cell_code_ui_error_note_type_to_text(NULL, note_type);
    gtk_label_set_text(GTK_LABEL(cs->error_status), note);
    g_free(note);

    gtk_widget_show(cs->error_status);

    gtk_widget_set_size_request(cs->error_status, -1, -1);
    layout = gtk_label_get_layout(GTK_LABEL(cs->error_status));

    if (pango_layout_get_line_count(layout) > 1)
    {
      GtkAllocation size;

      gtk_widget_get_allocation(cs->modem_picker, &size);
      gtk_widget_set_size_request(cs->error_status, size.width, -1);
    }
  }
}

static void
_modem_selection_changed_cb(HildonTouchSelector *selector, gint column,
                            gpointer user_data)
{
  cellular_settings *cs = user_data;
  const gchar *current;
  gchar *id;

  GtkTreeModel *model;
  GtkTreeIter iter;

  if (!hildon_touch_selector_get_selected (selector, 0, &iter))
    return;

  model = hildon_touch_selector_get_model(selector, 0);
  current = g_object_get_data(G_OBJECT(selector), "modem_id");

  gtk_tree_model_get(model, &iter, MODEM_COLUMN_ID, &id , -1);

  if (!current || strcmp(current, id))
  {
    cellular_settings_cancel_service_calls(cs);

    if (current && !cellular_settings_apply(cs, TRUE))
    {
      gint active =
          GPOINTER_TO_INT(g_object_get_data(G_OBJECT(selector), "active"));
      g_object_set_data_full(G_OBJECT(selector), "modem_id", id, g_free);
      hildon_touch_selector_set_active(selector, 0, active);
      return;
    }

    g_object_set_data(
          G_OBJECT(selector), "active",
          GINT_TO_POINTER(hildon_touch_selector_get_active(selector, 0)));
    g_object_set_data_full(G_OBJECT(selector), "modem_id", id, g_free);
    _modem_show(id, cs);
  }

  gtk_widget_set_sensitive(cs->modem_picker,
                           gtk_tree_model_iter_n_children(model, NULL) > 1);
}

GtkWidget *
_modem_widgets_create(cellular_settings *cs)
{
  GtkListStore *store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
  GtkWidget *button;
  HildonTouchSelector *selector;
  GList *l;
  GList *modems;

  connui_cell_modem_status_register(_modem_state_cb, cs);

  modems = connui_cell_modem_get_modems();
  selector = HILDON_TOUCH_SELECTOR(hildon_touch_selector_new_text());
  button = hildon_picker_button_new(HILDON_SIZE_FINGER_HEIGHT,
                                    HILDON_BUTTON_ARRANGEMENT_VERTICAL);
  hildon_picker_button_set_selector(HILDON_PICKER_BUTTON(button), selector);
  hildon_touch_selector_set_model(selector, 0, GTK_TREE_MODEL(store));

  for (l = modems; l; l = l->next)
    _modem_selector_append_modem(store, l->data);

  g_list_free_full(modems, g_free);

  if (!gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store), NULL))
  {
    gtk_widget_destroy(button);
    return NULL;
  }

  hildon_button_set_title(HILDON_BUTTON(button), _("conn_ti_phone_modem"));

  g_signal_connect (G_OBJECT(selector), "changed",
                    G_CALLBACK(_modem_selection_changed_cb), cs);

  gtk_widget_set_sensitive(button, FALSE);
  gtk_button_set_alignment(GTK_BUTTON(button), 0.0, 0.5);

  return button;
}

void
_modem_destroy(cellular_settings *cs)
{
  connui_cell_modem_status_close(_modem_state_cb);
}
