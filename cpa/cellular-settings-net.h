/*
 * cellular-settings-net.h
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

#ifndef CELLULARSETTINGSNET_H
#define CELLULARSETTINGSNET_H

void _net_widgets_create(cellular_settings *cs, GtkWidget *parent,
                         GtkSizeGroup *size_group);
void _net_show(cellular_settings *cs, const gchar *modem_id);
void _net_apply(cellular_settings *cs, const gchar *modem_id);
void _net_cancel(cellular_settings *cs);

#endif // CELLULARSETTINGSNET_H
