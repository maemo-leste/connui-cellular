#include <hildon/hildon.h>
#include <connui/connui.h>
#include <connui/connui-conndlgs.h>
#include <connui/connui-dbus.h>
#include <connui/connui-log.h>
#include <icd/osso-ic-gconf.h>
#include <icd/dbus_api.h>

#include <stdlib.h>

#include <libintl.h>

#include "connui-cellular.h"

#include "config.h"

#define _(msgid) dgettext(GETTEXT_PACKAGE, msgid)

#define CELLULAR_UI_DBUS_INTERFACE "com.nokia.cellular_ui"
#define CELLULAR_UI_DBUS_PATH "/com/nokia/cellular_ui"
#define CELLULAR_UI_SHOW_ERROR_NOTE "show_error_note"

#undef ICD_UI_DBUS_INTERFACE
#undef ICD_UI_DBUS_PATH

#define ICD_UI_DBUS_INTERFACE CELLULAR_UI_DBUS_INTERFACE
#define ICD_UI_DBUS_PATH CELLULAR_UI_DBUS_PATH

#define ICD_UI_GCONF_SETTINGS ICD_GCONF_SETTINGS "/ui/"

static void
cell_dialog_error_note_modem_state(const char *modem_id,
                                   const connui_modem_status *status,
                                   gpointer user_data);

static void
cell_dialog_error_note_net_status(const char *modem_id,
                                  const cell_network_state *status,
                                  gpointer user_data);

static void
check_pending_acks();

static GtkWidget *_dialog;
static GtkWidget *_label;
static GtkWidget *_sent_label;
static GtkWidget *_received_label;

static guchar _prev_net_reg_status;
static iap_dialogs_done_fn done_func;
static int done_func_dialog_id;
static GConfClient *_gconf;
static guint _gconf_notify;
static gboolean _is_home;

static network_entry *_network;

static gboolean home_pending_ack = FALSE;
static gboolean roaming_pending_ack = FALSE;

IAP_DIALOGS_PLUGIN_DEFINE_EXTENDED(error_note, CELLULAR_UI_SHOW_ERROR_NOTE,
{
  _prev_net_reg_status = -1;

  check_pending_acks();

  if (!connui_cell_net_status_register(cell_dialog_error_note_net_status, NULL))
    CONNUI_ERR("Unable to register cellular net status listener!");

  if (!connui_cell_modem_status_register(cell_dialog_error_note_modem_state,
                                         NULL))
  {
    CONNUI_ERR("Unable to register modem state callback");
  }
});

static const struct
{
  const char *msgid;
  const char *fmt;
}
datacounter_msgid[] =
{
  {"conn_fi_received_sent_byte", "%s B" },
  {"conn_fi_received_sent_kilobyte", "%s kB" },
  {"conn_fi_received_sent_megabyte", "%s MB" },
  {"conn_fi_received_sent_gigabyte", "%s GB" },
  {NULL, NULL}
};

static void
check_pending_acks()
{
  GConfClient *gconf = gconf_client_get_default();

  if (gconf)
  {
    if (!gconf_client_get_bool(
          gconf, ICD_UI_GCONF_SETTINGS "gprs_data_warning_home_acknowledged",
          NULL))
    {
      home_pending_ack = TRUE;
    }

    if (!gconf_client_get_bool(
          gconf, ICD_UI_GCONF_SETTINGS "gprs_data_warning_roaming_acknowledged",
          NULL))
    {
      roaming_pending_ack = TRUE;
    }

    g_object_unref(G_OBJECT(gconf));
  }
}

static gboolean
ack_pending()
{
  return home_pending_ack | roaming_pending_ack;
}

static gboolean
show_error_note(const gchar *msg)
{
  DBusMessage *mcall;

  mcall = connui_dbus_create_method_call("com.nokia.cellular_ui",
                                         "/com/nokia/cellular_ui",
                                         "com.nokia.cellular_ui",
                                         "show_error_note",
                                         DBUS_TYPE_INVALID);
  if (mcall)
  {
    if (dbus_message_append_args(mcall, DBUS_TYPE_STRING, &msg, NULL))
      connui_dbus_send_system_mcall(mcall, -1, NULL, NULL, NULL);
    else
      CONNUI_ERR("Could not append args to show error note method call");

    dbus_message_unref(mcall);
  }
  else
  {
    CONNUI_ERR("could not create show error note method call");
  }

  return FALSE;
}

