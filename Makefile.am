PACKAGE = @PACKAGE@
VERSION = @VERSION@

if BUILD_DOCS
SUBDIRS_DOCS = docs
else
SUBDIRS_DOCS =
endif

if BUILD_TESTS
SUBDIRS_TESTS = test 
else
SUBDIRS_TESTS =
endif

if BUILD_EXAMPLES
SUBDIRS_EXAMPLES = examples
else
SUBDIRS_EXAMPLES =
endif

SUBDIRS = gnl $(SUBDIRS_TESTS) $(SUBDIRS_EXAMPLES) $(SUBDIRS_DOCS)

# These are all the possible subdirs
DIST_SUBDIRS = gnl test examples docs


bin_SCRIPTS = gnonlin-config

m4datadir = $(datadir)/aclocal
m4data_DATA = gnonlin.m4

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gnonlin.pc

man_MANS = gnonlin-config.1

EXTRA_DIST = gnonlin.spec.in gnonlin-config.in gnonlin.m4 \
	     gnonlin.pc.in gnonlin-uninstalled.pc.in \
	     configure.base autogen.sh \
	     LICENSE REQUIREMENTS ABOUT-NLS $(man_MANS) 

dist-hook:
	cp gnonlin.spec $(distdir)

rpm: dist
	rpm -ta $(distdir).tar.gz

deb:
	dpkg-buildpacakge -rfakeroot -us -uc

deb-maint:
	debuild

# Rules to generate autoconf scripts if configure.base is updated
$(top_srcdir)/configure.in: $(top_srcdir)/configure.base
	$(top_srcdir)/makeconfigure <$(top_srcdir)/configure.base >$(top_srcdir)/configure.in configure.in

$(top_srcdir)/configure.ac: $(top_srcdir)/configure.base
	$(top_srcdir)/makeconfigure <$(top_srcdir)/configure.base >$(top_srcdir)/configure.ac configure.ac

