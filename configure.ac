dnl This is the configure.ac for the gnonlin library
AC_INIT
AC_CANONICAL_TARGET

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE

dnl when going to/from release please set the nano (fourth number) right !
AS_VERSION(gnonlin, GNL_VERSION, 0, 1, 9, 1, GNL_CVS="no", GNL_CVS="yes")

dnl Required GStreamer version
GST_REQ=0.8.9

AM_INIT_AUTOMAKE($PACKAGE, $VERSION)

GST_MAJORMINOR=0.8
AC_SUBST(GST_MAJORMINOR)

dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
AS_LIBTOOL(GNL, 1, 0, 0)
AM_PROG_LIBTOOL()

AC_CONFIG_SRCDIR(configure.ac)
AM_CONFIG_HEADER(config.h)

dnl Add parameters for aclocal
dnl (This must come after AM_INIT_AUTOMAKE, since it modifies ACLOCAL)
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4 $ACLOCAL_FLAGS")

AC_PROG_CC
AC_PROG_CC_STDC
AS="${CC}"

dnl decide on error flags
AS_COMPILER_FLAG(-Wall, GST_WALL="yes", GST_WALL="no")

if test "x$GST_WALL" = "xyes"; then
   GST_ERROR="$GST_ERROR -Wall"

   if test "x$GST_CVS" = "xyes"; then
     AS_COMPILER_FLAG(-Werror,GST_ERROR="$GST_ERROR -Werror",GST_ERROR="$GST_ERROR")
     GST_ERROR="$GST_ERROR -DG_DISABLE_DEPRECATED"
   fi
fi

AC_HEADER_STDC([])

dnl modify pkg-config path
AC_ARG_WITH(pkg-config-path, 
   AC_HELP_STRING([--with-pkg-config-path],
                  [colon-separated list of pkg-config(1) dirs]),
   [export PKG_CONFIG_PATH=${withval}])

GST_DOC()
GST_ARCH()


dnl check for gstreamer; uninstalled is selected preferentially -- see pkg-config(1)
PKG_CHECK_MODULES(GST, gstreamer-$GST_MAJORMINOR >= $GST_REQ,
  HAVE_GST="yes", HAVE_GST="no")
                                                                                
if test "x$HAVE_GST" = "xno"; then
  AC_MSG_ERROR(no GStreamer found)
fi

GST_PLUGIN_LDFLAGS="-module -avoid-version $EXPORT_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS)

plugindir = "\$(libdir)/gstreamer-$GST_MAJORMINOR"
AC_SUBST(plugindir)

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
  docs/Makefile
  docs/libs/Makefile
  gnonlin-config
  gnonlin.pc
  gnonlin.spec
])