/* there is absolutely the same code in data-counter.c, FIXME someday */
static gchar *
format_data_counter(float val, int pow)
{
  gchar *s, *rv;

  if (pow < 0)
    return NULL;
  else if (!pow)
    s = g_strdup_printf("%3g", val);
  else if (val < 10.0)
    s = g_strdup_printf("%1.2f", val);
  else if (val < 100.0)
    s = g_strdup_printf("%2.1f", val);
  else if (val >= 1000.0)
    s = g_strdup_printf("%.0f", val);
  else
    s = g_strdup_printf("%3.f", val);

  rv = g_strdup_printf(_(datacounter_msgid[pow].msgid), s);
  g_free(s);

  return rv;
}

static void
cell_dialog_error_note_modem_state(const char *modem_id,
                                   const connui_modem_status *status,
                                   gpointer user_data)
{
  if (*status == CONNUI_MODEM_STATUS_REMOVED)
  {
    const char *description =
        connui_cell_code_ui_error_note_type_to_text(modem_id, "modem_poweroff");
    GtkWidget *note = hildon_note_new_information(NULL, description);

    gtk_dialog_run(GTK_DIALOG(note));
    gtk_widget_destroy(note);
  }
}

static void
cell_dialog_error_note_close(gboolean process_pending)
{
  if (_dialog)
  {
    gtk_widget_destroy(_dialog);
    _dialog = NULL;
  }

  if (done_func)
  {
    done_func(done_func_dialog_id, process_pending);
    done_func_dialog_id = 0;
    done_func = NULL;
  }

  if (_gconf)
  {
    gconf_client_remove_dir(_gconf, ICD_GCONF_NETWORK_MAPPING_GPRS, NULL);
    gconf_client_notify_remove(_gconf, _gconf_notify);
    g_object_unref(G_OBJECT(_gconf));
    _gconf = NULL;
  }
}

static void
cell_dialog_error_note_close_no_process_pending()
{
  cell_dialog_error_note_close(FALSE);
}

static void
cell_dialog_error_note_net_status(const char *modem_id,
                                  const cell_network_state *status,
                                  gpointer user_data)
{
  gint reg_status;
  int net_selection_mode;
  const char *description = NULL;

  g_return_if_fail(status != NULL);

  reg_status = status->reg_status;

  if (roaming_pending_ack && reg_status == CONNUI_NET_REG_STATUS_ROAMING)
  {
    roaming_pending_ack = FALSE;
    g_idle_add((GSourceFunc)show_error_note, "roaming_notification");
  }
  else if (home_pending_ack && reg_status == CONNUI_NET_REG_STATUS_HOME)
  {
    home_pending_ack = FALSE;
    g_idle_add((GSourceFunc)show_error_note, "home_notification");
  }

  if (reg_status == CONNUI_NET_REG_STATUS_DENIED)
  {
    description = connui_cell_code_ui_error_note_type_to_text(modem_id,
                                                              "sim_reg_fail");
  }
  else
  {
    if (reg_status != CONNUI_NET_REG_STATUS_UNKNOWN)
    {
      if (_prev_net_reg_status == CONNUI_NET_REG_STATUS_UNKNOWN && _dialog)
        cell_dialog_error_note_close(FALSE);
      else
        goto out;
    }
    else
    {
      net_selection_mode =
          connui_cell_net_get_network_selection_mode(modem_id, NULL);

      if (net_selection_mode == CONNUI_NET_SELECT_MODE_UNKNOWN ||
          net_selection_mode == CONNUI_NET_SELECT_MODE_MANUAL)
      {
        description =
            connui_cell_code_ui_error_note_type_to_text(modem_id,
                                                        "sim_select_network");
      }
    }
  }

  if (description)
  {
    done_func_dialog_id = iap_dialog_request_dialog(60, &done_func, NULL);

    if (done_func_dialog_id != -1)
    {
      _dialog = hildon_note_new_information(NULL, description);
      g_signal_connect(
            G_OBJECT(_dialog), "response",
            G_CALLBACK(cell_dialog_error_note_close_no_process_pending), NULL);
      gtk_widget_show_all(_dialog);
    }
  }

  if (!description || (description && done_func_dialog_id == -1))
    CONNUI_ERR("Unable to show SIM registration failed dialog");

  if (reg_status == CONNUI_NET_REG_STATUS_DENIED)
    connui_cell_net_status_close(cell_dialog_error_note_net_status);

out:
  _prev_net_reg_status = reg_status;
}

