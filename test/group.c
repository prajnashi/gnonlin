#include <gnl/gnl.h>


int
main (int argc, gchar *argv[]) 
{
  GnlTimeline *timeline;
  GnlGroup *group;
  GstElement *element1, *effect_element, *pipeline, *sink;

  gnl_init (&argc, &argv);

  pipeline = gst_pipeline_new ("main_pipeline");
  timeline = gnl_timeline_new ("main_timeline");
  group = gnl_group_new ("main_group");

  gnl_timeline_add_group (timeline, group);

  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add (GST_BIN (pipeline), sink);
  gst_element_connect_pads (GST_ELEMENT (group), "src", sink, "sink");

  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (timeline));

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));
  
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
}
