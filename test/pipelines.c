
#include <gnl/gnl.h>

static void
new_pad (GstElement *element, GstPad *pad, GstBin *bin)
{
  g_print ("new pad %s\n", GST_PAD_NAME (pad));
  if (strstr (GST_PAD_NAME (pad), "video")) {
    GstElement *decoder;
    GstPad *newpad, *target;
    
    gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PAUSED);

    decoder = gst_element_factory_make ("ffdec_msmpeg4", "v_decoder");
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

GstElement*
make_avi_element (const gchar *filename)
{
  GstElement *bin;
  GstElement *src, *demux;

  bin = gst_bin_new ("bin");
  src = gst_element_factory_make ("filesrc", "src");
  g_object_set (G_OBJECT (src), "location", filename, NULL);
  demux = gst_element_factory_make ("avidemux", "demux");

  gst_element_connect_pads (src, "src", demux, "sink");

  gst_bin_add (GST_BIN (bin), src);
  gst_bin_add (GST_BIN (bin), demux);

  g_signal_connect (G_OBJECT (demux), "new_pad", G_CALLBACK (new_pad), bin);
  
  return bin;
}


GstElement*
make_dv_element (const gchar *filename)
{
  GstElement *bin;
  GstElement *src, *demux;

  bin = gst_bin_new ("bin");
  src = gst_element_factory_make ("filesrc", "src");
  g_object_set (G_OBJECT (src), "location", filename, NULL);
  demux = gst_element_factory_make ("dvdec", "demux");

  gst_element_connect_pads (src, "src", demux, "sink");

  gst_bin_add (GST_BIN (bin), src);
  gst_bin_add (GST_BIN (bin), demux);

  gst_element_add_ghost_pad (GST_ELEMENT (bin), gst_element_get_pad (demux, "video"), "v_src");
  gst_element_add_ghost_pad (GST_ELEMENT (bin), gst_element_get_pad (demux, "audio"), "a_src");
  
  return bin;
}

