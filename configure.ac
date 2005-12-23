dnl autoconf configuration file for gnonlin
AC_INIT
AC_CANONICAL_TARGET

dnl We disable static building for development, for time savings
dnl this goes before AS_LIBTOOL to appease autoconf
dnl *NOTE*: dnl this line before release, so release does static too
AM_DISABLE_STATIC

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE

dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AS_VERSION(gnonlin, GNONLIN_VERSION, 0, 10, 0, 3,
    GNL_CVS="no", GNL_CVS="yes")

AM_INIT_AUTOMAKE($PACKAGE,$VERSION)

dnl our libraries and install dirs use major.minor as a version
GST_MAJORMINOR=$GNONLIN_VERSION_MAJOR.$GNONLIN_VERSION_MINOR
dnl we override it here if we need to for the release candidate of new series
GST_MAJORMINOR=0.10
AC_SUBST(GST_MAJORMINOR)

dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
AS_LIBTOOL(GNONLIN, 0, 0, 0)
AS_LIBTOOL_TAGS
AM_PROG_LIBTOOL

AC_CONFIG_SRCDIR([gnl/gnl.c])
AM_CONFIG_HEADER(config.h)

dnl Add parameters for aclocal
dnl (This must come after AM_INIT_AUTOMAKE, since it modifies ACLOCAL)
ACLOCAL_FLAGS="-I m4 -I common/m4"
AC_SUBST(ACLOCAL_AMFLAGS, $ACLOCAL_FLAGS)

AC_PROG_CC
AM_PROG_CC_STDC
AM_PROG_AS
AS="${CC}"
AS_PROG_OBJC

dnl the gettext stuff needed
AM_GNU_GETTEXT_VERSION([0.11.5])
AM_GNU_GETTEXT([external])
GST_GETTEXT([gnonlin-$GST_MAJORMINOR])

dnl define LOCALEDIR in config.h
AS_AC_EXPAND(LOCALEDIR, $datadir/locale)
AC_DEFINE_UNQUOTED([LOCALEDIR], "$LOCALEDIR",
                   [gettext locale dir])

dnl decide on error flags
AS_COMPILER_FLAG(-Wall, GST_WALL="yes", GST_WALL="no")

if test "x$GST_WALL" = "xyes"; then
   GST_ERROR="$GST_ERROR -Wall"

   if test "x$GST_CVS" = "xyes"; then
     AS_COMPILER_FLAG(-Werror,GST_ERROR="$GST_ERROR -Werror",GST_ERROR="$GST_ERROR")
   fi
fi

dnl determine c++ compiler
AC_PROG_CXX
dnl determine if c++ is available on this system
AC_CHECK_PROG(HAVE_CXX, $CXX, yes, no)
dnl determine c++ preprocessor
AC_PROG_CXXCPP
AC_ISC_POSIX

AC_HEADER_STDC([])
AC_C_INLINE
AX_CREATE_STDINT_H

dnl Check for malloc.h
AC_CHECK_HEADER(malloc.h,[
  AC_DEFINE(HAVE_MALLOC_H, 1, [whether malloc.h available])
])

dnl Check for a way to display the function name in debug output
GST_CHECK_FUNCTION()

dnl ##############################
dnl # Do automated configuration #
dnl ##############################

dnl Check for tools:
dnl ================

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE()

dnl modify pkg-config path
AC_ARG_WITH(pkg-config-path, 
   AC_HELP_STRING([--with-pkg-config-path],[colon-separated list of pkg-config(1) dirs]),
   [export PKG_CONFIG_PATH=${withval}])

GST_DOCBOOK_CHECK()
dnl check architecture
GST_ARCH()

dnl check for gconftool-2

dnl this macro defines an am conditional, so it needs to be run always
dnl AM_GCONF_SOURCE_2

dnl translit(dnm, m, l) AM_CONDITIONAL(USE_GCONFTOOL, true)
dnl GST_CHECK_FEATURE(GCONFTOOL, [GConf schemas], , [
dnl   AC_PATH_PROG(GCONFTOOL, gconftool-2, no)
dnl   if test x$GCONFTOOL = xno; then
dnl     AC_MSG_WARN(Not installing GConf schemas)
dnl     HAVE_GCONFTOOL="no"
dnl   else
dnl     HAVE_GCONFTOOL="yes"
dnl   fi
dnl   AC_SUBST(HAVE_GCONFTOOL)
dnl ])

dnl dnl check for GConf libraries
dnl translit(dnm, m, l) AM_CONDITIONAL(USE_GCONF, true)
dnl GST_CHECK_FEATURE(GCONF, [GConf libraries], , [
dnl   PKG_CHECK_MODULES(GCONF, gconf-2.0, HAVE_GCONF="yes", HAVE_GCONF="no")
dnl   AC_SUBST(GCONF_CFLAGS)
dnl   AC_SUBST(GCONF_LIBS)
dnl ])

dnl check for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
GST_REQ=0.9.0.1
PKG_CHECK_MODULES(GST, gstreamer-$GST_MAJORMINOR >= $GST_REQ,
  HAVE_GST="yes", HAVE_GST="no")

if test "x$HAVE_GST" = "xno"; then
  AC_MSG_ERROR(no GStreamer found)
fi

GST_TOOLS_DIR=`pkg-config --variable=toolsdir gstreamer-$GST_MAJORMINOR`
if test -z $GST_TOOLS_DIR; then
  AC_MSG_ERROR([no tools dir defined in GStreamer pkg-config file; core upgrade needed.])
