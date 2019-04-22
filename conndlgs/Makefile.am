bin_PROGRAMS = pin-code-correct

pin_code_correct_SOURCES = pin_code_correct.c
pin_code_correct_CFLAGS = \
		$(OSSO_CFLAGS) $(HILDON_CFLAGS) \
		-DG_LOG_DOMAIN=\"$(PACKAGE)\" \
		-DOSSOLOG_COMPILE=1

pin_code_correct_LDADD = $(OSSO_LIBS) $(HILDON_LIBS)

CONNDLGS_LDFLAGS = -module -avoid-version

conndlgslib_LTLIBRARIES = \
	libcell_dialog_roaming.la \
	libcell_dialog_error_note.la \
	libcell_dialog_oma_cp_ui.la

libcell_dialog_roaming_la_SOURCES = cell-dialog-roaming.c
libcell_dialog_roaming_la_CFLAGS =  \
			$(OSSO_CFLAGS) $(HILDON_CFLAGS) $(CONNUI_CFLAGS) \
			-DG_LOG_DOMAIN=\"$(PACKAGE)\" -DOSSOLOG_COMPILE=1

libcell_dialog_roaming_la_LIBADD = $(OSSO_LIBS) $(HILDON_LIBS) $(CONNUI_LIBS)
libcell_dialog_roaming_la_LDFLAGS = $(CONNDLGS_LDFLAGS)

libcell_dialog_error_note_la_SOURCES = cell-dialog-error-note.c
libcell_dialog_error_note_la_CFLAGS =  \
			$(OSSO_CFLAGS) $(HILDON_CFLAGS) $(CONNUI_CFLAGS) \
			-DG_LOG_DOMAIN=\"$(PACKAGE)\" -DOSSOLOG_COMPILE=1 \
			-I$(top_builddir)/include

libcell_dialog_error_note_la_LIBADD = $(OSSO_LIBS) $(HILDON_LIBS) $(CONNUI_LIBS)
libcell_dialog_error_note_la_LDFLAGS = $(CONNDLGS_LDFLAGS)

libcell_dialog_oma_cp_ui_la_SOURCES = cell-dialog-oma-cp-ui.c
libcell_dialog_oma_cp_ui_la_CFLAGS =  \
			$(OSSO_CFLAGS) $(HILDON_CFLAGS) $(CONNUI_CFLAGS) \
			-DG_LOG_DOMAIN=\"$(PACKAGE)\" -DOSSOLOG_COMPILE=1 \
			$(CLUI_CFLAGS) -I$(top_builddir)/include

libcell_dialog_oma_cp_ui_la_LIBADD = \
			$(OSSO_LIBS) $(HILDON_LIBS) $(CONNUI_LIBS) $(CLUI_LIBS)
libcell_dialog_oma_cp_ui_la_LDFLAGS = $(CONNDLGS_LDFLAGS)

MAINTAINERCLEANFILES = Makefile.in
