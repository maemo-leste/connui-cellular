#include <libosso.h>
#include <connui/iap-common.h>
#include <connui/connui-flightmode.h>
#include <connui/connui-log.h>
#include <connui/connui-utils.h>
#include <conbtui/gateway/common.h>
#include <hildon/hildon.h>
#include <hildon-cp-plugin/hildon-cp-plugin-interface.h>
#include <icd/osso-ic-gconf.h>
#include <gconf/gconf-client.h>

#include <libintl.h>
#include <string.h>

#include "connui-cellular.h"

#include "net-selection.h"
#include "data-counter.h"

#include "config.h"

#define _(msgid) dgettext(GETTEXT_PACKAGE, msgid)

/* #define CONTACT_CHOOSER */

struct _cell_settings
{
  GtkWidget *dialog;
  GtkWidget *pannable_area;
  GtkWidget *selection_dialog;
  GtkWidget *contact_chooser;
  GtkWidget *datacounter_dialog;
  GtkWidget *send_call_id;
  GtkWidget *call_waiting;
  GtkWidget *call_divert;
  GtkWidget *call_divert_to;
  GtkWidget *call_divert_contact;
  GtkWidget *network_select;
  GtkWidget *network_mode;
  GtkWidget *network_data_roaming;
  GtkWidget *network_pin_request;
  GtkWidget *network_pin;
  osso_context_t *osso;
  gboolean network_selection_mode_manual;
  gboolean user_activated;
  gint exporting_settings;
  gboolean set_forwarding_has_error;
  gboolean set_call_has_error;
  gboolean security_code_enabled;
  guint call_forwarding_type;
  gboolean network_select_in_sync;
  gboolean cs_status_inactive;
  gchar *cid_presentation;
  gboolean call_waiting_enabled;
  gint selected_divert;
  gchar *divert_phone_number;
  guchar radio_access_mode;
  guchar selected_net_mode;
  gboolean rat_changed;
  int call_status;
  guint idle_id;
  gint svc_call_in_progress;
  int caller_id;
  guint call_forwarding_id;
  guint call_wating_id;
};

struct caller_id_select_cb_data
{
  const gchar *caller_id;
  GtkWidget *button;
};

typedef struct _cell_settings cell_settings;

static void cellular_settings_quit(cell_settings **settings);
static void activate_widgets(cell_settings **settings);

static cell_settings **
get_settings()
{
  static cell_settings *settings;

  if (!settings)
    settings = g_new0(cell_settings, 1);

  return &settings;
}

static void
cellular_settings_call_status_cb(int status, gpointer user_data)
{
  cell_settings **settings = user_data;

  g_return_if_fail(settings != NULL && *settings != NULL);

  (*settings)->call_status = status;

  if (status)
  {
    HildonTouchSelector *selector = hildon_picker_button_get_selector(
          HILDON_PICKER_BUTTON((*settings)->network_mode));
    GtkWidget *dialog =
        gtk_widget_get_ancestor(GTK_WIDGET(selector), GTK_TYPE_DIALOG);

    if (GTK_IS_DIALOG(dialog))
      gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);

    gtk_widget_set_sensitive((*settings)->network_mode, FALSE);
  }
  else
    gtk_widget_set_sensitive((*settings)->network_mode, TRUE);
}

static void
cellular_settings_cs_status_cb(gboolean active, gpointer user_data)
{
  cell_settings **settings = user_data;

  g_return_if_fail(settings != NULL && *settings != NULL);

  if (!active)
  {
    (*settings)->cs_status_inactive = TRUE;

    if (!connui_cell_code_ui_cancel_dialog())
      cellular_settings_quit(settings);
  }
}

static void
cellular_settings_destroy(cell_settings **settings)
{
  connui_cell_net_cancel_service_call((*settings)->call_forwarding_id);
  connui_cell_net_cancel_service_call((*settings)->call_wating_id);

  if ((*settings)->idle_id)
    g_source_remove((*settings)->idle_id);

  connui_cell_call_status_close(cellular_settings_call_status_cb);
  connui_cell_cs_status_close(cellular_settings_cs_status_cb);
  g_free((*settings)->divert_phone_number);
  g_free((*settings)->cid_presentation);
  connui_cell_code_ui_destroy();
  cellular_net_selection_destroy();
  gtk_widget_destroy((*settings)->dialog);
  g_free(*settings);
  *settings = NULL;
}

static void
cellular_settings_quit(cell_settings **settings)
{
  cellular_settings_destroy(settings);
  gtk_main_quit();
}

osso_return_t
save_state(osso_context_t *osso, gpointer data)
{
  get_settings();

  return OSSO_ERROR;
}

static void
picker_button_set_inactive(GtkWidget *button)
{
  hildon_picker_button_set_active(HILDON_PICKER_BUTTON(button), FALSE);
  gtk_button_set_alignment(GTK_BUTTON(button), 0.0, 0.5);
}

static GtkWidget *
create_touch_selector(const gchar *text1, ...)
{
  HildonTouchSelector *selector;
  GtkWidget *button;
  va_list ap;

  va_start(ap, text1);

  selector = HILDON_TOUCH_SELECTOR(hildon_touch_selector_new_text());
  button = hildon_picker_button_new(HILDON_SIZE_FINGER_HEIGHT,
                                    HILDON_BUTTON_ARRANGEMENT_VERTICAL);
  hildon_picker_button_set_selector(HILDON_PICKER_BUTTON(button), selector);

  while (text1)
  {
      hildon_touch_selector_append_text(selector, text1);
      text1 = va_arg(ap, const gchar *);
  }

  picker_button_set_inactive(button);

  return button;
}

