
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
  GnlLayer *layer1;
  GnlSource *source1, *source2;
  GstElement *fakesrc1, *fakesrc2;
  GstElement *sink;
  GstPad *srcpad, *sinkpad;
  GstClockTime time;

  gnl_init (&argc, &argv);

  pipeline = gst_pipeline_new ("main_pipeline");

  fakesrc1 = gst_element_factory_make ("fakesrc", "src1");
  source1 = gnl_source_new ("my_source1", fakesrc1);
  gnl_source_set_media_start_stop (source1, 2, 5);
  gnl_source_set_start_stop (source1, 0, 3);

  fakesrc2 = gst_element_factory_make ("fakesrc", "src2");
  source2 = gnl_source_new ("my_source2", fakesrc2);
  gnl_source_set_media_start_stop (source2, 8, 15);
  gnl_source_set_start_stop (source2, 3, 10);

  layer1 = gnl_layer_new ("my_layer1");
  gnl_layer_add_source (layer1, source1, "src");
  gnl_layer_add_source (layer1, source2, "src");

  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add (GST_BIN (pipeline), sink);

  g_signal_connect (G_OBJECT (pipeline), "deep_notify", G_CALLBACK (gst_element_default_deep_notify), NULL);

  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (layer1));

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);
  
  sinkpad = gst_element_get_pad (sink, "sink");

  time = 0;

  do {
    gst_element_send_event (GST_ELEMENT (layer1),
                          gst_event_new_segment_seek (
                            GST_FORMAT_TIME |
                            GST_SEEK_METHOD_SET |
                            GST_SEEK_FLAG_FLUSH |
                            GST_SEEK_FLAG_ACCURATE,
                            time,  G_MAXUINT64));

    srcpad = gst_element_get_pad (GST_ELEMENT (layer1), "src");
    srcpad = GST_PAD (GST_PAD_REALIZE (srcpad));

    gst_pad_connect (srcpad, sinkpad);

    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
    while (gst_bin_iterate (GST_BIN (pipeline)));

    gst_pad_disconnect (srcpad, sinkpad);

    time = gnl_layer_get_time (GNL_LAYER (layer1));

    g_print ("stopped at time %lld\n", time);
  }
  while (gnl_layer_covers (GNL_LAYER (layer1), time, G_MAXUINT64, GNL_COVER_START));

  g_print ("doing layer seek to 2 7\n");

  time = 2;
  do {
    gst_element_send_event (GST_ELEMENT (layer1),
                          gst_event_new_segment_seek (
                            GST_FORMAT_TIME |
                            GST_SEEK_METHOD_SET |
                            GST_SEEK_FLAG_FLUSH |
                            GST_SEEK_FLAG_ACCURATE,
                            time,  7));

    srcpad = gst_element_get_pad (GST_ELEMENT (layer1), "src");
    srcpad = GST_PAD (GST_PAD_REALIZE (srcpad));

    gst_pad_connect (srcpad, sinkpad);

    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
    while (gst_bin_iterate (GST_BIN (pipeline)));

    gst_pad_disconnect (srcpad, sinkpad);

    time = gnl_layer_get_time (GNL_LAYER (layer1));

    g_print ("stopped at time %lld\n", time);
  }
  while (gnl_layer_covers (GNL_LAYER (layer1), time, G_MAXUINT64, GNL_COVER_START) && time < 7);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
}
