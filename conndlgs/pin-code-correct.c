#include <hildon/hildon.h>

#include <libintl.h>

#include "config.h"

#define _(msgid) dgettext(GETTEXT_PACKAGE, msgid)

static gboolean
pin_correct_timeout(gpointer user_data)
{
  gtk_main_quit();

  return FALSE;
}

int
main(int argc, char **argv)
{
  GtkWidget *banner;
  guint interval;

  hildon_gtk_init(&argc, &argv);
  banner = hildon_banner_show_information(NULL, NULL, _("conn_ib_correct_pin"));

  if (!banner)
    return -1;

  g_object_get(G_OBJECT(banner), "timeout", &interval, NULL);
  g_timeout_add(interval, pin_correct_timeout, NULL);
  gtk_main();

  return 0;
}