static gboolean
iap_dialog_error_note_cancel(DBusMessage *message)
{
  if (_dialog)
    gtk_dialog_response(GTK_DIALOG(_dialog), GTK_RESPONSE_CANCEL);

  return TRUE;
}

G_MODULE_EXPORT gboolean
iap_dialogs_plugin_cancel(DBusMessage *message)
{
  return iap_dialog_error_note_cancel(message);
}

static void
cell_dialog_error_set_auto_connect(char *type)
{
  GSList *l = g_slist_append(l, type);
  GError *error = NULL;

  if (!gconf_client_set_list(
        _gconf,
        "/system/osso/connectivity/network_type/auto_connect",
        GCONF_VALUE_STRING, l, &error))
  {
    CONNUI_ERR("Unable to set auto connect value: %s", error->message);
    g_clear_error(&error);
  }

  g_slist_free(l);
}

static void
cell_dialog_error_note_psd_automatic_response(GtkDialog *dialog,
                                              gint response_id,
                                              gpointer user_data)
{
  GError *error = NULL;

  if (!gconf_client_set_bool(
        _gconf, "/system/osso/connectivity/ui/gprs_auto_connect_asked",
        TRUE, &error) )
  {
    CONNUI_ERR("Unable to set auto connect asked value: %s", error->message);
    g_clear_error(&error);
  }

  if (response_id == GTK_RESPONSE_OK)
    cell_dialog_error_set_auto_connect("*");

  cell_dialog_error_note_close(FALSE);
}

static void
cell_dialog_error_note_inetstate_cb(enum inetstate_status status,
                                    network_entry *network, gpointer user_data)
{
  GtkWidget *widget = GTK_WIDGET(user_data);

  if (status == INETSTATE_STATUS_CONNECTED && network &&
      !g_strcmp0(network->network_type, "GPRS"))
  {
    _network = iap_network_entry_dup(network);
    gtk_widget_set_sensitive(widget, TRUE);
  }
  else
  {
    iap_network_entry_clear(_network);
    g_free(_network);
    _network = NULL;
    gtk_widget_set_sensitive(widget, FALSE);
  }
}

static GtkWidget*
iap_dialog_error_note_psd_auto_dialog(const char *err_text)
{
  GtkWidget *button_no;
  GtkWidget *label;
  gchar *markup;
  GtkWidget *dialog =
      gtk_dialog_new_with_buttons(
        _("conn_ti_use_device_psd_auto"), NULL,
        GTK_DIALOG_NO_SEPARATOR | GTK_DIALOG_DESTROY_WITH_PARENT |
        GTK_DIALOG_MODAL,
        NULL);

  gtk_dialog_add_button(GTK_DIALOG(_dialog),
                        dgettext("hildon-libs", "wdgt_bd_yes"),
                        GTK_RESPONSE_OK);

  button_no = gtk_dialog_add_button(GTK_DIALOG(_dialog),
                                    dgettext("hildon-libs", "wdgt_bd_no"),
                                    GTK_RESPONSE_CANCEL);
  gtk_widget_show(button_no);
  gtk_widget_set_no_show_all(button_no, FALSE);

  label = gtk_label_new(NULL);
  markup = g_strdup_printf("<small>%s</small>", err_text);
  gtk_label_set_markup(GTK_LABEL(label), markup);
  gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
  g_free(markup);

  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(_dialog)->vbox), label);
  gtk_widget_set_size_request(GTK_WIDGET(label), 580, 310);

  g_signal_connect(G_OBJECT(_dialog), "response",
                   G_CALLBACK(cell_dialog_error_note_psd_automatic_response),
                   NULL);

  return dialog;
}

static void
cell_dialog_error_set_warning_acknowledged(gboolean acknowledged)
{
  if (_is_home)
  {
    gconf_client_set_bool(
          _gconf,
          "/system/osso/connectivity/ui/gprs_data_warning_home_acknowledged",
          acknowledged, NULL);
  }
  else
  {
    gconf_client_set_bool(
          _gconf,
          "/system/osso/connectivity/ui/gprs_data_warning_roaming_acknowledged",
          acknowledged, NULL);
  }
}

