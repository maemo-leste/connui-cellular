#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <connui/connui-log.h>
#include <clui-code-dialog.h>
#include <X11/Xlib.h>

#include <libintl.h>

#include <string.h>

#include "context.h"
#include "connui-cell-note.h"

#include "config.h"

#define _(s) dgettext(GETTEXT_PACKAGE, s)

typedef enum {
  CONNUI_CELL_CODE_UI_STATE_NONE = 0x0,
  CONNUI_CELL_CODE_UI_STATE_NOTE = 0x1,
  CONNUI_CELL_CODE_UI_STATE_2 = 0x2,
  CONNUI_CELL_CODE_UI_STATE_3 = 0x3,
  CONNUI_CELL_CODE_UI_STATE_4 = 0x4,
  CONNUI_CELL_CODE_UI_STATE_5 = 0x5,
  CONNUI_CELL_CODE_UI_STATE_6 = 0x6,
  CONNUI_CELL_CODE_UI_STATE_7 = 0x7,
  CONNUI_CELL_CODE_UI_STATE_8 = 0x8
} connui_cell_code_ui_state;

struct _cell_code_ui
{
  connui_cell_code_ui_state state;
  GtkWidget *dialog;
  GtkWidget *note;
  GtkWindow *parent;
  gchar *pin_message;
  gchar *code;
  gint pin_count;
  guint sim_status_timeout;
  gboolean current_pin_entered;
  gboolean show_pin_code_correct;
  GStrv emergency_numbers;
  gchar *clui_em_number;
  guint emcall_timeout;
  guint unused_timeout;
  gint sim_status;
  gboolean unk_bool;
  gint code_min_len;
};

typedef struct _cell_code_ui cell_code_ui;

static cell_code_ui *_code_ui = NULL;
static guint code_ui_filters_count = 0;

static void connui_cell_code_ui_sim_status_cb(guint status, gpointer user_data);

const char *
connui_cell_code_ui_error_note_type_to_text(const char *note_type)
{
  const char *text;

  g_return_val_if_fail(note_type != NULL, NULL);

  if (!strcmp(note_type, "no_sim"))
    text = _("conn_ni_no_sim_card_in");
  else if (!strcmp(note_type, "no_pin"))
    text = _("conn_ni_no_pin_for_sim");
  else if (!strcmp(note_type, "no_network"))
    text = _("conn_ni_no_cell_network");
  else if (!strcmp(note_type, "sim_locked"))
    text = _("conn_ni_sim_lock");
  else if (!strcmp(note_type, "req_autoconn_confirmation_dlg"))
    text = _("conn_nc_use_device_psd_auto");
  else if (!strcmp(note_type, "sim_rejected"))
    text = _("conn_ni_sim_rejected");
  else if (!strcmp(note_type, "sim_reg_fail"))
    text = _("conn_ni_sim_reg_fail");
  else if (!strcmp(note_type, "sim_select_network"))
    text = _("conn_ni_select_network");
  else if (!strcmp(note_type, "modem_failure"))
    text = _("conn_ni_modem_failure");
  else if (!strcmp(note_type, "modem_poweroff"))
    text = _("conn_ni_sim_failure");
  else if (!strcmp(note_type, "home_notification"))
    text = _("conn_fi_phone_limit_dialog_home");
  else if (!strcmp(note_type, "roaming_notification"))
    text = _("conn_fi_phone_limit_dialog_roaming");
  else
    text = NULL;

  return text;
}

gboolean
connui_cell_code_ui_cancel_dialog()
{
  if (!_code_ui || !_code_ui->dialog)
    return FALSE;

  gtk_dialog_response(GTK_DIALOG(_code_ui->dialog), GTK_RESPONSE_CANCEL);

  return TRUE;
}

static void
connui_cell_code_ui_launch_note(cell_code_ui *code_ui, const char *note_type)
{
  DBusGConnection *bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, 0);
  GError *error = NULL;

  if (bus)
  {
    DBusGProxy *proxy = dbus_g_proxy_new_for_name(bus,
                                                  "com.nokia.cellular_ui",
                                                  "/com/nokia/cellular_ui",
                                                  "com.nokia.cellular_ui");
    dbus_g_connection_unref(bus);

    if (proxy)
    {
      if (dbus_g_proxy_call(proxy, "show_error_note", &error,
                            G_TYPE_STRING, note_type,
                            G_TYPE_INVALID, G_TYPE_INVALID))
      {
        g_object_unref(proxy);
        return;
      }

      g_object_unref(proxy);
      CONNUI_ERR("%s", error->message);
    }
    else
      CONNUI_ERR("Unable to get DBUS proxy");
  }
  else
    CONNUI_ERR("Unable to get DBUS connection");

  if (!strcmp(note_type, "no_network"))
  {
    hildon_banner_show_information(
          NULL, NULL, connui_cell_code_ui_error_note_type_to_text(note_type));
  }
  else
  {
    code_ui->note = connui_cell_note_new_information(
          code_ui->parent,
          connui_cell_code_ui_error_note_type_to_text(note_type));

    gtk_dialog_run(GTK_DIALOG(code_ui->note));
    gtk_widget_destroy(code_ui->note);
    code_ui->note = NULL;
  }
}

