/*
 * connui-cellular-connmgr.h
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

#ifndef CONNUICELLULARCONNMGR_H
#define CONNUICELLULARCONNMGR_H

typedef enum
{
  CONNUI_CONNMGR_BEARER_UNKNOWN = -1,
  CONNUI_CONNMGR_BEARER_NONE,
  CONNUI_CONNMGR_BEARER_GPRS,
  CONNUI_CONNMGR_BEARER_EDGE,
  CONNUI_CONNMGR_BEARER_UMTS,
  CONNUI_CONNMGR_BEARER_HSPDA,
  CONNUI_CONNMGR_BEARER_HSUPA,
  CONNUI_CONNMGR_BEARER_HSPA,
  CONNUI_CONNMGR_BEARER_LTE
}
connui_connmgr_bearer;

struct _cell_connection_status
{
  gboolean attached;
  connui_connmgr_bearer bearer;
  gboolean suspended;
  gboolean roaming_allowed;
  gboolean powered;
};

typedef struct _cell_connection_status cell_connection_status;

typedef void (*cell_connection_status_cb) (const char *modem_id,
                                           const cell_connection_status *state,
                                           gpointer user_data);

gboolean
connui_cell_connection_status_register(cell_connection_status_cb cb,
                                       gpointer user_data);
void
connui_cell_connection_status_close(cell_connection_status_cb cb);

const cell_connection_status *
connui_cell_connection_get_status(const char *modem_id, GError **error);

gboolean
connui_cell_connection_set_roaming_allowed(const char *modem_id,
                                           gboolean allowed,
                                           GError **error);

gboolean
connui_cell_connection_set_powered(const char *modem_id, gboolean powered,
                                   GError **error);

#endif // CONNUICELLULARCONNMGR_H