static GSList *
cell_dialog_error_get_auto_connect_values()
{
  GSList *vals;
  GError *error = NULL;

  vals = gconf_client_get_list(
        _gconf, "/system/osso/connectivity/network_type/auto_connect",
        GCONF_VALUE_STRING, &error);

  if (error)
  {
    CONNUI_ERR("Unable to fetch auto connect value: %s", error->message);
    g_clear_error(&error);
    vals = NULL;
  }

  return vals;
}

static void
iap_dialog_error_note_limit_dialog_response_cb(GtkDialog *dialog,
                                               gint response_id,
                                               gpointer user_data)
{
  connui_inetstate_close(cell_dialog_error_note_inetstate_cb);
  iap_network_entry_clear(_network);
  g_free(_network);
  _network = NULL;
  cell_dialog_error_set_warning_acknowledged(TRUE);
  cell_dialog_error_note_close(TRUE);
}

static void
disconnect_button_clicked_cb(GtkButton *button, gpointer user_data)
{
  GSList *auto_connect_values;
  GSList *l;

  iap_network_entry_disconnect(ICD_CONNECTION_FLAG_UI_EVENT, _network);
  auto_connect_values = cell_dialog_error_get_auto_connect_values();

  for (l = auto_connect_values; l; l = l->next)
  {
    gchar *s = l->data;

    if (s)
    {
      if (!g_strcmp0(s, "GPRS") )
        cell_dialog_error_set_auto_connect("");
      else if (!g_strcmp0(s, "*"))
        cell_dialog_error_set_auto_connect("WLAN_INFRA");

      g_free(s);
    }
  }

  g_slist_free(auto_connect_values);
  cell_dialog_error_set_warning_acknowledged(TRUE);
}

static void
iap_dialog_error_note_counters(const char *modem_id)
{
  const char *note_text;
  float rx_bytes = 0.0f;
  float tx_bytes = 0.0f;
  float ttl_bytes;
  int i = 0;
  gchar *ttl_fmt;
  gchar *ttl_text;
  gchar *tx_fmt;
  gchar *tx_text;
  gchar *rx_fmt;
  gchar *rx_text;
  gchar *rx;
  gchar *tx;

  if (_is_home)
  {
    note_text = connui_cell_code_ui_error_note_type_to_text(
          modem_id, "home_notification");
    rx = gconf_client_get_string(_gconf, GPRS_HOME_RX_BYTES, NULL);
    tx = gconf_client_get_string(_gconf, GPRS_HOME_TX_BYTES, NULL);
  }
  else
  {
    note_text = connui_cell_code_ui_error_note_type_to_text(
          modem_id, "roaming_notification");
    rx = gconf_client_get_string(_gconf, GPRS_ROAM_RX_BYTES, NULL);
    tx = gconf_client_get_string(_gconf, GPRS_ROAM_TX_BYTES, NULL);
  }

  if (rx)
    rx_bytes = strtoull(rx, NULL, 10);

  if (tx)
    tx_bytes = strtoull(tx, NULL, 10);

  g_free(rx);
  g_free(tx);

  ttl_bytes = rx_bytes + tx_bytes;

  while ((rx_bytes >= 1000.0 || tx_bytes >= 1000.0 || ttl_bytes >= 1000.0) &&
         datacounter_msgid[i + 1].msgid)
  {
    ttl_bytes /= 1000.0;
    rx_bytes /= 1000.0;
    tx_bytes /= 1000.0;
    i++;
  }

  ttl_fmt = format_data_counter(ttl_bytes, i);
  ttl_text = g_strconcat(note_text, " ", ttl_fmt, NULL);
  gtk_label_set_text(GTK_LABEL(_label), ttl_text);
  g_free(ttl_fmt);
  g_free(ttl_text);

  tx_fmt = format_data_counter(tx_bytes, i);
  tx_text = g_strconcat(_("conn_fi_phone_dc_sent"), " ", tx_fmt, NULL);
  gtk_label_set_text(GTK_LABEL(_sent_label), tx_text);
  g_free(tx_fmt);
  g_free(tx_text);

  rx_fmt = format_data_counter(rx_bytes, i);
  rx_text = g_strconcat(_("conn_fi_phone_dc_received"), " ", rx_fmt, NULL);
  gtk_label_set_text(GTK_LABEL(_received_label), rx_text);
  g_free(rx_fmt);
  g_free(rx_text);
}

