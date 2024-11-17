/*
 * cellular-settings-call.h
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

#ifndef CELLULARSETTINGSCALL_H
#define CELLULARSETTINGSCALL_H

void _call_widgets_create(cellular_settings *cs, GtkWidget *parent,
                          GtkSizeGroup *size_group);
void _call_show(cellular_settings *cs, const gchar *modem_id);
void _call_apply(cellular_settings *cs, const gchar *modem_id);
void _call_cancel(cellular_settings *cs);

#endif // CELLULARSETTINGSCALL_H
