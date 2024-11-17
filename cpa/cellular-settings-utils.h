/*
 * cellular-settings-utils.h
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

#ifndef CELLULARSETTINGSUTILS_H
#define CELLULARSETTINGSUTILS_H

#include <hildon/hildon.h>

void hildon_picker_button_init(GtkWidget *button, gint index);

GtkWidget *cellular_settings_create_widget(GtkWidget *parent,
                                           GtkSizeGroup *size_group,
                                           const gchar *title,
                                           GtkWidget *(*create_cb)());
GtkWidget *cellular_settings_create_button(GtkWidget *parent,
                                           GtkSizeGroup *size_group,
                                           const gchar *title);
GtkWidget *cellular_settings_create_check_button();
GtkWidget *cellular_settings_create_entry();
GtkWidget *cellular_settings_create_touch_selector(gint index,
                                                   const gchar *text1, ...);

#endif // CELLULARSETTINGSUTILS_H
