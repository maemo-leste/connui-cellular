/*
 * cellular-settings-utils.c
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

#include "config.h"

#include "cellular-settings-utils.h"

void
hildon_picker_button_init(GtkWidget *button, gint index)
{
  g_return_if_fail(HILDON_IS_PICKER_BUTTON(button));

  hildon_picker_button_set_active(HILDON_PICKER_BUTTON(button), index);
  gtk_button_set_alignment(GTK_BUTTON(button), 0.0, 0.5);
}

GtkWidget *
cellular_settings_create_button(GtkWidget *parent, GtkSizeGroup *size_group,
                                const gchar *title)
{
  GtkWidget *button =
      hildon_button_new_with_text(HILDON_SIZE_FINGER_HEIGHT,
                                  HILDON_BUTTON_ARRANGEMENT_HORIZONTAL, title,
                                  NULL);
  gtk_box_pack_start(GTK_BOX(parent), button, FALSE, FALSE, 0);

  return button;
}

GtkWidget *
cellular_settings_create_check_button()
{
  return hildon_check_button_new(HILDON_SIZE_FINGER_HEIGHT);
}

GtkWidget *
cellular_settings_create_entry()
{
  return hildon_entry_new(HILDON_SIZE_FINGER_HEIGHT);
}

GtkWidget *
cellular_settings_create_widget(GtkWidget *parent, GtkSizeGroup *size_group,
                                const gchar *title, GtkWidget *(*create_cb)())
{
  GtkWidget *widget = create_cb();

  if (HILDON_IS_BUTTON(widget))
      hildon_button_set_title(HILDON_BUTTON(widget), title);
  else if (HILDON_IS_CHECK_BUTTON(widget))
      gtk_button_set_label(GTK_BUTTON(widget), title);
  else
  {
    GtkWidget *hbox = gtk_hbox_new(0, 8);
    GtkWidget *label = gtk_label_new(title);

    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(hbox), widget,  TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(parent), hbox, FALSE, FALSE, 0);
    return widget;
  }

  gtk_box_pack_start(GTK_BOX(parent), widget, FALSE, FALSE, 0);

  return widget;
}

GtkWidget *
cellular_settings_create_touch_selector(gint index, const gchar *text1, ...)
{
  HildonTouchSelector *selector;
  GtkWidget *button;
  va_list ap;

  va_start(ap, text1);

  selector = HILDON_TOUCH_SELECTOR(hildon_touch_selector_new_text());
  button = hildon_picker_button_new(HILDON_SIZE_FINGER_HEIGHT,
                                    HILDON_BUTTON_ARRANGEMENT_VERTICAL);
  hildon_picker_button_set_selector(HILDON_PICKER_BUTTON(button), selector);

  while (text1)
  {
      hildon_touch_selector_append_text(selector, text1);
      text1 = va_arg(ap, const gchar *);
  }

  hildon_picker_button_init(button, index);

  return button;
}
