#include <hildon/hildon-note.h>

#include "connui-cell-note.h"

typedef struct _ConnuiCellNotePrivate ConnuiCellNotePrivate;

struct _ConnuiCellNotePrivate
{
  GdkWindow *window;
};

G_DEFINE_TYPE_WITH_PRIVATE(ConnuiCellNote, connui_cell_note, HILDON_TYPE_NOTE)

static GtkDialogClass *connui_cell_note_dialog_class;

static void
connui_cell_note_init(ConnuiCellNote *self)
{
}

static void
connui_cell_note_finalize(GObject *object)
{
  G_OBJECT_CLASS(connui_cell_note_parent_class)->finalize(object);
}

static void
connui_cell_note_map(GtkWidget *widget)
{
  ConnuiCellNotePrivate *priv =
      connui_cell_note_get_instance_private(CONNUI_CELL_NOTE(widget));
  GdkWindow *window;
  GdkWindowAttr attributes;

  GTK_WIDGET_CLASS(connui_cell_note_parent_class)->map(widget);

  if (priv->window)
    return;

  attributes.height = 10;
  attributes.width = 10;
  attributes.x = 0;
  attributes.window_type = GDK_WINDOW_TEMP;
  attributes.y = 0;
  attributes.event_mask = 0;
  attributes.wclass = GDK_INPUT_ONLY;
  attributes.override_redirect = TRUE;

  window = gdk_window_new(gtk_widget_get_root_window(widget),
                          &attributes,
                          GDK_WA_TYPE_HINT | GDK_WA_Y | GDK_WA_X);
  gdk_window_set_user_data(window, widget);
  gdk_window_show(window);
  priv->window = window;

  if (gdk_pointer_grab(window, TRUE, GDK_BUTTON_RELEASE_MASK, NULL, NULL, 0) ==
      GDK_GRAB_SUCCESS)
  {
    if (gdk_keyboard_grab(priv->window, TRUE, 0) == GDK_GRAB_SUCCESS)
      gtk_grab_add(widget);
    else
      gdk_display_pointer_ungrab(gtk_widget_get_display(widget), 0);
  }
  else
  {
    gdk_window_destroy(priv->window);
    priv->window = NULL;
  }
}

static void
connui_cell_note_unmap(GtkWidget *widget)
{
  ConnuiCellNotePrivate *priv =
      connui_cell_note_get_instance_private(CONNUI_CELL_NOTE(widget));

  if (priv->window)
  {
    gdk_display_pointer_ungrab(gtk_widget_get_display(widget), 0);
    gtk_grab_remove(widget);
    gdk_window_destroy(priv->window);
    priv->window = NULL;
  }

  GTK_WIDGET_CLASS(connui_cell_note_parent_class)->unmap(widget);
}

static gboolean
connui_cell_note_button_release_event(GtkWidget *widget, GdkEventButton *event)
{
  GdkRectangle rect;

  gdk_window_get_frame_extents(widget->window, &rect);

  if (event->x_root < rect.x || event->x_root > (rect.x + rect.width) ||
      event->y_root < rect.y || event->y_root > rect.y + rect.height)
  {
    GTK_WIDGET_CLASS(connui_cell_note_parent_class)->
        button_release_event(widget, event);
    gtk_dialog_response(GTK_DIALOG(widget), GTK_RESPONSE_CANCEL);
  }

  return FALSE;
}

static void
connui_cell_note_class_init(ConnuiCellNoteClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

  G_OBJECT_CLASS(klass)->finalize = connui_cell_note_finalize;

  connui_cell_note_dialog_class =
      GTK_DIALOG_CLASS(g_type_class_peek_parent(klass));

  widget_class->button_release_event = connui_cell_note_button_release_event;
  widget_class->map = connui_cell_note_map;
  widget_class->unmap = connui_cell_note_unmap;
}

GtkWidget *
connui_cell_note_new_information(GtkWindow *parent, const gchar *text)
{
  GtkWidget *note;

  g_return_val_if_fail(text != NULL, NULL);

  note = g_object_new(CONNUI_TYPE_CELL_NOTE,
                      "note-type", HILDON_NOTE_TYPE_INFORMATION_THEME,
                      "description", text,
                      NULL);
  if (parent)
    gtk_window_set_transient_for(GTK_WINDOW(note), parent);

  return note;
}
