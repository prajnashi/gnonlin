#include <gnl/gnl.h>
#include "pipelines.c"

gint
main (gint argc, gchar *argv[])
{
  GstElement *pipeline;
  GnlTimeline *timeline;
  GnlComposition *a_group, *v_group;
  GnlSource *source;
  GstElement *srcelement, *v_sink, *a_sink;
  GstPad *v_pad, *a_pad;
  
  gnl_init (&argc, &argv);

  pipeline = gst_pipeline_new ("pipeline");
  timeline = gnl_timeline_new ("timeline");

  srcelement = make_dv_element ("/opt/data/matrix.dv");
  source = gnl_source_new ("source", srcelement);
  gnl_object_set_media_start_stop (GNL_OBJECT (source), 10 * GST_SECOND, 12 * GST_SECOND);
  gnl_object_set_start_stop (GNL_OBJECT (source), 0 * GST_SECOND, 2 * GST_SECOND);

  //v_sink = gst_element_factory_make ("xvideosink", "v_sink");
  //a_sink = gst_element_factory_make ("osssink", "a_sink");
  //g_object_set (G_OBJECT (a_sink), "sync", FALSE, NULL);
  v_sink = gst_element_factory_make ("fakesink", "v_sink");
  a_sink = gst_element_factory_make ("fakesink", "a_sink");

  a_group = gnl_composition_new ("a_group");
  v_group = gnl_composition_new ("v_group");

  gnl_composition_add_object (a_group, GNL_OBJECT (source));
  gnl_composition_add_object (v_group, GNL_OBJECT (source));

  gnl_timeline_add_group (timeline, GNL_GROUP (a_group));
  gnl_timeline_add_group (timeline, GNL_GROUP (v_group));

  a_pad = gnl_timeline_get_pad_for_group (timeline, GNL_GROUP (a_group));
  v_pad = gnl_timeline_get_pad_for_group (timeline, GNL_GROUP (v_group));
	  
  gst_pad_connect (v_pad, gst_element_get_pad (v_sink, "sink"));
  gst_pad_connect (a_pad, gst_element_get_pad (a_sink, "sink"));

  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (timeline));

  g_signal_connect (pipeline, "deep_notify", G_CALLBACK (gst_element_default_deep_notify), NULL); 
  g_signal_connect (pipeline, "error", G_CALLBACK (gst_element_default_error), NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  while (gst_bin_iterate (GST_BIN (pipeline)));
  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  return 0;
}
