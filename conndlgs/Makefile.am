bin_PROGRAMS = pin-code-correct startup-pin-query

pin_code_correct_SOURCES = pin-code-correct.c
pin_code_correct_CFLAGS = \
		$(OSSO_CFLAGS) $(HILDON_CFLAGS) $(LAUNCHER_DEPS_CFLAGS)\
		-DG_LOG_DOMAIN=\"$(PACKAGE)\" \
		-DOSSOLOG_COMPILE=1
pin_code_correct_LDADD = $(OSSO_LIBS) $(HILDON_LIBS) $(LAUNCHER_DEPS_LIBS)
pin_code_correct_LDFLAGS = -Wl,--as-needed

startup_pin_query_SOURCES = startup-pin-query.c
startup_pin_query_CFLAGS = \
		$(OSSO_CFLAGS) $(HILDON_CFLAGS) $(CONNUI_CFLAGS) $(LIBGOFONO_CFLAGS) \
		$(CONBTUI_GFLAGS) $(LAUNCHER_DEPS_CFLAGS) \
		-I$(top_builddir)/include -DG_LOG_DOMAIN=\"$(PACKAGE)\" \
		-DOSSOLOG_COMPILE=1
startup_pin_query_LDADD = \
		$(OSSO_LIBS) $(HILDON_LIBS) $(CONNUI_LIBS) $(LIBGOFONO_LIBS) $(CONBTUI_LIBS) \
		$(LAUNCHER_DEPS_LIBS) $(top_builddir)/lib/libconnui_cell.la
startup_pin_query_LDFLAGS = -Wl,--as-needed

CONNDLGS_LDFLAGS = -Wl,--as-needed -module -avoid-version

conndlgslib_LTLIBRARIES = \
	libcell_dialog_roaming.la \
	libcell_dialog_error_note.la \
	libcell_dialog_oma_cp_ui.la

libcell_dialog_roaming_la_SOURCES = cell-dialog-roaming.c
libcell_dialog_roaming_la_CFLAGS = -I$(top_builddir)/include \
			$(OSSO_CFLAGS) $(HILDON_CFLAGS) $(CONNUI_CFLAGS) \
			-DG_LOG_DOMAIN=\"$(PACKAGE)\" -DOSSOLOG_COMPILE=1
libcell_dialog_roaming_la_LIBADD = $(OSSO_LIBS) $(HILDON_LIBS) $(CONNUI_LIBS)
libcell_dialog_roaming_la_LDFLAGS = $(CONNDLGS_LDFLAGS)

libcell_dialog_error_note_la_SOURCES = cell-dialog-error-note.c
libcell_dialog_error_note_la_CFLAGS =  \
			$(OSSO_CFLAGS) $(HILDON_CFLAGS) $(CONNUI_CFLAGS) \
			-DG_LOG_DOMAIN=\"$(PACKAGE)\" -DOSSOLOG_COMPILE=1 \
			-I$(top_builddir)/include
libcell_dialog_error_note_la_LIBADD = \
			$(OSSO_LIBS) $(HILDON_LIBS) $(CONNUI_LIBS) \
			$(top_builddir)/lib/libconnui_cell.la
libcell_dialog_error_note_la_LDFLAGS = $(CONNDLGS_LDFLAGS)

libcell_dialog_oma_cp_ui_la_SOURCES = cell-dialog-oma-cp-ui.c
libcell_dialog_oma_cp_ui_la_CFLAGS =  \
			$(OSSO_CFLAGS) $(HILDON_CFLAGS) $(CONNUI_CFLAGS) \
			-DG_LOG_DOMAIN=\"$(PACKAGE)\" -DOSSOLOG_COMPILE=1 \
			$(CLUI_CFLAGS) -I$(top_builddir)/include
libcell_dialog_oma_cp_ui_la_LIBADD = \
			$(OSSO_LIBS) $(HILDON_LIBS) $(CONNUI_LIBS) \
			$(CLUI_LIBS) $(top_builddir)/lib/libconnui_cell.la
libcell_dialog_oma_cp_ui_la_LDFLAGS = $(CONNDLGS_LDFLAGS)

MAINTAINERCLEANFILES = Makefile.in