static GtkWidget *
create_widget(GtkWidget *parent, GtkSizeGroup *size_group, const gchar *title,
              GtkWidget *(*create_cb)())
{
  GtkWidget *widget = create_cb();

  if (HILDON_IS_BUTTON(widget))
      hildon_button_set_title(HILDON_BUTTON(widget), title);
  else if (HILDON_IS_CHECK_BUTTON(widget))
      gtk_button_set_label(GTK_BUTTON(widget), title);
  else
  {
    GtkWidget *hbox = gtk_hbox_new(0, 8);
    GtkWidget *label = gtk_label_new(title);

    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(hbox), widget,  TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(parent), hbox, FALSE, FALSE, 0);
    return widget;
  }

  gtk_box_pack_start(GTK_BOX(parent), widget, FALSE, FALSE, 0);

  return widget;
}

static GtkWidget *
create_button(GtkWidget *parent, GtkSizeGroup *size_group, const gchar *title)
{
  GtkWidget *button =
      hildon_button_new_with_text(HILDON_SIZE_FINGER_HEIGHT,
                                  HILDON_BUTTON_ARRANGEMENT_HORIZONTAL, title,
                                  NULL);
  gtk_box_pack_start(GTK_BOX(parent), button, FALSE, FALSE, 0);

  return button;
}

static GtkWidget *
create_check_button()
{
  return hildon_check_button_new(HILDON_SIZE_FINGER_HEIGHT);
}

static GtkWidget *
create_entry()
{
  return hildon_entry_new(HILDON_SIZE_FINGER_HEIGHT);
}

static GtkWidget *
network_select_create()
{
  return create_touch_selector(_("conn_va_phone_network_sel_a"),
                               _("conn_va_phone_network_sel_m"),
                               NULL);
}

static gint
cellular_settings_get_selected_net_mode(cell_settings **settings)
{
  gint active = hildon_picker_button_get_active(
        HILDON_PICKER_BUTTON((*settings)->network_mode));

  if (active == 1)
    return 2;
  else if (active == 2)
    return 1;
  else
    return 0;
}

static void
network_select_sync_selection_mode(cell_settings **settings)
{
  (*settings)->network_select_in_sync = TRUE;
  hildon_picker_button_set_active(
        HILDON_PICKER_BUTTON((*settings)->network_select),
        (*settings)->network_selection_mode_manual);
  (*settings)->network_select_in_sync = FALSE;
}

static void
cellular_net_selection_response(GtkDialog *dialog, gint response_id,
                                gpointer user_data)
{
  cell_settings **settings = user_data;

  if (response_id == GTK_RESPONSE_CANCEL)
  {
    gint error_value = 0;
    guchar mode = connui_cell_net_get_network_selection_mode(&error_value);

    (*settings)->network_selection_mode_manual =
        !(mode == NETWORK_SELECT_MODE_AUTOMATIC);

    cellular_net_selection_hide();
    cellular_net_selection_reset_network();
    network_select_sync_selection_mode(settings);
  }
  else if (response_id == GTK_RESPONSE_OK)
  {
    (*settings)->network_selection_mode_manual = TRUE;
    cellular_net_selection_select_current();
  }
}

static void
cellular_settings_status_item_flightmode_cb(dbus_bool_t offline,
                                            gpointer user_data)
{
  cell_settings **settings = user_data;
  guchar selected_rat;
  GtkDialog *selection_dialog;

  if (offline)
  {
    if (!gateway_common_show_flight_mode(GTK_WINDOW((*settings)->dialog)))
    {
      network_select_sync_selection_mode(settings);
      connui_flightmode_close(cellular_settings_status_item_flightmode_cb);
      return;
    }

    if (!connui_flightmode_off())
      g_warning("Unable to turn flightmode off");
  }

  selected_rat = cellular_settings_get_selected_net_mode(settings);

  if ((*settings)->selected_net_mode != selected_rat)
  {
    gint error_value = 0;

    connui_cell_net_set_radio_access_mode(selected_rat, &error_value);

    if (error_value)
      CONNUI_ERR("Error while setting radio access mode: %d", error_value);
    else
      (*settings)->selected_net_mode = selected_rat;

    (*settings)->rat_changed = 1;
    cellular_net_clear_cache();
  }

  selection_dialog =
      cellular_net_selection_show(GTK_WINDOW((*settings)->dialog),
                                  G_CALLBACK(cellular_net_selection_response),
                                  settings);
  (*settings)->selection_dialog = GTK_WIDGET(selection_dialog);
  connui_flightmode_close(cellular_settings_status_item_flightmode_cb);
}

static void
network_select_value_changed_cb(HildonPickerButton *button, gpointer user_data)
{
  cell_settings **settings = user_data;
  gboolean active;

  if ((*settings)->user_activated || (*settings)->network_select_in_sync)
    return;

  active = hildon_picker_button_get_active(button);

  if ((*settings)->network_selection_mode_manual != active || !active)
  {
    if (active)
    {
      if (!connui_flightmode_status(
            cellular_settings_status_item_flightmode_cb, settings) )
      {
        g_warning("Unable to register flightmode status!");
      }
    }
    else
    {
      cellular_net_selection_select_automatic();
      (*settings)->network_selection_mode_manual = FALSE;
    }
  }
}

