#include <gnl/gnl.h>

gint
main (gint argc, gchar *argv[])
{
  GstElement *pipeline;
  GnlSource *source;
  GstElement *fakesrc, *fakesink;
  GstPad *pad;
  
  gnl_init (&argc, &argv);

  pipeline = gst_pipeline_new ("pipeline");

  fakesrc = gst_element_factory_make ("fakesrc", "fakesrc");
  source = gnl_source_new ("source", fakesrc);
  fakesink = gst_element_factory_make ("fakesink", "fakesink");

  gnl_source_set_media_start_stop (source, 0, 6);
  gnl_source_set_start_stop (source, 0, 6);

  pad = gnl_source_get_pad_for_stream (source, "src");

  gst_pad_connect (pad,
		   gst_element_get_pad (fakesink, "sink"));

  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (source));
  gst_bin_add (GST_BIN (pipeline), fakesink);

  g_signal_connect (pipeline, "deep_notify", G_CALLBACK (gst_element_default_deep_notify), NULL); 
  g_signal_connect (pipeline, "error", G_CALLBACK (gst_element_default_error), NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  while (gst_bin_iterate (GST_BIN (pipeline)));

  gnl_source_set_media_start_stop (source, 40, 45);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  while (gst_bin_iterate (GST_BIN (pipeline)));
  gst_element_set_state (pipeline, GST_STATE_NULL);
  
  return 0;
}
