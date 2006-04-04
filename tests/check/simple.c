
#include <gst/check/gstcheck.h>

typedef struct _Segment {
  gdouble	rate;
  GstFormat	format;
  gint64	start, stop, position;
}	Segment;

typedef struct _CollectStructure {
  GstElement	*comp;
  GstElement	*sink;
  guint64	last_time;
  GList		*expected_segments;
}	CollectStructure;

static GstElement *
gst_element_factory_make_or_warn (const gchar * factoryname, const gchar * name)
{
  GstElement * element;

  element = gst_element_factory_make (factoryname, name);
  fail_unless (element != NULL, "Failed to make element %s", factoryname);
  return element;
}

static void
composition_pad_added_cb (GstElement *composition, GstPad *pad, CollectStructure * collect)
{
  GstCaps *linkcaps;

  /* Connect new pad to fakesink sinkpad */
  linkcaps = gst_caps_from_string ("video/x-raw-yuv,framerate=(fraction)25/1");
  
  fail_if ((!(gst_element_link_filtered (composition, collect->sink, linkcaps))));

  gst_caps_unref (linkcaps);
}

/* return TRUE to discard the Segment */
static gboolean
compare_segments (Segment * segment, GstEvent * event)
{
  gboolean update;
  gdouble rate;
  GstFormat format;
  gint64 start, stop, position;

  gst_event_parse_new_segment (event, &update, &rate, &format, &start, &stop, &position);

  GST_DEBUG ("Got NewSegment update:%d, rate:%f, format:%d, start:%"GST_TIME_FORMAT
	     ", stop:%"GST_TIME_FORMAT", position:%"GST_TIME_FORMAT,
	     update, rate, format, GST_TIME_ARGS (start),
	     GST_TIME_ARGS (stop),
	     GST_TIME_ARGS (position));

  GST_DEBUG ("Expecting rate:%f, format:%d, start:%"GST_TIME_FORMAT
	     ", stop:%"GST_TIME_FORMAT", position:%"GST_TIME_FORMAT,
	     segment->rate, segment->format,
	     GST_TIME_ARGS (segment->start),
	     GST_TIME_ARGS (segment->stop),
	     GST_TIME_ARGS (segment->position));

  if (update) {
    GST_DEBUG ("was update, ignoring");
    return FALSE;
  }
  fail_if (rate != segment->rate);
  fail_if (format != segment->format);
  fail_if (start != segment->start);
  fail_if (stop != segment->stop);
  fail_if (position != segment->position);

  GST_DEBUG ("Segment was valid, discarding expected Segment");

  return TRUE;
}

static gboolean
sinkpad_event_probe (GstPad * sinkpad, GstEvent * event, CollectStructure * collect)
{
  Segment * segment;
  
  if (GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT) {
    fail_if (collect->expected_segments == NULL);
    segment = (Segment *) collect->expected_segments->data;

    if (compare_segments (segment, event)) {
      collect->expected_segments = g_list_next (collect->expected_segments);
      g_free (segment);
    }
  }

  return TRUE;
}

static GstElement *
videotest_gnl_src (const gchar * name, guint64 start, gint64 duration, gint pattern, guint priority)
{
  GstElement * gnlsource = NULL;
  GstElement * videotestsrc = NULL;

  videotestsrc = gst_element_factory_make_or_warn ("videotestsrc", NULL);
  g_object_set (G_OBJECT (videotestsrc), "pattern", pattern, NULL);

  gnlsource = gst_element_factory_make_or_warn ("gnlsource", name);
  fail_if (gnlsource == NULL);

  g_object_set (G_OBJECT (gnlsource),
		"start", start,
		"duration", duration,
		"media-start", start,
		"media-duration", duration,
		"priority", priority,
		NULL);

  gst_bin_add (GST_BIN (gnlsource), videotestsrc);
  
  return gnlsource;
}

static Segment *
segment_new (gdouble rate, GstFormat format, gint64 start, gint64 stop, gint64 position)
{
  Segment * segment;

  segment = g_new0 (Segment, 1);

  segment->rate = rate;
  segment->format = format;
  segment->start = start;
  segment->stop = stop;
  segment->position = position;

  return segment;
}

#define check_start_stop_duration(object, startval, stopval, durval)	\
  { \
    g_object_get (object, "start", &start, "stop", &stop, "duration", &duration, NULL); \
    fail_if (start != startval); \
    fail_if (stop != stopval); \
    fail_if (duration != durval); \
  }

