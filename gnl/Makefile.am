# cheap trick to build . first...

lib_LTLIBRARIES = libgnl.la

EXTRA_libgnl_la_SOURCES = gnlmarshal.list 

SUBDIRS = . 
DIST_SUBDIRS = 

libgnl_la_SOURCES =		\
	gnl.c			\
	gnlcomposition.c	\
	gnlelements.c		\
	gnlgroup.c		\
	gnllayer.c		\
	gnlmarshal.c		\
	gnloperation.c		\
	gnlsource.c		\
	gnltimeline.c		\
	gnlvlayer.c

BUILT_SOURCES = gnlmarshal.h gnlmarshal.c

libgnlincludedir = $(includedir)/gnl

built_headers =			\
	gnlmarshal.h

gnl_headers =			\
	gnl.h			\
	gnlcomposition.h	\
	gnlgroup.h		\
	gnllayer.h		\
	gnloperation.h		\
	gnlsource.h		\
	gnltimeline.h		\
	gnlvlayer.h

libgnlinclude_HEADERS =	$(gnl_headers) $(built_headers)

noinst_HEADERS = 	

gnlmarshal.h: gnlmarshal.list
	glib-genmarshal --header --prefix=gnl_marshal $^ > gnlmarshal.h.tmp
	mv gnlmarshal.h.tmp gnlmarshal.h

gnlmarshal.c: gnlmarshal.list
	echo "#include \"glib-object.h\"" >gnlmarshal.c.tmp
	echo "#include \"gnlmarshal.h\"" >> gnlmarshal.c.tmp
	glib-genmarshal --body --prefix=gnl_marshal $^ >> gnlmarshal.c.tmp
	mv gnlmarshal.c.tmp gnlmarshal.c

# Don't want the generated marshal files in the dist
dist-hook:
	rm -f $(distdir)/gnlmarshal.c $(distdir)/gnlmarshal.h

# Clean generated files
distclean-local:
	rm -f $(top_builddir)/gnl/gnlmarshal.c $(top_builddir)/gnl/gnlmarshal.h 


CFLAGS = $(GNL_CFLAGS) -D_GNU_SOURCE -DGST_CONFIG_DIR=\""@sysconfdir@/gnonlin"\" -Wall -Werror -g
LIBS = $(GNL_LIBS)
libgnl_la_LDFLAGS =

