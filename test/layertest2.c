
#include <gnl/gnl.h>

gchar *filename;

static GstElement*
create_source (void)
{
  GstElement *bin;
  GstElement *src;
  GstElement *mad;
  //GstElement *queue;

  bin = gst_element_factory_make ("bin", "thread");

  src = gst_element_factory_make ("filesrc", "filesrc");
  g_object_set (G_OBJECT (src), "location", filename, NULL);
  mad = gst_element_factory_make ("mad", "mad");
  //queue = gst_element_factory_make ("queue", "queue");

  gst_bin_add (GST_BIN (bin), src);
  gst_bin_add (GST_BIN (bin), mad);
  //gst_bin_add (GST_BIN (bin), queue);

  gst_element_connect_pads (src, "src", mad, "sink");
  //gst_element_connect_pads (mad, "src", queue, "sink");

  gst_element_add_ghost_pad (bin, gst_element_get_pad (mad, "src"), "src");

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
  GstPad *pad;

  gnl_init (&argc, &argv);

  if (argc > 1) {
    filename = argv[1];
  }
  else {
    filename = "/opt/data/south.mp3";
  }

  pipeline = gst_pipeline_new ("main_pipeline");
  timeline = gnl_timeline_new ("main_timeline");

  source1 = gnl_source_new ("my_source1", create_source());
  gnl_source_set_media_start_stop (source1, 0 * GST_SECOND, 2 * GST_SECOND);
  gnl_source_set_start_stop (source1, 0, 2 * GST_SECOND);

  fakesrc2 = gst_element_factory_make ("fakesrc", "src2");
  source2 = gnl_source_new ("my_source2", fakesrc2);
  gnl_source_set_media_start_stop (source2, 12 * GST_SECOND, 14 * GST_SECOND);
  gnl_source_set_start_stop (source2, 2 * GST_SECOND, 4 * GST_SECOND);

  layer1 = gnl_layer_new ("my_layer1");
  gnl_layer_add_source (layer1, source1, "src");
  gnl_layer_add_source (layer1, source2, "src");

  group = gnl_group_new ("my_group");

  //sink = gst_element_factory_make ("osssink", "sink");
  //g_object_set (G_OBJECT (sink), "sync", FALSE, NULL);
  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add (GST_BIN (pipeline), sink);

  gnl_group_append_layer (group, layer1);

  gnl_timeline_add_group (timeline, group);

  pad = gnl_timeline_get_pad_for_group (timeline, group);
  gst_pad_connect (pad, gst_element_get_pad (sink, "sink"));

  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (timeline));

  g_signal_connect (G_OBJECT (pipeline), "deep_notify", G_CALLBACK (gst_element_default_deep_notify), NULL);
  g_signal_connect (G_OBJECT (pipeline), "error", G_CALLBACK (gst_element_default_error), NULL);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));
  
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
   
}