static void
connui_cell_code_ui_launch_sim_locked_note(cell_code_ui *code_ui)
{
  code_ui->state = CONNUI_CELL_CODE_UI_STATE_NOTE;
  connui_cell_code_ui_launch_note(code_ui, "sim_locked");
}

static void
connui_cell_code_ui_launch_no_sim_note(cell_code_ui *code_ui)
{
  code_ui->state = CONNUI_CELL_CODE_UI_STATE_NOTE;
  connui_cell_code_ui_launch_note(code_ui, "no_sim");
}

gboolean
connui_cell_code_ui_is_sim_locked_with_error()
{
  cell_code_ui *code_ui = _code_ui;
  gboolean has_error = FALSE;

  g_return_val_if_fail(code_ui != NULL, FALSE);

  if (connui_cell_sim_is_locked(&has_error))
  {
    if (!has_error)
    {
      connui_cell_code_ui_launch_sim_locked_note(code_ui);
      return TRUE;
    }
  }
  else if (!has_error)
    return FALSE;

  g_warning("Reading simlock status failed");

  return FALSE;
}

gboolean
connui_cell_sim_is_locked(gboolean *has_error)
{
  connui_cell_context *ctx = connui_cell_context_get();
  gboolean rv;
  GError *error = NULL;
  gint status = 0;

  g_return_val_if_fail(ctx != NULL, FALSE);

  rv = dbus_g_proxy_call(ctx->phone_sim_proxy, "read_simlock_status", &error,
                         G_TYPE_INVALID,
                         G_TYPE_INT, &status,
                         G_TYPE_INVALID);

  if (rv)
  {
    if (has_error)
      *has_error = status == 0 || status == 8 || status == 7 || status == 5;

    rv = (status == 2 || status == 3 || status == 4);
  }
  else
  {
    CONNUI_ERR("Error with DBUS: %s", error->message);
    g_clear_error(&error);

    if (has_error)
      *has_error = TRUE;
  }

  connui_cell_context_destroy(ctx);

  return rv;
}

static void
connui_cell_code_ui_ssc_state_cb(const gchar *modem_state, gpointer user_data)
{
  *((gchar **)user_data) = g_strdup(modem_state);
}

static gboolean
sim_status_timeout_cb(cell_code_ui *code_ui)
{
  g_warning("Waited for better SIM status, but failed. Reporting error");
  code_ui->sim_status_timeout = 0;
  code_ui->state = CONNUI_CELL_CODE_UI_STATE_NOTE;
  connui_cell_sim_status_close(connui_cell_code_ui_sim_status_cb);

  return FALSE;
}

static void
connui_cell_code_ui_sim_status_cb(guint status, gpointer user_data)
{
  cell_code_ui *code_ui = user_data;

  g_return_if_fail(code_ui != NULL);

  if (code_ui->sim_status_timeout)
  {
    g_source_remove(code_ui->sim_status_timeout);
    code_ui->sim_status_timeout = 0;
  }

  code_ui->sim_status = status;

  switch (status)
  {
    case 1:
    {
      GtkWidget *note= code_ui->note;
      code_ui->state = CONNUI_CELL_CODE_UI_STATE_3;

      if (!note)
        goto sc_register;

      gtk_dialog_response(GTK_DIALOG(note), -5);
      return;
    }
    case 4:
    {
      code_ui->state = CONNUI_CELL_CODE_UI_STATE_NOTE;
      connui_cell_code_ui_launch_note(code_ui, "sim_rejected");
      break;
    }
    case 5:
    {
      if (code_ui->state != CONNUI_CELL_CODE_UI_STATE_4)
        connui_cell_code_ui_launch_no_sim_note(code_ui);

      code_ui->state = CONNUI_CELL_CODE_UI_STATE_NOTE;
      goto ss_close;
    }
    case 7:
    case 8:
    {
      if (code_ui->state == CONNUI_CELL_CODE_UI_STATE_4)
        goto sc_register;

      if (!code_ui->pin_count)
      {
        code_ui->state = CONNUI_CELL_CODE_UI_STATE_NOTE;
        connui_cell_code_ui_launch_note(code_ui, "no_pin");
      }

      break;
    }
    case 10:
    {
      connui_cell_code_ui_launch_sim_locked_note(code_ui);
      break;
    }
    case 0:
    case 2:
    case 6:
    {
      if (code_ui->state != CONNUI_CELL_CODE_UI_STATE_4)
      {
        connui_cell_code_ui_launch_no_sim_note(code_ui);
        break;
      }

      g_warning(
            "SIM in error state %d during startup - waiting for better state",
            status);
      goto wait_better_status;
    }
    default:
    {
      g_warning("Unable to handle SIM status %u", status);
      g_warning("Waiting for better status");
      goto wait_better_status;
    }
  }

  if (code_ui->state == CONNUI_CELL_CODE_UI_STATE_NOTE ||
      code_ui->state == CONNUI_CELL_CODE_UI_STATE_2)
  {
sc_register:
    if (!connui_cell_security_code_register(
          connui_cell_code_ui_code_cb, code_ui))
    {
      g_warning("Unable to register security code handler, exiting...");
      code_ui->state = CONNUI_CELL_CODE_UI_STATE_NOTE;
    }
  }

ss_close:
  connui_cell_sim_status_close(connui_cell_code_ui_sim_status_cb);

  return;

wait_better_status:
  code_ui->sim_status_timeout =
      g_timeout_add(60000, (GSourceFunc)sim_status_timeout_cb, code_ui);
}

