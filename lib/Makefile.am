AM_CFLAGS = -Wall -Werror $(HILDON_CFLAGS) $(CONNUI_CFLAGS) $(LIBGOFONO_CFLAGS) $(CLUI_CFLAGS) \
	    $(IAPSETTINGS_CFLAGS) $(TPGLIB_CFLAGS) -DG_LOG_DOMAIN=\"$(PACKAGE)\" \
	    -I$(top_builddir)/include -DOSSOLOG_COMPILE=1
AM_LDFLAGS = -Wl,--as-needed $(HILDON_LIBS) $(CONNUI_LIBS) $(LIBGOFONO_LIBS) $(IAPSETTINGS_LIBS) \
	     $(CLUI_LIBS) $(TPGLIB_LIBS) -Wl, no-undefined

lib_LTLIBRARIES = libconnui_cell.la

libconnui_cell_la_SOURCES = connui-cell-note.c \
			    context.c \
			    connui-cell-marshal.c \
			    network.c \
			    net.c \
			    sim.c \
			    emergency.c \
			    security_code.c \
			    ssc.c \
			    call.c \
			    datacounter.c \
			    code-ui.c \
			    ofono.c

BUILT_SOURCES = connui-cell-marshal.c connui-cell-marshal.h

connui-cell-marshal.c: connui-cell-marshal.list
	$(GLIB_GENMARSHAL) --prefix=connui_cell $< --body --internal > xgen-$(@F)	\
	&& ( cmp -s xgen-$(@F) $@ || cp xgen-$(@F) $@ )						\
	&& rm -f xgen-$(@F)

connui-cell-marshal.h: connui-cell-marshal.list
	$(GLIB_GENMARSHAL) --prefix=connui_cell $< --header --internal > xgen-$(@F)		\
	&& ( cmp -s xgen-$(@F) $@ || cp xgen-$(@F) $@ )						\
	&& rm -f xgen-$(@F)

CLEANFILES = $(BUILT_SOURCES)

MAINTAINERCLEANFILES = Makefile.in
