
#include <gnl/gnl.h>

/*
 (-0----->-----------5000----->------10000----->-----15000-)
 ! main_timeline                                           !
 !                                                         !
 ! (-----------------------------------------------------) !
 ! ! my_layer1                                           ! !
 ! ! (----------------)   (---------------)              ! !
 ! ! ! source1        !   ! source2       !              ! !
 ! ! !                !   !               !              ! !
 ! ! (----------------)   (---------------)              ! !
 ! (-----------------------------------------------------) !
 (---------------------------------------------------------)
*/

int
main (int argc, gchar *argv[]) 
{
  GstElement *pipeline;
  GnlTimeline *timeline;
  GnlGroup *group;
  GnlLayer *layer1;
  GnlSource *source1, *source2;
  GstElement *fakesrc1, *fakesrc2;
  GstElement *sink;

  gnl_init (&argc, &argv);

  pipeline = gst_pipeline_new ("main_pipeline");
  timeline = gnl_timeline_new ("main_timeline");

  group = gnl_group_new ("my_group1");

  source1 = gnl_source_new ("my_source1");
  fakesrc1 = gst_element_factory_make ("fakesrc", "src1");
  gnl_source_set_element (source1, fakesrc1);
  gnl_source_set_start_stop (source1, 2, 5);

  source2 = gnl_source_new ("my_source2");
  fakesrc2 = gst_element_factory_make ("fakesrc", "src2");
  gnl_source_set_element (source2, fakesrc2);
  gnl_source_set_start_stop (source2, 8, 15);

  layer1 = gnl_layer_new ("my_layer1");
  gnl_layer_add_source (layer1, source1, 0);
  gnl_layer_add_source (layer1, source2, 3);

  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add (GST_BIN (pipeline), sink);
  gst_element_connect_pads (GST_ELEMENT (group), "src", sink, "sink");

  gnl_composition_append_layer (GNL_COMPOSITION (group), layer1);
  gnl_timeline_add_group (timeline, group);

  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (timeline));

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  while (gst_bin_iterate (GST_BIN (pipeline)));
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
}
