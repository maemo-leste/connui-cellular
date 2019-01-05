hildondesktoplib_LTLIBRARIES =  connui-cellular-operator-home-item.la

connui_cellular_operator_home_item_la_SOURCES = \
			operator-name-cbs-home-item.c \
			operator-name-cbs-home-item.h \
			smsutil.c smsutil.h \
			util.c \
			util.h
connui_cellular_operator_home_item_la_CFLAGS = \
		-Wall -Werror $(LIBHILDON_CFLAGS) \
		$(HILDON_CFLAGS) $(CONNUI_CFLAGS) $(OSSO_CFLAGS) \
		-I$(top_builddir)/include -DG_LOG_DOMAIN=\"$(PACKAGE)\" \
		-DOSSOLOG_COMPILE=1

connui_cellular_operator_home_item_la_LDFLAGS = \
			-Wl,--as-needed $(LIBHILDON_LIBS) \
			$(HILDON_LIBS) $(CONNUI_LIBS) $(OSSO_LIBS) -module \
			-avoid-version -Wl, no-undefined

connui_cellular_operator_home_item_la_LIBADD = $(OPERATORNAMECBSHOMEITEM_LIBS)

MAINTAINERCLEANFILES = Makefile.in
