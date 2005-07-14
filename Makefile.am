PACKAGE = @PACKAGE@
VERSION = @VERSION@

SUBDIRS = common gnl test docs

# These are all the possible subdirs
DIST_SUBDIRS = common gnl test docs

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gnonlin.pc

EXTRA_DIST = gnonlin.spec.in gnonlin.spec gnonlin.pc \
             gnonlin-uninstalled.pc \
	     gnonlin.pc.in gnonlin-uninstalled.pc.in \
	     autogen.sh ChangeLog AUTHORS COPYING NEWS\
	     LICENSE REQUIREMENTS README ABOUT-NLS $(man_MANS) 

include $(top_srcdir)/common/release.mak
