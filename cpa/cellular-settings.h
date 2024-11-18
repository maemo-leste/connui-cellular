/*
 * cellular-settings.h
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

#ifndef CELLULARSETTINGS_H
#define CELLULARSETTINGS_H

#include <hildon/hildon.h>
#include <libosso.h>

#include "connui-cellular.h"

struct _CellularSettings
{
  osso_context_t *osso;
  GtkWidget *dialog;
  GtkWidget *modem_picker;
  GtkWidget *pannable_area;
  GtkWidget *error_status;

  struct
  {
    GtkWidget *pin_request;
    GtkWidget *pin;
    gboolean code_active;
  } sim;

  struct
  {
    GtkWidget *send_cid;
    guint anonimity;
    guint svc_call_id;

    struct
    {
      GtkWidget *button;
      gboolean enabled;
    } waiting;

    struct
    {
      GtkWidget *option;
      GtkWidget *to;
      GtkWidget *contact;
      gboolean enabled;
    } forward;
  } call;

  gint pending;
  gboolean applying;

  guint idle_id;
};

typedef struct _CellularSettings cellular_settings;

void cellular_settings_destroy();
void cellular_settings_stop_progress_indicator(cellular_settings *cs);
gboolean cellular_settings_apply(cellular_settings *cs, gboolean enable_after);

void
cellular_settings_cancel_service_calls(cellular_settings *cs);

#define GTK_RESPONSE_CONTACT 10

#endif // CELLULARSETTINGS_H
