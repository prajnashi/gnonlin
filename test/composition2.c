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

  source1 = gnl_source_new ("my_source1");
  element1 = gst_elementfactory_make ("fakesrc", "src1");
  gnl_source_set_element (source1, element1);
  gnl_source_set_start_stop (source1, 0, 9);

  source2 = gnl_source_new ("my_source2");
  element2 = gst_elementfactory_make ("fakesrc", "src2");
  gnl_source_set_element (source2, element2);
  gnl_source_set_start_stop (source2, 0, 9);

  effect = gnl_operation_new ("effect");
  effect_element = gst_elementfactory_make ("aggregator", "effect");
  gst_element_request_pad_by_name (effect_element, "sink%d");
  gst_element_request_pad_by_name (effect_element, "sink%d");
  gnl_source_set_element (GNL_SOURCE (effect), effect_element);
  gnl_source_set_start_stop (GNL_SOURCE (effect), 0, 6);

  layer1 = gnl_layer_new ("layer1");
  gnl_layer_add_source (layer1, source1, 0);
  layer2 = gnl_layer_new ("layer2");
  gnl_layer_add_source (layer2, source2, 0);

  group = gnl_group_new ("group");

  gnl_composition_add_operation (GNL_COMPOSITION (group), effect, 0);
  gnl_composition_append_layer (GNL_COMPOSITION (group), layer1);
  gnl_composition_append_layer (GNL_COMPOSITION (group), layer2);

  gnl_timeline_add_group (timeline, group);

  sink = gst_elementfactory_make ("fakesink", "sink");
  gst_bin_add (GST_BIN (pipeline), sink);
  gst_element_connect (GST_ELEMENT (group), "src", sink, "sink");

  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (timeline));

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));
  
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
}
