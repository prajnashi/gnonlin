AC_INIT
AS_VERSION(gnonlin, GNL_VERSION, 0, 1, 9)

AC_CONFIG_SRCDIR(gnl/gnl.c)
AM_CONFIG_HEADER(config.h)

dnl libtool
GNL_CURRENT=0
GNL_REVISION=0
GNL_AGE=0

ACLOCAL="$ACLOCAL -I common/m4 $ACLOCAL_FLAGS"

AC_PROG_CC
AC_STDC_HEADERS
AM_DISABLE_STATIC
AC_LIBTOOL_DLOPEN
AM_PROG_LIBTOOL

dnl modify pkg-config path
AC_ARG_WITH(pkg-config-path, 
   AC_HELP_STRING([--with-pkg-config-path],[colon-separated list of pkg-config(1) dirs]),
   [export PKG_CONFIG_PATH=${withval}])

PKG_CHECK_MODULES(GST, gstreamer)

dnl get the genmarshal command for the *.list files
GLIB_GENMARSHAL=`pkg-config --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)

AC_SUBST(GNL_CFLAGS, "$GNOME_CFLAGS $GST_CFLAGS")
AC_SUBST(GNL_LIBS, "$GNOME_LIBS $GST_LIBS")

AC_OUTPUT([
  Makefile
  gnl/Makefile
  gnl/gnlversion.h
  test/Makefile
  gnonlin-config
])
