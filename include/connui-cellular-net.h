/*
 * connui-cellular-net.h
 *
 * Copyright (C) 2023 Ivaylo Dimitrov <ivo.g.dimitrov.75@gmail.com>
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#ifndef __CONNUI_CELLULAR_NET_H_INCLUDED__
#define __CONNUI_CELLULAR_NEW_H_INCLUDED__

struct _cell_network
{
  const char *modem_id;
  guchar service_status;
  gchar *country_code;
  gchar *operator_code;
  gchar *operator_name;
  guchar umts_avail;
  guchar network_type;
};
typedef struct _cell_network cell_network;

typedef enum
{
  CONNUI_NET_REG_STATUS_UNKNOWN = -1,
  CONNUI_NET_REG_STATUS_UNREGISTERED,
  CONNUI_NET_REG_STATUS_HOME,
  CONNUI_NET_REG_STATUS_SEARCHING,
  CONNUI_NET_REG_STATUS_DENIED,
  CONNUI_NET_REG_STATUS_ROAMING
}
connui_net_registration_status;

typedef enum
{
  CONNUI_NET_RAT_UNKNOWN,
  CONNUI_NET_RAT_GSM,
  CONNUI_NET_RAT_UMTS,
  CONNUI_NET_RAT_LTE,
  CONNUI_NET_RAT_NR
}
connui_net_radio_access_tech;

struct _cell_network_state
{
  connui_net_registration_status reg_status;
  guint lac;
  guint cell_id;
  cell_network *network;
  guchar supported_services;
  guchar network_signals_bar;
  connui_net_radio_access_tech rat_name;
  gboolean network_hsdpa_allocated;
  gchar *operator_name;
};

typedef struct _cell_network_state cell_network_state;

typedef void (*cell_network_state_cb) (
    const char *modem_id, const cell_network_state *state, gpointer user_data);

typedef void (*cell_get_anonymity_cb)(guint anonymity, GError *error, gpointer user_data);

typedef void (*cell_set_cb)(GError *error, gpointer user_data);

void connui_cell_network_free(cell_network *network);
cell_network *connui_cell_network_dup(const cell_network *network);


typedef void (*cell_cs_status_cb) (gboolean active, gpointer user_data);
typedef void (*cell_net_list_cb) (GSList *networks, gpointer user_data);
typedef void (*cell_net_select_cb) (gboolean success, guint network_reject_code, gpointer user_data);


enum network_alpha_tag_name_type
{
// The operator name contains only 'ordinary' characters. The operator name is
// from the hard-coded operator name list
    NETWORK_HARDCODED_LATIN_OPER_NAME = 0,
// The operator name contains some language specific characters (like Chinese).
// The operator name is from the hard-coded operator name list
    NETWORK_HARDCODED_USC2_OPER_NAME,
// The operator name is received from the cellular network in a special message
// containing the operator name. The network has indicated that this name is a
// short name.
    NETWORK_NITZ_SHORT_OPER_NAME,
// The operator name is received from the cellular network in a special message
// containing the operator name. The network has indicated that this is a full
// name operator name.
    NETWORK_NITZ_FULL_OPER_NAME,
// The operator name from SIM card
    NETWORK_SIM_OPER_NAME = 0x08,
// The operator name is received from the SIM and it is mapped to certain LAC.
// The SIM has indicated that this name is a short name.
    NETWORK_EONS_SHORT_OPER_NAME,
// The operator name is received from the SIM and it is mapped to certain LAC.
// The SIM has indicated that this is a full name
    NETWORK_EONS_FULL_OPER_NAME
};

#define NETWORK_MASK_GPRS_SUPPORT   0x01
#define NETWORK_MASK_CS_SERVICES    0x02
#define NETWORK_MASK_EGPRS_SUPPORT  0x04
#define NETWORK_MASK_HSDPA_AVAIL    0x08
#define NETWORK_MASK_HSUPA_AVAIL    0x10

typedef enum
{
  CONNUI_NET_SELECT_MODE_UNKNOWN,
  CONNUI_NET_SELECT_MODE_MANUAL,
  CONNUI_NET_SELECT_MODE_AUTO,
  CONNUI_NET_SELECT_MODE_AUTO_ONLY
}
connui_net_selection_mode;

#if 0
// CS performs user reselection. This is currently GSM-specific procedure, which is
// specified in GSM TS 03.22.
#define NETWORK_SELECT_MODE_NO_SELECTION  0x04

// Network status availability of operators
#define NETWORK_OPER_STATUS_UNKNOWN       0x00  // It is not known in which category operator belongs
#define NETWORK_OPER_STATUS_AVAILABLE     0x01  // The operator is heard and allowed
#define NETWORK_OPER_STATUS_CURRENT       0x02  // The CS is currently in service in this operator
#define NETWORK_OPER_STATUS_FORBIDDEN     0x03  // The operator is forbidden

// Selected rat type
#define NET_GSS_DUAL_SELECTED_RAT            0x00
#define NET_GSS_GSM_SELECTED_RAT             0x01
#define NET_GSS_UMTS_SELECTED_RAT            0x02
#define NET_GSS_UNKNOWN_SELECTED_RAT         0x03

#endif

#define ICD_GCONF_NETWORK_MAPPING_GPRS ICD_GCONF_NETWORK_MAPPING "/GPRS"

#define GPRS_HOME_RX_BYTES ICD_GCONF_NETWORK_MAPPING_GPRS "/gprs_home_rx_bytes"
#define GPRS_HOME_TX_BYTES ICD_GCONF_NETWORK_MAPPING_GPRS "/gprs_home_tx_bytes"
#define GPRS_HOME_RST_TIME ICD_GCONF_NETWORK_MAPPING_GPRS "/gprs_home_reset_time"
#define GPRS_HOME_WARNING_LIMIT ICD_GCONF_NETWORK_MAPPING_GPRS "/gprs_home_warning_limit"
#define GPRS_HOME_NTFY_ENABLE ICD_GCONF_NETWORK_MAPPING_GPRS "/gprs_home_notification_enabled"
#define GPRS_HOME_LAST_NTFY ICD_GCONF_NETWORK_MAPPING_GPRS "/gprs_home_last_notification"
#define GPRS_HOME_NTFY_PERIOD ICD_GCONF_NETWORK_MAPPING_GPRS "/gprs_home_notification_period"

#define GPRS_ROAM_RX_BYTES ICD_GCONF_NETWORK_MAPPING_GPRS "/gprs_roaming_rx_bytes"
#define GPRS_ROAM_TX_BYTES ICD_GCONF_NETWORK_MAPPING_GPRS "/gprs_roaming_tx_bytes"
#define GPRS_ROAM_RST_TIME ICD_GCONF_NETWORK_MAPPING_GPRS "/gprs_roaming_reset_time"
#define GPRS_ROAM_WARNING_LIMIT ICD_GCONF_NETWORK_MAPPING_GPRS "/gprs_roaming_warning_limit"
#define GPRS_ROAM_NTFY_ENABLE ICD_GCONF_NETWORK_MAPPING_GPRS "/gprs_roaming_notification_enabled"
#define GPRS_ROAM_LAST_NTFY ICD_GCONF_NETWORK_MAPPING_GPRS "/gprs_roaming_last_notification"
#define GPRS_ROAM_NTFY_PERIOD ICD_GCONF_NETWORK_MAPPING_GPRS "/gprs_roaming_notification_period"
#define GPRS_ROAM_DISABLED ICD_GCONF_NETWORK_MAPPING_GPRS "/gprs_roaming_disabled"

typedef void (*service_call_cb_f)(gboolean enabled, gint error_value, const gchar *phone_number, gpointer user_data);

/* NET */
gboolean connui_cell_net_status_register(cell_network_state_cb cb, gpointer user_data);
void connui_cell_net_status_close(cell_network_state_cb cb);