GST_START_TEST (test_one_after_other)
{
  GstElement *pipeline;
  GstElement *comp, *sink, *source1, *source2;
  CollectStructure *collect;
  GstBus *bus;
  GstMessage *message;
  gboolean	carry_on = TRUE;
  guint64 start, stop;
  gint64 duration;
  GstPad *sinkpad;

  pipeline = gst_pipeline_new ("test_pipeline");
  comp = gst_element_factory_make_or_warn ("gnlcomposition", "test_composition");
  fail_if (comp == NULL);

  /*
    Source 1
    Start : 0s
    Duration : 2s
    Priority : 1
  */
  source1 = videotest_gnl_src ("source1", 0, 1 * GST_SECOND, 1, 1);
  fail_if (source1 == NULL);
  check_start_stop_duration(source1, 0, 1 * GST_SECOND, 1 * GST_SECOND);

  /*
    Source 2
    Start : 2s
    Duration : 2s
    Priority : 1
  */
  source2 = videotest_gnl_src ("source2", 1 * GST_SECOND, 1 * GST_SECOND, 2, 1);
  fail_if (source2 == NULL);
  check_start_stop_duration(source2, 1 * GST_SECOND, 2 * GST_SECOND, 1 * GST_SECOND);

  /* Add one source */

  gst_bin_add (GST_BIN (comp), source1);
  check_start_stop_duration(comp, 0, 1 * GST_SECOND, 1 * GST_SECOND);

  /* Second source */

  gst_bin_add (GST_BIN (comp), source2);
  check_start_stop_duration(comp, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  /* Remove second source */

  gst_object_ref (source1);
  gst_bin_remove (GST_BIN (comp), source1);
  check_start_stop_duration(comp, 1 * GST_SECOND, 2 * GST_SECOND, 1 * GST_SECOND);

  /* Re-add second source */

  gst_bin_add (GST_BIN (comp), source1);
  check_start_stop_duration(comp, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  sink = gst_element_factory_make_or_warn ("fakesink", "sink");
  fail_if (sink == NULL);

  gst_bin_add_many (GST_BIN (pipeline), comp, sink, NULL);

  /* Shared data */
  collect = g_new0 (CollectStructure, 1);
  collect->comp = comp;
  collect->sink = sink;
  
  /* Expected segments */
  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   0, GST_CLOCK_TIME_NONE, 0));
  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   0, 1 * GST_SECOND, 0));
  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   0, GST_CLOCK_TIME_NONE, 1 * GST_SECOND));
  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   1 * GST_SECOND, 2 * GST_SECOND,
							   1 * GST_SECOND));

  g_signal_connect (G_OBJECT (comp), "pad-added",
		    G_CALLBACK (composition_pad_added_cb), collect);

  sinkpad = gst_element_get_pad (sink, "sink");
  gst_pad_add_event_probe (sinkpad, G_CALLBACK (sinkpad_event_probe), collect);

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  while (carry_on) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 20);
    if (message) {
      switch (GST_MESSAGE_TYPE (message)) {
      case GST_MESSAGE_EOS:
	/* we should check if we really finished here */
	carry_on = FALSE;
	break;
      case GST_MESSAGE_SEGMENT_START:
      case GST_MESSAGE_SEGMENT_DONE:
	/* check if the segment is the correct one (0s-4s) */
	carry_on = FALSE;
	break;
      case GST_MESSAGE_ERROR:
	fail_if (FALSE);
      default:
	break;
      }
      gst_mini_object_unref (GST_MINI_OBJECT (message));
    }
  }

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  fail_if (collect->expected_segments != NULL);

  GST_DEBUG ("Resetted pipeline to READY");

  /* Expected segments */
  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   0, GST_CLOCK_TIME_NONE, 0));
  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   0, 1 * GST_SECOND, 0));
  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   0, GST_CLOCK_TIME_NONE, 1 * GST_SECOND));
  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   1 * GST_SECOND, 2 * GST_SECOND,
							   1 * GST_SECOND));  

  GST_DEBUG ("Setting pipeline to PLAYING again");

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  carry_on = TRUE;
  while (carry_on) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 20);
    if (message) {
      switch (GST_MESSAGE_TYPE (message)) {
      case GST_MESSAGE_EOS:
	/* we should check if we really finished here */
	carry_on = FALSE;
	break;
      case GST_MESSAGE_SEGMENT_START:
      case GST_MESSAGE_SEGMENT_DONE:
	/* check if the segment is the correct one (0s-4s) */
	carry_on = FALSE;
	break;
      case GST_MESSAGE_ERROR:
	fail_if (FALSE);
      default:
	break;
      }
      gst_mini_object_unref (GST_MINI_OBJECT (message));
    }
  }

  fail_if (collect->expected_segments != NULL);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  ASSERT_OBJECT_REFCOUNT_BETWEEN(pipeline, "main pipeline", 1, 2);
  gst_object_unref (pipeline);
  ASSERT_OBJECT_REFCOUNT_BETWEEN(bus, "main bus", 1, 2);
  gst_object_unref (bus);
}