gboolean
connui_cell_code_ui_init(GtkWindow *parent, gboolean show_pin_code_correct)
{
  cell_code_ui *code_ui = _code_ui;

  if (!code_ui)
  {
    code_ui = g_new0(cell_code_ui, 1);
    _code_ui = code_ui;
  }

  code_ui->parent = parent;
  code_ui->show_pin_code_correct = show_pin_code_correct;

  if (show_pin_code_correct)
  {
    gchar *modem_state = NULL;

    code_ui->state = CONNUI_CELL_CODE_UI_STATE_4;

    if (!connui_cell_ssc_state_register(connui_cell_code_ui_ssc_state_cb,
                                        &modem_state))
    {
      return FALSE;
    }

    if (modem_state)
    {
      if (!strcmp(modem_state, "initialize"))
      {
        g_free(modem_state);
        modem_state = 0;

        do
          g_main_context_iteration(0, 1);
        while (!modem_state);
      }

      if (!strcmp(modem_state, "service") || !strcmp(modem_state, "poweroff"))
      {
        GtkWidget *note;
        const char *note_type;

        if (!strcmp(modem_state, "service"))
          note_type = "modem_failure";
        else
          note_type = "modem_poweroff";

        note = connui_cell_note_new_information(
              code_ui->parent,
              connui_cell_code_ui_error_note_type_to_text(note_type));

        code_ui->note = note;
        gtk_dialog_run(GTK_DIALOG(note));
        gtk_widget_destroy(code_ui->note);
        code_ui->note = NULL;
        connui_cell_ssc_state_close(connui_cell_code_ui_ssc_state_cb);
        g_free(modem_state);
        return FALSE;
      }
    }
    else
      CONNUI_ERR("Could not get modem state, it is NULL");

    connui_cell_ssc_state_close(connui_cell_code_ui_ssc_state_cb);
    g_free(modem_state);
  }
  else
  {
    code_ui->state = CONNUI_CELL_CODE_UI_STATE_NONE;

    if (!connui_cell_net_is_activated(FALSE))
    {
      GtkWidget *note;

      g_warning("Phone is deactivated. Phone settings will not be opened.");
      note = connui_cell_note_new_information(
            code_ui->parent,
            connui_cell_code_ui_error_note_type_to_text("no_network"));
      gtk_dialog_run(GTK_DIALOG(note));
      gtk_widget_destroy(note);
      connui_cell_code_ui_destroy();
      return FALSE;
    }
  }

  if (!connui_cell_sim_status_register(connui_cell_code_ui_sim_status_cb,
                                       code_ui))
  {
    g_warning("Unable to register SIM status callback");

    if (!show_pin_code_correct)
      connui_cell_code_ui_launch_no_sim_note(code_ui);

    connui_cell_code_ui_destroy();
    return FALSE;
  }

  while (1)
  {
    if (code_ui->state == CONNUI_CELL_CODE_UI_STATE_NOTE ||
        code_ui->state == CONNUI_CELL_CODE_UI_STATE_2 ||
        code_ui->state == CONNUI_CELL_CODE_UI_STATE_3)
    {
      break;
    }

    g_main_context_iteration(0, 1);
  }

  if (code_ui->state == CONNUI_CELL_CODE_UI_STATE_3)
    return TRUE;

  connui_cell_code_ui_destroy();
  return FALSE;
}

