#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <connui/connui-log.h>
#include <clui-code-dialog.h>
#include <X11/Xlib.h>

#include <libintl.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "context.h"
#include "ofono-context.h"
#include "connui-cell-note.h"

#include "config.h"

#define LOCK_FILE "/tmp/connui-cell-code-ui.lock"

#define _(s) dgettext(GETTEXT_PACKAGE, s)

typedef enum {
  CONNUI_CELL_CODE_UI_STATE_NONE,
  CONNUI_CELL_CODE_UI_STATE_SIM_ERROR,
  CONNUI_CELL_CODE_UI_STATE_PIN_ERROR,
  CONNUI_CELL_CODE_UI_STATE_PIN_RETRY,
  CONNUI_CELL_CODE_UI_STATE_OK,
  CONNUI_CELL_CODE_UI_STATE_STARTUP,
  CONNUI_CELL_CODE_UI_STATE_PIN_ENABLE,
  CONNUI_CELL_CODE_UI_STATE_NEW_PIN,
  CONNUI_CELL_CODE_UI_SIM_UNLOCK,
  CONNUI_CELL_CODE_UI_STATE_CONFIRM_PIN
} connui_cell_code_ui_state;

#define STATE_IS_ERROR(s) (((s) == CONNUI_CELL_CODE_UI_STATE_SIM_ERROR) || \
  ((s) == CONNUI_CELL_CODE_UI_STATE_PIN_ERROR))

struct _cell_code_ui
{
  gchar *modem_id;
  connui_cell_code_ui_state state;
  const char *error_note;
  GtkWidget *dialog;
  GtkWidget *note;
  GtkWindow *parent;
  gchar *pin_message;
  gchar *code;
  gboolean pin_verified;
  guint sim_status_timeout;
  gboolean get_current_pin;
  gboolean show_status_notes;
  GStrv emergency_numbers;
  gchar *clui_em_number;
  guint emcall_timeout;
  guint unused_timeout;
  connui_sim_status sim_status;
  gboolean verified_ok;
  gint code_min_len;
};

typedef struct _cell_code_ui cell_code_ui;

static cell_code_ui *_code_ui = NULL;
static gint code_ui_filters_count = 0;

static void
connui_cell_code_ui_sim_status_cb(const char *modem_id,
                                  const connui_sim_status *status,
                                  gpointer user_data);
static void
connui_cell_code_ui_code_cb(const char *modem_id,
                            const connui_sim_security_code_type *code_type,
                            gchar ***old_code, gchar ***new_code,
                            cell_sec_code_query_cb_callback *query_cb,
                            gpointer *query_user_data, gpointer user_data);

static GdkFilterReturn
gdk_filter(GdkXEvent *xevent, GdkEvent *event, gpointer data)
{

  XEvent *xev = xevent;

  if (xev->type == ButtonRelease)
    gtk_dialog_response(GTK_DIALOG(_code_ui->dialog), GTK_RESPONSE_CANCEL);

  return GDK_FILTER_CONTINUE;
}

gchar *connui_cell_code_ui_error_note_type_to_text(const char *modem_id,
                                                   const char *note_type)
{
  gchar *text;
  const gchar *model = NULL;

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

  if (text)
  {
    if (modem_id)
      model = connui_cell_modem_get_model(modem_id, NULL);

    if (model)
      text = g_strdup_printf("%s\n%s", model, text);
    else
      text = g_strdup(text);
  }

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
connui_cell_code_ui_launch_note(const gchar *modem_id, cell_code_ui *code_ui,
                                const char *note_type)
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
                            G_TYPE_STRING, modem_id,
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
    gchar *text = connui_cell_code_ui_error_note_type_to_text(
          modem_id, note_type);
    hildon_banner_show_information(NULL, NULL, text);
    g_free(text);
  }
  else
  {
    gchar *text = connui_cell_code_ui_error_note_type_to_text(modem_id,
                                                              note_type);
    code_ui->note = connui_cell_note_new_information(code_ui->parent, text);

    g_free(text);
    gtk_dialog_run(GTK_DIALOG(code_ui->note));
    gtk_widget_destroy(code_ui->note);
    code_ui->note = NULL;
  }
}

