hildondesktoplib_LTLIBRARIES = connui-cellular-status-item.la

AM_CFLAGS = -Wall -Werror $(ICD2_CFLAGS) $(LIBHILDON_CFLAGS) \
	    $(HILDON_CFLAGS) $(CONNUI_CFLAGS) $(OSSO_CFLAGS) \
	    -I$(top_builddir)/include -DG_LOG_DOMAIN=\"$(PACKAGE)\" \
	    -DOSSOLOG_COMPILE=1

AM_LDFLAGS = -Wl,--as-needed $(ICD2_LIBS) $(LIBHILDON_LIBS) \
	     $(HILDON_LIBS) $(CONNUI_LIBS) $(OSSO_LIBS) -module \
	     -avoid-version -Wl, no-undefined

connui_cellular_status_item_la_SOURCES = status-item.c

connui_cellular_status_item_la_LIBADD = $(top_builddir)/lib/libconnui_cell.la

hildonstatusmenudesktopentry_DATA = connui-cellular.desktop
EXTRA_DIST = $(hildonstatusmenudesktopentry_DATA)

MAINTAINERCLEANFILES = Makefile.in
