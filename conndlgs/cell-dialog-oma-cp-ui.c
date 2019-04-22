#include <connui/connui.h>
#include <connui/connui-conndlgs.h>
#include <connui/connui-dbus.h>
#include <connui/connui-log.h>
#include <connui/connui-utils.h>
#include <icd/osso-ic-gconf.h>
#include <clui-code-dialog.h>

#include <string.h>

#include <libintl.h>

#include "connui-cellular.h"

#include "config.h"

#define _(msgid) dgettext(GETTEXT_PACKAGE, msgid)

#define CELLULAR_OMA_CP_UI_DBUS_INTERFACE "com.nokia.oma_cp_ui"
#define CELLULAR_OMA_CP_UI_DBUS_PATH "/com/nokia/oma_cp_ui"
#define CELLULAR_OMA_CP_UI_IAP_SELECT_REQ "iap_select_req"
#define CELLULAR_OMA_CP_UI_PIN_QUERY_REQ "pin_query_req"
#define CELLULAR_OMA_CP_UI_NOTIFICATION_REQ "notification_req"
#define CELLULAR_OMA_CP_UI_IAP_SELECTED "iap_selected"
#define CELLULAR_OMA_CP_UI_PIN_SIG "pin_sig"

static GtkWidget *_dialog;
static GtkWidget *_selector;
static gchar *_sender;
static iap_dialogs_done_fn done_fn;
static int _iap_id;

static gboolean
iap_dialog_oma_cp_ui_show(int iap_id, DBusMessage *message,
                           iap_dialogs_showing_fn showing,
                           iap_dialogs_done_fn done,
                           osso_context_t *libosso);

G_MODULE_EXPORT const gchar *
g_module_check_init(GModule *module G_GNUC_UNUSED)
{
  iap_dialog_register_service(CELLULAR_OMA_CP_UI_DBUS_INTERFACE,
                              CELLULAR_OMA_CP_UI_DBUS_PATH);

  return NULL;
}

G_MODULE_EXPORT void
g_module_unload(GModule *module G_GNUC_UNUSED)
{
  iap_dialog_unregister_service(CELLULAR_OMA_CP_UI_DBUS_INTERFACE,
                                CELLULAR_OMA_CP_UI_DBUS_PATH);
}

G_MODULE_EXPORT gboolean
iap_dialogs_plugin_match(DBusMessage *message)
{
  return
      dbus_message_is_method_call(message, CELLULAR_OMA_CP_UI_DBUS_INTERFACE,
                                  CELLULAR_OMA_CP_UI_IAP_SELECT_REQ) ||
      dbus_message_is_method_call(message, CELLULAR_OMA_CP_UI_DBUS_INTERFACE,
                                  CELLULAR_OMA_CP_UI_PIN_QUERY_REQ) ||
      dbus_message_is_method_call(message, CELLULAR_OMA_CP_UI_DBUS_INTERFACE,
                                  CELLULAR_OMA_CP_UI_NOTIFICATION_REQ);
}

static gboolean
iap_dialog_oma_cp_ui_cancel(DBusMessage *message)
{
  if ( _dialog )
   gtk_dialog_response(GTK_DIALOG(_dialog), GTK_RESPONSE_CANCEL);

  return TRUE;
}

G_MODULE_EXPORT gboolean
iap_dialogs_plugin_cancel(DBusMessage *message)
{
  return iap_dialog_oma_cp_ui_cancel(message);
}

G_MODULE_EXPORT gboolean
iap_dialogs_plugin_show(int iap_id, DBusMessage *message,
                        iap_dialogs_showing_fn showing,
                        iap_dialogs_done_fn done,
                        osso_context_t *libosso)
{
  g_return_val_if_fail(showing != NULL, FALSE);
  g_return_val_if_fail(done != NULL, FALSE);
  g_return_val_if_fail(libosso != NULL, FALSE);

  return iap_dialog_oma_cp_ui_show(iap_id, message, showing, done, libosso);
}

static void
cell_dialog_oma_cp_ui_close()
{
  if (_dialog)
  {
    gtk_widget_destroy(_dialog);
    _dialog = NULL;
    _selector = NULL;
  }

  g_free(_sender);
  _sender = NULL;

  if (done_fn)
  {
    done_fn(_iap_id, FALSE);
    _iap_id = 0;
    done_fn = NULL;
  }
}

