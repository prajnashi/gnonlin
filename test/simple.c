
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

static void
property_change_callback (GObject *object, GstObject *orig, GParamSpec *pspec, GstElement *pipeline)
{
  GValue value = { 0, }; /* the important thing is that value.type = 0 */
  gchar *str = 0;

  if (pspec->flags & G_PARAM_READABLE) {
    /* let's not print these out for excluded properties... */
    g_value_init(&value, G_PARAM_SPEC_VALUE_TYPE (pspec));
    g_object_get_property (G_OBJECT (orig), pspec->name, &value);
    str = g_strdup_value_contents (&value);
    g_print ("%s: %s = %s\n", GST_OBJECT_NAME (orig), pspec->name, str);
    g_free (str);
    g_value_unset(&value);
  } else {
    g_warning ("Parameter not readable. What's up with that?");
  } 
} 

int
main (int argc, gchar *argv[]) 
{
  GstElement *pipeline;
  GnlTimeline *timeline;
  GnlGroup *group;
  GnlLayer *layer1, *layer2;
  GnlSource *source1, *source2;
  GstElement *fakesrc1, *fakesrc2, *sink;

  gnl_init (&argc, &argv);

  pipeline = gst_pipeline_new ("main_pipeline");
  timeline = gnl_timeline_new ("main_timeline");

  source1 = gnl_source_new ("my_source1");
  fakesrc1 = gst_element_factory_make ("fakesrc", "src1");
  gnl_source_set_element (source1, fakesrc1);
  gnl_source_set_start_stop (source1, 0, 6);

  source2 = gnl_source_new ("my_source2");
  fakesrc2 = gst_element_factory_make ("fakesrc", "src2");
  gnl_source_set_element (source2, fakesrc2);
  gnl_source_set_start_stop (source2, 0, 6);

  layer1 = gnl_layer_new ("my_layer1");
  gnl_layer_add_source (layer1, source1, 0);
  layer2 = gnl_layer_new ("my_layer2");
  gnl_layer_add_source (layer2, source2, 3);

  group = gnl_group_new ("my_group");

  gnl_composition_append_layer (GNL_COMPOSITION (group), layer1);
  gnl_composition_append_layer (GNL_COMPOSITION (group), layer2);

  gnl_timeline_add_group (timeline, group);

  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add (GST_BIN (pipeline), sink);
  gst_element_connect_pads (GST_ELEMENT (group), "src", sink, "sink");

  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (timeline));

  g_signal_connect (pipeline, "deep_notify", G_CALLBACK (property_change_callback), pipeline);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));
  
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
   
}
