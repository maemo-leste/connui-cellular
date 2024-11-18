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
#define PRIVATE(o) \
  ((ConnuiCellularStatusItemPrivate *)connui_cellular_status_item_get_instance_private((ConnuiCellularStatusItem *)o))

typedef struct _ConnuiCellularStatusItem ConnuiCellularStatusItem;
typedef struct _ConnuiCellularStatusItemClass ConnuiCellularStatusItemClass;
typedef struct _ConnuiCellularStatusItemPrivate ConnuiCellularStatusItemPrivate;

struct _ConnuiCellularStatusItem
{
  HDStatusMenuItem parent;
};

struct _ConnuiCellularStatusItemClass
{
  HDStatusMenuItemClass parent;
};

struct _ConnuiCellularModem
{
  gchar *id;
  cell_network_state state;
  connui_sim_status sim_status;
  const gchar *mode;
  const gchar *bars;
};

typedef struct _ConnuiCellularModem ConnuiCellularModem;

struct _ConnuiCellularStatusItemPrivate
{
  GList *modems;
  gboolean offline;
  osso_context_t *osso_context;
  ConnuiPixbufCache *pixbuf_cache;
  osso_display_state_t display_state;
  gboolean display_was_off;
  gboolean modems_changed;
};

HD_DEFINE_PLUGIN_MODULE_EXTENDED(
    ConnuiCellularStatusItem,
    connui_cellular_status_item,
    HD_TYPE_STATUS_MENU_ITEM,
    G_ADD_PRIVATE_DYNAMIC(ConnuiCellularStatusItem), , );

static void
connui_cellular_status_item_class_finalize(ConnuiCellularStatusItemClass *klass)
{
}

static ConnuiCellularModem *
_find_modem(ConnuiCellularStatusItem *item, const char *modem_id)
{
  ConnuiCellularStatusItemPrivate *priv = PRIVATE(item);
  GList *l;

  for (l = priv->modems; l; l = l->next)
  {
    if (!strcmp(((ConnuiCellularModem *)l->data)->id, modem_id))
      return l->data;
  }

  return NULL;
}

static ConnuiCellularModem *
_get_modem(ConnuiCellularStatusItem *item, const char *modem_id)
{
  ConnuiCellularStatusItemPrivate *priv = PRIVATE(item);
  ConnuiCellularModem *modem = _find_modem(item, modem_id);

  if (!modem)
  {
    modem = g_new0(ConnuiCellularModem, 1);
    modem->id = g_strdup(modem_id);
    priv->modems = g_list_append(priv->modems, modem);
  }

  return modem;
}

static void
_free_modem(ConnuiCellularModem *modem)
{
  g_free(modem->id);
  g_free(modem);
}

