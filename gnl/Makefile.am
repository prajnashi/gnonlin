# cheap trick to build . first...

built_header_configure = gnlversion.h
built_header_make = gnlmarshal.h
build_source_make = gnlmarshal.c

plugin_LTLIBRARIES = libgnl.la

EXTRA_libgnl_la_SOURCES = gnlmarshal.list 

libgnl_la_SOURCES =		\
	gnl.c			\
	gnlobject.c		\
	gnlcomposition.c	\
	gnlelements.c		\
	gnlgroup.c		\
	gnlmarshal.c		\
	gnloperation.c		\
	gnlsource.c		\
	gnltimeline.c

libgnlincludedir = $(includedir)/gnl

gnl_headers =			\
	gnl.h			\
	gnlobject.h		\
	gnlcomposition.h	\
	gnltypes.h		\
	gnlgroup.h		\
	gnloperation.h		\
	gnlsource.h		\
	gnltimeline.h

#files built on make all/check/instal
BUILT_SOURCES =			\
	$(built_header_configure)	\
	$(built_header_make)		\
	$(built_source_make)

CLEANFILES = $(built_header_make) $(built_source_make)
DISTCLEANFILE = $(CLEANFILES) $(built_header_configure)

libgnlinclude_HEADERS =	$(gnl_headers)
nodist_libgnlinclude_HEADERS = $(built_header_make) $(built_header_configure)

nodist_libgnl_la_SOURCES = $(built_source_make)

noinst_HEADERS = 

gnlmarshal.h: gnlmarshal.list
	glib-genmarshal --header --prefix=gnl_marshal $^ > gnlmarshal.h.tmp
	mv gnlmarshal.h.tmp gnlmarshal.h

gnlmarshal.c: gnlmarshal.list
	echo "#include \"glib-object.h\"" >gnlmarshal.c.tmp
	echo "#include \"gnlmarshal.h\"" >> gnlmarshal.c.tmp
	glib-genmarshal --body --prefix=gnl_marshal $^ >> gnlmarshal.c.tmp
	mv gnlmarshal.c.tmp gnlmarshal.c

AM_CFLAGS = $(GNL_CFLAGS) -I$(top_srcdir) -D_GNU_SOURCE -DGST_CONFIG_DIR=\""@sysconfdir@/gnonlin"\" -Wall -Werror -g
LIBS = $(GNL_LIBS)
libgnl_la_LDFLAGS =

