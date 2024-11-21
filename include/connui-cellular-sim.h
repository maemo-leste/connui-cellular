/*
 * connui-cellular-sim.h
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

#ifndef __CONNUI_CELLULAR_SIM_H_INCLUDED__
#define __CONNUI_CELLULAR_SIM_H_INCLUDED__

typedef enum
{
  CONNUI_SIM_STATUS_UNKNOWN = -1,
  CONNUI_SIM_STATUS_NO_SIM = 0,
  CONNUI_SIM_STATUS_OK = 1,
  CONNUI_SIM_STATUS_OK_PIN_REQUIRED = 2,
  CONNUI_SIM_STATUS_OK_PUK_REQUIRED = 3,
  CONNUI_SIM_STATUS_OK_PUK2_REQUIRED = 4,
  CONNUI_SIM_STATE_REJECTED = 5,
  CONNUI_SIM_STATE_LOCKED = 6
}
connui_sim_status;

typedef enum
{
  CONNUI_SIM_SECURITY_CODE_UNKNOWN = -1,
  CONNUI_SIM_SECURITY_CODE_NONE = 0,
  CONNUI_SIM_SECURITY_CODE_PIN = 1,
  CONNUI_SIM_SECURITY_CODE_PUK = 2,
  CONNUI_SIM_SECURITY_CODE_PIN2 = 3,
  CONNUI_SIM_SECURITY_CODE_PUK2 = 4,
  CONNUI_SIM_SECURITY_CODE_UNSUPPORTED
}
connui_sim_security_code_type;

typedef void (*cell_sec_code_query_cb_callback)(
    const char *modem_id, connui_sim_security_code_type code_type, gboolean ok,
    gpointer user_data, GError *error);

typedef void (*cell_sim_status_cb) (
    const char *modem_id,
    const connui_sim_status *status,
    gpointer user_data);
typedef void (*cell_sec_code_query_cb) (
    const char *modem_id,
    const connui_sim_security_code_type *code_type,
    gchar ***old_code, gchar ***new_code,
    cell_sec_code_query_cb_callback *query_cb,
    gpointer *query_user_data,
    gpointer user_data);

gboolean
connui_cell_sim_status_register(cell_sim_status_cb cb, gpointer user_data);
void
connui_cell_sim_status_close(cell_sim_status_cb cb);

connui_sim_status
connui_cell_sim_get_status(const char *modem_id, GError **error);

gchar *
connui_cell_sim_get_service_provider(const char *modem_id, GError **error);

guint
connui_cell_sim_verify_attempts_left(const char *modem_id,
                                     connui_sim_security_code_type code_type,
                                     GError **error_value);

gboolean
connui_cell_sim_needs_pin(const char *modem_id, GError **error);

gboolean
connui_cell_sim_is_locked(const char *modem_id, GError **error);

gboolean
connui_cell_sim_deactivate_lock(const char *modem_id, const gchar *pin_code,
                                GError **error);

gboolean connui_cell_sim_is_network_in_service_provider_info(guint mnc, guint mcc);

/* SIM security code*/
connui_sim_security_code_type
connui_cell_security_code_get_active(const char *modem_id, GError **error);
gboolean
connui_cell_security_code_set_active(const char *modem_id,
                                     connui_sim_security_code_type code_type,
                                     GError **error);

gboolean
connui_cell_security_code_register(cell_sec_code_query_cb cb,
                                   gpointer user_data);
void
connui_cell_security_code_close(cell_sec_code_query_cb cb);

gboolean
connui_cell_security_code_change(const char *modem_id,
                                 connui_sim_security_code_type code_type,
                                 GError **error);

gboolean
connui_cell_security_code_get_enabled(const char *modem_id,
                                      connui_sim_security_code_type code_type,
                                      GError **error);

gboolean
connui_cell_security_code_set_enabled(const char *modem_id,
                                      gboolean active, GError **error);

#endif /* __CONNUI_CELLULAR_SIM_H_INCLUDED__ */