static void
connui_cell_code_ui_launch_sim_locked_note(const gchar *modem_id,
                                           cell_code_ui *code_ui)
{
  code_ui->state = CONNUI_CELL_CODE_UI_STATE_SIM_ERROR;
  connui_cell_code_ui_launch_note(modem_id, code_ui, "sim_locked");
}

static void
connui_cell_code_ui_launch_no_sim_note(const gchar *modem_id,
                                       cell_code_ui *code_ui)
{
  code_ui->state = CONNUI_CELL_CODE_UI_STATE_SIM_ERROR;
  connui_cell_code_ui_launch_note(modem_id, code_ui, "no_sim");
}

static void
connui_cell_code_ui_launch_no_pin_note(const gchar *modem_id,
                                       cell_code_ui *code_ui)
{
  code_ui->state = CONNUI_CELL_CODE_UI_STATE_PIN_ERROR;
  connui_cell_code_ui_launch_note(modem_id, code_ui, "no_pin");
}

static void
connui_cell_code_ui_launch_sim_rejected_note(const gchar *modem_id,
                                             cell_code_ui *code_ui)
{
  code_ui->state = CONNUI_CELL_CODE_UI_STATE_SIM_ERROR;
  connui_cell_code_ui_launch_note(modem_id, code_ui, "sim_rejected");
}

gboolean
connui_cell_code_ui_is_sim_locked_with_error(const char *modem_id)
{
  cell_code_ui *code_ui = _code_ui;
  GError *error = NULL;
  gboolean rv = FALSE;

  g_return_val_if_fail(code_ui != NULL, FALSE);

  if (connui_cell_sim_is_locked(modem_id, &error))
  {
      connui_cell_code_ui_launch_sim_locked_note(modem_id, code_ui);
      rv = TRUE;
  }

  g_warning("Reading simlock status failed");

  g_clear_error(&error);

  return rv;
}

static void
connui_cell_code_ui_modem_state_cb(const char *modem_id,
                                   const connui_modem_status *status,
                                   gpointer user_data)
{
  if (!strcmp(modem_id, _code_ui->modem_id))
      *((connui_modem_status *)user_data) = *status;
}

static gboolean
sim_status_timeout_cb(cell_code_ui *code_ui)
{
  g_warning("Waited for better SIM status, but failed. Reporting error");
  code_ui->sim_status_timeout = 0;
  code_ui->state = CONNUI_CELL_CODE_UI_STATE_SIM_ERROR;
  connui_cell_sim_status_close(connui_cell_code_ui_sim_status_cb);

  return FALSE;
}

static gchar *
connui_cell_code_ui_get_code(connui_sim_security_code_type code_type,
                             cell_code_ui *code_ui)
{
  const char *clui_title = NULL;
  gint res = 0;

  if (code_ui->dialog)
  {
    gtk_widget_destroy(code_ui->dialog);
    code_ui->dialog = NULL;

    if (code_ui_filters_count > 0)
    {
      gdk_window_remove_filter(gdk_get_default_root_window(), gdk_filter, NULL);
      code_ui_filters_count--;
    }
  }

  if (code_type == CONNUI_SIM_SECURITY_CODE_PIN)
  {
    switch (code_ui->state)
    {
      case CONNUI_CELL_CODE_UI_STATE_NEW_PIN:
      {
        clui_title = _("conn_ti_enter_new_pin_code");
        break;
      }
      case CONNUI_CELL_CODE_UI_STATE_CONFIRM_PIN:
      {
        clui_title = _("conn_ti_re_enter_new_pin_code");
        break;
      }
      case CONNUI_CELL_CODE_UI_STATE_STARTUP:
      case CONNUI_CELL_CODE_UI_STATE_PIN_ENABLE:
      {
        if (code_ui->get_current_pin)
        {
          clui_title = _("conn_ti_enter_current_pin_code");
          break;
        }
      } /* fallthrough */
      default:
        clui_title = _("conn_ti_enter_pin_code");
        break;
    }
  }
  else if (code_ui->state == CONNUI_CELL_CODE_UI_SIM_UNLOCK)
    clui_title = _("conn_ti_enter_sim_unlock_code");
  else if (code_type == CONNUI_SIM_SECURITY_CODE_PUK)
    clui_title = _("conn_ti_enter_puk_code");

  connui_cell_code_ui_create_dialog(code_ui->modem_id, clui_title, 4);

  while (1)
  {
    GtkWidget *dialog = code_ui->dialog;

    if (!GTK_IS_DIALOG(dialog))
      break;

    if (res == GTK_RESPONSE_DELETE_EVENT || res == GTK_RESPONSE_CANCEL)
      break;

    res = gtk_dialog_run(GTK_DIALOG(dialog));

    if (res == GTK_RESPONSE_OK)
      return clui_code_dialog_get_code(CLUI_CODE_DIALOG(dialog));
  }

  if (!GTK_IS_WIDGET(code_ui->dialog))
    return NULL;

  gtk_widget_destroy(code_ui->dialog);
  code_ui->dialog = NULL;

  if (code_ui_filters_count <= 0)
    return NULL;

  gdk_window_remove_filter(gdk_get_default_root_window(), gdk_filter, NULL);
  code_ui_filters_count--;

  return NULL;
}