static void
cell_dialog_oma_cp_ui_select_iap_response(GtkDialog *dialog, gint response_id,
                                          gpointer user_data)
{
  DBusMessage *reply =
      dbus_message_new_signal(CELLULAR_OMA_CP_UI_DBUS_PATH,
                              CELLULAR_OMA_CP_UI_DBUS_INTERFACE,
                              CELLULAR_OMA_CP_UI_IAP_SELECTED);

  if (reply)
  {
    gchar *iap = NULL;

    if (response_id == GTK_RESPONSE_OK)
    {
      iap = hildon_touch_selector_get_current_text(
            HILDON_TOUCH_SELECTOR(_selector));
    }
    else
      iap = g_strdup("");

    if (dbus_message_append_args(reply,
                                 DBUS_TYPE_STRING, &iap,
                                 DBUS_TYPE_INVALID))
    {
      dbus_message_set_destination(reply, _sender);
      connui_dbus_send_system_msg(reply);
    }
    else
      CONNUI_ERR("could not append args");

    dbus_message_unref(reply);
    g_free(iap);
  }

  cell_dialog_oma_cp_ui_close();
}

static void
settings_received_response_cb(GtkDialog *dialog, gint response_id,
                              gpointer user_data)
{
  cell_dialog_oma_cp_ui_close();
}

static gboolean
settings_updated_timeout_cb(gpointer user_data)
{
  cell_dialog_oma_cp_ui_close();
  return FALSE;
}

static GtkWidget *
cell_dialog_oma_cp_ui_notification(const char *type, gboolean *no_dialog)
{
  GtkWidget *rv = NULL;

  g_return_val_if_fail(type != NULL, NULL);

  if (!strcmp(type, "settings_received"))
  {
    rv = hildon_note_new_information(NULL, _("conn_in_ap_settings_equal"));
    iap_common_set_close_response(rv, GTK_RESPONSE_OK);
    g_signal_connect(G_OBJECT(rv), "response",
                     G_CALLBACK(settings_received_response_cb), NULL);
    return rv;
  }
  if (!strcmp(type, "settings_received_updated") ||
      !strcmp(type, "settings_updated") )
  {
    guint interval;

    rv = hildon_banner_show_information(NULL, NULL,
                                        _("conn_ib_ap_settings_updated"));
    g_object_get(G_OBJECT(rv), "timeout", &interval, NULL);
    g_timeout_add(interval, settings_updated_timeout_cb, NULL);
  }
  else if (!strcmp(type, "pin_failed"))
  {
    *no_dialog = TRUE;
    hildon_banner_show_information(NULL, NULL, _("conn_ti_iap_pin_incorrect"));
  }

  return rv;
}

static void
cell_dialog_oma_cp_ui_pin_query_response(GtkDialog *dialog, gint response_id,
                                         gpointer user_data)
{
  DBusMessage *reply =
      dbus_message_new_signal(CELLULAR_OMA_CP_UI_DBUS_PATH,
                              CELLULAR_OMA_CP_UI_DBUS_INTERFACE,
                              CELLULAR_OMA_CP_UI_PIN_SIG);
  if (reply)
  {
    gchar *code = NULL;

    if (response_id == GTK_RESPONSE_OK)
      code = clui_code_dialog_get_code(CLUI_CODE_DIALOG(dialog));
    else
      code = g_strdup("");

    if (!dbus_message_append_args(reply,
                                  DBUS_TYPE_STRING, &code,
                                  DBUS_TYPE_INVALID))
    {
      CONNUI_ERR("could not append args");
      dbus_message_unref(reply);
    }
    else
    {
      dbus_message_set_destination(reply, _sender);
      connui_dbus_send_system_msg(reply);
      dbus_message_unref(reply);
      connui_cell_code_ui_destroy();
      _dialog = NULL;
    }

    g_free(code);
  }

  cell_dialog_oma_cp_ui_close();
}

