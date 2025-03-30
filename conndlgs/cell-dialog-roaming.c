#include "config.h"

#include <connui/connui.h>
#include <connui/connui-conndlgs.h>
#include <connui/connui-dbus.h>
#include <connui/connui-log.h>
#include <icd/osso-ic-gconf.h>

#include <glib/gi18n-lib.h>

#include "connui-cellular.h"

#define CELLULAR_UI_DBUS_INTERFACE "com.nokia.cellular_ui"
#define CELLULAR_UI_DBUS_PATH "/com/nokia/cellular_ui"
#define CELLULAR_UI_SHOW_ROAMING_DLG "show_roaming_dlg"
#define CELLULAR_UI_ROAMING_SIGNAL "roaming"

#undef ICD_UI_DBUS_INTERFACE
#undef ICD_UI_DBUS_PATH

#define ICD_UI_DBUS_INTERFACE CELLULAR_UI_DBUS_INTERFACE
#define ICD_UI_DBUS_PATH CELLULAR_UI_DBUS_PATH

#define ICD_GCONF_UI ICD_GCONF_SETTINGS "/ui/"
#define ROAM_ASKED ICD_GCONF_UI "gprs_roaming_asked"

#define IS_EMPTY(s) (!(s) || !*(s))

IAP_DIALOGS_PLUGIN_DEFINE(roaming, CELLULAR_UI_SHOW_ROAMING_DLG);

static GtkWidget *_dialog;
static iap_dialogs_done_fn done_fn;
static int _iap_id;
static DBusMessage *_message;

static gboolean
_gconf_list_contains(GConfClient *gconf, const char *key, const char *value)
{
  GSList *values = gconf_client_get_list(gconf, key, GCONF_VALUE_STRING, NULL);
  gboolean rv = FALSE;
  GSList *l;

  for (l = values; l; l = l->next)
  {
    if (!g_strcmp0(value, l->data))
    {
      rv = TRUE;
      break;
    }
  }

  g_slist_free_full(values, g_free);

  return rv;
}

static void
_gconf_list_append(GConfClient *gconf, const char *key, const char *value)
{
  GSList *l;

  if (_gconf_list_contains(gconf, key, value))
    return;

  l = gconf_client_get_list(gconf, key, GCONF_VALUE_STRING, NULL);
  l = g_slist_prepend(l, g_strdup(value));
  gconf_client_set_list(gconf, key, GCONF_VALUE_STRING, l, NULL);
  g_slist_free_full(l, g_free);
}

static gboolean
iap_dialog_roaming_cancel(DBusMessage *message)
{
  if (_dialog)
    gtk_dialog_response(GTK_DIALOG(_dialog), GTK_RESPONSE_CANCEL);

  return TRUE;
}

static gboolean
roaming_send_reply(const char *sender,const char *imsi, dbus_bool_t enable)
{
  DBusMessage *message = dbus_message_new_signal(CELLULAR_UI_DBUS_PATH,
                                                 CELLULAR_UI_DBUS_INTERFACE,
                                                 CELLULAR_UI_ROAMING_SIGNAL);

  if (!message)
    return FALSE;

  if (dbus_message_append_args(message,
                               DBUS_TYPE_STRING, &imsi,
                               DBUS_TYPE_BOOLEAN, &enable,
                               DBUS_TYPE_INVALID))
  {
    dbus_message_set_destination(message, sender);
    connui_dbus_send_system_msg(message);
    dbus_message_unref(message);
    return TRUE;
  }

  CONNUI_ERR("could not append args to roaming reply");
  dbus_message_unref((DBusMessage *)message);

  return FALSE;
}

static void
roaming_dialog_close(void)
{
  if (_dialog)
  {
    gtk_widget_destroy(_dialog);
    _dialog = NULL;
  }

  if (done_fn)
  {
    done_fn(_iap_id, FALSE);
    _iap_id = 0;
    done_fn = NULL;
  }

  if (_message)
  {
    dbus_message_unref(_message);
    _message = NULL;
  }
}

static void
roaming_note_response_cb(GtkDialog *dialog, gint response_id,
                         gpointer user_data)
{
  roaming_send_reply(dbus_message_get_sender(_message),
                     user_data, response_id == GTK_RESPONSE_OK);
  roaming_dialog_close();
}

static void
roaming_confirm_enable(const char *imsi)
{
  _dialog = hildon_note_new_confirmation(NULL, _("conn_nc_phone_data_roaming"));
  g_signal_connect(G_OBJECT(_dialog), "response",
                   G_CALLBACK(roaming_note_response_cb), (gpointer)imsi);
  gtk_widget_show_all(_dialog);
}

