#include <gnl/gnl.h>

/*
*
*   0 - 1 - 2 - 3 - 4 - 5 - 6 - 7 - 8 - 9 -10 -11 -12 -13 -14 -15 -16 -17
*
*   (-----------------------)
*   ! operation             !
*   (-----------------------)
*   (-----------------------------------)
*   ! source1                           !
*   (-----------------------------------)
*   (-----------------------------------)
*   ! source2                           !
*   (-----------------------------------)
*/

int
main (int argc, gchar *argv[]) 
{
  GnlTimeline *timeline;
  GnlGroup *group;
  GnlLayer *layer1, *layer2;
  GnlSource *source1, *source2;
  GnlOperation *effect;
  GstElement *element1, *element2, *effect_element, *pipeline, *sink;

  gnl_init (&argc, &argv);

  pipeline = gst_pipeline_new ("main_pipeline");
  timeline = gnl_timeline_new ("main_timeline");

  element1 = gst_element_factory_make ("fakesrc", "src1");
  source1 = gnl_source_new ("my_source1", element1);
  gnl_source_set_start_stop (source1, 0, 9);

  element2 = gst_element_factory_make ("fakesrc", "src2");
  source2 = gnl_source_new ("my_source2", element2);
  gnl_source_set_start_stop (source2, 0, 9);

  effect_element = gst_element_factory_make ("aggregator", "effect");
  gst_element_get_request_pad (effect_element, "sink%d");
  gst_element_get_request_pad (effect_element, "sink%d");
  effect = gnl_operation_new ("effect", effect_element);
  gnl_source_set_start_stop (GNL_SOURCE (effect), 0, 6);

  layer1 = gnl_layer_new ("layer1");
  gnl_layer_add_source (layer1, source1, "src", 0);
  layer2 = gnl_layer_new ("layer2");
  gnl_layer_add_source (layer2, source2, "src", 0);

  group = gnl_group_new ("group");

  gnl_composition_add_operation (GNL_COMPOSITION (group), effect, 0);
  gnl_group_append_layer (group, layer1);
  gnl_group_append_layer (group, layer2);

  gnl_timeline_add_group (timeline, group);

  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add (GST_BIN (pipeline), sink);
  gst_element_connect_pads (GST_ELEMENT (group), "src", sink, "sink");

  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (timeline));

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));
  
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
}