static gchar *
connui_cell_code_ui_code_get_new_code(connui_sim_security_code_type code_type,
                                      cell_code_ui *code_ui)
{
  gchar *new_code = NULL;

  if (code_type == CONNUI_SIM_SECURITY_CODE_PUK)
    code_type = CONNUI_SIM_SECURITY_CODE_PIN;

  while (1)
  {
    gchar *confirm_code = NULL;

    code_ui->state = CONNUI_CELL_CODE_UI_STATE_NEW_PIN;
    g_free(new_code);

    new_code = g_strdup(connui_cell_code_ui_get_code(code_type, code_ui));

    if (new_code)
    {
      code_ui->state = CONNUI_CELL_CODE_UI_STATE_CONFIRM_PIN;
      confirm_code = g_strdup(connui_cell_code_ui_get_code(code_type, code_ui));
    }

    if (!new_code || !confirm_code)
    {
      g_free(new_code);
      g_free(confirm_code);
      code_ui->state = CONNUI_CELL_CODE_UI_STATE_PIN_ERROR;
      return NULL;
    }

    if (!strcmp(new_code, confirm_code))
    {
      g_free(confirm_code);
      break;
    }

    g_free(confirm_code);
    g_free(code_ui->pin_message);
    code_ui->pin_message = g_strdup(_("conn_ib_incorrect_pin_change"));
  }

  return new_code;
}

static void
connui_cell_code_ui_pin_correct(cell_code_ui *code_ui)
{
  g_return_if_fail(code_ui != NULL);

  if (GTK_IS_WIDGET(code_ui->dialog))
  {
    gtk_widget_destroy(code_ui->dialog);
    code_ui->dialog = NULL;

    if (code_ui_filters_count > 0)
    {
      gdk_window_remove_filter(gdk_get_default_root_window(), gdk_filter, NULL);
      code_ui_filters_count = 0;
    }
  }
  code_ui->sim_status = CONNUI_SIM_STATUS_OK;
  code_ui->state = CONNUI_CELL_CODE_UI_STATE_OK;
}