static void
roaming_auto_dialog_response_cb(GtkDialog *dialog, gint response_id,
                                gpointer user_data)
{
  gboolean ok = response_id == GTK_RESPONSE_OK;
  GConfClient *gconf = gconf_client_get_default();
  const char *imsi = user_data;

  if (gconf)
  {
    GError *error = NULL;

    gconf_client_set_bool(gconf, ROAM_ASKED, TRUE, &error);

    if (error)
    {
      CONNUI_ERR("Error: %s\n", error->message);
      g_clear_error(&error);
    }

    gconf_client_set_bool(gconf, GPRS_ROAM_DISABLED, !ok, &error);

    if (error)
    {
      CONNUI_ERR("Error: %s\n", error->message);
      g_clear_error(&error);
    }

    g_object_unref(G_OBJECT(gconf));
  }

  if (ok)
  {
    roaming_send_reply(dbus_message_get_sender(_message), imsi, TRUE);
    roaming_dialog_close();
  }
  else
  {
    gtk_widget_destroy(_dialog);
    _dialog = NULL;
    roaming_confirm_enable(imsi);
  }
}

static void
_auto_roam_ask(const char *imsi)
{
  GtkWidget *button_no;
  GtkWidget *label;
  gchar *markup;
  GtkWidget *area;

  _dialog = hildon_dialog_new_with_buttons(
        _("conn_ti_roaming"), NULL,
        GTK_DIALOG_NO_SEPARATOR | GTK_DIALOG_DESTROY_WITH_PARENT |
        GTK_DIALOG_MODAL, NULL);

  gtk_dialog_add_button(GTK_DIALOG(_dialog),
                        dgettext("hildon-libs", "wdgt_bd_yes"),
                        GTK_RESPONSE_OK);
  button_no = gtk_dialog_add_button(GTK_DIALOG(_dialog),
                                    dgettext("hildon-libs", "wdgt_bd_no"),
                                    GTK_RESPONSE_CANCEL);
  /* fmg - why the special care? */
  gtk_widget_show(button_no);
  gtk_widget_set_no_show_all(button_no, FALSE);

  label = gtk_label_new(NULL);
  markup = g_strdup_printf("<small>%s</small>", _("conn_nc_roaming"));
  gtk_label_set_markup(GTK_LABEL(label), markup);
  gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.0);
  g_free(markup);

  area = hildon_pannable_area_new();
  hildon_pannable_area_add_with_viewport(HILDON_PANNABLE_AREA(area), label);
  gtk_widget_set_size_request(GTK_WIDGET(area), -1, 180);
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(_dialog)->vbox), area);
  g_signal_connect(G_OBJECT(_dialog), "response",
                   G_CALLBACK(roaming_auto_dialog_response_cb), (gpointer)imsi);
  gtk_widget_show_all(_dialog);
}

static gboolean
iap_dialog_roaming_show(int iap_id, DBusMessage *message,
                        iap_dialogs_showing_fn showing,
                        iap_dialogs_done_fn done, osso_context_t *libosso)
{
  GConfClient *gconf;
  DBusError dbus_error;
  const char *imsi = NULL;

  dbus_error_init(&dbus_error);

  if (!dbus_message_get_args(message, &dbus_error,
                             DBUS_TYPE_STRING, &imsi,
                             DBUS_TYPE_INVALID))
  {
    CONNUI_ERR("could not get arguments: %s", dbus_error.message);
    dbus_error_free(&dbus_error);
    return FALSE;
  }

  if (IS_EMPTY(imsi))
  {
    CONNUI_ERR("Empty IMSI provided");
    return FALSE;
  }

  done_fn = done;
  _iap_id = iap_id;
  _message = dbus_message_ref(message);

  showing();

  gconf = gconf_client_get_default();

  if (gconf)
  {
    gboolean enabled = !_gconf_list_contains(gconf, GPRS_ROAM_DISABLED, imsi);
    gboolean asked = _gconf_list_contains(gconf, ROAM_ASKED, imsi);

    if (asked)
    {
      if (enabled)
      {
        roaming_send_reply(dbus_message_get_sender(_message), imsi, TRUE);
        roaming_dialog_close();
      }
      else
        roaming_confirm_enable(imsi);

      g_object_unref(G_OBJECT(gconf));
      return TRUE;
    }
  }

  g_object_unref(G_OBJECT(gconf));

  _auto_roam_ask(imsi);

  return TRUE;
}
