/*
 * connui-cellular-sups.h
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

#ifndef __CONNUI_CELLULAR_SUPS_H_INCLUDED__
#define __CONNUI_CELLULAR_SUPS_H_INCLUDED__

typedef enum
{
  CONNUI_SUPS_BUSY = 1,
  CONNUI_SUPS_NO_REPLY = 2,
  CONNUI_SUPS_UNREACHABLE = 4
} connui_sups_call_forward;

typedef void (*call_waiting_enabled_cb_f)(gboolean enabled, gpointer user_data,
                                          GError *error);

typedef void (*call_forwarding_enabled_cb_f)(gboolean enabled,
                                             const gchar *phone_number,
                                             gpointer user_data,
                                             GError *error);

guint
connui_cell_sups_get_call_waiting_enabled(const char *modem_id,
                                          call_waiting_enabled_cb_f cb,
                                          gpointer user_data);
guint
connui_cell_sups_get_call_forwarding_enabled(const char *modem_id,
                                             connui_sups_call_forward type,
                                             call_forwarding_enabled_cb_f cb,
                                             gpointer user_data);

void
connui_cell_sups_cancel_service_call(guint id);

#endif /* __CONNUI_CELLULAR_SUPS_H_INCLUDED__ */