static GtkWidget *
network_mode_create()
{
  return create_touch_selector(_("conn_va_phone_network_mode_b"),
                               _("conn_va_phone_network_mode_3g"),
                               _("conn_va_phone_network_mode_2g"),
                                 NULL);
}

static GtkWidget *
network_data_roaming_create()
{
  return create_touch_selector(_("conn_va_phone_network_roam_ask"),
                               _("conn_va_phone_network_roam_allow"),
                               NULL);
}

static void
network_data_counter_clicked_cb(HildonButton *button, gpointer user_data)
{
  cell_settings **settings = user_data;

  gtk_dialog_response(GTK_DIALOG((*settings)->dialog), 11);
}

static void
roaming_network_data_counter_clicked_cb(HildonButton *button,
                                        gpointer user_data)
{
  cell_settings **settings = user_data;

  gtk_dialog_response(GTK_DIALOG((*settings)->dialog), 12);
}

static void
init_network_options(cell_settings **settings, GtkWidget *parent,
                     GtkSizeGroup *size_group)
{
  cell_settings *cs = *settings;
  GtkWidget *button;

  cs->network_select = create_widget(parent, size_group,
                                     _("conn_fi_phone_network_sel"),
                                     network_select_create);
  g_signal_connect(G_OBJECT(cs->network_select), "value-changed",
                   (GCallback)network_select_value_changed_cb, settings);

  cs->network_mode = create_widget(parent, size_group,
                                   _("conn_fi_phone_network_mode"),
                                   network_mode_create);
  cs->network_data_roaming = create_widget(
        parent, size_group, _("conn_fi_phone_network_data_roam"),
        network_data_roaming_create);

  button = create_button(
        parent, size_group, _("conn_bd_phone_network_data_counter"));
  g_signal_connect(G_OBJECT(button), "clicked",
                   (GCallback)network_data_counter_clicked_cb, settings);

  button = create_button(
        parent, size_group, _("conn_bd_phone_roaming_network_data_counter"));

  g_signal_connect(G_OBJECT(button), "clicked",
                   (GCallback)roaming_network_data_counter_clicked_cb,
                   settings);
}

static void
cellular_settings_set_call_forwarding_cb(gboolean enabled,
                                         gint error_value,
                                         const gchar *phone_number,
                                         gpointer user_data)
{
  cell_settings **settings = user_data;

  g_return_if_fail(settings != NULL && *settings != NULL);

  if (error_value )
  {
    if (error_value == 10001)
      (*settings)->set_forwarding_has_error = TRUE;

    CONNUI_ERR("Error while settings supplementary services value: %d",
               error_value);
  }

  (*settings)->svc_call_in_progress--;

  return;
}

static void
cellular_settings_set_call_cb(gboolean enabled, gint error_value,
                              const gchar *phone_number, gpointer user_data)
{
  cell_settings **settings = user_data;

  g_return_if_fail(settings != NULL && *settings != NULL);

  if (error_value)
  {
    (*settings)->set_call_has_error = TRUE;
    CONNUI_ERR("Error while settings supplementary services value: %d",
               error_value);
  }

  (*settings)->svc_call_in_progress--;
}

static void
cellular_settings_export(cell_settings **settings)
{
  gboolean divert_active;
  const gchar *divert_phone = NULL;
  gint radio_access_mode;
  const gchar *cid_presentation_bluez;
  char *cid_presentation;
  GtkTreeIter iter;
  gint roaming_type;
  GConfClient *gconf;
  HildonTouchSelector *selector;
  GtkTreeModel *model;

  divert_active = hildon_picker_button_get_active(
        HILDON_PICKER_BUTTON((*settings)->call_divert)) == 0;

  if (divert_active)
  {
    divert_phone =
        hildon_entry_get_text(HILDON_ENTRY((*settings)->call_divert_to));
  }

  if ((*settings)->selected_divert == divert_active)
  {
    if (divert_phone && (*settings)->divert_phone_number &&
        strcmp(divert_phone, (*settings)->divert_phone_number))
    {
      if (connui_cell_net_set_call_forwarding_enabled(
            divert_active, divert_phone,
            cellular_settings_set_call_forwarding_cb, settings))
      {
        (*settings)->svc_call_in_progress++;
      }

      while ((*settings)->svc_call_in_progress > 0)
        g_main_context_iteration(NULL, TRUE);
    }
  }

  if (!(*settings)->set_forwarding_has_error)
  {
    gboolean call_waiting_enabled = hildon_check_button_get_active(
          HILDON_CHECK_BUTTON((*settings)->call_waiting));

    if (call_waiting_enabled != (*settings)->call_waiting_enabled)
    {
      if (connui_cell_net_set_call_waiting_enabled(
            call_waiting_enabled, cellular_settings_set_call_cb,
            settings))
      {
        (*settings)->svc_call_in_progress++;
      }

      while ((*settings)->svc_call_in_progress > 0)
        g_main_context_iteration(NULL, TRUE);
    }

    radio_access_mode = cellular_settings_get_selected_net_mode(settings);

    if ((*settings)->radio_access_mode != radio_access_mode)
    {
      gint err_val = 0;

      connui_cell_net_set_radio_access_mode(radio_access_mode, &err_val);

      if (err_val)
        CONNUI_ERR("Error while setting radio access mode: %d", err_val);
    }

    roaming_type = hildon_picker_button_get_active(
          HILDON_PICKER_BUTTON((*settings)->network_data_roaming));
    gconf = gconf_client_get_default();

    if (gconf)
    {
      GError *error = NULL;

      gconf_client_set_bool(
            gconf,
            "/system/osso/connectivity/network_type/GPRS/gprs_roaming_disabled",
            roaming_type == 0, &error);

      if (error)
      {
        CONNUI_ERR("Unable to set roaming setting to GConf: %s",
                   error->message);
        g_clear_error(&error);
      }

      gconf_client_set_bool(gconf,
                            "/system/osso/connectivity/ui/gprs_roaming_asked",
                            roaming_type == 1, &error);

      if (error)
      {
        CONNUI_ERR("Unable to set roaming asked setting to GConf: %s",
                   error->message);
        g_clear_error(&error);
      }

      g_object_unref(gconf);
    }
    else
      CONNUI_ERR("Unable to get GConfClient!");

    cid_presentation = "";

    selector = hildon_picker_button_get_selector(
          HILDON_PICKER_BUTTON((*settings)->send_call_id));
    model = hildon_touch_selector_get_model(selector, 0);

    if (hildon_touch_selector_get_selected(selector, 0, &iter))
      gtk_tree_model_get(model, &iter, 1, &cid_presentation, -1);

    if (strcmp(cid_presentation, (*settings)->cid_presentation))
    {
      if (connui_cell_net_set_caller_id_presentation(
            cid_presentation, cellular_settings_set_call_cb, settings))
      {
        (*settings)->svc_call_in_progress++;
      }

      while ((*settings)->svc_call_in_progress > 0);
          g_main_context_iteration(NULL, TRUE);

      if (!strcmp(cid_presentation, "no-id"))
        cid_presentation_bluez = "allowed";
      else if (!strcmp(cid_presentation, "id"))
        cid_presentation_bluez = "restricted";
      else if (*cid_presentation == 0)
          cid_presentation_bluez = "none";
      else
        cid_presentation_bluez = NULL;

      connui_cell_net_set_caller_id_presentation_bluez(cid_presentation_bluez);
    }
  }
}

