#ifndef __CONNUI_CELLULAR_H__
#define __CONNUI_CELLULAR_H__

#include <stdint.h>

struct _cell_network
{
  uint8_t service_status;
  gchar *country_code;
  gchar *operator_code;
  gchar *operator_name;
  uint8_t umts_avail;
  uint8_t network_type;
};
typedef struct _cell_network cell_network;

struct _cell_network_state
{
  int32_t reg_status;
  uint32_t lac;
  uint32_t cell_id;
  cell_network *network;
  uint8_t supported_services;
  uint8_t network_signals_bar;
  uint8_t rat_name;
  uint8_t network_hsdpa_allocated;
  uint8_t operator_name_type;
  gchar *operator_name;
  gchar *alternative_operator_name;
};

typedef struct _cell_network_state cell_network_state;

typedef void (*call_status_cb) (gboolean calls, gpointer user_data);
typedef void (*cs_status_cb) (gchar state, gpointer user_data);
typedef void (*cell_network_state_cb) (const cell_network_state *state,
                                       gpointer user_data);
typedef void (*cell_sim_status_cb) (guint status, gpointer user_data);
typedef void (*ssc_state_cb) (gchar *state, gpointer user_data);

void connui_cell_network_free(cell_network *network);

enum net_registration_status
{
    NETWORK_REG_STATUS_HOME = 0x00, // CS is registered to home network
    NETWORK_REG_STATUS_ROAM,        // CS is registered to some other network than home network
    NETWORK_REG_STATUS_ROAM_BLINK,  // CS is registered to non-home system in a non-home area ('a' or 'b' area)
    NETWORK_REG_STATUS_NOSERV,      // CS is not in service
    NETWORK_REG_STATUS_NOSERV_SEARCHING,    // CS is not in service, but is currently searching for service
    NETWORK_REG_STATUS_NOSERV_NOTSEARCHING, // CS is not in service and it is not currently searching for service
    NETWORK_REG_STATUS_NOSERV_NOSIM,        // CS is not in service due to missing SIM or missing subscription
    NETWORK_REG_STATUS_POWER_OFF = 0x08,    // CS is in power off state
    NETWORK_REG_STATUS_NSPS,                // CS is in No Service Power Save State (currently not listening to any cell)
    NETWORK_REG_STATUS_NSPS_NO_COVERAGE,    // CS is in No Service Power Save State (CS is entered to this state because there is no network coverage)
    NETWORK_REG_STATUS_NOSERV_SIM_REJECTED_BY_NW // CS is not in service due to missing subscription
};

#define NETWORK_RAT_NAME_UNKNOWN         0x00
#define NETWORK_GSM_RAT                  0x01
#define NETWORK_UMTS_RAT                 0x02

#define NETWORK_MASK_GPRS_SUPPORT   0x01
#define NETWORK_MASK_CS_SERVICES    0x02
#define NETWORK_MASK_EGPRS_SUPPORT  0x04
#define NETWORK_MASK_HSDPA_AVAIL    0x08
#define NETWORK_MASK_HSUPA_AVAIL    0x10

gboolean connui_cell_net_status_register(cell_network_state_cb cb, gpointer user_data);
void connui_cell_net_status_close(cell_network_state_cb cb);

gboolean connui_cell_sim_status_register(cell_sim_status_cb cb, gpointer user_data);
void connui_cell_sim_status_close(cell_sim_status_cb cb);
gboolean connui_cell_sim_is_network_in_service_provider_info(gint *error_value, guchar *code);

#endif /* __CONNUI_CELLULAR_H__ */
