#!/bin/bash
# Run this to generate all the initial makefiles, etc.

DIE=0
package=gnonlin
srcfile=gnl/gnl.c

# source helper functions
if test ! -e common/gnonlin-autogen.sh;
then
  echo There is something wrong with your source tree.
  echo You are missing common/gnonlin-autogen.sh
  exit 1
fi
. common/gnonlin-autogen.sh

autogen_options $@

echo "+ checking for build tools"
version_check "autoconf" "ftp://ftp.gnu.org/pub/gnu/autoconf/" 2 52 || DIE=1
version_check "automake" "ftp://ftp.gnu.org/pub/gnu/automake/" 1 5 || DIE=1
version_check "libtool" "ftp://ftp.gnu.org/pub/gnu/libtool/" 1 4 0 || DIE=1
version_check "pkg-config" "http://www.freedesktop.org/software/pkgconfig" 0 8 0 || DIE=1

autoconf_2.52d_check || DIE=1

if test "$DIE" -eq 1; then exit 1; fi

# CONFIGURE_OPT='--enable-maintainer-mode --enable-plugin-builddir'

CONFIGURE_OPT=""

# if no arguments specified then this will be printed

if test -z "$*"; then
	echo "+ Checking for autogen.sh options"
	echo "  This autogen script will automatically run ./configure as:"
        echo "  ./configure $CONFIGURE_OPT"
        echo "  To pass any additional options, please specify them on the $0"
        echo "  command line."
fi

toplevel_check $srcfile

tool_run "aclocal" "-I common/m4 $ACLOCAL_FLAGS"

# FIXME : why does libtoolize keep complaining about aclocal ?
echo "+ not running libtoolize until libtool fix has flown downstream"
# tool_run "libtoolize" "--copy --force"
tool_run "autoheader"

# touch the stamp-h.in build stamp so we don't re-run autoheader in maintainer mode -- wingo
echo timestamp > stamp-h.in 2> /dev/null
tool_run "autoconf"
tool_run "automake" "-a -c"

test -n "$NOCONFIGURE" && {
    echo "skipping configure stage for package $package, as requested."
    echo "autogen.sh done."
    exit 0
}

echo "+ running configure ... "
echo "./configure default flags: $CONFIGURE_OPT"
echo "using: $CONFIGURE_OPT $@"
echo

./configure $CONFIGURE_OPT "$@" || {
	echo
	echo "configure failed"
	exit 1
}

echo 
echo "Now type 'make' to compile $package."