static void
disable_ui(gboolean disable, cell_settings **settings)
{
  hildon_gtk_window_set_progress_indicator(GTK_WINDOW((*settings)->dialog),
                                           disable ? 1 : 0);
  gtk_widget_set_sensitive(GTK_DIALOG((*settings)->dialog)->vbox, !disable);
  gtk_dialog_set_response_sensitive(GTK_DIALOG((*settings)->dialog),
                                    GTK_RESPONSE_OK, !disable);
}

#if CONTACT_CHOOSER
static void
contact_chooser_dialog_response(GtkDialog *dialog, gint response_id,
                                gpointer user_data)
{
  cell_settings **settings = user_data;
  gchar *phone_number = cellular_abook_get_selected_number();

  cellular_abook_destroy();
  (*settings)->contact_chooser = NULL;

  if (phone_number)
  {
    if (response_id == GTK_RESPONSE_OK)
    {
      hildon_entry_set_text(HILDON_ENTRY((*settings)->call_divert_to),
                            phone_number);
    }

    g_free(phone_number);
  }
}
#endif

static void
datacounter_dialog_response(GtkDialog *dialog, gint response_id,
                            gpointer user_data)
{
  cell_settings **settings = user_data;

  switch(response_id)
  {
    case 2:
      cellular_data_counter_save();
      /* fallthrough */
    case GTK_RESPONSE_CANCEL:
    {
      cellular_data_counter_destroy();
      (*settings)->datacounter_dialog = NULL;
      break;
    }
    case 1:
    {
      cellular_data_counter_reset();
      break;
    }
  }
}

static void
cellular_settings_response(GtkDialog *dialog, gint response_id,
                           gpointer user_data)
{
  cell_settings **settings = user_data;

  g_return_if_fail(settings != NULL && *settings != NULL);

  if ((*settings)->exporting_settings)
    return;

  switch (response_id)
  {
    case GTK_RESPONSE_CANCEL:
    {
      if (hildon_check_button_get_active(
            HILDON_CHECK_BUTTON((*settings)->network_pin_request)) !=
          (*settings)->security_code_enabled )
      {
        connui_cell_code_ui_set_current_code_active(
              (*settings)->security_code_enabled);
      }

      if ((*settings)->rat_changed)
      {
        gint err_val = 0;

        connui_cell_net_set_radio_access_mode((*settings)->radio_access_mode,
                                              &err_val);

        if (err_val)
          CONNUI_ERR("Error while setting radio access mode: %d", err_val);
      }

      break;
    }
    case GTK_RESPONSE_OK:
    {
      const gchar *divert_phone =
          hildon_entry_get_text(HILDON_ENTRY((*settings)->call_divert_to));

      if (hildon_picker_button_get_active(
           HILDON_PICKER_BUTTON((*settings)->call_divert)) == 0 &&
          (!divert_phone || !*divert_phone) )
      {
        hildon_banner_show_information(
              GTK_WIDGET(dialog), NULL,
              dgettext("hildon-common-strings", "ckct_ib_enter_a_value"));
        gtk_widget_grab_focus((*settings)->call_divert_to);
        return;
      }

      (*settings)->set_forwarding_has_error = FALSE;
      (*settings)->exporting_settings = TRUE;
      (*settings)->set_call_has_error = FALSE;
      disable_ui(TRUE, settings);
      cellular_settings_export(settings);
      (*settings)->exporting_settings = FALSE;

      if ((*settings)->set_forwarding_has_error)
      {
        disable_ui(FALSE, settings);

        hildon_banner_show_information(GTK_WIDGET(dialog), NULL,
                                       _("conn_ib_invalid_number"));
        gtk_widget_grab_focus((*settings)->call_divert_to);
        return;
      }

      break;
    }
#if CONTACT_CHOOSER
    case 10:
    {
      (*settings)->contact_chooser =
          GTK_WIDGET(cellular_abook_show(GTK_WINDOW((*settings)->dialog),
                                         (*settings)->osso));
      g_signal_connect(G_OBJECT((*settings)->contact_chooser), "response",
                       G_CALLBACK(contact_chooser_dialog_response), settings);
      return;
    }
#endif
    case 11:
    case 12:
    {
      gboolean home_counter = response_id == 11;
      GtkDialog *datacounter_dialog = cellular_data_counter_show(
            GTK_WINDOW((*settings)->dialog), home_counter);

      (*settings)->datacounter_dialog = GTK_WIDGET(datacounter_dialog);

      g_signal_connect((*settings)->datacounter_dialog, "response",
                       G_CALLBACK(datacounter_dialog_response), settings);
      return;
    }

    default:
      return;
  }

  cellular_settings_destroy(settings);
  gtk_main_quit();
}

