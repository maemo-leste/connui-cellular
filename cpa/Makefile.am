controlpanelpluginlib_LTLIBRARIES = libcellularsettings.la

libcellularsettings_la_SOURCES = net-selection.c \
				 cellularsettings.c

libcellularsettings_la_CFLAGS = \
		$(OSSO_CFLAGS) $(HILDON_CFLAGS) $(CPL_CFLAGS) $(CONNUI_CFLAGS) \
		$(ICD2_CFLAGS) -I$(top_builddir)/include \
		-DG_LOG_DOMAIN=\"$(PACKAGE)\" -DOSSOLOG_COMPILE=1

libcellularsettings_la_LDFLAGS = -Wl,--as-needed -module -avoid-version

libcellularsettings_la_LIBADD = $(OSSO_LIBS) $(HILDON_LIBS) \
				$(CPL_LIBS) $(CONNUI_LIBS) $(ICD2_LIBS)

controlpanelplugindesktopentry_DATA = connui-cellular-settings.desktop
EXTRA_DIST = $(controlpanelplugindesktopentry_DATA)

MAINTAINERCLEANFILES = Makefile.in
