
#include <gnl/gnl.h>
#include "pipelines.c"

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
  GstElement *src1, *src2;
  GstElement *sink;

  gnl_init (&argc, &argv);

  pipeline = gst_pipeline_new ("main_pipeline");

  src1 = gst_element_factory_make ("fakesrc", "src1");
  source1 = gnl_source_new ("my_source1", src1);
  gnl_source_set_start_stop (source1, 2, 5);

  src2 = gst_element_factory_make ("fakesrc", "src2");
  source2 = gnl_source_new ("my_source2", src2);
  gnl_source_set_start_stop (source2, 8, 15);

  layer1 = gnl_layer_new ("my_layer1");
  gnl_layer_add_source (layer1, source1, "src", 0);
  gnl_layer_add_source (layer1, source2, "src", 3);

  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add (GST_BIN (pipeline), sink);
  gst_element_connect_pads (GST_ELEMENT (layer1), "src", sink, "sink");

  g_signal_connect (G_OBJECT (pipeline), "deep_notify", G_CALLBACK (gst_element_default_deep_notify), NULL);

  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (layer1));

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  while (gst_bin_iterate (GST_BIN (pipeline)));
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
}
