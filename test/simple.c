
#include <gnl/gnl.h>

/*
 (-0----->-----------5000----->------10000----->-----15000-)
 ! main_timeline                                           !
 !                                                         !
 ! (-----------------------------------------------------) !
 ! ! my_track                                            ! !
 ! ! (-------------------------------)                   ! !
 ! ! ! source1                       !                   ! !
 ! ! !                               !                   ! !
 ! ! (-------------------------------)                   ! !
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
  GnlTimeline *timeline;
  GnlTrack *track;
  GnlSource *source1, *source2;

  timeline = gnl_timeline_new ("main_timeline");

  source1 = gnl_source_new_from_pipeline ("my_source1", bin1);
  gnl_source_set_start_time (source1, 0);
  gnl_source_set_end_time (source1, 10000);

  source2 = gnl_source_new_from_pipeline ("my_source2", bin2);
  gnl_source_set_start_time (source2, 0);
  gnl_source_set_end_time (source2, 10000);

  track = gnl_track_new ("my_track");
  gnl_track_add_source (track, source1, 0);
  gnl_track_add_source (track, source2, 5000);

  gnl_timeline_add_track (timeline, track);

  gst_element_set_state (GST_ELEMENT (timeline), GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (timeline)));
  
  gst_element_set_state (GST_ELEMENT (timeline), GST_STATE_NULL);
   
}
