#ifndef CONNUICELLNOTE_H
#define CONNUICELLNOTE_H

#define CONNUI_TYPE_CELL_NOTE (connui_cell_note_get_type())

#define CONNUI_CELL_NOTE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), CONNUI_TYPE_CELL_NOTE, ConnuiCellNote))

#define CONNUI_IS_CELL_NOTE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CONNUI_TYPE_CELL_NOTE))

#define CONNUI_CELL_NOTE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), CONNUI_TYPE_CELL_NOTE, ConnuiCellNoteClass))

GType connui_cell_note_get_type(void) G_GNUC_CONST;

typedef struct _ConnuiCellNote ConnuiCellNote;
typedef struct _ConnuiCellNoteClass ConnuiCellNoteClass;

struct _ConnuiCellNote
{
  HildonNote parent_instance;
};

struct _ConnuiCellNoteClass
{
  HildonNoteClass parent_class;
};

ConnuiCellNote *connui_cell_note_new_information(GtkWindow *parent,
                                                 const gchar *text);

#endif // CONNUICELLNOTE_H
