#include <hildon/hildon.h>
#include <connui/connui.h>
#include <connui/connui-log.h>
#include <icd/dbus_api.h>

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
        const gchar *msgid = "conn_ib_network_selected";
        gchar *net_name = g_strdup_printf(_(msgid), name);
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

static void
inetstate_status_cb(enum inetstate_status status, network_entry *network,
                    gpointer user_data)
{
  cell_network *cnet = user_data;

  /* fmg - not really sure about the condition here, please revisit if needed */
  if (!network ||
      (status != INETSTATE_STATUS_CONNECTING &&
       status != INETSTATE_STATUS_CONNECTED) ||
      g_strcmp0(network->network_type, "GPRS"))
  {
    connui_inetstate_close(inetstate_status_cb);
    cellular_net_selection_select(cnet);
    connui_cell_network_free(cnet);
  }
  else
    iap_network_entry_disconnect(ICD_CONNECTION_FLAG_UI_EVENT, network);
}

void
cellular_net_selection_select_current()
{
  GtkTreeSelection *selection;
  GtkTreeIter iter;
  GtkTreeModel *model = NULL;
  cell_network *cnet = NULL;

  g_return_if_fail(selection_dialog != NULL);

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));

  if (gtk_tree_selection_get_selected(selection, &model, &iter))
    gtk_tree_model_get(model, &iter, 2, &cnet, -1);

  if (!cnet)
    return;

  cnet = connui_cell_network_dup(cnet);
  gtk_dialog_set_response_sensitive(GTK_DIALOG(selection_dialog),
                                    GTK_RESPONSE_OK, FALSE);

  if (banner)
    gtk_widget_destroy(banner);

  banner = hildon_banner_show_animation(selection_dialog, NULL,
                                        _("conn_pb_connecting"));

  if (!connui_inetstate_status(inetstate_status_cb, cnet))
    g_warning("Unable to query inetstate");
}

static GType connui_cell_network_type()
{
  static GType type = 0;

  if (G_UNLIKELY (!type))
  {
      type = g_boxed_type_register_static (
            "ConnuiCellNetwork",
            (GBoxedCopyFunc) connui_cell_network_dup,
            (GBoxedFreeFunc) connui_cell_network_free);
  }

  return type;
}

GtkDialog *
cellular_net_selection_show(GtkWindow *parent, GCallback response_cb,
                            gpointer user_data)
{
  if (connecting)
  {
    connecting = FALSE;
    connui_cell_net_cancel_select(connui_cell_net_select_cb);
  }

  if (!selection_dialog)
  {
    GtkTreeViewColumn *col_umts;
    GtkTreeViewColumn *col_name;
    GtkWidget *window = gtk_scrolled_window_new(NULL, NULL);
    GtkListStore *list_store = gtk_list_store_new(3, GDK_TYPE_PIXBUF,
                                                  G_TYPE_STRING,
                                                  connui_cell_network_type());

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(window),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    selection_dialog =
        gtk_dialog_new_with_buttons(
          _("conn_ti_phone_sel_cell_network"), parent,
          GTK_DIALOG_NO_SEPARATOR | GTK_DIALOG_DESTROY_WITH_PARENT |
          GTK_DIALOG_MODAL, _("conn_bd_dialog_ok"), GTK_RESPONSE_OK, NULL);

    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(list_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree_view), FALSE);
    gtk_tree_selection_set_mode(
          gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view)),
          GTK_SELECTION_SINGLE);

    col_umts = gtk_tree_view_column_new_with_attributes(
          "UMTS", gtk_cell_renderer_pixbuf_new(), "pixbuf", 0, NULL);

    col_name = gtk_tree_view_column_new_with_attributes(
          "Name", gtk_cell_renderer_text_new(), "text", 1, NULL);
    gtk_tree_view_column_set_expand(col_name, TRUE);

    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_umts);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col_name);

    gtk_container_add(GTK_CONTAINER(window), tree_view);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(selection_dialog)->vbox),
                      window);
    gtk_widget_set_size_request(GTK_WIDGET(window), 410, 180);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(selection_dialog),
                                      GTK_RESPONSE_OK, FALSE);
    iap_common_set_close_response(selection_dialog, GTK_RESPONSE_CANCEL);
    g_signal_connect(G_OBJECT(selection_dialog), "response", response_cb,
                     user_data);
  }

  unknown_bool_2 = FALSE;

  if (!pixbuf_cache)
    pixbuf_cache = connui_pixbuf_cache_new();

  if (unknown_bool_1)
  {
    struct timespec tp;

    clock_gettime(1, &tp);

    if (tp.tv_sec - net_list_ts.tv_sec <= 20)
    {
      gtk_dialog_set_response_sensitive(GTK_DIALOG(selection_dialog),
                                        GTK_RESPONSE_OK, TRUE);
    }
    else
    {
      gtk_list_store_clear(
            GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(tree_view))));
      unknown_bool_1 = FALSE;
      gtk_dialog_set_response_sensitive(GTK_DIALOG(selection_dialog),
                                        GTK_RESPONSE_OK, FALSE);
    }
  }

  gtk_widget_show_all(selection_dialog);

  if (!net_list_in_progress && !unknown_bool_1)
  {
    net_list_in_progress = TRUE;

    if (connui_cell_net_list(connui_cell_net_list_cb, NULL))
    {
      if (banner)
        gtk_widget_destroy(banner);

      banner = hildon_banner_show_animation(selection_dialog, NULL,
                                            _("conn_pb_searching"));
    }
    else
      net_list_in_progress = 0;
  }

  return GTK_DIALOG(selection_dialog);
}