GST_END_TEST;

GST_START_TEST (test_one_under_another)
{
  GstElement *pipeline;
  GstElement *comp, *sink, *source1, *source2;
  CollectStructure *collect;
  GstBus *bus;
  GstMessage *message;
  gboolean	carry_on = TRUE;
  guint64 start, stop;
  gint64 duration;
  GstPad *sinkpad;

  pipeline = gst_pipeline_new ("test_pipeline");
  comp = gst_element_factory_make_or_warn ("gnlcomposition", "test_composition");
  fail_if (comp == NULL);

  /*
    Source 1
    Start : 0s
    Duration : 4s
    Priority : 1
  */
  source1 = videotest_gnl_src ("source1", 0, 2 * GST_SECOND, 1, 1);
  fail_if (source1 == NULL);
  check_start_stop_duration(source1, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  /*
    Source 2
    Start : 2s
    Duration : 4s
    Priority : 2
  */
  source2 = videotest_gnl_src ("source2", 1 * GST_SECOND, 2 * GST_SECOND, 2, 2);
  fail_if (source2 == NULL);
  check_start_stop_duration(source2, 1 * GST_SECOND, 3 * GST_SECOND, 2 * GST_SECOND);

  /* Add one source */

  gst_bin_add (GST_BIN (comp), source1);
  check_start_stop_duration(comp, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  /* Second source */

  gst_bin_add (GST_BIN (comp), source2);
  check_start_stop_duration(comp, 0, 3 * GST_SECOND, 3 * GST_SECOND);

  /* Remove second source */

  gst_object_ref (source1);
  gst_bin_remove (GST_BIN (comp), source1);
  check_start_stop_duration(comp, 1 * GST_SECOND, 3 * GST_SECOND, 2 * GST_SECOND);

  /* Re-add second source */

  gst_bin_add (GST_BIN (comp), source1);
  check_start_stop_duration(comp, 0, 3 * GST_SECOND, 3 * GST_SECOND);

  sink = gst_element_factory_make_or_warn ("fakesink", "sink");
  fail_if (sink == NULL);

  gst_bin_add_many (GST_BIN (pipeline), comp, sink, NULL);

  /* Shared data */
  collect = g_new0 (CollectStructure, 1);
  collect->comp = comp;
  collect->sink = sink;
  
  /* Expected segments */
  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   0, GST_CLOCK_TIME_NONE, 0));

  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   0, 2 * GST_SECOND, 0));

  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   0, GST_CLOCK_TIME_NONE, 1 * GST_SECOND));

  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   2 * GST_SECOND, 3 * GST_SECOND,
							   2 * GST_SECOND));

  g_signal_connect (G_OBJECT (comp), "pad-added",
		    G_CALLBACK (composition_pad_added_cb), collect);

  sinkpad = gst_element_get_pad (sink, "sink");
  gst_pad_add_event_probe (sinkpad, G_CALLBACK (sinkpad_event_probe), collect);

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

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
	/* check if the segment is the correct one (0s-4s) */
	carry_on = FALSE;
	break;
      case GST_MESSAGE_ERROR:
	fail_if (FALSE);
      default:
	break;
      }
      gst_message_unref (message);
    }
  }

  fail_if (collect->expected_segments != NULL);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  ASSERT_OBJECT_REFCOUNT_BETWEEN(pipeline, "main pipeline", 1, 2);
  gst_object_unref (pipeline);
  ASSERT_OBJECT_REFCOUNT_BETWEEN(bus, "main bus", 1, 2);
  gst_object_unref (bus);
}

