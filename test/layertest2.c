
#include <gnl/gnl.h>

gchar *filename;

static GstElement*
create_source (void)
{
  GstElement *bin;
  GstElement *src;
  GstElement *mad;
  GstElement *queue;

  bin = gst_elementfactory_make ("thread", "thread");

  src = gst_elementfactory_make ("filesrc", "filesrc");
  g_object_set (G_OBJECT (src), "location", filename, NULL);
  mad = gst_elementfactory_make ("mad", "mad");
  queue = gst_elementfactory_make ("queue", "queue");

  gst_bin_add (GST_BIN (bin), src);
  gst_bin_add (GST_BIN (bin), mad);
  gst_bin_add (GST_BIN (bin), queue);

  gst_element_connect (src, "src", mad, "sink");
  gst_element_connect (mad, "src", queue, "sink");

  gst_element_add_ghost_pad (bin, gst_element_get_pad (queue, "src"), "src");

  return bin;
}


int
main (int argc, gchar *argv[]) 
{
  GstElement *pipeline;
  GnlTimeline *timeline;
  GnlGroup *group;
  GnlLayer *layer1;
  GnlSource *source1, *source2;
  GstElement *fakesrc2;
  GstElement *sink;

  gnl_init (&argc, &argv);

  if (argc > 1) {
    filename = argv[1];
  }
  else {
    filename = "/opt/data/south.mp3";
  }

  pipeline = gst_pipeline_new ("main_pipeline");
  timeline = gnl_timeline_new ("main_timeline");

  source1 = gnl_source_new ("my_source1");
  gnl_source_set_element (source1, create_source());
  gnl_source_set_start_stop (source1, 3000000, 4000000);

  source2 = gnl_source_new ("my_source2");
  fakesrc2 = gst_elementfactory_make ("fakesrc", "src2");
  gnl_source_set_element (source2, create_source());
  gnl_source_set_start_stop (source2, 13500000, 14900000);

  layer1 = gnl_layer_new ("my_layer1");
  gnl_layer_add_source (layer1, source1, 0);
  gnl_layer_add_source (layer1, source2, 1000000);

  group = gnl_group_new ("my_group");

  sink = gst_elementfactory_make ("osssink", "sink");
  gst_bin_add (GST_BIN (pipeline), sink);

  gst_element_connect (GST_ELEMENT (group), "src", sink, "sink");

  gnl_composition_append_layer (GNL_COMPOSITION (group), layer1);

  gnl_timeline_add_group (timeline, group);

  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (timeline));

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));
  
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
   
}