fi
AC_SUBST(GST_TOOLS_DIR)

dnl check for gstreamer-base; uninstalled is selected preferentially
PKG_CHECK_MODULES(GST_BASE, gstreamer-base-$GST_MAJORMINOR >= $GST_REQ,
  HAVE_GST_BASE="yes", HAVE_GST_BASE="no")

if test "x$HAVE_GST_BASE" = "xno"; then
  AC_MSG_ERROR(no GStreamer Base Libs found)
fi

AC_SUBST(GST_BASE_LIBS)
AC_SUBST(GST_BASE_CFLAGS)

dnl check for gstreamer-plugins-base; uinstalled is selected preferentially
PKG_CHECK_MODULES(GST_PLUGINS_BASE,
  gstreamer-plugins-base-$GST_MAJORMINOR >= $GST_REQ,
  HAVE_GST_PLUGINS_BASE="yes", HAVE_GST_PLUGINS_BASE="no")

if test "x$HAVE_GST_PLUGINS_BASE" = "xno"; then
  AC_MSG_ERROR(no GStreamer Base Plugins development files found)
fi

AC_SUBST(GST_PLUGINS_BASE_LIBS)
AC_SUBST(GST_PLUGINS_BASE_CFLAGS)

PKG_CHECK_MODULES(GST_CHECK, gstreamer-check-$GST_MAJORMINOR >= $GST_REQ,
  HAVE_GST_CHECK="yes", HAVE_GST_CHECK="no")

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

dnl Check for essential libraries first:
dnl ====================================

GST_GLIB_CHECK([2.6])

dnl Check for additional libraries that we might use:
dnl =================================================

# we set the defaults always to make sure we have non-empty variables
# for the Makefile

PKG_CHECK_MODULES(LIBOIL, liboil-0.3 >= 0.3.0, HAVE_LIBOIL=yes, HAVE_LIBOIL=no)
AC_SUBST(LIBOIL_CFLAGS)
AC_SUBST(LIBOIL_LIBS)
if test "x${HAVE_LIBOIL}" = xyes ; then
  #AC_DEFINE_UNQUOTED(HAVE_LIBOIL, 1, [Define if liboil is being used])
  true
fi

dnl ===========================================================================
dnl ============================= gst plug-ins ================================
dnl ===========================================================================

plugindir="\$(libdir)/gstreamer-$GST_MAJORMINOR"
AC_SUBST(plugindir)

GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^[_]*gst_plugin_desc\$\$' -no-undefined"
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl also add builddir include for enumtypes and marshal
GST_CFLAGS="-I\$(top_srcdir)/gst-libs -I\$(top_builddir)/gst-libs $GST_CFLAGS $GST_ERROR"

AC_SUBST(GST_LIBS)
AC_SUBST(GST_CFLAGS)

dnl check for "check", unit testing library/header
AM_PATH_CHECK(0.9.2, HAVE_CHECK=yes, HAVE_CHECK=no)
AM_CONDITIONAL(HAVE_CHECK, test "x$HAVE_CHECK" = "xyes")

dnl ######################
dnl # Checks for gtk-doc #
dnl ######################

GTK_DOC_CHECK([1.3])
AS_PATH_PYTHON([2.1])

dnl ############################
dnl # Set up some more defines #
dnl ############################

dnl set license and copyright notice
AC_DEFINE(GST_LICENSE, "LGPL", [GNonLin license])

dnl package name in plugins
AC_ARG_WITH(package-name,
AC_HELP_STRING([--with-package-name],[specify package name to use in plugins]),
[case "${withval}" in
  yes) AC_MSG_ERROR(bad value ${withval} for --with-package-name) ;;
  no) AC_MSG_ERROR(bad value ${withval} for --with-package-name) ;;
  *) GST_PACKAGE="${withval}" ;;
esac], 
[
dnl default value
if test "x$GNL_CVS" = "xyes"
then
  dnl nano >= 1
  GST_PACKAGE="GNonLin CVS/prerelease"
else
  GST_PACKAGE="GNonLin source release"
fi
]
)
AC_MSG_NOTICE(Using $GST_PACKAGE as package name)
AC_DEFINE_UNQUOTED(GST_PACKAGE, "$GST_PACKAGE", [package name in plugins])

dnl package origin URL
AC_ARG_WITH(package-origin,
AC_HELP_STRING([--with-package-origin],[specify package origin URL to use in plugins]),
[case "${withval}" in
  yes) AC_MSG_ERROR(bad value ${withval} for --with-package-origin) ;;
  no) AC_MSG_ERROR(bad value ${withval} for --with-package-origin) ;;
  *) GST_ORIGIN="${withval}" ;;
esac], 
[GST_ORIGIN="http://gnonlin.sourceforge.net/"]) dnl Default value
AC_MSG_NOTICE(Using $GST_ORIGIN as package origin)
AC_DEFINE_UNQUOTED(GST_ORIGIN, "$GST_ORIGIN", [package origin])

dnl #########################
dnl # Make the output files #
dnl #########################

dnl keep this alphabetic per directory, please
AC_CONFIG_FILES(
Makefile
docs/Makefile
common/Makefile
common/m4/Makefile
m4/Makefile
test/Makefile
gnl/Makefile
gnl/gnlversion.h
pkgconfig/Makefile
pkgconfig/gnonlin.pc
gnonlin.spec
)
AC_OUTPUT

echo "GNonLin configured, use make to build"
