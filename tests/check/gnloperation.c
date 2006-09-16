#include "common.h"

GST_START_TEST (test_simple_operation)
{
  GstElement *pipeline;
  guint64 start, stop;
  gint64 duration;
  GstElement *comp, *oper, *source, *sink;
  CollectStructure *collect;
  GstBus * bus;
  GstMessage * message;
  gboolean carry_on = TRUE;
  GstPad * sinkpad;

  pipeline = gst_pipeline_new ("test_pipeline");
  comp = gst_element_factory_make_or_warn ("gnlcomposition", "test_composition");

  /*
    source
    Start : 0s
    Duration : 3s
    Priority : 1
  */

  source = videotest_gnl_src ("source", 0, 3 * GST_SECOND, 1 , 1);
  fail_if (source == NULL);
  check_start_stop_duration (source, 0, 3 * GST_SECOND, 3 * GST_SECOND);

  /*
    operation
    Start : 1s
    Duration : 1s
    Priority : 0
  */
  
  oper = new_operation ("oper", "identity", 1 * GST_SECOND, 1 * GST_SECOND, 0);
  fail_if (oper == NULL);
  check_start_stop_duration (oper, 1 * GST_SECOND, 2 * GST_SECOND, 1 * GST_SECOND);

  /* Add source */
  ASSERT_OBJECT_REFCOUNT(source, "source", 1);
  ASSERT_OBJECT_REFCOUNT(oper, "oper", 1);

  gst_bin_add (GST_BIN (comp), source);
  check_start_stop_duration(comp, 0, 3 * GST_SECOND, 3 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT(source, "source", 1);

  /* Add operaton */

  gst_bin_add (GST_BIN (comp), oper);
  check_start_stop_duration(comp, 0, 3 * GST_SECOND, 3 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT(oper, "oper", 1);

  /* remove source */

  gst_object_ref (source);
  gst_bin_remove (GST_BIN (comp), source);
  check_start_stop_duration (comp, 1 * GST_SECOND, 2 * GST_SECOND, 1 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT(source, "source", 1);

  /* re-add source */
  gst_bin_add (GST_BIN (comp), source);
  check_start_stop_duration(comp, 0, 3 * GST_SECOND, 3 * GST_SECOND);
  gst_object_unref (source);

  ASSERT_OBJECT_REFCOUNT(source, "source", 1);

  
  sink = gst_element_factory_make_or_warn ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (pipeline), comp, sink, NULL);

  /* Shared data */
  collect = g_new0 (CollectStructure, 1);
  collect->comp = comp;
  collect->sink = sink;

  /* Expected segments */
  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   0, 1 * GST_SECOND, 0));
  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   1 * GST_SECOND, 2 * GST_SECOND,
							   1 * GST_SECOND));
  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   2 * GST_SECOND, 3 * GST_SECOND,
							   2 * GST_SECOND));

  g_signal_connect (G_OBJECT (comp), "pad-added",
		    G_CALLBACK (composition_pad_added_cb), collect);

  sinkpad = gst_element_get_pad (sink, "sink");
  gst_pad_add_event_probe (sinkpad, G_CALLBACK (sinkpad_event_probe), collect);
  gst_pad_add_buffer_probe (sinkpad, G_CALLBACK (sinkpad_buffer_probe), collect);

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));

  GST_DEBUG ("Setting pipeline to PLAYING");
  ASSERT_OBJECT_REFCOUNT(source, "source", 1);

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  GST_DEBUG ("Let's poll the bus");

  while (carry_on) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 2);
    if (message) {
      switch (GST_MESSAGE_TYPE (message)) {
      case GST_MESSAGE_EOS:
	/* we should check if we really finished here */
	GST_WARNING ("Got an EOS");
	carry_on = FALSE;
	break;
      case GST_MESSAGE_SEGMENT_START:
      case GST_MESSAGE_SEGMENT_DONE:
	/* We shouldn't see any segement messages, since we didn't do a segment seek */
	GST_WARNING ("Saw a Segment start/stop");
	fail_if (TRUE);
	break;
      case GST_MESSAGE_ERROR:
	GST_WARNING ("Saw an ERROR");
	fail_if (TRUE);
      default:
	break;
      }
      gst_mini_object_unref (GST_MINI_OBJECT (message));
    }
  }

  GST_DEBUG ("Setting pipeline to NULL");

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  fail_if (collect->expected_segments != NULL);

  GST_DEBUG ("Resetted pipeline to READY");

  /* Expected segments */
  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   0, 1 * GST_SECOND, 0));
  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   1 * GST_SECOND, 2 * GST_SECOND,
							   1 * GST_SECOND));  
  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   2 * GST_SECOND, 3 * GST_SECOND,
							   2 * GST_SECOND));  
  collect->gotsegment = FALSE;


  GST_DEBUG ("Setting pipeline to PLAYING again");

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  carry_on = TRUE;

  GST_DEBUG ("Let's poll the bus");

  while (carry_on) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 2);
    if (message) {
      switch (GST_MESSAGE_TYPE (message)) {
      case GST_MESSAGE_EOS:
	/* we should check if we really finished here */
	carry_on = FALSE;
	break;
      case GST_MESSAGE_SEGMENT_START:
      case GST_MESSAGE_SEGMENT_DONE:
	/* We shouldn't see any segement messages, since we didn't do a segment seek */
	GST_WARNING ("Saw a Segment start/stop");
	fail_if (TRUE);
	break;
      case GST_MESSAGE_ERROR:
	GST_ERROR ("Saw an ERROR");
	fail_if (TRUE);
      default:
	break;
      }
      gst_mini_object_unref (GST_MINI_OBJECT (message));
    } else {
      GST_DEBUG ("bus_poll responded, but there wasn't any message...");
    }
  }

  fail_if (collect->expected_segments != NULL);

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE);

  gst_object_unref (GST_OBJECT(sinkpad));
  ASSERT_OBJECT_REFCOUNT_BETWEEN(pipeline, "main pipeline", 1, 2);
  gst_object_unref (pipeline);
  ASSERT_OBJECT_REFCOUNT_BETWEEN(bus, "main bus", 1, 2);
  gst_object_unref (bus);

  g_free (collect);
}

GST_END_TEST;

Suite *
gnonlin_suite (void)
{
  Suite *s = suite_create ("gnonlin");
  TCase *tc_chain = tcase_create ("gnloperation");
  guint major, minor, micro, nano;

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_simple_operation);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gnonlin_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
