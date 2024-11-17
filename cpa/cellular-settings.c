#include "config.h"

#include "cellular-settings.h"

#include <glib/gi18n-lib.h>
#include <connui/iap-common.h>
#include <connui/connui-utils.h>

#include "connui-cell-note.h"

#include "cellular-settings-utils.h"
#include "cellular-settings-modem.h"
#include "cellular-settings-sim.h"
#include "cellular-settings-call.h"
#include "cellular-settings-abook.h"

static cellular_settings *_cellular_settings = NULL;

static cellular_settings *
cellular_settings_create()
{

  if (!_cellular_settings)
    _cellular_settings = g_new0(cellular_settings, 1);

  return _cellular_settings;
}

void
cellular_settings_cancel_service_calls(cellular_settings *cs)
{
  _sim_cancel(cs);
  _call_cancel(cs);

  while (cs->pending)
    g_main_context_iteration(NULL, TRUE);
}

void
cellular_settings_destroy()
{
  cellular_settings *cs = _cellular_settings;

  if (!cs)
    return;

  cellular_settings_cancel_service_calls(cs);

  _modem_destroy(cs);

  if (cs->dialog)
    gtk_widget_destroy(cs->dialog);

  g_free(cs);

  connui_cell_code_ui_destroy();
  _cellular_settings = NULL;
}

void
cellular_settings_stop_progress_indicator(cellular_settings *cs)
{
  g_return_if_fail(cs->pending);

  if (!--cs->pending)
  {
    cs->applying = FALSE;
    hildon_gtk_window_set_progress_indicator(GTK_WINDOW(cs->dialog), FALSE);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(cs->dialog),
                                      GTK_RESPONSE_OK, TRUE);
  }
}

static void
_cellular_settings_applying(cellular_settings *cs, gboolean applying)
{
  hildon_gtk_window_set_progress_indicator(
        GTK_WINDOW(cs->dialog), applying ? 1 : 0);
  gtk_widget_set_sensitive(GTK_DIALOG(cs->dialog)->vbox, !applying);
  gtk_dialog_set_response_sensitive(
        GTK_DIALOG(cs->dialog), GTK_RESPONSE_OK, !applying);
}

gboolean
cellular_settings_apply(cellular_settings *cs, gboolean enable_after)
{
  HildonTouchSelector *selector;
  const gchar *modem_id;

  const gchar *divert_phone =
      hildon_entry_get_text(HILDON_ENTRY(cs->call.forward.to));

  if (hildon_picker_button_get_active(
       HILDON_PICKER_BUTTON(cs->call.forward.option)) == 0 &&
      (!divert_phone || !*divert_phone) )
  {
    hildon_banner_show_information(
          GTK_WIDGET(cs->dialog), NULL,
          dgettext("hildon-common-strings", "ckct_ib_enter_a_value"));
    gtk_widget_grab_focus(cs->call.forward.to);
    return FALSE;
  }
/*
  (*cs)->set_forwarding_has_error = FALSE;
  */
  selector = hildon_picker_button_get_selector(
              HILDON_PICKER_BUTTON(cs->modem_picker));
  modem_id = g_object_get_data(G_OBJECT(selector), "modem_id");

  cs->applying = TRUE;
  //(*cs)->set_call_has_error = FALSE;

  _cellular_settings_applying(cs, TRUE);
  _sim_apply(cs, modem_id);
  _call_apply(cs, modem_id);

  cs->applying = FALSE;

  if (enable_after)
    _cellular_settings_applying(cs, FALSE);

  while (cs->pending)
    g_main_context_iteration(NULL, TRUE);

  return TRUE;
}

static void
_contact_chooser_dialog_response(GtkDialog *dialog, gint response_id,
                                 gpointer user_data)
{
  cellular_settings *cs = user_data;
  gchar *phone_number = cellular_abook_get_selected_number();

  cellular_abook_destroy();

  if (phone_number)
  {
    if (response_id == GTK_RESPONSE_OK)
      hildon_entry_set_text(HILDON_ENTRY(cs->call.forward.to), phone_number);

    g_free(phone_number);
  }
}

static void
_cellular_settings_response_cb(GtkDialog *dialog, gint response_id,
                               gpointer user_data)
{
  cellular_settings *cs = user_data;

  g_return_if_fail(cs != NULL);

  switch (response_id)
  {
    case GTK_RESPONSE_CANCEL:
    {
      if (hildon_check_button_get_active(
            HILDON_CHECK_BUTTON(cs->sim.pin_request)) != cs->sim.code_active)
      {
        connui_cell_code_ui_set_current_code_active(cs->sim.code_active);
      }
      break;
    }
    case GTK_RESPONSE_OK:
    {
      if (!cellular_settings_apply(cs, FALSE))
        return;

      break;
    }
    case GTK_RESPONSE_CONTACT:
    {
      GtkDialog *contact_chooser =
          cellular_abook_show(GTK_WINDOW(cs->dialog),cs->osso);

      g_signal_connect(G_OBJECT(contact_chooser), "response",
                       G_CALLBACK(_contact_chooser_dialog_response), cs);
      return;
    }
    case 11:
    case 12:
    {
/*      gboolean home_counter = response_id == 11;
      GtkDialog *datacounter_dialog = cellular_data_counter_show(
            GTK_WINDOW((*cs)->dialog), home_counter);

      (*cs)->datacounter_dialog = GTK_WIDGET(datacounter_dialog);

      g_signal_connect((*cs)->datacounter_dialog, "response",
                       G_CALLBACK(datacounter_dialog_response), cs);
      return;
      */
    }
    default:
      return;
  }

  cellular_settings_destroy();
  gtk_main_quit();
}

