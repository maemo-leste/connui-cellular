#ifndef __CONNUI_CELL_CONTEXT_H__
#define __CONNUI_CELL_CONTEXT_H__

#include "connui-cellular.h"

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
  GSList *security_code_register_cbs;
  DBusGProxyCall *get_sim_status_call_1;
  GSList *net_status_cbs;
  DBusGProxyCall *get_registration_status_call;
  GSList *net_list_cbs;
  DBusGProxyCall *get_available_network_call;
  GSList *net_select_cbs;
  DBusGProxyCall *select_network_call;
  GSList *cs_status_cbs;
  GSList *ssc_state_cbs;
  cell_network network;
  GHashTable *service_calls;
  int service_call_id;
  GSList *call_status_cbs;
  GCallback clir_cb;
  gpointer clir_cb_data;
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

#endif /* __CONNUI_CELL_CONTEXT_H__ */
