
#include <gnl/gnl.h>

/*
 
 .-0----->-----------5000----->------10000----->-----15000---.
 ! main_timeline                                             !
 !                                                           !
 ! .- composition -----------------------------------------. !
 ! !                                                       ! !
 ! ! .- layer1 ------------------------------------------. ! !
 ! ! !                                                   ! ! !
 ! ! !                .- operation ---.                  ! ! !
 ! ! !                !               !                  ! ! !
 ! ! !                ! blend         !                  ! ! !
 ! ! !                '---------------'                  ! ! !
 ! ! '---------------------------------------------------' ! !
 ! ! .- layer2 ------------------------------------------. ! !
 ! ! !                                                   ! ! !
 ! ! ! .-- source1 -------------------.                  ! ! !
 ! ! ! !                              !                  ! ! !
 ! ! ! ! /myfile.avi                  !                  ! ! !
 ! ! ! '------------------------------'                  ! ! !
 ! ! '---------------------------------------------------' ! !
 ! ! .- layer3 ------------------------------------------. ! !
 ! ! !                .- source2 ---------------------.  ! ! !
 ! ! !                !                               !  ! ! !
 ! ! !                ! /myfile2.avi                  !  ! ! !
 ! ! !                '-------------------------------'  ! ! !
 ! ! '---------------------------------------------------' ! !
 ! '-------------------------------------------------------' !
 '-----------------------------------------------------------'
*/

int
main (int argc, gchar *argv[]) 
{
  GnlTimeline *timeline;
  GnlComposition *composition;
  GnlLayer *layer1, *layer2, *layer3;
  GnlSource *source1, *source2;
  GnlOperation *blend;
  GstElement *element1, *element2, *blend_element;

  gnl_init (&argc, &argv);

  timeline = gnl_timeline_new ("main_timeline");

  source1 = gnl_source_new ("my_source1");
  element1 = gst_elementfactory_make ("fakesrc", "src1");
  gnl_source_set_element (source1, element1);
  gnl_source_set_start_stop (source1, 0, 10000);

  source2 = gnl_source_new ("my_source2");
  element2 = gst_elementfactory_make ("fakesrc", "src2");
  gnl_source_set_element (source2, element2);
  gnl_source_set_start_stop (source2, 0, 10000);

  blend = gnl_operation_new ("blend");
  blend_element = gst_elementfactory_make ("aggregator", "blend");
  gnl_source_set_element (GNL_SOURCE (blend), blend_element);
  gnl_source_set_start_stop (GNL_SOURCE (blend), 0, 5000);

  composition = gnl_composition_new ("my_composition");
  gnl_source_set_start_stop (GNL_SOURCE (composition), 0, 5000);

  layer1 = gnl_layer_new ("layer1");
  gnl_layer_add_source (layer1, GNL_SOURCE (blend), 5000);
  layer2 = gnl_layer_new ("layer2");
  gnl_layer_add_source (layer2, source1, 0);
  layer3 = gnl_layer_new ("layer3");
  gnl_layer_add_source (layer3, source2, 5000);

  gnl_composition_append_layer (composition, layer1);
  gnl_composition_append_layer (composition, layer2);
  gnl_composition_append_layer (composition, layer3);

  gnl_composition_append_layer (GNL_COMPOSITION (timeline), GNL_LAYER (composition));

  gst_element_set_state (GST_ELEMENT (timeline), GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (timeline)));
  
  gst_element_set_state (GST_ELEMENT (timeline), GST_STATE_NULL);
   
}
