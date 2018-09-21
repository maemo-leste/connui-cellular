AM_CFLAGS = -Wall -Werror $(HILDON_CFLAGS) $(CONNUI_CFLAGS) \
	    $(IAPSETTINGS_CFLAGS) -DG_LOG_DOMAIN=\"$(PACKAGE)\" \
	    -DOSSOLOG_COMPILE=1
AM_LDFLAGS = -Wl,--as-needed $(HILDON_LIBS) $(CONNUI_LIBS) $(IAPSETTINGS_LIBS) \
	     -module -avoid-version -Wl, no-undefined

iapsettingslib_LTLIBRARIES = libiap_wizard_gprs.la

libiap_wizard_gprs_la_SOURCES = gprs.c