static void
create_settings_widgets(GtkWidget *vbox, cellular_settings *cs)
{
  GtkSizeGroup *size_group;
  int i;
  struct
  {
    const char *label;
    void (*create)(cellular_settings *, GtkWidget *, GtkSizeGroup *);
  }
  settings[] =
  {
    {_("conn_ti_phone_call"), _call_widgets_create},
    /*{_("conn_ti_phone_network"), init_network_options},*/
    {_("conn_ti_phone_sim"), _sim_widgets_create}
  };

  size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

  for(i = 0; i < G_N_ELEMENTS(settings); i++)
  {
    if (settings[i].label)
    {
      GtkWidget *label = gtk_label_new(settings[i].label);

      if (i)
      {
        GtkWidget *lbl = gtk_label_new("");

        gtk_widget_set_size_request(lbl, -1, 35);
        gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);
      }

      gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0.5);
      hildon_helper_set_logical_font(label, "LargeSystemFont");

      gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    }

    settings[i].create(cs, vbox, size_group);
  }

  g_object_unref(size_group);
}

static osso_return_t
cellular_settings_show(cellular_settings *cs, GtkWindow *parent)
{
  GtkWidget *vbox;

  g_return_val_if_fail(cs != NULL, OSSO_ERROR);

  if (cs->dialog)
  {
    gtk_widget_show_all(cs->dialog);

    return OSSO_OK;
  }

  cs->modem_picker = _modem_widgets_create(cs);

  if (!cs->modem_picker)
  {
    gchar *s = connui_cell_code_ui_error_note_type_to_text(NULL, "no_network");
    GtkWidget *note = connui_cell_note_new_information(parent, s);

    g_free(s);
    cellular_settings_destroy(cs);
    gtk_dialog_run(GTK_DIALOG(note));
    gtk_widget_destroy(note);
    return OSSO_ERROR;
  }

  cs->dialog = gtk_dialog_new_with_buttons(
        _("conn_ti_phone_cpa"), parent,
        GTK_DIALOG_NO_SEPARATOR |
        GTK_DIALOG_DESTROY_WITH_PARENT |
        GTK_DIALOG_MODAL,
        dgettext("hildon-libs", "wdgt_bd_save"), GTK_RESPONSE_OK, NULL);

  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(cs->dialog)->vbox), cs->modem_picker,
                     FALSE, FALSE, 0);

  /* error label */
  cs->error_status = gtk_label_new("Some Error");
  gtk_label_set_line_wrap(GTK_LABEL(cs->error_status), TRUE);
  gtk_misc_set_alignment(GTK_MISC(cs->error_status), 0.5, 0.5);
  gtk_label_set_justify(GTK_LABEL(cs->error_status), GTK_JUSTIFY_CENTER);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG((cs->dialog))->vbox),
                     cs->error_status, TRUE, TRUE, 0);

  /* pannable area */
  cs->pannable_area = hildon_pannable_area_new();

  hildon_pannable_area_set_center_on_child_focus(
        HILDON_PANNABLE_AREA(cs->pannable_area), TRUE);
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG((cs->dialog))->vbox),
                    cs->pannable_area);
  gtk_widget_set_size_request(cs->pannable_area, -1, 350);

  vbox = gtk_vbox_new(0, 8);

  create_settings_widgets(vbox, cs);

  hildon_pannable_area_add_with_viewport(
        HILDON_PANNABLE_AREA(cs->pannable_area), vbox);
  iap_common_set_close_response(cs->dialog, GTK_RESPONSE_CANCEL);

  g_signal_connect(G_OBJECT(cs->dialog), "response",
                   (GCallback)_cellular_settings_response_cb, cs);

  gtk_widget_show_all(cs->dialog);
  gtk_widget_hide(cs->pannable_area);
  gtk_widget_hide(cs->error_status);

  return OSSO_OK;
}

static gboolean
cellular_settings_get_state(gpointer user_data)
{
  cellular_settings *cs = user_data;

  cs->idle_id = 0;
  hildon_picker_button_set_active(HILDON_PICKER_BUTTON(cs->modem_picker), 0);

  return FALSE;
}

osso_return_t
execute(osso_context_t *osso, gpointer data, gboolean user_activated)
{
  cellular_settings *cs = cellular_settings_create();
  osso_return_t rv = OSSO_OK;

  cs->osso = connui_utils_inherit_osso_context(
        osso, PACKAGE_NAME, PACKAGE_VERSION);

  if((rv = cellular_settings_show(cs, data)) != OSSO_OK)
    return rv;

  if (user_activated)
    cs->idle_id = g_idle_add(cellular_settings_get_state, cs);
//  else
//    cs->idle_id = g_idle_add(cellular_settings_restore_state, cs);

  gtk_main();

  return rv;
}
