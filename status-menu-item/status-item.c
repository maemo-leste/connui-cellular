#include <connui/connui.h>
#include <libhildondesktop/libhildondesktop.h>
#include <icd/dbus_api.h>
#include <osso-log.h>

#include <libintl.h>
#include <string.h>

#include "connui-cellular.h"

#include "config.h"

#define _(x) dgettext(GETTEXT_PACKAGE, x)

#define CONNUI_CELLULAR_STATUS_ITEM_TYPE (connui_cellular_status_item_get_type())
#define CONNUI_CELLULAR_STATUS_ITEM(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), CONNUI_CELLULAR_STATUS_ITEM_TYPE, ConnuiCellularStatusItem))

typedef struct _ConnuiCellularStatusItem ConnuiCellularStatusItem;
typedef struct _ConnuiCellularStatusItemClass ConnuiCellularStatusItemClass;
typedef struct _ConnuiCellularStatusItemPrivate ConnuiCellularStatusItemPrivate;

struct _ConnuiCellularStatusItem
{
  HDStatusMenuItem parent;
  ConnuiCellularStatusItemPrivate *priv;
};

struct _ConnuiCellularStatusItemClass
{
  HDStatusMenuItemClass parent;
};

struct _ConnuiCellularStatusItemPrivate
{
  cell_network_state state;
  guint sim_status;
  gboolean offline;
  osso_context_t *osso_context;
  ConnuiPixbufCache *pixbuf_cache;
  osso_display_state_t display_state;
  int needs_display_state_update;
};

static gchar *current_mode_icon;
static gchar *current_bars_icon;

HD_DEFINE_PLUGIN_MODULE_EXTENDED(ConnuiCellularStatusItem,
                        connui_cellular_status_item,
                        HD_TYPE_STATUS_MENU_ITEM,
                        G_ADD_PRIVATE_DYNAMIC(ConnuiCellularStatusItem), , );

static void
connui_cellular_status_item_class_finalize(ConnuiCellularStatusItemClass *klass)
{
}

static void
connui_cellular_status_item_update_icon(ConnuiCellularStatusItem *item)
{
  GdkPixbuf *pixbuf;
  GdkPixbuf *bars_icon_pixbuf;
  GdkPixbuf *bars_icon_pixbuf2;
  GdkPixbuf *mode_icon_pixbuf;
  gchar *mode_icon = NULL;
  gchar *bars_icon;
  ConnuiCellularStatusItemPrivate *priv = item->priv;

  if (priv->display_state == OSSO_DISPLAY_OFF)
  {
    priv->needs_display_state_update = TRUE;
    return;
  }

  guint sim_status = priv->sim_status;

  if (sim_status != 3 && sim_status != 0 && sim_status != 5)
  {
    switch (priv->state.reg_status)
    {
      case NETWORK_REG_STATUS_HOME:
      case NETWORK_REG_STATUS_ROAM:
      case NETWORK_REG_STATUS_ROAM_BLINK:
      {
        char rat = priv->state.rat_name;

        if (rat == NETWORK_LTE_RAT)
        {
            mode_icon = "statusarea_cell_mode_4g";
        } else if (rat == NETWORK_UMTS_RAT)
        {
          if (priv->state.network_hsdpa_allocated == 1)
            mode_icon = "statusarea_cell_mode_3_5g";
          else
            mode_icon = "statusarea_cell_mode_3g";
        }
        else if (rat == NETWORK_GSM_RAT)
        {
          if (priv->state.supported_services & NETWORK_MASK_EGPRS_SUPPORT)
            mode_icon = "statusarea_cell_mode_2_5g";
          else
            mode_icon = "statusarea_cell_mode_2g";
        }
        else
        {
          g_warning("status->rat unknown %hhu!", rat);
          mode_icon = NULL;
        }

        char bars = priv->state.network_signals_bar;

        if (bars > 80)
          bars_icon = "statusarea_cell_level5";
        else if (bars > 60)
          bars_icon = "statusarea_cell_level4";
        else if (bars > 40)
          bars_icon = "statusarea_cell_level3";
        else if (bars > 20)
          bars_icon = "statusarea_cell_level2";
        else if (priv->state.network_signals_bar)
          bars_icon = "statusarea_cell_level1";
        else
          bars_icon = "statusarea_cell_level0";

        break;
      }
      case NETWORK_REG_STATUS_NOSERV_NOSIM:
      {
        mode_icon = 0;
        bars_icon = "statusarea_cell_off";
        break;
      }
      default:
      {

        mode_icon = 0;
        bars_icon = "statusarea_cell_level0";
        break;
      }
    }
  }
  else
  {
    mode_icon = 0;
    bars_icon = "statusarea_cell_off";
  }

  if (priv->offline)
  {
    mode_icon = "statusarea_offline_mode";
    bars_icon = "statusarea_cell_level0";
  }

  if (bars_icon != current_bars_icon || mode_icon != current_mode_icon)
  {
    pixbuf = gdk_pixbuf_new(0, 1, 8, 18, 36);
    gdk_pixbuf_fill(pixbuf, 0);

    if (mode_icon)
    {
      mode_icon_pixbuf = connui_pixbuf_cache_get(priv->pixbuf_cache, mode_icon, 11);
      bars_icon_pixbuf = connui_pixbuf_cache_get(priv->pixbuf_cache, bars_icon, 25);
      gdk_pixbuf_composite(bars_icon_pixbuf, pixbuf, 0, 0, 18, 25, 0.0, 0.0, 1.0, 1.0, 0, 255);
      if (mode_icon_pixbuf)
        gdk_pixbuf_composite(mode_icon_pixbuf, pixbuf, 0, 25, 18, 11, 0.0, 25.0, 1.0, 1.0, 0, 255);
    }
    else
    {
      bars_icon_pixbuf2 = connui_pixbuf_cache_get(priv->pixbuf_cache, bars_icon, 25);
      gdk_pixbuf_composite(bars_icon_pixbuf2, pixbuf, 0, 0, 18, 25, 0.0, 0.0, 1.0, 1.0, 0, 255);
    }

    hd_status_plugin_item_set_status_area_icon(HD_STATUS_PLUGIN_ITEM(item), pixbuf);
    g_object_unref((gpointer)pixbuf);
    current_mode_icon = mode_icon;
    current_bars_icon = bars_icon;
  }
}

