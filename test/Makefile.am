noinst_PROGRAMS = \
	sourcetest1	\
	sourcetest2	\
	sourcetest3	\
	sourcetest4
#\
#simple simple1 composition layertest layertest2 simple2 group \
#		  sourcetest1 sourcetest2 sourcetest3 \
#		  layertest4 vlayertest sourcetest4
EXTRA_DIST = pipelines.c

LDADD = $(GST_LIBS) $(GNL_LIBS) ../gnl/libgnl.la
AM_CFLAGS = $(GST_CFLAGS) $(GNL_CFLAGS) -I$(top_srcdir)