static void
connui_cell_code_ui_verify_code_cb(const char *modem_id,
                                   connui_sim_security_code_type code_type,
                                   gboolean ok, gpointer user_data,
                                   GError *error)
{
  cell_code_ui *code_ui = user_data;
  GError *local_error = NULL;

  g_return_if_fail(code_ui != NULL);

  if (ok)
  {
    if (code_ui->show_status_notes)
    {
      static gchar *argv[] = {"pin-code-correct", NULL};
      GPid pid = 0;
      GError *error = NULL;

      if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
                         &pid, &error))
      {
        CONNUI_ERR("%s", error->message);
        g_clear_error(&error);
      }

      g_spawn_close_pid(pid);
    }
    else
      code_ui->verified_ok = TRUE;

    connui_cell_code_ui_pin_correct(code_ui);
    return;
  }

  CONNUI_ERR("%s", error->message);

  if (code_ui->dialog)
  {
    gtk_widget_destroy(code_ui->dialog);
    code_ui->dialog = NULL;

    if (code_ui_filters_count > 0)
    {
      gdk_window_remove_filter(gdk_get_default_root_window(), gdk_filter, NULL);
      code_ui_filters_count--;
    }
  }

  code_ui->pin_verified = TRUE;
  code_ui->sim_status = CONNUI_SIM_STATUS_NO_SIM;

  if (code_type == CONNUI_SIM_SECURITY_CODE_PIN ||
      code_type == CONNUI_SIM_SECURITY_CODE_PUK)
  {
    g_free(code_ui->code);
    code_ui->code = NULL;
  }

  g_free(code_ui->pin_message);

  code_ui->pin_message = NULL;

  if (code_type == CONNUI_SIM_SECURITY_CODE_PIN)
  {
    guint left = connui_cell_sim_verify_attempts_left(
          modem_id, code_type, &local_error);

    if (left == 1 || local_error)
      code_ui->pin_message = g_strdup(_("conn_ib_incorrect_pin_1_tries"));
    else if (left > 1)
    {
      const gchar *message = _("conn_ib_incorrect_pin_2_tries");
      code_ui->pin_message =
          g_strdup_printf(message, left);
    }
    else if (!left)
      code_ui->pin_message = g_strdup(_("conn_ib_incorrect_pin_0_tries"));
  }
  else if (code_type == CONNUI_SIM_SECURITY_CODE_PUK)
  {
    guint left = connui_cell_sim_verify_attempts_left(
          modem_id, code_type, &local_error);

    if (local_error)
      left = 1;

    code_ui->pin_message =
        g_strdup_printf(dngettext("osso-connectivity-ui",
                                  "conn_ib_incorrect_puk_final",
                                  "conn_ib_incorrect_puk", left));
  }

  g_clear_error(&local_error);

  connui_cell_security_code_close(connui_cell_code_ui_code_cb);

  if (!connui_cell_sim_status_register(connui_cell_code_ui_sim_status_cb,
                                       code_ui))
  {
    g_warning("Unable to register SIM status callback");
    connui_cell_code_ui_launch_no_sim_note(modem_id, code_ui);
  }
}

static void
connui_cell_code_ui_code_cb(const char *modem_id,
                            const connui_sim_security_code_type *code_type,
                            gchar ***old_code, gchar ***new_code,
                            cell_sec_code_query_cb_callback *query_cb,
                            gpointer *query_user_data,
                            gpointer user_data)
{
  cell_code_ui *code_ui = user_data;
  gchar *code;

  if (strcmp(modem_id, code_ui->modem_id))
    return;

  g_return_if_fail(
        code_ui != NULL && code_ui->state != CONNUI_CELL_CODE_UI_STATE_NONE);

  if (code_ui->state != CONNUI_CELL_CODE_UI_STATE_STARTUP)
    code_ui->state = CONNUI_CELL_CODE_UI_STATE_PIN_ENABLE;

  if (*old_code && *code_type == CONNUI_SIM_SECURITY_CODE_PIN && code_ui->code)
    **old_code = g_strdup(code_ui->code);
  else
  {
    code = connui_cell_code_ui_get_code(*code_type, code_ui);

    if (!code)
    {
      code_ui->state = CONNUI_CELL_CODE_UI_STATE_PIN_ERROR;
      return;
    }

    **old_code = g_strdup(code);
  }

  if (*new_code)
  {
    code = connui_cell_code_ui_code_get_new_code(*code_type, code_ui);

    if (!code)
    {
      g_free(**old_code);
      **old_code = NULL;
      return;
    }

    **new_code = code;
  }
  else
    code = NULL;

  if (*code_type == CONNUI_SIM_SECURITY_CODE_PIN ||
      *code_type == CONNUI_SIM_SECURITY_CODE_PUK)
  {
    g_free(code_ui->code);

    if (*code_type == CONNUI_SIM_SECURITY_CODE_PUK)
      code_ui->code = g_strdup(code);
    else
    {
      if (!code)
        code_ui->code = g_strdup(**old_code);
      else
        code_ui->code = g_strdup(code);
    }
  }

  *query_user_data = code_ui;
  *query_cb = connui_cell_code_ui_verify_code_cb;

  return;
}

