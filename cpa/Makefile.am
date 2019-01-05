controlpanelpluginlib_LTLIBRARIES = operator-name-cbs-cpa.la

operator_name_cbs_cpa_la_SOURCES = operator-name-cbs-cpa.c
operator_name_cbs_cpa_la_CFLAGS = \
		$(OSSO_CFLAGS) $(HILDON_CFLAGS) $(CPL_CFLAGS) $(CONNUI_CFLAGS) \
		-I$(top_builddir)/include -DG_LOG_DOMAIN=\"$(PACKAGE)\" \
		-DOSSOLOG_COMPILE=1

operator_name_cbs_cpa_la_LDFLAGS = -Wl,--as-needed -module -avoid-version

operator_name_cbs_cpa_la_LIBADD = $(OSSO_LIBS) $(HILDON_LIBS) \
				  $(CPL_LIBS) $(CONNUI_LIBS)

controlpanelplugindesktopentry_DATA = operator-name-cbs-cpa.desktop
EXTRA_DIST = $(controlpanelplugindesktopentry_DATA)

MAINTAINERCLEANFILES = Makefile.in