static GtkWidget *
send_call_id_create()
{
  HildonTouchSelector *selector;
  HildonTouchSelectorColumn *column;
  GtkWidget *button;
  GtkTreeIter iter;
  GtkListStore *list_store =
      gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);

  gtk_list_store_insert_with_values(list_store, &iter, 0,
                                    0, dgettext("hildon-libs", "wdgt_bd_yes"),
                                    1, "no-id",
                                    -1);
  gtk_list_store_insert_with_values(list_store, &iter, 1,
                                    0, dgettext("hildon-libs", "wdgt_bd_no"),
                                    1, "id",
                                    -1);
  gtk_list_store_insert_with_values(list_store, &iter, 2,
                                    0, _("conn_va_phone_clir_network"),
                                    1, "",
                                    -1);
  selector = HILDON_TOUCH_SELECTOR(hildon_touch_selector_new());

  column = hildon_touch_selector_append_text_column(
        selector, GTK_TREE_MODEL(list_store), TRUE);

  g_object_unref(G_OBJECT(list_store));
  g_object_set(G_OBJECT(column), "text-column", 0, NULL);


  button = hildon_picker_button_new(HILDON_SIZE_FINGER_HEIGHT,
                                    HILDON_BUTTON_ARRANGEMENT_VERTICAL);

  hildon_picker_button_set_selector(HILDON_PICKER_BUTTON(button), selector);
  picker_button_set_inactive(button);

  return button;
}

static GtkWidget *
call_divert_create_widget()
{
  return create_touch_selector(_("conn_fi_phone_call_divert_note"),
                               dgettext("hildon-libs", "wdgt_bd_no"),
                               NULL);
}

static void
call_divert_value_changed_cb(HildonPickerButton *widget,
                             gpointer user_data)
{
  cell_settings **settings = user_data;

  if (!(*settings)->user_activated)
    activate_widgets(settings);
}

static void
call_divert_contact_clicked_cb(GtkButton *button, gpointer user_data)
{
  cell_settings **settings = user_data;

  gtk_dialog_response(GTK_DIALOG((*settings)->dialog), 10);
}

static void
init_call_options(cell_settings **settings, GtkWidget *parent,
                  GtkSizeGroup *size_group)
{
  cell_settings *cs = *settings;

  cs->send_call_id = create_widget(parent, size_group,
                                   _("conn_fi_phone_send_call_id"),
                                   send_call_id_create);

  cs->call_waiting = create_widget(parent, size_group,
                                   _("conn_fi_phone_call_waiting"),
                                   create_check_button);

  cs->call_divert = create_widget(parent, size_group,
                                  _("conn_fi_phone_call_divert"),
                                  call_divert_create_widget);
  g_signal_connect(G_OBJECT(cs->call_divert), "value-changed",
                   (GCallback)call_divert_value_changed_cb, settings);

  cs->call_divert_to = create_widget(parent, size_group,
                                     _("conn_fi_phone_call_divert_to"),
                                     create_entry);
  hildon_gtk_entry_set_input_mode(GTK_ENTRY(cs->call_divert_to),
                                  HILDON_GTK_INPUT_MODE_TELE);

  cs->call_divert_contact = create_button(
        parent, size_group, _("conn_bd_phone_call_divert_contact"));
  g_signal_connect(G_OBJECT(cs->call_divert_contact), "clicked",
                   (GCallback)call_divert_contact_clicked_cb, settings);
}

static void
network_pin_request_toggled_cb(HildonCheckButton *button, gpointer user_data)
{
  cell_settings **settings = user_data;

  if (!(*settings)->user_activated)
  {
    gboolean active = hildon_check_button_get_active(button);

    if (!connui_cell_code_ui_set_current_code_active(active))
    {
      (*settings)->user_activated = TRUE;
      hildon_check_button_set_active(button, active == 0);
      (*settings)->user_activated = FALSE;
    }

    call_divert_value_changed_cb((HildonPickerButton *)button, settings);

    if ((*settings)->cs_status_inactive)
      cellular_settings_quit(settings);
  }
}

