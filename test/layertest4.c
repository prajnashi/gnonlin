#include <gnl/gnl.h>
#include "pipelines.c"

gint
main (gint argc, gchar *argv[])
{
  GstElement *pipeline;
  GnlTimeline *timeline;
  GnlGroup *v_group, *a_group;
  GnlLayer *v_layer, *a_layer;
  GstPad *v_pad, *a_pad;
  GstElement *srcelement, *v_sink, *a_sink;
  GnlSource *source;
  
  gnl_init (&argc, &argv);

  timeline = gnl_timeline_new ("timeline");
  pipeline = gst_pipeline_new ("pipeline");

  srcelement = make_avi_element ("/opt/data/Matrix_f900.avi");
  //srcelement = make_dv_element ("/opt/data/Matrix.dv");
  source = gnl_source_new ("source", srcelement);

  //v_sink = gst_element_factory_make ("xvideosink", "v_sink");
  //a_sink = gst_element_factory_make ("osssink", "a_sink");
  //g_object_set (G_OBJECT (a_sink), "sync", FALSE, NULL);
  v_sink = gst_element_factory_make ("fakesink", "v_sink");
  a_sink = gst_element_factory_make ("fakesink", "a_sink");

  gnl_source_set_media_start_stop (source, 11 * GST_SECOND, 12 * GST_SECOND);
  gnl_source_set_start_stop (source, 0, 1 * GST_SECOND);

  v_layer = gnl_layer_new ("v_layer");
  gnl_layer_add_source (v_layer, source, "v_src");

  a_layer = gnl_layer_new ("a_layer");
  gnl_layer_add_source (a_layer, source, "a_src");

  v_group = gnl_group_new ("v_group");
  gnl_group_append_layer (v_group, v_layer);
  a_group = gnl_group_new ("a_group");
  gnl_group_append_layer (a_group, a_layer);
  
  gnl_timeline_add_group (timeline, v_group);
  gnl_timeline_add_group (timeline, a_group);

  v_pad = gnl_timeline_get_pad_for_group (timeline, v_group);
  a_pad = gnl_timeline_get_pad_for_group (timeline, a_group);

  gst_pad_connect (v_pad, gst_element_get_pad (v_sink, "sink"));
  gst_pad_connect (a_pad, gst_element_get_pad (a_sink, "sink"));

  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (timeline));

  gst_bin_add (GST_BIN (pipeline), v_sink);
  gst_bin_add (GST_BIN (pipeline), a_sink);

  g_signal_connect (pipeline, "deep_notify", G_CALLBACK (gst_element_default_deep_notify), NULL); 
  g_signal_connect (pipeline, "error", G_CALLBACK (gst_element_default_error), NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  while (gst_bin_iterate (GST_BIN (pipeline)));
  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  return 0;
}
