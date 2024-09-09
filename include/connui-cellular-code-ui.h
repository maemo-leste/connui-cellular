/*
 * connui-cellular-code-ui.h
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

#ifndef __CONNUI_CELLULAR_CODE_UI_H_INCLUDED__
#define __CONNUI_CELLULAR_CODE_UI_H_INCLUDED__

gboolean connui_cell_code_ui_cancel_dialog();
gboolean connui_cell_code_ui_is_sim_locked_with_error();

gboolean
connui_cell_code_ui_init(const char *modem_id,
                         GtkWindow *parent,
                         gboolean show_pin_code_correct);

void connui_cell_code_ui_destroy();
GtkWidget *connui_cell_code_ui_create_dialog(const char *modem_id,
                                             const gchar *title,
                                             int code_min_len);
gboolean connui_cell_code_ui_deactivate_simlock();
gboolean connui_cell_code_ui_change_code(connui_sim_security_code_type code_type);
gboolean connui_cell_code_ui_update_sim_status();
gboolean connui_cell_code_ui_set_current_code_active(gboolean active);

gchar *connui_cell_code_ui_error_note_type_to_text(const char *modem_id,
                                                   const char *note_type);

#endif // __CONNUI_CELLULAR_CODE_UI_H_INCLUDED__
