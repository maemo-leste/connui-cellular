AM_CFLAGS = -Wall -Werror $(HILDON_CFLAGS) $(CONNUI_CFLAGS) $(GIOUNIX_CFLAGS) \
	    $(CLUI_CFLAGS) $(IAPSETTINGS_CFLAGS) \
	    $(TPGLIB_CFLAGS) $(XML_CFLAGS) -DG_LOG_DOMAIN=\"$(PACKAGE)\" \
	    -I$(top_builddir)/include -DOSSOLOG_COMPILE=1 \
	    -DMBPI_DATABASE=\"$(MBPI_DATABASE)\"
AM_LDFLAGS = -Wl,--as-needed $(HILDON_LIBS) $(CONNUI_LIBS) \
	     $(IAPSETTINGS_LIBS) $(CLUI_LIBS) $(TPGLIB_LIBS) $(XML_LIBS) \
	     $(GIOUNIX_LIBS) -Wl, no-undefined

lib_LTLIBRARIES = libconnui_cell.la

OFONO_GDBUS_WRAPPERS = org.ofono.Manager.c\
		       org.ofono.Modem.c \
		       org.ofono.SimManager.c  \
		       org.ofono.NetworkRegistration.c \
		       org.ofono.VoiceCallManager.c \
		       org.ofono.SupplementaryServices.c \
		       org.ofono.ConnectionManager.c

libconnui_cell_la_SOURCES = $(OFONO_GDBUS_WRAPPERS) \
			    connui-cell-note.c \
			    context.c \
			    connui-cell-marshal.c \
			    network.c \
			    service-call.c \
			    sim.c \
			    net.c \
			    sups.c \
			    connmgr.c \
			    modem.c \
			    emergency.c \
			    call.c \
			    datacounter.c \
			    code-ui.c


BUILT_SOURCES = connui-cell-marshal.c connui-cell-marshal.h \
		$(OFONO_GDBUS_WRAPPERS) $(OFONO_GDBUS_WRAPPERS:.c=.h)

connui-cell-marshal.c: connui-cell-marshal.list
	$(GLIB_GENMARSHAL) --prefix=connui_cell $< --body --internal > xgen-$(@F)	\
	&& ( cmp -s xgen-$(@F) $@ || cp xgen-$(@F) $@ )						\
	&& rm -f xgen-$(@F)

connui-cell-marshal.h: connui-cell-marshal.list
	$(GLIB_GENMARSHAL) --prefix=connui_cell $< --header --internal > xgen-$(@F)		\
	&& ( cmp -s xgen-$(@F) $@ || cp xgen-$(@F) $@ )						\
	&& rm -f xgen-$(@F)

.NOTPARALLEL:
%.c: $(top_builddir)/data/%.xml
	gdbus-codegen --generate-c-code $(@:%.c=%) $<

CLEANFILES = $(BUILT_SOURCES)

MAINTAINERCLEANFILES = Makefile.in
