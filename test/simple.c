
#include <gnl/gnl.h>

/*
 (-0----->-----------5-------->------10-------->-----15----)
 ! main_timeline                                           !
 !                                                         !
 ! (-----------------------------------------------------) !
 ! ! my_layer1                                           ! !
 ! ! (-------------------------------)                   ! !
 ! ! ! source1                       !                   ! !
 ! ! !                               !                   ! !
 ! ! (-------------------------------)                   ! !
 ! (-----------------------------------------------------) !
 ! (-----------------------------------------------------) !
 ! ! my_layer2                                           ! !
 ! !                  (-------------------------------)  ! !
 ! !                  ! source2                       !  ! !
 ! !                  !                               !  ! !
 ! !                  (-------------------------------)  ! !
 ! (-----------------------------------------------------) !
 !                                                         !
 (---------------------------------------------------------)
*/

int
main (int argc, gchar *argv[]) 
{
  GstElement *pipeline;
  GnlTimeline *timeline;
  GnlGroup *group;
  GnlLayer *layer1, *layer2;
  GnlSource *source1, *source2;
  GstElement *fakesrc1, *fakesrc2, *sink;
  GstPad *pad;

  gnl_init (&argc, &argv);

  pipeline = gst_pipeline_new ("main_pipeline");
  timeline = gnl_timeline_new ("main_timeline");

  fakesrc1 = gst_element_factory_make ("fakesrc", "src1");
  source1 = gnl_source_new ("my_source1", fakesrc1);
  gnl_source_set_media_start_stop (source1, 0, 6);
  gnl_source_set_start_stop (source1, 0, 6);

  fakesrc2 = gst_element_factory_make ("fakesrc", "src2");
  source2 = gnl_source_new ("my_source2", fakesrc2);
  gnl_source_set_media_start_stop (source2, 0, 6);
  gnl_source_set_start_stop (source2, 3, 9);

  layer1 = gnl_layer_new ("my_layer1");
  gnl_layer_add_source (layer1, source1, "src");
  layer2 = gnl_layer_new ("my_layer2");
  gnl_layer_add_source (layer2, source2, "src");

  group = gnl_group_new ("my_group");

  gnl_group_append_layer (group, layer1);
  gnl_group_append_layer (group, layer2);

  gnl_timeline_add_group (timeline, group);

  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add (GST_BIN (pipeline), sink);
  pad = gnl_timeline_get_pad_for_group (timeline, group);
  gst_pad_connect (pad, gst_element_get_pad (sink, "sink"));

  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (timeline));

  g_signal_connect (pipeline, "deep_notify", G_CALLBACK (gst_element_default_deep_notify), NULL);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));
  
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
   
}
