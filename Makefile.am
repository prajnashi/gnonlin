PACKAGE = @PACKAGE@
VERSION = @VERSION@

SUBDIRS = gnl test docs

# These are all the possible subdirs
DIST_SUBDIRS = gnl test docs

bin_SCRIPTS = gnonlin-config

## m4datadir = $(datadir)/aclocal
## m4data_DATA = gnonlin.m4

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gnonlin.pc

man_MANS = gnonlin-config.1

EXTRA_DIST = gnonlin.spec.in gnonlin-config.in \
	     gnonlin.pc.in gnonlin-uninstalled.pc.in \
	     autogen.sh ChangeLog AUTHORS COPYING NEWS\
	     LICENSE REQUIREMENTS README ABOUT-NLS $(man_MANS) 

include $(top_srcdir)/common/release.mak
