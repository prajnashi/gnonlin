dnl This is the configure.ac for the gnonlin library
AC_INIT

dnl when going to/from release please set the nano (fourth number) right !
AS_VERSION(gnonlin, GNL_VERSION, 0, 1, 9, 0, GNL_CVS="no", GNL_CVS="yes")
AM_INIT_AUTOMAKE($PACKAGE, $VERSION)

dnl We disable static building for development, for time savings
dnl AM_DISABLE_STATIC
AS_LIBTOOL(GNL, 0, 0, 0, yes)
dnl AC_LIBTOOL_DLOPEN
AM_PROG_LIBTOOL

AC_CONFIG_SRCDIR([gnl/gnl.c])
AM_CONFIG_HEADER(config.h)

dnl libtool
dnl GNL_CURRENT=0
dnl GNL_REVISION=0
dnl GNL_AGE=0

dnl Add parameters for aclocal
dnl (This must come after AM_INIT_AUTOMAKE, since it modifies ACLOCAL)
ACLOCAL="$ACLOCAL -I common/m4 $ACLOCAL_FLAGS"

AC_PROG_CC
AC_PROG_CC_STDC

dnl stuff found by autoscan
AC_CHECK_FUNCS([strstr])
AC_CHECK_HEADERS([unistd.h])
AC_C_CONST
AC_FUNC_STAT

dnl GST_DEBUGINFO

dnl modify pkg-config path
AC_ARG_WITH(pkg-config-path, 
   AC_HELP_STRING([--with-pkg-config-path],[colon-separated list of pkg-config(1) dirs]),
   [export PKG_CONFIG_PATH=${withval}])

dnl check for appropriate GStreamer libs. first check for 0.8, then 0.7.
HAVE_GSTREAMER=no
GSTREAMER_REQ="0.7.1"
AC_SUBST(GSTREAMER_REQ)

GSTREAMER_MAJORMINOR="0.8"
PKG_CHECK_MODULES(GST, gstreamer-$GSTREAMER_MAJORMINOR >= $GSTREAMER_REQ,
                       HAVE_GSTREAMER=yes,HAVE_GSTREAMER=no)
if test "x$HAVE_GSTREAMER" = "xno"; then
GSTREAMER_MAJORMINOR="0.7"
PKG_CHECK_MODULES(GST, gstreamer-$GSTREAMER_MAJORMINOR >= $GSTREAMER_REQ,
                       HAVE_GSTREAMER=yes,HAVE_GSTREAMER=no)
fi

if test "x$HAVE_GSTREAMER" = "xno"; then
   AC_MSG_ERROR([No GStreamer development libraries found])
fi

dnl get the genmarshal command for the *.list files
GLIB_GENMARSHAL=`pkg-config --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)

AC_SUBST(GNL_CFLAGS, "$GST_CFLAGS")
AC_SUBST(GNL_LIBS, "$GST_LIBS")

AC_DEFINE(GNL_LICENSE, "LGPL", [Gnonlin license])
AC_DEFINE(GNL_PACKAGE, "Gnonlin", [Gnonlin package])
AC_DEFINE(GNL_ORIGIN, "http://gnonlin.sf.net", [Gnonlin origin])

AC_OUTPUT([
  Makefile
  gnl/Makefile
  gnl/gnlversion.h
  test/Makefile
  gnonlin-config
  gnonlin.pc
])
