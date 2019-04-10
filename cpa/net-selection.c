#include <hildon/hildon.h>
#include <connui/connui-pixbuf-cache.h>
#include <connui/connui-log.h>

#include <libintl.h>

#include "connui-cellular.h"
#include "net-selection.h"

#include "config.h"

#define _(s) dgettext(GETTEXT_PACKAGE, s)

static gboolean net_list_in_progress = FALSE;
static gboolean unknown_bool_1 = FALSE;
static gboolean unknown_bool_2 = FALSE;
static gboolean connecting = FALSE;
static GtkWidget *selection_dialog = NULL;
static GtkWidget *tree_view = NULL;
static ConnuiPixbufCache *pixbuf_cache;
static cell_network *current_network = NULL;
static GtkWidget *banner = NULL;
static struct timespec net_list_ts = {0};

static void
connui_cell_net_list_cb(GSList *networks, gpointer user_data)
{
  GSList *l;
  GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree_view));
  GtkTreeSelection *selection =
      gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
  GdkPixbuf *pixbuf;
  GtkTreeIter iter;

  net_list_in_progress = FALSE;

  if (banner)
  {
    gtk_widget_destroy(banner);
    banner = NULL;
  }

  pixbuf = gdk_pixbuf_new(0, 1, 8, 20, 20);
  gdk_pixbuf_fill(pixbuf, 0);

  for (l = networks; l; l = l->next)
  {
    const char *mode_icon;
    GdkPixbuf *icon;
    cell_network *net = l->data;
    gchar *operator_name = net->operator_name;

    if (!operator_name)
      operator_name = g_strdup_printf("%s %s", net->country_code, net->operator_code);

    if (!net->umts_avail)
      mode_icon = "statusarea_cell_mode_2g";
    else
      mode_icon = "statusarea_cell_mode_3g";

    icon = connui_pixbuf_cache_get(pixbuf_cache, mode_icon, 20);
    gdk_pixbuf_composite(icon, pixbuf, 0, 0, 20, 20, 0.0, 0.0, 1.0, 1.0, 0, 255);
    gtk_list_store_insert_with_values(GTK_LIST_STORE(model), &iter, 0x7FFFFFFF,
                                      0, icon,
                                      1, operator_name,
                                      2, net,
                                      -1);

    if (!net->operator_name)
      g_free(operator_name);

    connui_cell_network_free(net);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(selection_dialog),
                                      GTK_RESPONSE_OK, TRUE);

    if (!gtk_tree_selection_get_selected(selection, NULL, NULL))
      gtk_tree_selection_select_iter(selection, &iter);
  }

  clock_gettime(1, &net_list_ts);
  unknown_bool_1 = TRUE;
}

void
cellular_net_selection_destroy()
{
  if (net_list_in_progress)
  {
    connui_cell_net_cancel_list(connui_cell_net_list_cb);
    net_list_in_progress = FALSE;
  }

  unknown_bool_1 = FALSE;

  if (selection_dialog)
  {
    gtk_widget_destroy(selection_dialog);
    selection_dialog = NULL;
    tree_view = NULL;
  }

  if (current_network)
    connui_cell_network_free(current_network);

  current_network = NULL;

  if (pixbuf_cache)
    connui_pixbuf_cache_destroy(pixbuf_cache);

  pixbuf_cache = NULL;
}

void
cellular_net_clear_cache()
{
  if (tree_view)
    gtk_list_store_clear(GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(tree_view))));

  unknown_bool_1 = FALSE;
}

void
cellular_net_selection_reset_network()
{
  if (unknown_bool_2)
  {
    ULOG_INFO("Cell reset network");
    connui_cell_reset_network();
  }
}

static void
connui_cell_net_select_cb(gboolean success, guint network_reject_code,
                          gpointer user_data)
{
  if (!connecting)
    return;

  if (banner)
  {
    gtk_widget_destroy(banner);
    banner = NULL;
  }

  if (success)
  {
    if (selection_dialog)
    {
      gchar *name =
          connui_cell_net_get_operator_name(current_network, TRUE, NULL);

      if (name)
      {
        gchar *net_name = g_strdup_printf(_("conn_ib_network_selected"), name);
        GtkWindow *parent =
            gtk_window_get_transient_for(GTK_WINDOW(selection_dialog));

        hildon_banner_show_information(GTK_WIDGET(parent), NULL, net_name);
        g_free(net_name);
      }

      g_free(name);
    }
  }
  else
  {
    connui_cell_network_free(current_network);
    current_network = NULL;

    if (selection_dialog)
    {
      GtkWidget *note;

      unknown_bool_2 = TRUE;
      note = hildon_note_new_information(GTK_WINDOW(selection_dialog),
                                         _("conn_ni_no_access"));
      gtk_dialog_run(GTK_DIALOG(note));
      gtk_widget_destroy(note);
    }
  }

  if (success || (!success && !selection_dialog))
    cellular_net_selection_hide();

  gtk_dialog_set_response_sensitive(GTK_DIALOG(selection_dialog),
                                    GTK_RESPONSE_OK, TRUE);

  connecting = FALSE;
}

gboolean
cellular_net_selection_select(cell_network *net)
{
  gboolean rv;

  connecting = TRUE;

  if ((rv = connui_cell_net_select(net, connui_cell_net_select_cb, NULL)))
  {
    if (banner)
      gtk_widget_destroy(banner);

    banner = hildon_banner_show_animation(selection_dialog, 0,
                                          _("conn_pb_connecting"));
  }
  else
    connecting = FALSE;

  if (current_network)
    connui_cell_network_free(current_network);

  current_network = connui_cell_network_dup(net);

  return rv;
}

gboolean
cellular_net_selection_select_automatic()
{
  return cellular_net_selection_select(NULL);
}

void
cellular_net_selection_hide()
{
  if (net_list_in_progress)
  {
    connui_cell_net_cancel_list(connui_cell_net_list_cb);
    net_list_in_progress = FALSE;
  }

  if (connecting)
  {
    connui_cell_net_cancel_select(NULL);
    connecting = FALSE;
    unknown_bool_2 = TRUE;
  }

  if (banner)
  {
    gtk_widget_destroy(banner);
    banner = NULL;
  }

  gtk_widget_hide_all(GTK_WIDGET(selection_dialog));
}
