
#include <gnl/gnl.h>

/*
  0 - 1 - 2 - 3 - 4 - 5 - 6 - 7 - 8 - 9 -10 -11 -12 -13 -14 -15 -16 -17

              (-----------------------)       (-----------------------)
              ! source1               !       ! source 2	      !
              (-----------------------)       (-----------------------)
  (-----------------------)       (----------------------)
  ! source3               !       ! source4              !
  (-----------------------)       (----------------------)

  */
int
main (int argc, gchar *argv[]) 
{
  GstElement *pipeline;
  GnlTimeline *timeline;
  GnlVLayer *vlayer;
  GnlLayer *layer1, *layer2;
  GnlSource *source1, *source2, *source3, *source4;
  GstElement *fakesrc1, *fakesrc2, *fakesrc3, *fakesrc4, *sink;
  GstPad *srcpad, *sinkpad;
  gint i = 0;
  GstClockTime time;

  gnl_init (&argc, &argv);

  pipeline = gst_pipeline_new ("main_pipeline");

  fakesrc1 = gst_element_factory_make ("fakesrc", "src1");
  source1 = gnl_source_new ("my_source1", fakesrc1);
  gnl_source_set_media_start_stop (source1, 0, 6);
  gnl_source_set_start_stop (source1, 3, 9);

  fakesrc2 = gst_element_factory_make ("fakesrc", "src2");
  source2 = gnl_source_new ("my_source2", fakesrc2);
  gnl_source_set_media_start_stop (source2, 0, 6);
  gnl_source_set_start_stop (source2, 11, 17);

  fakesrc3 = gst_element_factory_make ("fakesrc", "src3");
  source3 = gnl_source_new ("my_source3", fakesrc3);
  gnl_source_set_media_start_stop (source3, 0, 6);
  gnl_source_set_start_stop (source3, 0, 6);

  fakesrc4 = gst_element_factory_make ("fakesrc", "src4");
  source4 = gnl_source_new ("my_source2", fakesrc4);
  gnl_source_set_media_start_stop (source4, 0, 6);
  gnl_source_set_start_stop (source4, 8, 14);

  layer1 = gnl_layer_new ("my_layer1");
  gnl_layer_add_source (layer1, source1, "src");
  gnl_layer_add_source (layer1, source2, "src");
  layer2 = gnl_layer_new ("my_layer2");
  gnl_layer_add_source (layer2, source3, "src");
  gnl_layer_add_source (layer2, source4, "src");

  vlayer = gnl_vlayer_new ("my_vlayer");

  gnl_vlayer_append_layer (vlayer, layer1);
  gnl_vlayer_append_layer (vlayer, layer2);

  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add (GST_BIN (pipeline), sink);
  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (vlayer));

  g_signal_connect (G_OBJECT (pipeline), "deep_notify", G_CALLBACK (gst_element_default_deep_notify), NULL);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);

  sinkpad = gst_element_get_pad (sink, "sink");

  time = 0;
  do {
    gst_element_send_event (GST_ELEMENT (vlayer),
                           gst_event_new_segment_seek (
                              GST_FORMAT_TIME |
                              GST_SEEK_METHOD_SET |
                              GST_SEEK_FLAG_FLUSH |
                              GST_SEEK_FLAG_ACCURATE,
                              time,  G_MAXUINT64));

    srcpad = gst_element_get_pad (GST_ELEMENT (vlayer), "src");
    srcpad = GST_PAD (GST_PAD_REALIZE (srcpad));

    gst_pad_connect (srcpad, sinkpad);

    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
    while (gst_bin_iterate (GST_BIN (pipeline)));

    gst_pad_disconnect (srcpad, sinkpad);

    time = gnl_layer_get_time (GNL_LAYER (vlayer));

    g_print ("stopped at time %lld\n", time);
  }
  while (gnl_layer_covers (GNL_LAYER (vlayer), time, G_MAXUINT64, GNL_COVER_START));

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
   
}
