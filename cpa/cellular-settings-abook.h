/*
 * cellular-settings-abook.h
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

#ifndef CELLULARSETTINGSABOOK_H
#define CELLULARSETTINGSABOOK_H

GtkDialog *
cellular_abook_show(GtkWindow *parent, osso_context_t *osso);

void
cellular_abook_destroy();

char *
cellular_abook_get_selected_number();

#endif // CELLULARSETTINGSABOOK_H