static void
connui_cell_code_ui_sim_status_cb(const char *modem_id,
                                  const connui_sim_status *status,
                                  gpointer user_data)
{
  cell_code_ui *code_ui = user_data;

  g_return_if_fail(code_ui != NULL);

  if (strcmp(modem_id, code_ui->modem_id))
      return;

  if (code_ui->sim_status_timeout)
  {
    g_source_remove(code_ui->sim_status_timeout);
    code_ui->sim_status_timeout = 0;
  }

  code_ui->sim_status = *status;

  switch (*status)
  {
    case CONNUI_SIM_STATUS_OK:
    {
      code_ui->state = CONNUI_CELL_CODE_UI_STATE_OK;

      if (code_ui->note)
      {
        gtk_dialog_response(GTK_DIALOG(code_ui->note), GTK_RESPONSE_OK);
        return;
      }

      break;
    }
    case CONNUI_SIM_STATE_REJECTED: /* not currently supported */
    {
      if (code_ui->show_status_notes)
        connui_cell_code_ui_launch_sim_rejected_note(modem_id, code_ui);
      else
      {
        code_ui->error_note = "sim_rejected";
        code_ui->state = CONNUI_CELL_CODE_UI_STATE_SIM_ERROR;
      }

      break;
    }
    case CONNUI_SIM_STATUS_NO_SIM:
    {
      if (code_ui->state != CONNUI_CELL_CODE_UI_STATE_STARTUP)
      {
        if (code_ui->show_status_notes)
          connui_cell_code_ui_launch_no_sim_note(modem_id, code_ui);
        else
        {
          code_ui->error_note = "no_sim";
          code_ui->state = CONNUI_CELL_CODE_UI_STATE_SIM_ERROR;
        }
      }

      break;
    }
    case CONNUI_SIM_STATUS_OK_PIN_REQUIRED:
    case CONNUI_SIM_STATUS_OK_PUK_REQUIRED:
    {
      if (code_ui->state == CONNUI_CELL_CODE_UI_STATE_STARTUP)
        break;

      if (!code_ui->pin_verified)
      {
        if (code_ui->show_status_notes)
          connui_cell_code_ui_launch_no_pin_note(modem_id, code_ui);
        else
        {
          code_ui->error_note = "no_pin";
          code_ui->state = CONNUI_CELL_CODE_UI_STATE_PIN_RETRY;
        }
      }

      break;
    }
    case CONNUI_SIM_STATE_LOCKED:
    {
      if (code_ui->show_status_notes)
        connui_cell_code_ui_launch_sim_locked_note(modem_id, code_ui);
      else
      {
        code_ui->error_note = "sim_locked";
        code_ui->state = CONNUI_CELL_CODE_UI_STATE_SIM_ERROR;
      }

      break;
    }
    case CONNUI_SIM_STATUS_UNKNOWN:
    {
      if (code_ui->state != CONNUI_CELL_CODE_UI_STATE_STARTUP)
      {
        if (code_ui->show_status_notes)
          connui_cell_code_ui_launch_no_sim_note(modem_id, code_ui);
        else
        {
          code_ui->error_note = "no_sim";
          code_ui->state = CONNUI_CELL_CODE_UI_STATE_SIM_ERROR;
        }

        break;
      }

      g_warning(
            "SIM in error state %d during startup - waiting for better state",
            *status);
      goto wait_better_status;
    }
    default:
    {
      g_warning("Unable to handle SIM status %u", *status);
      g_warning("Waiting for better status");
      goto wait_better_status;
    }
  }

  if (!STATE_IS_ERROR(code_ui->state) &&
      !connui_cell_security_code_register(connui_cell_code_ui_code_cb, code_ui))
  {
    g_warning("Unable to register security code handler, exiting...");
    code_ui->state = CONNUI_CELL_CODE_UI_STATE_SIM_ERROR;
  }

  connui_cell_sim_status_close(connui_cell_code_ui_sim_status_cb);

  return;

wait_better_status:
  code_ui->sim_status_timeout =
      g_timeout_add(60000, (GSourceFunc)sim_status_timeout_cb, code_ui);
}

