#ifndef __CONNUI_CELL_CONTEXT_H__
#define __CONNUI_CELL_CONTEXT_H__

#include "connui-cellular.h"

#include <gofono_modem.h>
#include <gofono_manager.h>
#include <gofono_simmgr.h>

// XXX: need this to read connection status
#include <gofono_connmgr.h>
#include <gofono_connctx.h>

struct _connui_cell_context
{
  gboolean initialized;
  cell_network_state state;
  DBusGProxy *phone_net_proxy;
  DBusGProxy *phone_sim_proxy;
  DBusGProxy *phone_sim_security_proxy;
  DBusGProxy *csd_ss_proxy;
  DBusGProxy *csd_call_proxy;
  DBusGProxy *phone_ssc_proxy;
  GSList *sim_status_cbs;
  DBusGProxyCall *get_sim_status_call;
  GSList *sec_code_cbs;
  DBusGProxyCall *get_sim_status_call_1;
  GSList *net_status_cbs;
  DBusGProxyCall *get_registration_status_call;
  GSList *net_list_cbs;
  DBusGProxyCall *get_available_network_call;
  GSList *net_select_cbs;
  DBusGProxyCall *select_network_call;
  GSList *cs_status_cbs;
  GSList *ssc_state_cbs;
  /* Move this above dbus stuff? */
  cell_network network;
  /* Move this above dbus stuff? */
  GHashTable *service_calls;
  /* Move this above dbus stuff? */
  int service_call_id;
  GSList *call_status_cbs;
  cell_clir_cb clir_cb;
  gpointer clir_cb_data;

  OfonoManager* ofono_manager;

  /* Let's not support multiple modems and sim manager atm, so we don't need
   * more than a single var either. */
  OfonoModem* ofono_modem;
  OfonoSimMgr* ofono_sim_manager;
  OfonoConnMgr* ofono_conn_manager;
  gulong ofono_manager_valid_id;
  gulong ofono_sim_manager_valid_id;
  gulong ofono_modem_added_id;
  gulong ofono_modem_removed_id;
  gulong ofono_sim_property_changed;

  gchar* ofono_modem_path;
};

typedef struct _connui_cell_context connui_cell_context;

struct _sim_status_data
{
  GCallback cb;
  gpointer data;
};

typedef struct _sim_status_data sim_status_data;

connui_cell_context *connui_cell_context_get();
void connui_cell_context_destroy(connui_cell_context *ctx);
void destroy_sim_status_data(gpointer mem_block);


gboolean register_ofono(connui_cell_context *ctx);
void unregister_ofono(connui_cell_context *ctx);

#endif /* __CONNUI_CELL_CONTEXT_H__ */