static void
network_pin_changed_cb(cell_settings **settings)
{
  if (!(*settings)->user_activated)
  {
    connui_cell_code_ui_change_code(SIM_SECURITY_CODE_PIN);

    (*settings)->user_activated = TRUE;
    hildon_entry_set_text(HILDON_ENTRY((*settings)->network_pin), "1234");
    (*settings)->user_activated = FALSE;

    if ((*settings)->cs_status_inactive)
      cellular_settings_quit(settings);
  }
}

static void
init_sim_options(cell_settings **settings, GtkWidget *parent,
                 GtkSizeGroup *size_group)
{
  (*settings)->network_pin_request = create_widget(
        parent, size_group, _("conn_fi_phone_network_pin_request"),
        create_check_button);
  g_signal_connect(G_OBJECT((*settings)->network_pin_request), "toggled",
                   G_CALLBACK(network_pin_request_toggled_cb), settings);

  (*settings)->network_pin = create_widget(parent, size_group,
                                           _("conn_fi_phone_network_pin"),
                                           create_entry);
  gtk_entry_set_visibility(GTK_ENTRY((*settings)->network_pin), FALSE);
  g_signal_connect_swapped(G_OBJECT((*settings)->network_pin), "changed",
                           G_CALLBACK(network_pin_changed_cb), settings);
  g_signal_connect_swapped(G_OBJECT((*settings)->network_pin),
                           "button-press-event",
                           G_CALLBACK(network_pin_changed_cb), settings);
}

static osso_return_t
cellular_settings_show(cell_settings **settings, GtkWindow *parent)
{
  struct
  {
    const char *text;
    void (*init)(cell_settings **, GtkWidget *, GtkSizeGroup *);
  }
  options[] =
  {
    {_("conn_ti_phone_call"), init_call_options},
    {_("conn_ti_phone_network"), init_network_options},
    {_("conn_ti_phone_sim"), init_sim_options}
  };
  GtkSizeGroup *size_group;
  GtkWidget *vbox;
  int i;

  g_return_val_if_fail(settings != NULL && *settings != NULL, OSSO_ERROR);

  if ((*settings)->dialog)
  {
    gtk_widget_show_all((*settings)->dialog);

    if ((*settings)->selection_dialog)
      gtk_widget_show_all((*settings)->selection_dialog);

    return OSSO_OK;
  }

  (*settings)->dialog = gtk_dialog_new_with_buttons(
        _("conn_ti_phone_cpa"), parent,
        GTK_DIALOG_NO_SEPARATOR|GTK_DIALOG_DESTROY_WITH_PARENT|GTK_DIALOG_MODAL,
        dgettext("hildon-libs", "wdgt_bd_save"), GTK_RESPONSE_OK,
        NULL);

  (*settings)->pannable_area = hildon_pannable_area_new();

  hildon_pannable_area_set_center_on_child_focus(
        HILDON_PANNABLE_AREA((*settings)->pannable_area), TRUE);
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(((*settings)->dialog))->vbox),
                    (*settings)->pannable_area);
  gtk_widget_set_size_request((*settings)->pannable_area, 800, 350);
  size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

  vbox = gtk_vbox_new(0, 8);

  for(i = 0; i < G_N_ELEMENTS(options); i++)
  {
    if (options[i].text)
    {
      GtkWidget *label = gtk_label_new(options[i].text);

      if (i)
      {
        GtkWidget *empty_label = gtk_label_new("");

        gtk_widget_set_size_request(empty_label, -1, 35);
        gtk_box_pack_start(GTK_BOX(vbox), empty_label, FALSE, FALSE, 0);
      }

      gtk_misc_set_alignment(GTK_MISC(label), 0.5, 0.5);
      hildon_helper_set_logical_font(label, "LargeSystemFont");

      gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    }

    options[i].init(settings, vbox, size_group);
  }

  g_object_unref(size_group);

  hildon_pannable_area_add_with_viewport(
        HILDON_PANNABLE_AREA((*settings)->pannable_area), vbox);
  iap_common_set_close_response((*settings)->dialog, GTK_RESPONSE_CANCEL);

  g_signal_connect(G_OBJECT((*settings)->dialog), "response",
                   (GCallback)cellular_settings_response, settings);

  if (!connui_cell_code_ui_init(GTK_WINDOW((*settings)->dialog), 0))
  {
    cellular_settings_destroy(settings);
    return OSSO_ERROR;
  }

  connui_cell_call_status_register(cellular_settings_call_status_cb, settings);
  connui_cell_cs_status_register(cellular_settings_cs_status_cb, settings);
  gtk_widget_show_all((*settings)->dialog);

  if ((*settings)->call_divert_to)
    gtk_widget_hide_all((*settings)->call_divert_to);

  gtk_widget_hide_all((*settings)->call_divert_contact);

  return OSSO_OK;
}

static gboolean
cellular_settings_restore_state(gpointer user_data)
{
  cell_settings **settings = user_data;

  (*settings)->idle_id = 0;

  g_return_val_if_fail(settings != NULL && *settings != NULL, FALSE);

  return FALSE;
}