/*
 * TRUE && fd != -1 - locked
 * TRUE && fd = -1 - CodeUI is already running
 * FALSE - some error has occured
 */
static gboolean
connui_cell_code_ui_lock(int *fd)
{
  gboolean rv = FALSE;
  int res;

  *fd = open(LOCK_FILE, O_RDWR | O_CREAT, 0666);

  if (*fd != -1)
  {
    res = flock(*fd, LOCK_EX | LOCK_NB);

    if (res == -1)
    {
      if (errno == EWOULDBLOCK)
      {
        g_warning("Code UI is already running.");
        rv = TRUE;
      }
      else
        g_warning("flock() on %s failed[%d]", LOCK_FILE, errno);

      close(*fd);
      *fd = -1;
    }
    else
      rv = TRUE;
  }
  else
    g_warning("open() on %s failed[%d]", LOCK_FILE, errno);

  return rv;
}

static void
connui_cell_code_ui_unlock(int fd)
{
  flock(fd, LOCK_UN);
  close(fd);
  unlink(LOCK_FILE);
}

gboolean
connui_cell_code_ui_init(const char *modem_id,
                         GtkWindow *parent,
                         gboolean show_status_notes)
{
  cell_code_ui *code_ui = _code_ui;
  int fd;
  int rv = FALSE;

  if (!connui_cell_code_ui_lock(&fd) || fd == -1)
    return rv;

  if (!code_ui)
  {
    code_ui = g_new0(cell_code_ui, 1);
    _code_ui = code_ui;
  }

  code_ui->modem_id = g_strdup(modem_id);
  code_ui->parent = parent;
  code_ui->show_status_notes = show_status_notes;

  if (show_status_notes)
  {
    connui_modem_status modem_status = CONNUI_MODEM_STATUS_UNKNOWN;

    code_ui->state = CONNUI_CELL_CODE_UI_STATE_STARTUP;

    if (!connui_cell_modem_status_register(connui_cell_code_ui_modem_state_cb,
                                           &modem_status))
    {
      goto out;
    }

    while (modem_status == CONNUI_MODEM_STATUS_UNKNOWN)
      g_main_context_iteration(NULL, TRUE);

    connui_cell_modem_status_close(connui_cell_code_ui_modem_state_cb);

    if (modem_status != CONNUI_MODEM_STATUS_ONLINE)
    {
      GtkWidget *note;
      const char *note_type;
      gchar *text;

      if (modem_status == CONNUI_MODEM_STATUS_POWERED)
        note_type = "modem_failure";
      else
        note_type = "modem_poweroff";

      text = connui_cell_code_ui_error_note_type_to_text(modem_id, note_type);
      note = connui_cell_note_new_information(code_ui->parent, text);
      g_free(text);
      code_ui->note = note;
      gtk_dialog_run(GTK_DIALOG(note));
      gtk_widget_destroy(code_ui->note);
      code_ui->note = NULL;
      goto out;
    }
  }
  else
  {
    code_ui->state = CONNUI_CELL_CODE_UI_STATE_NONE;

    if (!connui_cell_modem_is_powered(modem_id, NULL))
    {
      GtkWidget *note;
      gchar *text = connui_cell_code_ui_error_note_type_to_text(
            modem_id, "no_network");

      g_warning("Phone is deactivated. Phone settings will not be opened.");
      note = connui_cell_note_new_information(code_ui->parent, text);
      g_free(text);
      gtk_dialog_run(GTK_DIALOG(note));
      gtk_widget_destroy(note);
      connui_cell_code_ui_destroy();
      goto out;
    }
  }

  if (!connui_cell_sim_status_register(connui_cell_code_ui_sim_status_cb,
                                       code_ui))
  {
    g_warning("Unable to register SIM status callback");

    if (!show_status_notes)
      connui_cell_code_ui_launch_no_sim_note(modem_id, code_ui);

    connui_cell_code_ui_destroy();
    goto out;
  }

  while (!STATE_IS_ERROR(code_ui->state) &&
         code_ui->state != CONNUI_CELL_CODE_UI_STATE_OK)
  {
    g_main_context_iteration(NULL, TRUE);
  }

  if (code_ui->state == CONNUI_CELL_CODE_UI_STATE_OK)
    rv = TRUE;
  else
    connui_cell_code_ui_destroy();

out:
  connui_cell_code_ui_unlock(fd);

  return rv;
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

    g_free(_code_ui->modem_id);
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
      if (code_ui->show_status_notes)
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

static GtkWidget *
find_done_button(GtkWidget *parent)
{
  GList *l = gtk_container_get_children(GTK_CONTAINER(parent));

  for (; l; l = l->next)
  {
    gpointer child = l->data;

    if (GTK_IS_CONTAINER(child))
    {
      child = find_done_button(child);

      if (child)
        return child;
    }
    else if (GTK_IS_BUTTON(child))
    {
      const gchar *label = gtk_button_get_label(GTK_BUTTON(child));

      if (label && !strcmp(label, dgettext("hildon-libs", "wdgt_bd_done")))
          return child;
    }
  }

  return NULL;
}

static gboolean
get_em_mode(cell_code_ui *code_ui)
{
  GStrv em_numbers = code_ui->emergency_numbers;
  int i = 0;

  if (!code_ui->clui_em_number)
    return FALSE;

  while (em_numbers[i])
  {
    if (!strcmp(code_ui->clui_em_number, em_numbers[i]))
      return TRUE;

    i++;
  }

  return FALSE;
}

static void
connui_cell_code_ui_dialog_input(cell_code_ui *code_ui, gchar *digit_str,
                                 CluiCodeDialog *code_dialog)
{
  GtkWidget *done_button = NULL;

  g_return_if_fail(code_ui != NULL && code_ui->dialog != NULL &&
      code_ui->emergency_numbers != NULL && digit_str != NULL);

  if (strcmp(digit_str, "BSP")) /* BackSPace ;) */
  {
    gchar *code = clui_code_dialog_get_code(CLUI_CODE_DIALOG(code_ui->dialog));

    g_free(code_ui->clui_em_number);
    code_ui->clui_em_number = g_strconcat(code, digit_str, NULL);
    g_free(code);
  }
  else if (*code_ui->clui_em_number)
      code_ui->clui_em_number[strlen(code_ui->clui_em_number) - 1] = '\0';

  clui_code_dialog_set_emergency_mode(CLUI_CODE_DIALOG(code_ui->dialog),
                                      get_em_mode(code_ui));

  if (GTK_IS_DIALOG(code_ui->dialog))
    done_button = find_done_button(GTK_DIALOG(code_ui->dialog)->action_area);

  if (done_button)
  {
    if (code_ui->clui_em_number &&
        strlen(code_ui->clui_em_number) >= code_ui->code_min_len)
    {
      gtk_widget_set_sensitive(done_button, TRUE);
    }
    else
      gtk_widget_set_sensitive(done_button, FALSE);
  }
}

GtkWidget *
connui_cell_code_ui_create_dialog(const char *modem_id,
                                  const gchar *title, int code_min_len)
{
  GtkWidget *dialog;
  char *cancel_button_label;

  if (!_code_ui)
    _code_ui = g_new0(cell_code_ui, 1);

  g_return_val_if_fail(_code_ui->dialog == NULL, NULL);

  dialog = clui_code_dialog_new(TRUE);
  clui_code_dialog_set_max_code_length(CLUI_CODE_DIALOG(dialog), 8);

  if (_code_ui->show_status_notes)
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

  if (!_code_ui->emergency_numbers)
  {
    _code_ui->emergency_numbers =
        connui_cell_emergency_get_numbers(modem_id, NULL);
  }

  g_signal_connect_swapped(G_OBJECT(dialog), "input",
                           (GCallback)connui_cell_code_ui_dialog_input, _code_ui);
  g_signal_connect(G_OBJECT(dialog), "response",
                   (GCallback)connui_cell_code_ui_dialog_response, _code_ui);

  _code_ui->dialog = dialog;

  return dialog;
}

/* No support for simlock until we find a device we can test on */
#if 0
gboolean
connui_cell_code_ui_deactivate_simlock()
{
  gboolean rv = FALSE;
  gchar *pin_code;
  int has_error = 0;

  g_return_val_if_fail(_code_ui != NULL, FALSE);

  if (!connui_cell_sim_is_locked(&has_error) && !has_error)
    return TRUE;

  if (has_error)
  {
    g_warning("Reading simlock status failed");
    return FALSE;
  }

  connui_cell_code_ui_launch_sim_locked_note(_code_ui);

  do
  {
    _code_ui->state = CONNUI_CELL_CODE_UI_SIM_UNLOCK;
    pin_code = connui_cell_code_ui_get_code(0xFFFF, _code_ui);

    if (!pin_code)
      break;

    rv = connui_cell_sim_deactivate_lock(pin_code, &has_error);

    if (has_error)
      g_warning("Error %d occurred while deactivating simlock", has_error);
  }
  while (!rv);

  return rv;
}
#endif

gboolean
connui_cell_code_ui_change_code(connui_sim_security_code_type code_type)
{
  gboolean ok = FALSE;

  g_return_val_if_fail(_code_ui != NULL, FALSE);

  _code_ui->verified_ok = FALSE;

  if (connui_cell_code_ui_update_sim_status())
    g_warning("Unable to get sim status");

  if (STATE_IS_ERROR(_code_ui->state))
    return FALSE;

  if (_code_ui->verified_ok)
    return TRUE;

  _code_ui->verified_ok = FALSE;
  _code_ui->get_current_pin = TRUE;

  do
  {
    ok = connui_cell_security_code_change(_code_ui->modem_id,
                                          code_type, NULL);

    while (_code_ui->sim_status != CONNUI_SIM_STATUS_OK)
    {
      if (STATE_IS_ERROR(_code_ui->state))
        break;

      g_main_context_iteration(NULL, TRUE);
    }
  }
  while (!ok && !STATE_IS_ERROR(_code_ui->state) && !_code_ui->verified_ok);

  _code_ui->get_current_pin = FALSE;

  return ok;
}

gboolean
connui_cell_code_ui_update_sim_status()
{
  gboolean rv = FALSE;

  g_return_val_if_fail(_code_ui != NULL, FALSE);

  if (connui_cell_sim_status_register(connui_cell_code_ui_sim_status_cb,
                                      _code_ui))
  {
    _code_ui->state = CONNUI_CELL_CODE_UI_STATE_STARTUP;

    do
    {
      g_main_context_iteration(NULL, TRUE);
    }
    while (!STATE_IS_ERROR(_code_ui->state) &&
           _code_ui->state != CONNUI_CELL_CODE_UI_STATE_OK);

    rv = TRUE;
  }
  else
    g_warning("Unable to register SIM status callback");

  return rv;
}

gboolean
connui_cell_code_ui_set_current_code_active(gboolean active)
{
  gboolean rv;

  g_return_val_if_fail(_code_ui != NULL, FALSE);

  if (!connui_cell_code_ui_update_sim_status())
    g_warning("Unable to get sim status");

  if (STATE_IS_ERROR(_code_ui->state))
    return FALSE;

  _code_ui->get_current_pin = TRUE;

  while ((_code_ui->sim_status == CONNUI_SIM_STATUS_OK_PUK_REQUIRED ||
          !(rv = connui_cell_security_code_set_enabled(_code_ui->modem_id,
                                                       active, NULL))) &&
         !STATE_IS_ERROR(_code_ui->state))
  {
    g_main_context_iteration(NULL, TRUE);
  }

  _code_ui->get_current_pin = FALSE;

  return rv;
}