static gboolean
_get_icons(ConnuiCellularModem *modem, gboolean offline)
{
  connui_sim_status sim_status = modem->sim_status;
  const gchar *mode = NULL;
  const gchar *bars = "statusarea_cell_off";
  gboolean changed = FALSE;

  if (sim_status != CONNUI_SIM_STATE_TIMEOUT &&
      sim_status != CONNUI_SIM_STATUS_UNKNOWN &&
      sim_status != CONNUI_SIM_STATUS_NO_SIM)
  {
    switch (modem->state.reg_status)
    {
      case CONNUI_NET_REG_STATUS_HOME:
      case CONNUI_NET_REG_STATUS_ROAMING:
      {
        connui_net_radio_access_tech rat = modem->state.rat_name;

        if (rat == CONNUI_NET_RAT_LTE)
            mode = "statusarea_cell_mode_4g";
        else if (rat == CONNUI_NET_RAT_UMTS)
        {
          if (modem->state.network_hsdpa_allocated)
            mode = "statusarea_cell_mode_3_5g";
          else
            mode = "statusarea_cell_mode_3g";
        }
        else if (rat == CONNUI_NET_RAT_GSM)
        {
          if (modem->state.supported_services & NETWORK_MASK_EGPRS_SUPPORT)
            mode = "statusarea_cell_mode_2_5g";
          else
            mode = "statusarea_cell_mode_2g";
        }
        else
          g_warning("status->rat unknown %hhu!", rat);

        guchar signal_bars = modem->state.network_signals_bar;

        if (signal_bars > 80)
          bars = "statusarea_cell_level5";
        else if (signal_bars > 60)
          bars = "statusarea_cell_level4";
        else if (signal_bars > 40)
          bars = "statusarea_cell_level3";
        else if (signal_bars > 20)
          bars = "statusarea_cell_level2";
        else if (signal_bars)
          bars = "statusarea_cell_level1";
        else
          bars = "statusarea_cell_level0";

        break;
      }
      case CONNUI_NET_REG_STATUS_UNREGISTERED:
      {
        bars = "statusarea_cell_off";
        break;
      }
      default:
      {
        bars = "statusarea_cell_level0";
        break;
      }
    }
  }
  else if (offline)
  {
    mode = "statusarea_offline_mode";
    bars = "statusarea_cell_level0";
  }

  if (modem->bars != bars || modem->mode != mode)
    changed = TRUE;

  modem->bars = bars;
  modem->mode = mode;

  return changed;
}

static void
connui_cellular_status_item_update_icon(ConnuiCellularStatusItem *item,
                                        const char *modem_id)
{
  ConnuiCellularStatusItemPrivate *priv = PRIVATE(item);
  GdkPixbuf *pixbuf = NULL;
  GdkPixbuf *icon;
  ConnuiCellularModem *modem;
  GList *modems, *l;
  gboolean changed = priv->modems_changed;
  int x = 0;
  int count = 0;

  if (priv->display_state == OSSO_DISPLAY_OFF)
  {
    priv->display_was_off = TRUE;
    return;
  }

  if (modem_id)
    changed = _get_icons(_get_modem(item, modem_id), priv->offline);
  else if(g_list_length(priv->modems))
  {
    for (l = priv->modems; l; l = l->next)
      changed |= _get_icons(l->data, priv->offline);
  }

  if (!changed)
    return;

  modems = connui_cell_modem_get_modems();

  count = g_list_length(modems);

  if (count > 2)
    count = 2;

  if (count)
  {
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 18 * count, 36);
    gdk_pixbuf_fill(pixbuf, 0);
  }

  for (l = modems; l && count; l = l->next)
  {
    modem = _find_modem(item, l->data);

    if (modem)
    {
      icon = connui_pixbuf_cache_get(priv->pixbuf_cache, modem->bars, 25);
      gdk_pixbuf_composite(icon, pixbuf, x , 0, 18, 25, x, 0.0, 1.0, 1.0,
                           GDK_INTERP_NEAREST, 255);

      if (modem->mode)
      {
        icon = connui_pixbuf_cache_get(priv->pixbuf_cache, modem->mode, 11);

        if (icon)
        {
          gdk_pixbuf_composite(icon, pixbuf, x, 25, 18, 11, x, 25.0, 1.0, 1.0,
                               GDK_INTERP_NEAREST, 255);
        }
      }
    }

    count--;
    x += 18;
  }

  hd_status_plugin_item_set_status_area_icon(HD_STATUS_PLUGIN_ITEM(item),
                                             pixbuf);
  g_list_free_full(modems, g_free);
  g_object_unref(pixbuf);
}

static void
connui_cellular_status_item_display_cb(osso_display_state_t state,
                                       gpointer user_data)
{
  ConnuiCellularStatusItem *item = CONNUI_CELLULAR_STATUS_ITEM(user_data);
  ConnuiCellularStatusItemPrivate *priv = PRIVATE(item);

  priv->display_state = state;

  if (state == OSSO_DISPLAY_ON && priv->display_was_off)
  {
    connui_cellular_status_item_update_icon(item, NULL);
    priv->display_was_off = FALSE;
  }
}

