
#include <gnl/gnl.h>

/*
 (-0----->-----------5000----->------10000----->-----15000-)
 ! main_timeline                                           !
 !                                                         !
 ! (-----------------------------------------------------) !
 ! ! my_layer1                                           ! !
 ! ! (------------------------------------)              ! !
 ! ! ! source1                            !              ! !
 ! ! !                                    !              ! !
 ! ! (------------------------------------)              ! !
 ! (-----------------------------------------------------) !
 (---------------------------------------------------------)
*/

int
main (int argc, gchar *argv[]) 
{
  GstElement *pipeline;
  GnlTimeline *timeline;
  GnlGroup *group1, *group2;
  GnlLayer *layer1, *layer2;
  GnlSource *source1;
  GstElement *src;
  GstElement *sink1, *sink2;
  GstPad *pad;

  gnl_init (&argc, &argv);

  pipeline = gst_pipeline_new ("main_pipeline");
  timeline = gnl_timeline_new ("main_timeline");

  group1 = gnl_group_new ("my_group1");
  group2 = gnl_group_new ("my_group2");

  src = gst_element_factory_make ("fakesrc", "src");
  pad = gst_element_get_request_pad (src, "src%d");
  source1 = gnl_source_new ("my_source1", src);
  gnl_source_set_start_stop (source1, 0, 10);

  layer1 = gnl_layer_new ("my_layer1");
  gnl_layer_add_source (layer1, source1, "src", 0);
  layer2 = gnl_layer_new ("my_layer2");
  gnl_layer_add_source (layer2, source1, gst_pad_get_name (pad), 0);

  sink1 = gst_element_factory_make ("fakesink", "sink1");
  gst_bin_add (GST_BIN (pipeline), sink1);
  gst_element_connect_pads (GST_ELEMENT (group1), "src", sink1, "sink");
  sink2 = gst_element_factory_make ("fakesink", "sink2");
  gst_bin_add (GST_BIN (pipeline), sink2);
  gst_element_connect_pads (GST_ELEMENT (group2), "src", sink2, "sink");

  gnl_group_append_layer (group1, layer1);
  gnl_group_append_layer (group2, layer2);
  gnl_timeline_add_group (timeline, group1);
  gnl_timeline_add_group (timeline, group2);

  g_signal_connect (G_OBJECT (pipeline), "deep_notify", G_CALLBACK (gst_element_default_deep_notify), NULL);

  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (timeline));

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  while (gst_bin_iterate (GST_BIN (pipeline))) {
  }
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
}
