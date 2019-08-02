AC_PREREQ([2.52])

AC_INIT([cellular-settings], patsubst(esyscmd([dpkg-parsechangelog | sed -n '/^Version: \(.*\)$/ {s//\1/;p}']), [
]), [], [cellular-settings])

LT_CURRENT=1
LT_REVISION=0
LT_AGE=0
AC_SUBST([LT_CURRENT])
AC_SUBST([LT_REVISION])
AC_SUBST([LT_AGE])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE
AM_CONFIG_HEADER(config.h)
AC_CONFIG_FILES([
	Makefile
	data/Makefile
	conndlgs/Makefile
	lib/Makefile
	wizard/Makefile
	status-menu-item/Makefile
	home-item/Makefile
	cpa/Makefile
	connui-cellular.pc
])

AC_ISC_POSIX
AC_PROG_CC
AM_PROG_CC_STDC
AC_HEADER_STDC
AC_DISABLE_STATIC
AC_PROG_LIBTOOL
AM_GCONF_SOURCE_2

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)

PKG_CHECK_MODULES(OSSO_AF_SETTINGS, osso-af-settings)
localedir=`pkg-config osso-af-settings --variable=localedir`
AC_SUBST(localedir)

PKG_CHECK_MODULES(X11, x11)
AC_SUBST(X11_CFLAGS)
AC_SUBST(X11_LIBS)

PKG_CHECK_MODULES(OSSO, libosso)
AC_SUBST(OSSO_CFLAGS)
AC_SUBST(OSSO_LIBS)

PKG_CHECK_MODULES(HILDON, hildon-1)
AC_SUBST(HILDON_CFLAGS)
AC_SUBST(HILDON_LIBS)

PKG_CHECK_MODULES(CONNUI, connui)
AC_SUBST(CONNUI_CFLAGS)
AC_SUBST(CONNUI_LIBS)

PKG_CHECK_MODULES(LIBGOFONO, libgofono libglibutil)
AC_SUBST(LIBGOFONO_CFLAGS)
AC_SUBST(LIBGOFONO_LIBS)

PKG_CHECK_MODULES(CONBTUI, conbtui)
AC_SUBST(CONBTUI_CFLAGS)
AC_SUBST(CONBTUI_LIBS)

PKG_CHECK_MODULES(CLUI, codelockui)
AC_SUBST(CLUI_CFLAGS)
AC_SUBST(CLUI_LIBS)

PKG_CHECK_MODULES(GMODULE, gmodule-2.0)
AC_SUBST(GMODULE_CFLAGS)
AC_SUBST(GMODULE_LIBS)

PKG_CHECK_MODULES(GCONF, gconf-2.0)
AC_SUBST(GCONF_CFLAGS)
AC_SUBST(GCONF_LIBS)

PKG_CHECK_MODULES(IAPSETTINGS, connui-iapsettings)
AC_SUBST(IAPSETTINGS_CFLAGS)
AC_SUBST(IAPSETTINGS_LIBS)

PKG_CHECK_MODULES(LIBHILDON, libhildondesktop-1)
AC_SUBST(LIBHILDON_CFLAGS)
AC_SUBST(LIBHILDON_LIBS)

PKG_CHECK_MODULES(ICD2, icd2)
AC_SUBST(ICD2_CFLAGS)
AC_SUBST(ICD2_LIBS)

PKG_CHECK_MODULES(TPGLIB, telepathy-glib)
AC_SUBST(TPGLIB_CFLAGS)
AC_SUBST(TPGLIB_LIBS)

PKG_CHECK_MODULES([CPL], [hildon-control-panel])
AC_SUBST([CPL_CFLAGS])
AC_SUBST([CPL_LIBS])

dnl Localization
GETTEXT_PACKAGE=osso-connectivity-ui
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "${GETTEXT_PACKAGE}", [gettext package])

dnl Installation directories
hildondesktoplibdir="`$PKG_CONFIG --variable=hildondesktoplibdir libhildondesktop-1`"
AC_SUBST(hildondesktoplibdir)

hildonstatusmenudesktopentrydir="`$PKG_CONFIG --variable=hildonstatusmenudesktopentrydir libhildondesktop-1`"
AC_SUBST(hildonstatusmenudesktopentrydir)

controlpanelpluginlibdir=`pkg-config hildon-control-panel --variable=pluginlibdir`
AC_SUBST([controlpanelpluginlibdir])

controlpanelplugindesktopentrydir=`pkg-config hildon-control-panel --variable=plugindesktopentrydir`
AC_SUBST([controlpanelplugindesktopentrydir])

conndlgslibdir="`$PKG_CONFIG --variable=pluginlibdir connui`"
AC_SUBST(conndlgslibdir)

iapsettingslibdir="`$PKG_CONFIG --variable=pluginlibdir connui-iapsettings`"
AC_SUBST(iapsettingslibdir)

AC_ARG_ENABLE(cast-checks,  [  --disable-cast-checks   compile with GLIB cast checks disabled],[cchecks=${enableval}],cchecks=yes)
if test "x$cchecks" = "xno"; then
    CFLAGS="$CFLAGS -DG_DISABLE_CAST_CHECKS"
fi

AC_ARG_ENABLE(asserts,      [  --disable-asserts       compile with GLIB assertions disabled],[asserts=${enableval}],asserts=yes)
if test "x$asserts" = "xno"; then
    CFLAGS="$CFLAGS -DG_DISABLE_ASSERTS"
fi

AC_ARG_ENABLE(checks,       [  --disable-checks        compile with GLIB checks disabled],[checks=${enableval}],checks=yes)
if test "x$checks" = "xno"; then
    CFLAGS="$CFLAGS -DG_DISABLE_CHECKS"
fi

AC_ARG_ENABLE(debug,     [  --enable-debug          enable printing of debug messages],[ddebug=${enableval}],ddebug=no)
if test "x$ddebug" != "xyes"; then
    CFLAGS="$CFLAGS -DG_DEBUG_DISABLE"
fi

AC_ARG_ENABLE([maemo-launcher],
              [AS_HELP_STRING([--enable-maemo-launcher],
                              [build with maemo-launcher support])],
                              [case "${enableval}" in
                               yes) maemo_launcher=true ;;
                               no)  maemo_launcher=false ;;
                               *) AC_MSG_ERROR([bad value ${enableval} for --enable-maemo-launcher]) ;;
                               esac], [maemo_launcher=false])

MAEMO_LAUNCHER_PACKAGE=""
if test "x$maemo_launcher" = "xtrue"; then
  MAEMO_LAUNCHER_PACKAGE="maemo-launcher-app"
  PKG_CHECK_MODULES(LAUNCHER_DEPS, $MAEMO_LAUNCHER_PACKAGE)
else
  LAUNCHER_DEPS_CFLAGS=""
  LAUNCHER_DEPS_LIBS=""
fi

AC_SUBST(LAUNCHER_DEPS_CFLAGS)
AC_SUBST(LAUNCHER_DEPS_LIBS)

AC_OUTPUT