GST_END_TEST;

GST_START_TEST (test_one_above_another)
{
  GstElement *pipeline;
  GstElement *comp, *sink, *source1, *source2;
  CollectStructure *collect;
  GstBus *bus;
  GstMessage *message;
  gboolean	carry_on = TRUE;
  guint64 start, stop;
  gint64 duration;
  GstPad *sinkpad;

  pipeline = gst_pipeline_new ("test_pipeline");
  comp = gst_element_factory_make_or_warn ("gnlcomposition", "test_composition");
  fail_if (comp == NULL);

  /*
    Source 1
    Start : 0s
    Duration : 2s
    Priority : 2
  */
  source1 = videotest_gnl_src ("source1", 0, 2 * GST_SECOND, 1, 2);
  fail_if (source1 == NULL);
  check_start_stop_duration(source1, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  /*
    Source 2
    Start : 2s
    Duration : 2s
    Priority : 1
  */
  source2 = videotest_gnl_src ("source2", 1 * GST_SECOND, 2 * GST_SECOND, 2, 1);
  fail_if (source2 == NULL);
  check_start_stop_duration(source2, 1 * GST_SECOND, 3 * GST_SECOND, 2 * GST_SECOND);

  /* Add one source */

  gst_bin_add (GST_BIN (comp), source1);
  check_start_stop_duration(comp, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  /* Second source */

  gst_bin_add (GST_BIN (comp), source2);
  check_start_stop_duration(comp, 0, 3 * GST_SECOND, 3 * GST_SECOND);

  /* Remove second source */

  gst_object_ref (source1);
  gst_bin_remove (GST_BIN (comp), source1);
  check_start_stop_duration(comp, 1 * GST_SECOND, 3 * GST_SECOND, 2 * GST_SECOND);

  /* Re-add second source */

  gst_bin_add (GST_BIN (comp), source1);
  check_start_stop_duration(comp, 0, 3 * GST_SECOND, 3 * GST_SECOND);

  sink = gst_element_factory_make_or_warn ("fakesink", "sink");
  fail_if (sink == NULL);

  gst_bin_add_many (GST_BIN (pipeline), comp, sink, NULL);

  /* Shared data */
  collect = g_new0 (CollectStructure, 1);
  collect->comp = comp;
  collect->sink = sink;
  
  /* Expected segments */
  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   0, GST_CLOCK_TIME_NONE, 0));

  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   0, 1 * GST_SECOND, 0));

  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   0, GST_CLOCK_TIME_NONE, 1 * GST_SECOND));

  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   1 * GST_SECOND, 3 * GST_SECOND,
							   1 * GST_SECOND));

  g_signal_connect (G_OBJECT (comp), "pad-added",
		    G_CALLBACK (composition_pad_added_cb), collect);

  sinkpad = gst_element_get_pad (sink, "sink");
  gst_pad_add_event_probe (sinkpad, G_CALLBACK (sinkpad_event_probe), collect);

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

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
	/* check if the segment is the correct one (0s-4s) */
	carry_on = FALSE;
	break;
      case GST_MESSAGE_ERROR:
	fail_if (FALSE);
      default:
	break;
      }
      gst_message_unref (message);
    }
  }

  fail_if (collect->expected_segments != NULL);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  ASSERT_OBJECT_REFCOUNT_BETWEEN(pipeline, "main pipeline", 1, 2);
  gst_object_unref (pipeline);
  ASSERT_OBJECT_REFCOUNT_BETWEEN(bus, "main bus", 1, 2);
  gst_object_unref (bus);
}

GST_END_TEST;

Suite *
gnonlin_suite (void)
{
  Suite *s = suite_create ("gnonlin");
  TCase *tc_chain = tcase_create ("general");
  guint major, minor, micro, nano;

  suite_add_tcase (s, tc_chain);

  /* Only add the following test for core > 0.10.4 */
  gst_version (&major, &minor, &micro, &nano);
  if ((micro > 4) || (micro == 4 && nano > 0)) {
    tcase_add_test (tc_chain, test_one_after_other);
    tcase_add_test (tc_chain, test_one_under_another);
    
    /* Uncomment and fix this after the release 2006-03-29 */
    /*   tcase_add_test (tc_chain, test_one_above_another); */
  }
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
