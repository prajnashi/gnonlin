PACKAGE = @PACKAGE@
VERSION = @VERSION@

SUBDIRS = gnl test ## docs

# These are all the possible subdirs
DIST_SUBDIRS = gnl test docs

bin_SCRIPTS = gnonlin-config

m4datadir = $(datadir)/aclocal
m4data_DATA = gnonlin.m4

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gnonlin.pc

man_MANS = gnonlin-config.1

EXTRA_DIST = gnonlin.spec.in gnonlin-config.in gnonlin.m4 \
	     gnonlin.pc.in gnonlin-uninstalled.pc.in \
	     autogen.sh \
	     LICENSE REQUIREMENTS ABOUT-NLS $(man_MANS) 

dist-hook:
	cp gnonlin.spec $(distdir)

rpm: dist
	rpm -ta $(distdir).tar.gz

deb:
	dpkg-buildpackage -rfakeroot -us -uc

deb-maint:
	debuild

