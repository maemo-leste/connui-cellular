/*
 * sim.h
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

#ifndef __CONNUI_INTERNAL_SIM_H_INCLUDED__
#define __CONNUI_INTERNAL_SIM_H_INCLUDED__

#include "ofono.h"
#include "org.ofono.Modem.h"
#include "org.ofono.SimManager.h"

void
connui_cell_modem_add_simmgr(connui_cell_context *ctx, const char *path);

void
connui_cell_modem_remove_simmgr(OrgOfonoModem *modem);

#endif /* __CONNUI_INTERNAL_SIM_H_INCLUDED__ */