static void
activate_all_widgets(cell_settings **settings, gboolean sensitive)
{
  GtkWidget *widgets[] =
  {
    (*settings)->send_call_id,
    (*settings)->call_waiting,
    (*settings)->call_divert,
    (*settings)->call_divert_to,
    (*settings)->call_divert_contact,
    NULL
  };
  GtkWidget **w = widgets;

  while (*w)
  {
    gtk_widget_set_sensitive(*w, sensitive);
    w++;
  }

  gtk_dialog_set_response_sensitive(GTK_DIALOG((*settings)->dialog),
                                    GTK_RESPONSE_OK, sensitive);
  gtk_widget_set_sensitive((*settings)->network_select, sensitive);
  gtk_widget_set_sensitive((*settings)->network_pin_request, sensitive);
  gtk_widget_set_sensitive((*settings)->network_pin, sensitive);
}

static void
activate_widgets(cell_settings **settings)
{
  if ((*settings)->user_activated)
  {
    activate_all_widgets(settings, FALSE);
    gtk_widget_queue_draw((*settings)->dialog);
  }
  else
  {
    gint call_divert_active = hildon_picker_button_get_active(
          HILDON_PICKER_BUTTON((*settings)->call_divert));
    gboolean sensitive = call_divert_active == 0;

    activate_all_widgets(settings, TRUE);

    gtk_widget_set_sensitive((*settings)->call_divert_to, sensitive);
    gtk_widget_set_sensitive((*settings)->call_divert_contact, sensitive);

    if ( sensitive )
    {
      GtkWidget *parent = gtk_widget_get_parent((*settings)->call_divert_to);

      if (parent)
        gtk_widget_show_all(parent);

      gtk_widget_show_all((*settings)->call_divert_contact);
    }
    else
    {
      GtkWidget *parent = gtk_widget_get_parent((*settings)->call_divert_to);

      if (parent)
        gtk_widget_hide_all(parent);

      gtk_widget_hide_all((*settings)->call_divert_contact);
    }
  }
}

static gboolean
caller_id_select_cb(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
                    gpointer user_data)
{
  struct caller_id_select_cb_data *data = user_data;
  gboolean rv = FALSE;
  gchar *caller_id = NULL;

  gtk_tree_model_get(model, iter, 1, &caller_id, -1);

  if (caller_id && data->caller_id && !strcmp(caller_id, data->caller_id))
  {
    hildon_touch_selector_select_iter(
          hildon_picker_button_get_selector(HILDON_PICKER_BUTTON(data->button)),
          0, iter, FALSE);
    rv = TRUE;
  }

  g_free(caller_id);

  return rv;
}

static void
cellular_settings_stop_progress_indicator(cell_settings **settings)
{
  (*settings)->svc_call_in_progress--;

  if (!(*settings)->svc_call_in_progress)
  {
    (*settings)->user_activated = FALSE;
    (*settings)->exporting_settings = FALSE;
    hildon_gtk_window_set_progress_indicator(
          GTK_WINDOW((*settings)->dialog), FALSE);
    activate_widgets(settings);
  }
}

static void
cellular_settings_get_call_forwarding_cb(gboolean enabled, int error_value,
                                         const gchar *phone_number,
                                         gpointer user_data)
{
  cell_settings **settings = user_data;
  gint active_index;

  g_return_if_fail(settings != NULL && *settings != NULL);

  if (error_value)
    CONNUI_ERR("Error in while fetching call forwarding: %d", error_value);

  if (enabled)
  {
    (*settings)->call_forwarding_id = 0;
    cellular_settings_stop_progress_indicator(settings);
    active_index = 0;
    (*settings)->selected_divert = enabled;
  }
  else
  {
    if ((*settings)->call_forwarding_type <= 3) /* no answer */
    {
      (*settings)->call_forwarding_type++;

      (*settings)->call_forwarding_id =
          connui_cell_net_get_call_forwarding_enabled(
            (*settings)->call_forwarding_type,
            cellular_settings_get_call_forwarding_cb, settings);

      if ((*settings)->call_forwarding_id)
        return;
    }

    (*settings)->call_forwarding_id = 0;
    cellular_settings_stop_progress_indicator(settings);
    active_index = 1;
    (*settings)->selected_divert = 0;
  }

  hildon_picker_button_set_active(
        HILDON_PICKER_BUTTON((*settings)->call_divert), active_index);

  if (phone_number)
  {
    (*settings)->divert_phone_number = g_strdup(phone_number);
    hildon_entry_set_text(HILDON_ENTRY((*settings)->call_divert_to),
                          phone_number);
  }
  else
    (*settings)->divert_phone_number = NULL;
}

static void
cellular_settings_get_call_waiting_cb(gboolean enabled, gint error_value,
                                      const gchar *phone_number,
                                      gpointer user_data)
{
  cell_settings **settings = user_data;

  g_return_if_fail(settings != NULL && *settings != NULL);

  (*settings)->call_forwarding_type = 2; /* busy */

  (*settings)->call_forwarding_id = connui_cell_net_get_call_forwarding_enabled(
        2, cellular_settings_get_call_forwarding_cb, settings);

  if ((*settings)->call_forwarding_id)
    (*settings)->svc_call_in_progress++;

  (*settings)->call_wating_id = 0;

  cellular_settings_stop_progress_indicator(settings);

  if (error_value)
    CONNUI_ERR("Error while fetching call waiting: %d", error_value);

  (*settings)->call_waiting_enabled = enabled;
  hildon_check_button_set_active(HILDON_CHECK_BUTTON((*settings)->call_waiting),
                                 enabled);
}