static void
connui_cellular_status_item_display_cb(osso_display_state_t state,
                                       gpointer user_data)
{
  ConnuiCellularStatusItem *item = CONNUI_CELLULAR_STATUS_ITEM(user_data);
  ConnuiCellularStatusItemPrivate *priv;

  g_return_if_fail(item != NULL && item->priv != NULL);

  priv = item->priv;
  priv->display_state = state;

  if (state == OSSO_DISPLAY_ON && priv->needs_display_state_update)
  {
    connui_cellular_status_item_update_icon(item);
    priv->needs_display_state_update = FALSE;
  }
}

static void
connui_cellular_status_item_flightmode_cb(dbus_bool_t offline,
                                          gpointer user_data)
{
  ConnuiCellularStatusItem *item =
      CONNUI_CELLULAR_STATUS_ITEM(user_data);

  g_return_if_fail(item != NULL && item->priv != NULL);

  item->priv->offline = offline;
  connui_cellular_status_item_update_icon(item);
}

static void
connui_cellular_status_item_pin_query()
{
  static const char *spq = BINDIR "/startup-pin-query";
  pid_t pid = fork();

  g_return_if_fail(pid != -1);

  if (!pid)
  {
    execl(spq, spq, NULL);
    exit(1);
  }
}

static void
connui_cellular_status_item_sim_status_cb(guint sim_status, gpointer user_data)
{
  ConnuiCellularStatusItem *item =
      CONNUI_CELLULAR_STATUS_ITEM(user_data);

  g_return_if_fail(item != NULL && item->priv != NULL);

  if (sim_status == 7 || sim_status == 8)
    connui_cellular_status_item_pin_query();

  item->priv->sim_status = sim_status;
  connui_cellular_status_item_update_icon(item);
}

static void
connui_cellular_status_item_net_status_cb(const cell_network_state *state,
                                          gpointer user_data)
{
  ConnuiCellularStatusItem *item = CONNUI_CELLULAR_STATUS_ITEM(user_data);

  if (state)
    item->priv->state = *state;
  else
    memset(&item->priv->state, 0, sizeof(item->priv->state));

  connui_cellular_status_item_update_icon(item);
}

static void
connui_cellular_status_item_finalize(GObject *self)
{
  ConnuiCellularStatusItemPrivate *priv =
      CONNUI_CELLULAR_STATUS_ITEM(self)->priv;

  if (priv->osso_context)
  {
    osso_deinitialize(priv->osso_context);
    priv->osso_context = 0;
  }

  if (priv->pixbuf_cache)
  {
    connui_pixbuf_cache_destroy(priv->pixbuf_cache);
    priv->pixbuf_cache = 0;
  }

  connui_cell_net_status_close(connui_cellular_status_item_net_status_cb);
  connui_cell_sim_status_close(connui_cellular_status_item_sim_status_cb);
  connui_flightmode_close(connui_cellular_status_item_flightmode_cb);
  
  G_OBJECT_CLASS(connui_cellular_status_item_parent_class)->finalize(self);
}

static void
connui_cellular_status_item_class_init(ConnuiCellularStatusItemClass *klass)
{
  G_OBJECT_CLASS(klass)->finalize = connui_cellular_status_item_finalize;
}

static void
connui_cellular_status_item_init(ConnuiCellularStatusItem *self)
{
  ConnuiCellularStatusItemPrivate *priv = (ConnuiCellularStatusItemPrivate*)connui_cellular_status_item_get_instance_private(self);

  self->priv = priv;
  priv->pixbuf_cache = connui_pixbuf_cache_new();
  priv->osso_context = osso_initialize("connui_cellular_status_item",
                                       PACKAGE_VERSION, TRUE, 0);

   osso_hw_set_display_event_cb(priv->osso_context,
                                connui_cellular_status_item_display_cb, self);

  if (!connui_flightmode_status(connui_cellular_status_item_flightmode_cb, self))
    g_warning("Unable to register flightmode status!");

  if (!connui_cell_sim_status_register(connui_cellular_status_item_sim_status_cb, self))
    g_warning("Unable to register SIM status");

  if (!connui_cell_net_status_register(connui_cellular_status_item_net_status_cb, self))
    g_warning("Unable to register cell net status!");

  connui_cellular_status_item_update_icon(self);
}