static gboolean
iap_dialog_oma_cp_ui_show(int iap_id, DBusMessage *message,
                           iap_dialogs_showing_fn showing,
                           iap_dialogs_done_fn done,
                           osso_context_t *libosso)
{
  GtkWidget *_dialog;
  DBusError error;
  gboolean no_dialog = FALSE;

  done_fn = done;
  _iap_id = iap_id;
  dbus_error_init(&error);

  if (dbus_message_is_method_call(message,
                                  CELLULAR_OMA_CP_UI_DBUS_INTERFACE,
                                  CELLULAR_OMA_CP_UI_IAP_SELECT_REQ))
  {
    int len = 0;
    gchar **iaps = NULL;

    if (dbus_message_get_args(message, &error,
                              DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &iaps, &len,
                              DBUS_TYPE_INVALID))
    {
      int i;
      GtkWidget *label;

      _dialog = hildon_dialog_new_with_buttons(
                 _("conn_ti_iap_select"),
                 NULL,
                 GTK_DIALOG_NO_SEPARATOR | GTK_DIALOG_DESTROY_WITH_PARENT
                                                             | GTK_DIALOG_MODAL,
                 dgettext("hildon-libs", "wdgt_bd_save"),
                 GTK_RESPONSE_OK,
                 NULL);
      _selector = hildon_touch_selector_new_text();

      for(i = 0; i < len; i++)
      {
        hildon_touch_selector_append_text(HILDON_TOUCH_SELECTOR(_selector),
                                          iaps[i]);
      }

      hildon_touch_selector_set_active(HILDON_TOUCH_SELECTOR(_selector), 0, 0);

      label = g_object_new(GTK_TYPE_LABEL,
                           "label", _("conn_fi_iap_select_text"),
                           "wrap", TRUE,
                           "xalign", 0.0f,
                           "yalign", 0.0f,
                           NULL);
      gtk_container_add(GTK_CONTAINER(GTK_DIALOG(_dialog)->vbox), label);
      gtk_container_add(GTK_CONTAINER(GTK_DIALOG(_dialog)->vbox), _selector);
      gtk_widget_set_size_request(_selector, 800, 196);
      iap_common_set_close_response(_dialog, GTK_RESPONSE_CANCEL);
      g_signal_connect(G_OBJECT(_dialog), "response",
                       G_CALLBACK(cell_dialog_oma_cp_ui_select_iap_response),
                       NULL);
    }
    else
    {
      CONNUI_ERR("could not get arguments: %s", error.message);
      dbus_error_free(&error);
    }
  }
  else if (dbus_message_is_method_call(message,
                                       CELLULAR_OMA_CP_UI_DBUS_INTERFACE,
                                       CELLULAR_OMA_CP_UI_PIN_QUERY_REQ))
  {
    dbus_bool_t pin_failed;

    if (dbus_message_get_args(message, &error,
                              DBUS_TYPE_BOOLEAN, &pin_failed,
                              DBUS_TYPE_INVALID))
    {
      gboolean unused;

      if (pin_failed)
        cell_dialog_oma_cp_ui_notification("pin_failed", &unused);

      _dialog =
          connui_cell_code_ui_create_dialog(_("conn_ti_iap_select_pin"), 1);
      g_signal_connect(G_OBJECT(_dialog), "response",
                       G_CALLBACK(cell_dialog_oma_cp_ui_pin_query_response),
                       NULL);
    }
    else
    {
      CONNUI_ERR("could not get arguments: %s", error.message);
      dbus_error_free(&error);
    }
  }
  else if (dbus_message_is_method_call(message,
                                       CELLULAR_OMA_CP_UI_DBUS_INTERFACE,
                                       CELLULAR_OMA_CP_UI_NOTIFICATION_REQ))
  {
    gchar *notification_req_type = NULL;

    if (dbus_message_get_args(message, &error,
                              DBUS_TYPE_STRING, &notification_req_type,
                              DBUS_TYPE_INVALID))
    {
      _dialog =
          cell_dialog_oma_cp_ui_notification(notification_req_type, &no_dialog);
    }
    else
    {
      CONNUI_ERR("could not get arguments: %s", error.message);
      dbus_error_free(&error);
    }
  }

  if (!no_dialog)
  {
    if (!_dialog)
      return FALSE;

    _sender = g_strdup(dbus_message_get_sender(message));
    showing();
    connui_utils_unblank_display();
    gtk_widget_show_all(_dialog);
  }

  return TRUE;
}
