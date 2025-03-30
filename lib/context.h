#ifndef __CONNUI_CELL_CONTEXT_H__
#define __CONNUI_CELL_CONTEXT_H__

#include <dbus/dbus-glib.h>

#include "connui-cellular.h"

#include "org.ofono.Manager.h"

struct _connui_cell_context
{
  gboolean initialized;
  DBusGProxy *phone_net_proxy;
  DBusGProxy *phone_sim_proxy;
  DBusGProxy *csd_ss_proxy;
  DBusGProxy *csd_call_proxy;
  GSList *sim_status_cbs;
  DBusGProxyCall *get_sim_status_call;
  GSList *sec_code_cbs;
  DBusGProxyCall *get_sim_status_call_1;
  GSList *net_status_cbs;
  GSList *conn_status_cbs;
  DBusGProxyCall *get_registration_status_call;
  GSList *net_list_cbs;
  DBusGProxyCall *get_available_network_call;
  GSList *net_select_cbs;
  DBusGProxyCall *select_network_call;
  /* Move this above dbus stuff? */
  cell_network network;
  /* Move this above dbus stuff? */
  GHashTable *service_calls;
  /* Move this above dbus stuff? */
  int service_call_id;
  GSList *call_status_cbs;

  /* sim.c properties */
  gulong ofono_sim_present_changed_valid_id;
  gulong ofono_sim_pinrequired_changed_valid_id;


  ///////////////////////////////////////////////////////
  ConnuiCellManager *manager;
  GHashTable *modems;
  GSList *modem_cbs;
  gulong modem_added_id;
  gulong modem_removed_id;
};

typedef struct _connui_cell_context connui_cell_context;

struct _sim_status_data
{
  GCallback cb;
  gpointer data;
};

typedef struct _sim_status_data sim_status_data;

connui_cell_context *connui_cell_context_get(GError **error);
void connui_cell_context_destroy(connui_cell_context *ctx);
void destroy_sim_status_data(gpointer mem_block);


#endif /* __CONNUI_CELL_CONTEXT_H__ */
