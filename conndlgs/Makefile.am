bin_PROGRAMS = pin-code-correct

pin_code_correct_SOURCES = pin_code_correct.c
pin_code_correct_CFLAGS = \
		$(OSSO_CFLAGS) $(HILDON_CFLAGS) \
		-DG_LOG_DOMAIN=\"$(PACKAGE)\" \
		-DOSSOLOG_COMPILE=1

pin_code_correct_LDADD = $(OSSO_LIBS) $(HILDON_LIBS)

MAINTAINERCLEANFILES = Makefile.in
