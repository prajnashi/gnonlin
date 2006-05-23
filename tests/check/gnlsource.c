#include <gst/check/gstcheck.h>

typedef struct _Segment {
  gdouble	rate;
  GstFormat	format;
  gint64	start, stop, position;
}	Segment;

typedef struct _CollectStructure {
  GstElement	*source;
  GstElement	*sink;
  guint64	last_time;
  gboolean	gotsegment;
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
gnlsource_pad_added_cb (GstElement *gnlsource, GstPad *pad, GstElement *sink)
{
  GST_DEBUG_OBJECT (gnlsource, "About to link to sink");
  fail_if (!(gst_element_link (gnlsource, sink)));
  GST_DEBUG_OBJECT (gnlsource, "Linked to sink");
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
  
  GST_DEBUG ("Got new event");

  if (GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT) {
    fail_if (collect->expected_segments == NULL);
    segment = (Segment *) collect->expected_segments->data;

    if (compare_segments (segment, event)) {
      collect->expected_segments = g_list_next (collect->expected_segments);
      g_free (segment);
    }
    collect->gotsegment = TRUE;
  }

  return TRUE;
}

static gboolean
sinkpad_buffer_probe (GstPad * sinkpad, GstBuffer * buffer, CollectStructure * collect)
{
  fail_if(!collect->gotsegment);
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

static GstElement *
videotest_in_bin_gnl_src (const gchar * name, guint64 start, gint64 duration, gint pattern, guint priority)
{
  GstElement * gnlsource = NULL;
  GstElement * videotestsrc = NULL;
  GstElement * bin = NULL;

  videotestsrc = gst_element_factory_make_or_warn ("videotestsrc", NULL);
  g_object_set (G_OBJECT (videotestsrc), "pattern", pattern, NULL);
  bin = gst_bin_new (NULL);

  gnlsource = gst_element_factory_make_or_warn ("gnlsource", name);
  fail_if (gnlsource == NULL);

  g_object_set (G_OBJECT (gnlsource),
		"start", start,
		"duration", duration,
		"media-start", start,
		"media-duration", duration,
		"priority", priority,
		NULL);

  gst_bin_add (GST_BIN (bin), videotestsrc);

  gst_bin_add (GST_BIN (gnlsource), bin);
  
  gst_element_add_pad (bin, gst_ghost_pad_new ("src", gst_element_get_pad (videotestsrc, "src")));

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

GST_START_TEST (test_simple_videotestsrc)
{
  GstElement * pipeline;
  GstElement * gnlsource, *sink;
  CollectStructure * collect;
  GstBus * bus;
  GstMessage * message;
  gboolean carry_on = TRUE;
  guint64 start, stop;
  gint64 duration;
  GstPad * sinkpad;

  pipeline = gst_pipeline_new ("test_pipeline");
  
  /*
    Source 1
    Start : 0s
    Duration : 1s
    Priority : 1
  */
  gnlsource = videotest_gnl_src ("source1", 0, 1 * GST_SECOND, 1, 1);
  fail_if (gnlsource == NULL);
  check_start_stop_duration(gnlsource, 0, 1 * GST_SECOND, 1 * GST_SECOND);

  sink = gst_element_factory_make_or_warn ("fakesink", "sink");
  fail_if (sink == NULL);

  gst_bin_add_many (GST_BIN (pipeline), gnlsource, sink, NULL);

  /* Shared data */
  collect = g_new0 (CollectStructure, 1);
  collect->source = gnlsource;
  collect->sink = sink;
  
  /* Expected segments */
  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   0, 1 * GST_SECOND, 0));
  
  g_signal_connect (G_OBJECT (gnlsource), "pad-added",
		    G_CALLBACK (gnlsource_pad_added_cb),
		    sink);

  sinkpad = gst_element_get_pad (sink, "sink");
  fail_if (sinkpad == NULL);
  gst_pad_add_event_probe (sinkpad, G_CALLBACK (sinkpad_event_probe),
			   collect);
  gst_pad_add_buffer_probe (sinkpad, G_CALLBACK (sinkpad_buffer_probe),
			   collect);

  bus = gst_element_get_bus (pipeline);

  GST_DEBUG ("Setting pipeline to PLAYING");
  ASSERT_OBJECT_REFCOUNT (gnlsource, "gnlsource", 1);

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  GST_DEBUG ("Let's poll the bus");

  while (carry_on) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 2);
    GST_LOG ("poll");
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
	fail_if (FALSE);
	carry_on = FALSE;
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

  gst_object_unref (GST_OBJECT (sinkpad));

  GST_DEBUG ("Resetted pipeline to READY");  
}

GST_END_TEST;

GST_START_TEST (test_videotestsrc_in_bin)
{
  GstElement * pipeline;
  GstElement * gnlsource, *sink;
  CollectStructure * collect;
  GstBus * bus;
  GstMessage * message;
  gboolean carry_on = TRUE;
  guint64 start, stop;
  gint64 duration;
  GstPad * sinkpad;

  pipeline = gst_pipeline_new ("test_pipeline");
  
  /*
    Source 1
    Start : 0s
    Duration : 1s
    Priority : 1
  */
  gnlsource = videotest_in_bin_gnl_src ("source1", 0, 1 * GST_SECOND, 1, 1);
  fail_if (gnlsource == NULL);
  check_start_stop_duration(gnlsource, 0, 1 * GST_SECOND, 1 * GST_SECOND);

  sink = gst_element_factory_make_or_warn ("fakesink", "sink");
  fail_if (sink == NULL);

  gst_bin_add_many (GST_BIN (pipeline), gnlsource, sink, NULL);

  /* Shared data */
  collect = g_new0 (CollectStructure, 1);
  collect->source = gnlsource;
  collect->sink = sink;
  
  /* Expected segments */
  collect->expected_segments = g_list_append (collect->expected_segments,
					      segment_new (1.0, GST_FORMAT_TIME,
							   0, 1 * GST_SECOND, 0));
  
  g_signal_connect (G_OBJECT (gnlsource), "pad-added",
		    G_CALLBACK (gnlsource_pad_added_cb),
		    sink);

  sinkpad = gst_element_get_pad (sink, "sink");
  fail_if (sinkpad == NULL);
  gst_pad_add_event_probe (sinkpad, G_CALLBACK (sinkpad_event_probe),
			   collect);
  gst_pad_add_buffer_probe (sinkpad, G_CALLBACK (sinkpad_buffer_probe),
			   collect);

  bus = gst_element_get_bus (pipeline);

  GST_DEBUG ("Setting pipeline to PLAYING");
  ASSERT_OBJECT_REFCOUNT (gnlsource, "gnlsource", 1);

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  GST_DEBUG ("Let's poll the bus");

  while (carry_on) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 2);
    GST_LOG ("poll");
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
	fail_if (FALSE);
	carry_on = FALSE;
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

  gst_object_unref (GST_OBJECT (sinkpad));

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  fail_if (collect->expected_segments != NULL);

  GST_DEBUG ("Resetted pipeline to READY");  
}

GST_END_TEST;

Suite *
gnonlin_suite (void)
{
  Suite *s = suite_create ("gnonlin");
  TCase *tc_chain = tcase_create ("gnlsource");
  guint major, minor, micro, nano;

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_simple_videotestsrc);
  tcase_add_test (tc_chain, test_videotestsrc_in_bin);

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
