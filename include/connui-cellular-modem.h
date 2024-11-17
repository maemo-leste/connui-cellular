/*
 * connui-cellular-modem.h
 *
 * Copyright (C) 2024 Ivaylo Dimitrov <ivo.g.dimitrov.75@gmail.com>
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

#ifndef __CONNUI_CELLULAR_MODEM_H_INCLUDED__
#define __CONNUI_CELLULAR_MODEM_H_INCLUDED__

typedef enum
{
  /** modem is removed */
  CONNUI_MODEM_STATUS_UNKNOWN = -1,
  CONNUI_MODEM_STATUS_REMOVED = 0,
  CONNUI_MODEM_STATUS_OK = 1,
  CONNUI_MODEM_STATUS_POWERED = 2,
  CONNUI_MODEM_STATUS_ONLINE = 3
}
connui_modem_status;

typedef void (*cell_modem_status_cb) (
    const char *modem_id,
    const connui_modem_status *status,
    gpointer user_data);

gboolean
connui_cell_modem_is_online(const char *modem_id, GError **error);

gboolean
connui_cell_modem_is_powered(const char *modem_id, GError **error);

gboolean
connui_cell_modem_status_register(cell_modem_status_cb cb, gpointer user_data);

void
connui_cell_modem_status_close(cell_modem_status_cb cb);

gchar *
connui_cell_modem_get_model(const char *modem_id, GError **error);

gchar *
connui_cell_modem_get_serial(const char *modem_id, GError **error);

gchar *
connui_cell_modem_get_revision(const char *modem_id, GError **error);

gchar *
connui_cell_modem_get_manufacturer(const char *modem_id, GError **error);

GList *
connui_cell_modem_get_modems();

GStrv connui_cell_emergency_get_numbers(const char *modem_id, GError **error);

#endif /* __CONNUI_CELLULAR_MODEM_H_INCLUDED__ */
