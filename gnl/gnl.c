
#include <stdlib.h>

#include "gnl.h"

#define MAX_PATH_SPLIT	16

gchar *_gnl_progname;


static gboolean 	gnl_init_check 		(int *argc, gchar ***argv);

/**
 * gnl_init:
 * @argc: pointer to application's argc
 * @argv: pointer to application's argv
 *
 * Initializes the GStreamer library, setting up internal path lists,
 * registering built-in elements, and loading standard plugins.
 */
void 
gnl_init (int *argc, char **argv[]) 
{
  if (!gnl_init_check (argc,argv)) {
    exit (0);
  }

  gst_init (argc, argv);
}

/* returns FALSE if the program can be aborted */
static gboolean
gnl_init_check (int     *argc,
		gchar ***argv)
{
  gboolean ret = TRUE;

  _gnl_progname = NULL;

  if (argc && argv) {
    _gnl_progname = g_strdup(*argv[0]);
  }

  if (_gnl_progname == NULL) {
    _gnl_progname = g_strdup("gnlprog");
  }

  return ret;
}

/**
 * gnl_main:
 *
 * Enter the main GStreamer processing loop 
 */
void 
gnl_main (void) 
{
#ifndef USE_GLIB2
  gtk_main ();
#endif
}

/**
 * gnl_main_quit:
 *
 * Exits the main GStreamer processing loop 
 */
void 
gnl_main_quit (void) 
{
#ifndef USE_GLIB2
  gtk_main_quit ();
#endif
}
