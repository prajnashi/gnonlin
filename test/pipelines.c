
#include <gnl/gnl.h>

#define TYPE_AUDIO 1
#define TYPE_VIDEO 2

typedef struct	_TestElementData {
  GstBin	*bin;
  gint		type;
  gboolean	done;
}		TestElementData;

static void
new_pad (GstElement *element, GstPad *pad, TestElementData *data)
{
  GstElement	*identity;

  if (((data->type == TYPE_VIDEO) && (strstr (gst_caps_to_string(gst_pad_get_caps(pad)), "video")))
      || ((data->type == TYPE_AUDIO) && (strstr (gst_caps_to_string(gst_pad_get_caps(pad)), "audio")))) {
    identity = gst_bin_get_by_name(data->bin, "identity");
    if (!identity)
      return;
    if (!(gst_pad_link(pad, gst_element_get_pad (identity, "sink")))) {
      g_printf ("couldn't link with identity....");
      return;
    }
  }
}

GstElement*
make_media_element (const gchar *filename, gint type)
{
  GstElement	*bin;
  GstElement	*src, *dbin, *identity;
  TestElementData	*data;
  gulong	signalid;
  GstElement	*pipeline;
  GstElement	*fakesink;

  bin = gst_bin_new ("bin");

  data = g_new0(TestElementData, 1);
  data->bin = GST_BIN(bin);
  data->type = type;

  src = gst_element_factory_make ("filesrc", "src");
  g_object_set (G_OBJECT (src), "location", filename, NULL);

  dbin = gst_element_factory_make ("decodebin", "dbin");
  signalid = g_signal_connect (G_OBJECT(dbin), "new_pad", G_CALLBACK(new_pad), bin);

  identity = gst_element_factory_make ("identity", "identity");

  gst_bin_add (GST_BIN (bin), src);
  gst_bin_add (GST_BIN (bin), dbin);
  gst_bin_add (GST_BIN (bin), identity);

  gst_element_add_ghost_pad (bin, gst_element_get_pad (identity, "src"), "src");

  pipeline = gst_pipeline_new("pipeline");
  fakesink = gst_element_factory_make ("fakesink", "fakesink");

  g_printf("Adding bin to pipeline\n");
  gst_bin_add (GST_BIN (pipeline), bin);
  gst_bin_add (GST_BIN (pipeline), fakesink);
  gst_element_link (bin, fakesink);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_printf ("Iterating\n");

  while ((!data->done) && gst_bin_iterate(GST_BIN(pipeline)))
    g_printf("Iterating...\n");
  
  g_printf ("Finished Iterating\n");
  g_object_ref (G_OBJECT (bin));
  gst_element_unlink (bin, fakesink);
  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  gst_bin_remove (GST_BIN (pipeline), bin);

  g_object_unref (G_OBJECT (fakesink));
  g_object_unref (G_OBJECT (pipeline));

  return bin;
}

#define make_audio_element(truc) make_media_element(truc, TYPE_AUDIO)
#define make_video_element(truc) make_media_element(truc, TYPE_VIDEO)
