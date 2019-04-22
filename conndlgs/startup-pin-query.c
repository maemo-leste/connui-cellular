#include <libosso.h>
#include <hildon/hildon.h>
#include <dbus/dbus-glib.h>
#include <connui/connui-flightmode.h>
#include <conbtui/gateway/common.h>

#include <libintl.h>

#include "connui-cellular.h"

#include "config.h"

#define _(msgid) dgettext(GETTEXT_PACKAGE, msgid)

static gboolean exit_flightmode ;

static gboolean
initialize_sim_cb(gpointer user_data)
{
  if (connui_cell_code_ui_init(NULL, TRUE))
  {
    connui_cell_code_ui_is_sim_locked_with_error();
    connui_cell_code_ui_destroy();
  }
  else
    g_warning("Unable to initialize SIM");

  if (exit_flightmode)
  {
    if (!connui_flightmode_off())
      g_warning("Unable to turn flightmode off");
  }

  gtk_main_quit();

  return FALSE;
}

static void
flightmode_status_cb(dbus_bool_t offline, gpointer user_data)
{
  if (offline)
  {
    if (!gateway_common_show_flight_mode(NULL))
      gtk_main_quit();

    exit_flightmode = TRUE;
  }

  g_idle_add(initialize_sim_cb, NULL);
}

int
main(int argc, char **argv)
{
  setlocale(LC_ALL, "");
  bindtextdomain(GETTEXT_PACKAGE, "/usr/share/locale");
  textdomain(GETTEXT_PACKAGE);
  hildon_gtk_init(&argc, &argv);
  dbus_g_thread_init();

  if (!connui_flightmode_status(flightmode_status_cb, NULL))
    g_warning("Unable to register flightmode status!");
  gtk_main();

  return 0;
}
