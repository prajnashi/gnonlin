#include <gnl/gnl.h>
#include "pipelines.c"

gint
main (gint argc, gchar *argv[])
{
  GstElement *pipeline;
  GnlSource *source;
  GstElement *srcelement, *v_sink, *a_sink;
  GstPad *v_pad, *a_pad;
  
  gnl_init (&argc, &argv);

  pipeline = gst_pipeline_new ("pipeline");

  srcelement = make_dv_element ("/opt/data/matrix.dv");
  source = gnl_source_new ("source", srcelement);

  //v_sink = gst_element_factory_make ("xvideosink", "v_sink");
  //a_sink = gst_element_factory_make ("osssink", "a_sink");
  v_sink = gst_element_factory_make ("fakesink", "v_sink");
  a_sink = gst_element_factory_make ("fakesink", "a_sink");
  //g_object_set (G_OBJECT (a_sink), "sync", FALSE, NULL);

  gnl_object_set_media_start_stop (GNL_OBJECT (source), 10 * GST_SECOND, 12 * GST_SECOND);
  gnl_object_set_start_stop (GNL_OBJECT (source), 0 * GST_SECOND, 2 * GST_SECOND);

  v_pad = gnl_source_get_pad_for_stream (source, "v_src");
  a_pad = gnl_source_get_pad_for_stream (source, "a_src");

  gst_pad_connect (v_pad, gst_element_get_pad (v_sink, "sink"));
  gst_pad_connect (a_pad, gst_element_get_pad (a_sink, "sink"));

  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (source));
  gst_bin_add (GST_BIN (pipeline), v_sink);
  gst_bin_add (GST_BIN (pipeline), a_sink);

  g_signal_connect (pipeline, "deep_notify", G_CALLBACK (gst_element_default_deep_notify), NULL); 
  g_signal_connect (pipeline, "error", G_CALLBACK (gst_element_default_error), NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  while (gst_bin_iterate (GST_BIN (pipeline)));
  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  gnl_object_set_media_start_stop (GNL_OBJECT (source), 60 * GST_SECOND, 64 * GST_SECOND);

  gst_element_send_event (GST_ELEMENT (source), 
		          gst_event_new_segment_seek (
			    GST_FORMAT_TIME |
	                    GST_SEEK_METHOD_SET |
			    GST_SEEK_FLAG_FLUSH |
			    //GST_SEEK_FLAG_SEGMENT_LOOP |
			    GST_SEEK_FLAG_ACCURATE,
			    1 * GST_SECOND,
			    3 * GST_SECOND));
  
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  while (gst_bin_iterate (GST_BIN (pipeline)));
  gst_element_set_state (pipeline, GST_STATE_NULL);
  
  return 0;
}
