/*
 * cellular-settings-abook.c
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

#include <libosso-abook/osso-abook.h>

static GtkWidget *contact_chooser = NULL;
static EBook *system_addressbook = NULL;
static OssoABookContactModel *contact_model = NULL;

static void
_book_view_cb(EBook *book, EBookStatus status, EBookView *book_view,
               gpointer closure)
{
  if (status)
    g_object_unref(book);
  else
  {
    if (contact_model)
    {
      osso_abook_list_store_set_book_view(
            OSSO_ABOOK_LIST_STORE(contact_model), book_view);
    }

    e_book_view_start(book_view);
  }
}

static void
_book_open_cb(EBook *book, EBookStatus status, gpointer user_data)
{
  if (status)
  {
    g_object_unref(system_addressbook);
    system_addressbook = NULL;
  }
  else
  {
    EBookQuery *query = e_book_query_vcard_field_exists("TEL");

    e_book_async_get_book_view(book, query, NULL, -1, _book_view_cb, NULL);
    e_book_query_unref(query);
  }
}

GtkDialog *
cellular_abook_show(GtkWindow *parent, osso_context_t *osso)
{
  static gboolean abook_created = FALSE;

  if (contact_chooser)
  {
    gtk_widget_show_all(contact_chooser);
    return GTK_DIALOG(contact_chooser);
  }

  if (!abook_created)
  {
    if (!osso_abook_init_with_name("cellularabook", osso))
      return NULL;

    osso_abook_make_resident();
    abook_created = TRUE;
  }

  system_addressbook = osso_abook_system_book_dup_singleton(FALSE, NULL);

  if (system_addressbook)
  {
    e_book_async_open(system_addressbook, FALSE, _book_open_cb, NULL);
    contact_model = osso_abook_contact_model_new();
    contact_chooser =
        osso_abook_contact_chooser_new_with_capabilities(
          parent, NULL, OSSO_ABOOK_CAPS_PHONE, OSSO_ABOOK_CONTACT_ORDER_NAME);
    gtk_window_set_modal(GTK_WINDOW(contact_chooser), TRUE);
    gtk_widget_show_all(contact_chooser);

    return GTK_DIALOG(contact_chooser);
  }

  if (!contact_model)
    return NULL;

  g_object_unref(contact_model);
  contact_model = NULL;
  return NULL;
}

void
cellular_abook_destroy()
{
  if (contact_model)
  {
    g_object_unref(contact_model);
    contact_model = NULL;
  }

  if (contact_chooser)
  {
    gtk_widget_destroy(contact_chooser);
    contact_chooser = NULL;
  }
}

char *
cellular_abook_get_selected_number()
{
  GList *contact;
  GList *attr;
  char *phone_number = NULL;

  g_return_val_if_fail(contact_chooser != NULL, NULL);

  contact = osso_abook_contact_chooser_get_selection(
        OSSO_ABOOK_CONTACT_CHOOSER(contact_chooser));

  if (!contact)
    return NULL;

  attr = osso_abook_contact_get_attributes(E_CONTACT(contact->data), EVC_TEL);

  if (g_list_length(attr) <= 1)
  {
    phone_number = e_vcard_attribute_get_value(
          e_vcard_get_attribute(E_VCARD(contact->data), EVC_TEL));
  }
  else
  {
    GtkWidget *detail_selector =
        osso_abook_contact_detail_selector_new_for_contact(
          GTK_WINDOW(contact_chooser), contact->data,
          OSSO_ABOOK_CONTACT_DETAIL_PHONE);

    if (gtk_dialog_run(GTK_DIALOG(detail_selector)) == GTK_RESPONSE_OK)
    {
      EVCardAttribute *tel = osso_abook_contact_detail_selector_get_detail(
            OSSO_ABOOK_CONTACT_DETAIL_SELECTOR(detail_selector));
      phone_number = e_vcard_attribute_get_value(tel);
    }

    gtk_widget_destroy(detail_selector);
  }

  g_list_free(attr);
  g_list_free(contact);

  return phone_number;
}