static void
connui_cellular_status_item_flightmode_cb(dbus_bool_t offline,
                                          gpointer user_data)
{
  ConnuiCellularStatusItem *item = CONNUI_CELLULAR_STATUS_ITEM(user_data);
  ConnuiCellularStatusItemPrivate *priv = PRIVATE(item);

  priv->offline = offline;
  connui_cellular_status_item_update_icon(item, NULL);
}

static void
connui_cellular_status_item_pin_query(const char *modem_id)
{
  static const char *spq = BINDIR "/startup-pin-query";
  pid_t pid = fork();

  g_return_if_fail(pid != -1);

  if (!pid)
  {
    execl(spq, spq, modem_id, NULL);
    exit(1);
  }
}

static void
_sim_status_cb(const char *modem_id, const connui_sim_status *sim_status,
               gpointer user_data)
{
  ConnuiCellularStatusItem *item = CONNUI_CELLULAR_STATUS_ITEM(user_data);
  ConnuiCellularStatusItemPrivate *priv = PRIVATE(item);
  ConnuiCellularModem *modem = _get_modem(item, modem_id);

  if (*sim_status == CONNUI_SIM_STATUS_UNKNOWN)
  {
    priv->modems_changed = TRUE;
    priv->modems = g_list_remove(priv->modems, modem);
    _free_modem(modem);
    modem_id = NULL;
  }
  else
  {
    if (*sim_status == CONNUI_SIM_STATUS_OK_PIN_REQUIRED ||
        *sim_status == CONNUI_SIM_STATUS_OK_PUK_REQUIRED)
    {
      connui_cellular_status_item_pin_query(modem_id);
    }

    modem->sim_status = *sim_status;
  }

  connui_cellular_status_item_update_icon(item, modem_id);
}

static void
_net_status_cb(const char *modem_id, const cell_network_state *state,
               gpointer user_data)
{
  ConnuiCellularStatusItem *item = CONNUI_CELLULAR_STATUS_ITEM(user_data);
  ConnuiCellularModem *modem = _get_modem(item, modem_id);

  modem->state = *state;
  connui_cellular_status_item_update_icon(item, modem_id);
}

static void
connui_cellular_status_item_finalize(GObject *object)
{
  ConnuiCellularStatusItemPrivate *priv = PRIVATE(object);

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

  connui_cell_net_status_close(_net_status_cb);
  connui_cell_sim_status_close(_sim_status_cb);
  connui_flightmode_close(connui_cellular_status_item_flightmode_cb);
  g_list_free_full(priv->modems, (GDestroyNotify)_free_modem);

  G_OBJECT_CLASS(connui_cellular_status_item_parent_class)->finalize(object);
}

static void
connui_cellular_status_item_class_init(ConnuiCellularStatusItemClass *klass)
{
  G_OBJECT_CLASS(klass)->finalize = connui_cellular_status_item_finalize;
}

static void
connui_cellular_status_item_init(ConnuiCellularStatusItem *self)
{
  ConnuiCellularStatusItemPrivate *priv = PRIVATE(self);

  priv->pixbuf_cache = connui_pixbuf_cache_new();
  priv->osso_context = osso_initialize("connui_cellular_status_item",
                                       PACKAGE_VERSION, TRUE, 0);

   osso_hw_set_display_event_cb(priv->osso_context,
                                connui_cellular_status_item_display_cb, self);

  if (!connui_flightmode_status(connui_cellular_status_item_flightmode_cb, self))
    g_warning("Unable to register flightmode status!");

  if (!connui_cell_sim_status_register(_sim_status_cb, self))
    g_warning("Unable to register SIM status");

  if (!connui_cell_net_status_register(_net_status_cb, self))
    g_warning("Unable to register cell net status!");

  connui_cellular_status_item_update_icon(self, NULL);
}