static GdkFilterReturn
gdk_filter(GdkXEvent *xevent, GdkEvent *event, gpointer data)
{

  XEvent *xev = xevent;

  if (xev->type == ButtonRelease)
    gtk_dialog_response(GTK_DIALOG(_code_ui->dialog), GTK_RESPONSE_CANCEL);

  return GDK_FILTER_CONTINUE;
}

void
connui_cell_code_ui_destroy()
{
  if (_code_ui)
  {
    connui_cell_sim_status_close(connui_cell_code_ui_sim_status_cb);
    connui_cell_security_code_close(connui_cell_code_ui_code_cb);

    if (_code_ui->emcall_timeout)
      g_source_remove(_code_ui->emcall_timeout);

    if (_code_ui->unused_timeout)
      g_source_remove(_code_ui->unused_timeout);

    if (_code_ui->dialog)
    {
      gtk_widget_destroy(_code_ui->dialog);
      _code_ui->dialog = NULL;

      if (code_ui_filters_count > 0)
      {
        gdk_window_remove_filter(
              gdk_get_default_root_window(), gdk_filter, NULL);
        code_ui_filters_count--;
      }
    }

    if (_code_ui->note)
      gtk_widget_destroy(_code_ui->note);

    g_free(_code_ui->code);
    g_free(_code_ui->pin_message);
    g_free(_code_ui->clui_em_number);
    g_strfreev(_code_ui->emergency_numbers);
    g_free(_code_ui);
    _code_ui = NULL;
  }
}

static gboolean
connui_cell_code_ui_emcall_timeout(gpointer user_data)
{
  cell_code_ui *code_ui = user_data;

  g_return_val_if_fail(code_ui != NULL, FALSE);

  code_ui->emcall_timeout = 0;

  if (GTK_IS_WIDGET(code_ui->dialog))
    gtk_widget_set_sensitive(code_ui->dialog, TRUE);

  return FALSE;
}

static void
connui_cell_code_ui_dialog_response(GtkDialog *dialog, gint response_id,
                                    cell_code_ui *code_ui)
{
  if (response_id == 100)
  {
    g_signal_stop_emission_by_name(dialog, "response");

    if (connui_cell_emergency_call())
    {
      if (code_ui->show_pin_code_correct)
      {
        code_ui->emcall_timeout =
            g_timeout_add(5000, connui_cell_code_ui_emcall_timeout, code_ui);
        gtk_widget_set_sensitive(code_ui->dialog, FALSE);
      }
      else
        gtk_dialog_response(GTK_DIALOG(code_ui->dialog), GTK_RESPONSE_CANCEL);
    }
    else
      g_warning("Unable to start emergency call!");
  }
}

GtkWidget *
connui_cell_code_ui_create_dialog(gchar *title, int code_min_len)
{
  GtkWidget *dialog;
  char *cancel_button_label;

  if (!_code_ui)
    _code_ui = g_new0(cell_code_ui, 1);

  g_return_val_if_fail(_code_ui->dialog == NULL, NULL);

  dialog = clui_code_dialog_new(TRUE);
  clui_code_dialog_set_max_code_length(CLUI_CODE_DIALOG(dialog), 8);

  if (_code_ui->show_pin_code_correct)
  {
    GdkWindow *root_window = gdk_get_default_root_window();
    GdkEventMask mask = gdk_window_get_events(root_window);

    cancel_button_label = _("conn_bd_dialog_skip");
    gdk_window_set_events(root_window, mask | GDK_BUTTON_RELEASE_MASK);
    gdk_window_add_filter(root_window, gdk_filter, NULL);
    code_ui_filters_count++;
  }
  else
    cancel_button_label = dgettext("hildon-libs", "wdgt_bd_back");

  _code_ui->code_min_len = code_min_len;
  gtk_window_set_title(GTK_WINDOW(dialog), title);
  gtk_widget_show(dialog);

  if (cancel_button_label)
  {
    clui_code_dialog_set_cancel_button_with_label(CLUI_CODE_DIALOG(dialog),
                                                  cancel_button_label);
    gtk_widget_show_all(dialog);
  }

  if (_code_ui->pin_message)
  {
    hildon_banner_show_information(dialog, NULL, _code_ui->pin_message);
    g_free(_code_ui->pin_message);
    _code_ui->pin_message = NULL;
  }

  if ( !_code_ui->emergency_numbers )
    _code_ui->emergency_numbers = connui_cell_emergency_get_numbers();

  g_signal_connect_swapped(G_OBJECT(dialog), "input",
                           (GCallback)connui_cell_code_ui_dialog_input, _code_ui);
  g_signal_connect(G_OBJECT(dialog), "response",
                   (GCallback)connui_cell_code_ui_dialog_response, _code_ui);

  _code_ui->dialog = dialog;

  return dialog;
}
