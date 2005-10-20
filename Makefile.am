DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

SUBDIRS = \
	gnl \
	docs \
	m4 \
	po \
	common \
	test

DIST_SUBDIRS = $(SUBDIRS)

EXTRA_DIST = \
	gnonlin.spec depcomp \
	AUTHORS COPYING NEWS README RELEASE REQUIREMENTS \
	ChangeLog autogen.sh

DISTCLEANFILES = _stdint.h

include $(top_srcdir)/common/release.mak
include $(top_srcdir)/common/po.mak
