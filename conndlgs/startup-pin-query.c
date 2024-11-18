#include <libosso.h>
#include <hildon/hildon.h>
#include <dbus/dbus-glib.h>
#include <connui/connui-flightmode.h>
#include <conbtui/gateway/common.h>

#include <libintl.h>
#include <locale.h>

#include "connui-cellular.h"

#include "config.h"

static gboolean exit_flightmode;

static gboolean
initialize_sim_cb(gpointer user_data)
{
  const char *modem_id = user_data;

  if (connui_cell_code_ui_init(modem_id, NULL, TRUE))
  {
    connui_cell_code_ui_is_sim_locked_with_error(modem_id);
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

  g_idle_add(initialize_sim_cb, user_data);
}

int
main(int argc, char **argv)
{
  char *modem_id = NULL;

  setlocale(LC_ALL, "");
  bindtextdomain(GETTEXT_PACKAGE, "/usr/share/locale");
  textdomain(GETTEXT_PACKAGE);
  hildon_gtk_init(&argc, &argv);
  dbus_g_thread_init();

  if (argc > 1)
    modem_id = argv[1];

  if (!connui_flightmode_status(flightmode_status_cb, modem_id))
    g_warning("Unable to register flightmode status!");

  gtk_main();

  return 0;
}
