
#include <gnl/gnl.h>

static GstElement*
create_source (void)
{
  GstElement *bin;
  GstElement *src;
  GstElement *mad;

  bin = gst_elementfactory_make ("bin", "bin");

  src = gst_elementfactory_make ("filesrc", "filesrc");
  g_object_set (G_OBJECT (src), "location", "/opt/data/south.mp3", NULL);
  mad = gst_elementfactory_make ("mad", "mad");

  gst_bin_add (GST_BIN (bin), src);
  gst_bin_add (GST_BIN (bin), mad);

  gst_element_connect (src, "src", mad, "sink");

  gst_element_add_ghost_pad (bin, gst_element_get_pad (mad, "src"), "src");

  return bin;
}


int
main (int argc, gchar *argv[]) 
{
  GstElement *pipeline;
  GnlTimeline *timeline;
  GnlLayer *layer1;
  GnlSource *source1, *source2;
  GstElement *fakesrc2;
  GstElement *sink;

  gnl_init (&argc, &argv);

  pipeline = gst_pipeline_new ("main_pipeline");
  timeline = gnl_timeline_new ("main_timeline");

  source1 = gnl_source_new ("my_source1");
  gnl_source_set_element (source1, create_source());
  gnl_source_set_start_stop (source1, 300000, 500000);

  source2 = gnl_source_new ("my_source2");
  fakesrc2 = gst_elementfactory_make ("fakesrc", "src2");
  gnl_source_set_element (source2, fakesrc2);
  gnl_source_set_start_stop (source2, 600000, 800000);

  layer1 = gnl_layer_new ("my_layer1");
  gnl_layer_add_source (layer1, source1, 0);
  gnl_layer_add_source (layer1, source2, 20);

  sink = gst_elementfactory_make ("fakesink", "sink");
  gst_bin_add (GST_BIN (pipeline), sink);

  gst_element_connect (GST_ELEMENT (layer1), "src", sink, "sink");

  gnl_composition_append_layer (GNL_COMPOSITION (timeline), layer1);

  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (timeline));

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));
  
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
   
}