static void
cell_dialog_error_note_disable_warning_cb(GtkButton *button, gpointer user_data)
{
  GError *error = NULL;

  if (_is_home)
  {
    if (!gconf_client_set_bool(_gconf, GPRS_HOME_NTFY_ENABLE, FALSE, &error))
    {
      CONNUI_ERR("Unable to set auto connect asked value: %s", error->message);
      g_clear_error(&error);
    }

    if (!gconf_client_set_string(_gconf, GPRS_HOME_NTFY_PERIOD, "0", &error))
    {
      CONNUI_ERR("Unable to set auto connect asked value: %s", error->message);
      g_clear_error(&error);
    }
  }
  else
  {
    if (!gconf_client_set_bool(_gconf, GPRS_ROAM_NTFY_ENABLE, FALSE, &error))
    {
      CONNUI_ERR("Unable to set auto connect asked value: %s", error->message);
      g_clear_error(&error);
    }

    if (!gconf_client_set_string(_gconf, GPRS_ROAM_NTFY_PERIOD, "0", &error))
    {
      CONNUI_ERR("Unable to set auto connect asked value: %s", error->message);
      g_clear_error(&error);
    }
  }

  connui_inetstate_close(cell_dialog_error_note_inetstate_cb);
  iap_network_entry_clear(_network);
  g_free(_network);
  _network = NULL;
  cell_dialog_error_set_warning_acknowledged(TRUE);
  cell_dialog_error_note_close(TRUE);
}

static GtkWidget*
iap_dialog_error_note_limit_dialog(const char *modem_id, const char *note_type)
{
  GtkWidget *button_disable_warning;
  GtkWidget *button_disconnect;
  GtkWidget *dialog =
      gtk_dialog_new_with_buttons(
        _("conn_ti_phone_limit_dialog"), NULL,
        GTK_DIALOG_NO_SEPARATOR | GTK_DIALOG_DESTROY_WITH_PARENT |
        GTK_DIALOG_MODAL,
        NULL);

  _label = gtk_label_new("");
  _sent_label = gtk_label_new("");
  _is_home = FALSE;
  _received_label = gtk_label_new("");

  if (!g_strcmp0(note_type, "home_notification"))
    _is_home = TRUE;

  iap_dialog_error_note_counters(modem_id);

  button_disable_warning = hildon_button_new_with_text(
        HILDON_SIZE_FINGER_HEIGHT,HILDON_BUTTON_ARRANGEMENT_VERTICAL,
        _("conn_bd_phone_limit_disable_warning"), NULL);

  g_signal_connect(G_OBJECT(button_disable_warning), "clicked",
                   G_CALLBACK(cell_dialog_error_note_disable_warning_cb),
                   NULL);

  button_disconnect = hildon_button_new_with_text(
        HILDON_SIZE_FINGER_HEIGHT, HILDON_BUTTON_ARRANGEMENT_VERTICAL,
        _("conn_bd_phone_limit_disconnect"), NULL);
  g_signal_connect(G_OBJECT(button_disconnect), "clicked",
                   G_CALLBACK(disconnect_button_clicked_cb), NULL);

  if (!connui_inetstate_status(cell_dialog_error_note_inetstate_cb,
                               button_disconnect))
  {
    CONNUI_ERR("Unable to query inetstate");
  }

  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),
                     _label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),
                     _sent_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),
                     _received_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),
                     button_disable_warning, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),
                     button_disconnect, FALSE, FALSE, 0);
  g_signal_connect(G_OBJECT(dialog), "response",
                   G_CALLBACK(iap_dialog_error_note_limit_dialog_response_cb),
                   NULL);
  cell_dialog_error_set_warning_acknowledged(FALSE);

  return dialog;
}

static gboolean
iap_dialog_error_note_autoconnect_enabled()
{
  gboolean auto_connect_enabled = FALSE;
  GSList *auto_connect_values = cell_dialog_error_get_auto_connect_values();

  if (auto_connect_values)
  {
    GSList *l = auto_connect_values;

    for(l = auto_connect_values; l; l = l->next)
    {
      gchar *s = l->data;

      if (s)
      {
        if (!g_strcmp0(s, "GPRS") || !g_strcmp0(s, "*") )
          auto_connect_enabled = TRUE;

        g_free(s);
      }
    }

    g_slist_free(auto_connect_values);
  }

  return auto_connect_enabled;
}

