
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

static void
new_pad (GstElement *element, GstPad *pad, GstBin *bin)
{
  g_print ("new pad %s\n", GST_PAD_NAME (pad));
  if (strstr (GST_PAD_NAME (pad), "video")) {
    GstElement *decoder;
    GstPad *newpad, *target;
    
    gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PAUSED);

    decoder = gst_element_factory_make ("windec", "v_decoder");
    gst_bin_add (bin, decoder);

    target = gst_element_get_pad (decoder, "sink");
    gst_pad_connect (pad, target);
    newpad = gst_element_get_pad (decoder, "src");

    gst_element_add_ghost_pad (GST_ELEMENT (bin), newpad, "v_src");
    gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PLAYING);
  }
  else if (strstr (GST_PAD_NAME (pad), "audio")) {
    GstElement *decoder;
    GstPad *newpad, *target;
    
    gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PAUSED);

    decoder = gst_element_factory_make ("mad", "a_decoder");
    gst_bin_add (bin, decoder);

    target = gst_element_get_pad (decoder, "sink");
    gst_pad_connect (pad, target);
    newpad = gst_element_get_pad (decoder, "src");

    gst_element_add_ghost_pad (GST_ELEMENT (bin), newpad, "a_src");
    gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PLAYING);
  }
}

static GstElement*
make_element (void)
{
  GstElement *bin;
  GstElement *src, *demux;

  bin = gst_bin_new ("bin");
  src = gst_element_factory_make ("filesrc", "src");
  g_object_set (G_OBJECT (src), "location", "/opt/data/Matrix_f900.avi", NULL);
  demux = gst_element_factory_make ("avidemux", "demux");

  gst_element_connect_pads (src, "src", demux, "sink");

  gst_bin_add (GST_BIN (bin), src);
  gst_bin_add (GST_BIN (bin), demux);

  g_signal_connect (G_OBJECT (demux), "new_pad", G_CALLBACK (new_pad), bin);
  
  return bin;
}

int
main (int argc, gchar *argv[]) 
{
  GstElement *pipeline;
  GnlTimeline *timeline;
  GnlGroup *group1, *group2;
  GnlLayer *layer1, *layer2;
  GnlSource *source1;
  GstElement *sink1, *sink2;

  gnl_init (&argc, &argv);

  pipeline = gst_pipeline_new ("main_pipeline");
  timeline = gnl_timeline_new ("main_timeline");

  group1 = gnl_group_new ("my_group1");
  group2 = gnl_group_new ("my_group2");

  source1 = gnl_source_new ("my_source1", make_element ());
  gnl_source_set_start_stop (source1, 21 * GST_SECOND, 30 * GST_SECOND);

  layer1 = gnl_layer_new ("my_layer1");
  gnl_layer_add_source (layer1, source1, "v_src", 0 * GST_SECOND);
  layer2 = gnl_layer_new ("my_layer2");
  gnl_layer_add_source (layer2, source1, "a_src", 0 * GST_SECOND);

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
  while (gst_bin_iterate (GST_BIN (pipeline)));
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
}
