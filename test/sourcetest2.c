#include <gnl/gnl.h>
#include "pipelines.c"

gint
main (gint argc, gchar *argv[])
{
  GstElement *pipeline;
  GnlSource *asource, *vsource;
  GstElement *aelement, *velement;
  GstElement *v_sink, *a_sink;
  GstPad *v_pad, *a_pad;
  GstPad *v_sinkpad, *a_sinkpad;
  
  gnl_init (&argc, &argv);

  if (argc < 2) {
    g_printf ("Usage :\n\t%s <multimediafile>\n\tUse only audio AND video file, at least 15s long\n", argv[0]);
    exit(0);
  }
  
  pipeline = gst_pipeline_new ("pipeline");

  aelement = make_audio_element(argv[1]);
  velement = make_video_element(argv[1]);

  asource = gnl_source_new ("asource", aelement);
  vsource = gnl_source_new ("vsource", velement);

  if (!(v_sink = gst_element_factory_make ("xvimagesink", "v_sink"))) {
    g_warning ("couldn't create videosink element!\n");
    exit (0);
  };
  if (!(a_sink = gst_element_factory_make ("alsasink", "a_sink"))) {
    g_warning ("Couldn't create audiosink element!\n");
    exit (0);
  };

  g_printf ("Setting asource timing\n");
  gnl_object_set_media_start_stop (GNL_OBJECT (asource), 10 * GST_SECOND, 12 * GST_SECOND);
  gnl_object_set_start_stop (GNL_OBJECT (asource), 0 * GST_SECOND, 2 * GST_SECOND);

  g_printf ("Setting vsource timing\n");
  gnl_object_set_media_start_stop (GNL_OBJECT (vsource), 10 * GST_SECOND, 12 * GST_SECOND);
  gnl_object_set_start_stop (GNL_OBJECT (vsource), 0 * GST_SECOND, 2 * GST_SECOND);

  g_printf ("Getting source pads\n");
  v_pad = gnl_source_get_pad_for_stream (vsource, "src");
  a_pad = gnl_source_get_pad_for_stream (asource, "src");

  g_printf ("Getting sink pads\n");
  if (!(v_sinkpad = gst_element_get_pad (v_sink, "sink")))
    g_warning ("Can't get video sink pad!!!\n");
  if (!(a_sinkpad = gst_element_get_pad (a_sink, "sink")))
    g_warning ("Can't get audio sink pad!!!\n");
  
  g_printf ("Connecting source pads to outputs\n");
  gst_pad_connect (v_pad, v_sinkpad);
  gst_pad_connect (a_pad, a_sinkpad);

  g_printf ("Adding elements to pipeline\n");
  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (asource));
  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (vsource));
  gst_bin_add (GST_BIN (pipeline), v_sink);
  gst_bin_add (GST_BIN (pipeline), a_sink);

  g_signal_connect (pipeline, "deep_notify", G_CALLBACK (gst_element_default_deep_notify), NULL); 
  g_signal_connect (pipeline, "error", G_CALLBACK (gst_element_default_error), NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  while (gst_bin_iterate (GST_BIN (pipeline)));
  gst_element_set_state (pipeline, GST_STATE_PAUSED);

/*   gnl_object_set_media_start_stop (GNL_OBJECT (source), 60 * GST_SECOND, 64 * GST_SECOND); */

/*   gst_element_send_event (GST_ELEMENT (source),  */
/* 		          gst_event_new_segment_seek ( */
/* 			    GST_FORMAT_TIME | */
/* 	                    GST_SEEK_METHOD_SET | */
/* 			    GST_SEEK_FLAG_FLUSH | */
/* 			    //GST_SEEK_FLAG_SEGMENT_LOOP | */
/* 			    GST_SEEK_FLAG_ACCURATE, */
/* 			    1 * GST_SECOND, */
/* 			    3 * GST_SECOND)); */
  
/*   gst_element_set_state (pipeline, GST_STATE_PLAYING); */
/*   while (gst_bin_iterate (GST_BIN (pipeline))); */
/*   gst_element_set_state (pipeline, GST_STATE_NULL); */
  
  return 0;
}
