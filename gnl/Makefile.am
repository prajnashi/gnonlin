# cheap trick to build . first...

lib_LTLIBRARIES = libgnl.la

EXTRA_libgnl_la_SOURCES = 

SUBDIRS = . 
DIST_SUBDIRS = 

libgnl_la_SOURCES =		\
	gnl.c			\
	gnlcomposition.c	\
	gnllayer.c		\
	gnloperation.c		\
	gnlsource.c		\
	gnltimeline.c

libgnlincludedir = $(includedir)/gnl
libgnlinclude_HEADERS =		\
	gnl.h			\
	gnlcomposition.h	\
	gnllayer.h		\
	gnloperation.h		\
	gnlsource.h		\
	gnltimeline.h

noinst_HEADERS = 	

CFLAGS = $(GNL_CFLAGS) -D_GNU_SOURCE -DGST_CONFIG_DIR=\""@sysconfdir@/gnonlin"\"
LIBS = $(GNL_LIBS)
libgnl_la_LDFLAGS =