static void
cellular_settings_get_caller_id_cb(gboolean unk, gint error_value,
                                   const gchar *caller_id, gpointer user_data)
{
  cell_settings **settings = user_data;
  HildonTouchSelector *selector;
  struct caller_id_select_cb_data data;

  g_return_if_fail(settings != NULL && *settings != NULL);

  (*settings)->call_wating_id = connui_cell_net_get_call_waiting_enabled(
        cellular_settings_get_call_waiting_cb, user_data);

  if ((*settings)->call_wating_id)
    (*settings)->svc_call_in_progress++;

  (*settings)->caller_id = 0;

  if (error_value)
    CONNUI_ERR("Error while fetching caller ID: %d", error_value);

  data.button = (*settings)->send_call_id;
  data.caller_id = caller_id;

  (*settings)->cid_presentation = g_strdup(caller_id);

  selector = hildon_picker_button_get_selector(
        HILDON_PICKER_BUTTON((*settings)->send_call_id));
  gtk_tree_model_foreach(hildon_touch_selector_get_model(selector, 0),
                         caller_id_select_cb, &data);
}

static gboolean
cellular_settings_import(gpointer user_data)
{
  cell_settings **settings = user_data;
  gchar net_select_mode;
  guchar radio_access_mode;
  gint active_rat;
  GConfClient *gconf;
  gboolean security_code_enabled;
  gint error_value = 0;

  (*settings)->idle_id = 0;

  hildon_gtk_window_set_progress_indicator(
        GTK_WINDOW((*settings)->dialog), TRUE);

  net_select_mode = connui_cell_net_get_network_selection_mode(&error_value);

  if (error_value)
    CONNUI_ERR("Error while fetching network selection mode: %d", error_value);

  (*settings)->network_selection_mode_manual =
      net_select_mode == NETWORK_SELECT_MODE_MANUAL;

  hildon_picker_button_set_active(
        HILDON_PICKER_BUTTON((*settings)->network_select),
        (*settings)->network_selection_mode_manual);

  radio_access_mode = connui_cell_net_get_radio_access_mode(&error_value);
  (*settings)->radio_access_mode = radio_access_mode;
  (*settings)->selected_net_mode = radio_access_mode;

  if (error_value)
    CONNUI_ERR("Error while fetching radio access mode: %d", error_value);

  if (radio_access_mode == NET_GSS_GSM_SELECTED_RAT)
    active_rat = 2;
  else if(radio_access_mode == NET_GSS_UMTS_SELECTED_RAT)
    active_rat = 1;
  else
    active_rat = 0;

  hildon_picker_button_set_active(
        HILDON_PICKER_BUTTON((*settings)->network_mode),
        active_rat);

  gconf = gconf_client_get_default();
  if ( gconf )
  {
    GError *err = NULL;
    GConfValue *val =
        gconf_client_get(gconf, ICD_GCONF_SETTINGS "/ui/gprs_roaming_asked",
                         &err);

    if (err)
    {
      CONNUI_ERR("Unable to get roaming asked setting from GConf: %s",
                 err->message);
      g_clear_error(&err);
    }

    hildon_picker_button_set_active(
          HILDON_PICKER_BUTTON((*settings)->network_data_roaming), 0);

    if (val)
    {
      if (val->type == GCONF_VALUE_BOOL && gconf_value_get_bool(val))
      {
        gconf_value_free(val);
        val = gconf_client_get(
              gconf, ICD_GCONF_NETWORK_MAPPING "/GPRS/gprs_roaming_disabled",
              &err);

        if (err)
        {
          CONNUI_ERR("Unable to get roaming setting from GConf: %s",
                     err->message);
          g_clear_error(&err);
        }

        if (val)
        {
          if (val->type == GCONF_VALUE_BOOL && !gconf_value_get_bool(val))
          {
            hildon_picker_button_set_active(
                  HILDON_PICKER_BUTTON((*settings)->network_data_roaming), 1);
          }
        }
      }

      if (val)
        gconf_value_free(val);
    }

    g_object_unref(G_OBJECT(gconf));
  }
  else
    CONNUI_ERR("Unable to get GConf client");

  security_code_enabled = connui_cell_security_code_get_enabled(&error_value);

  (*settings)->security_code_enabled = security_code_enabled;

  if (error_value != 0)
    CONNUI_ERR("Error while fetching security code enabled: %d", error_value);

  hildon_check_button_set_active(
        HILDON_CHECK_BUTTON((*settings)->network_pin_request),
        (*settings)->security_code_enabled);
  hildon_entry_set_text(HILDON_ENTRY((*settings)->network_pin), "1234");

  (*settings)->caller_id =
      connui_cell_net_get_caller_id_presentation(
        cellular_settings_get_caller_id_cb, settings);

  if (!(*settings)->caller_id)
    CONNUI_ERR("Unable to get caller id");

  return FALSE;
}

osso_return_t
execute(osso_context_t *osso, gpointer data, gboolean user_activated)
{
  cell_settings **settings = get_settings();
  osso_return_t rv = OSSO_OK;

  (*settings)->osso =
      connui_utils_inherit_osso_context(osso, PACKAGE_NAME, PACKAGE_VERSION);

  if((rv = cellular_settings_show(settings, data)) != OSSO_OK)
    return rv;

  if (user_activated)
  {
    (*settings)->user_activated = TRUE;
    activate_widgets(settings);

    (*settings)->idle_id = g_idle_add(cellular_settings_import, settings);
  }
  else
  {
    (*settings)->idle_id =
        g_idle_add(cellular_settings_restore_state, settings);
  }

  gtk_main();

  return rv;
}