gchar *connui_cell_net_get_operator_name(cell_network *network, GError **error);

connui_net_selection_mode
connui_cell_net_get_network_selection_mode(const gchar *modem_id,
                                           GError **error);

connui_net_radio_access_tech
connui_cell_net_get_radio_access_mode(const char *modem_id,
                                      GError **error);

guint connui_cell_net_set_call_forwarding_enabled(gboolean enabled, const gchar *phone_number, service_call_cb_f cb, gpointer user_data);

gboolean connui_cell_net_list(cell_net_list_cb cb, gpointer user_data);

gboolean
connui_cell_net_select(cell_network *network, cell_net_select_cb cb,
                       gpointer user_data);

void connui_cell_net_cancel_select(cell_net_select_cb cb);
void connui_cell_reset_network();
void connui_cell_net_cancel_list(cell_net_list_cb cb);
const cell_network *connui_cell_net_get_current();

gboolean connui_cell_net_set_radio_access_mode(guchar selected_rat, gint *error_value);


gboolean connui_cell_net_get_caller_id_anonymity(cell_get_anonymity_cb clir_cb, gpointer user_data);
gboolean connui_cell_net_set_caller_id_anonymity(guint anonymity, cell_set_cb cb, gpointer user_data);
void connui_cell_net_set_caller_id_presentation_bluez(const gchar *caller_id);

#endif /* __CONNUI_CELLULAR_NET_H_INCLUDED__ */