static void
gconf_notify_cb(GConfClient *client, guint cnxn_id, GConfEntry *entry,
                gpointer user_data)
{
  /* FIXME - we shall support counters per modem/IMEI/IMSI */
  iap_dialog_error_note_counters(NULL);
}

static gboolean
iap_dialog_error_note_show(int iap_id, DBusMessage *message,
                           iap_dialogs_showing_fn showing,
                           iap_dialogs_done_fn done,
                           osso_context_t *libosso)
{
  const char *err_text;
  DBusError dbus_error;
  GError *error = NULL;
  const char *note_type = NULL;
  const char *modem_id = NULL;
  DBusMessageIter iter;
  gboolean has_modem_id = FALSE;
  gboolean failed;
  dbus_error_init(&dbus_error);
  dbus_message_iter_init (message, &iter);

  if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING)
  {
    dbus_message_iter_next (&iter);

    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING)
      has_modem_id = TRUE;
  }

  if (has_modem_id)
  {
    failed = !dbus_message_get_args(message, &dbus_error,
                                    DBUS_TYPE_STRING, &note_type,
                                    DBUS_TYPE_STRING, &modem_id,
                                    DBUS_TYPE_INVALID);
  }
  else
  {
    failed = !dbus_message_get_args(message, &dbus_error,
                                    DBUS_TYPE_STRING, &note_type,
                                    DBUS_TYPE_INVALID);
  }

  if (failed)
  {
    CONNUI_ERR("could not get arguments: %s", dbus_error.message);
    dbus_error_free(&dbus_error);
    return FALSE;
  }

  err_text = connui_cell_code_ui_error_note_type_to_text(modem_id, note_type);

  if (!err_text)
  {
    CONNUI_ERR("Unknown cellular error note type '%s'", note_type);
    return FALSE;
  }

  if (!g_strcmp0(note_type, "no_network"))
  {
    hildon_banner_show_information(
          NULL, NULL,
          connui_cell_code_ui_error_note_type_to_text(modem_id, note_type));
    return TRUE;
  }

  done_func_dialog_id = iap_id;
  done_func = done;

  if (!_gconf)
    _gconf = gconf_client_get_default();

  if (!_gconf)
  {
    CONNUI_ERR("Could not get default gconf client");
    return FALSE;
  }

  _gconf_notify = gconf_client_notify_add(
        _gconf, ICD_GCONF_NETWORK_MAPPING_GPRS, gconf_notify_cb, NULL, NULL,
        &error);

  if (error)
  {
    CONNUI_ERR("Unable to add GConf notify: %s", error->message);
    g_clear_error(&error);
  }

  gconf_client_add_dir(_gconf, ICD_GCONF_NETWORK_MAPPING_GPRS,
                       GCONF_CLIENT_PRELOAD_ONELEVEL, &error);
  if (error)
  {
    CONNUI_ERR("Unable to add GConf notify dir: %s", error->message);
    g_clear_error(&error);
  }

  showing();

  if ((!g_strcmp0(note_type, "home_notification") &&
       !gconf_client_get_bool(_gconf, GPRS_HOME_NTFY_ENABLE, NULL)) ||
      (!g_strcmp0(note_type, "roaming_notification") &&
       !gconf_client_get_bool(_gconf, GPRS_ROAM_NTFY_ENABLE, NULL)))
  {
    cell_dialog_error_note_close(FALSE);
    return TRUE;

  }

  if (!g_strcmp0(note_type, "home_notification") ||
      !g_strcmp0(note_type, "roaming_notification"))
  {
    _dialog = iap_dialog_error_note_limit_dialog(modem_id, note_type);
  }
  else if (!g_strcmp0(note_type, "req_autoconn_confirmation_dlg"))
  {
    if (gconf_client_get_bool(
          _gconf,
          "/system/osso/connectivity/ui/gprs_auto_connect_asked", NULL) ||
        iap_dialog_error_note_autoconnect_enabled())
    {
      cell_dialog_error_note_close(FALSE);
      return TRUE;
    }

    _dialog = iap_dialog_error_note_psd_auto_dialog(err_text);
  }
  else
  {
    _dialog = hildon_note_new_information(NULL, err_text);
    g_signal_connect(
          G_OBJECT(_dialog), "response",
          G_CALLBACK(cell_dialog_error_note_close_no_process_pending), NULL);

  }

  gtk_widget_show_all(_dialog);

  return TRUE;
}
