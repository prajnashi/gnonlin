/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@chello.be>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#include "gnltimeline.h"

#define GNL_TYPE_TIMELINE_TIMER \
  (gnl_timeline_timer_get_type())
#define GNL_TIMELINE_TIMER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GNL_TYPE_TIMELINE_TIMER,GnlTimelineTimer))
#define GNL_TIMELINE_TIMER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GNL_TYPE_TIMELINE_TIMER,GnlTimelineTimerClass))
#define GNL_IS_TIMELINE_TIMER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GNL_TYPE_TIMELINE_TIMER))
#define GNL_IS_TIMELINE_TIMER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GNL_TYPE_TIMELINE_TIMER))

#define TIMER_CLASS(timer)  GNL_TIMELINE_TIMER_CLASS (G_OBJECT_GET_CLASS (timeline))

typedef struct _GnlTimelineTimerClass GnlTimelineTimerClass;

typedef struct {
  GnlGroup 	*group;
  GstPad	*srcpad;
  GstPad	*sinkpad;
  GstClockTime	 time;
} TimerGroupConnection;

struct _GnlTimelineTimer {
  GstElement		 parent;

  GList			*connections;
  TimerGroupConnection	*current;
};

struct _GnlTimelineTimerClass {
  GstElementClass	parent_class;
};

static GstElementClass *timer_parent_class = NULL;

static void 		gnl_timeline_timer_class_init 		(GnlTimelineTimerClass *klass);
static void 		gnl_timeline_timer_init 		(GnlTimelineTimer *timer);

static void 		gnl_timeline_timer_loop 		(GstElement *timer);

GType
gnl_timeline_timer_get_type (void)
{
  static GType timeline_timer_type = 0;

  if (!timeline_timer_type) {
    static const GTypeInfo timeline_timer_info = {
      sizeof (GnlTimelineClass),
      NULL,
      NULL,
      (GClassInitFunc) gnl_timeline_timer_class_init,
      NULL,
      NULL,
      sizeof (GnlTimeline),
      32,
      (GInstanceInitFunc) gnl_timeline_timer_init,
    };
    timeline_timer_type = g_type_register_static (GST_TYPE_ELEMENT, "GnlTimelineTimer", &timeline_timer_info, 0);
  }
  return timeline_timer_type;
}

static void
gnl_timeline_timer_class_init (GnlTimelineTimerClass *klass)
{
  GObjectClass 		*gobject_class;
  GstElementClass 	*gstelement_class;

  gobject_class = 	(GObjectClass*)klass;
  gstelement_class = 	(GstElementClass*)klass;

  timer_parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
}


static void
gnl_timeline_timer_init (GnlTimelineTimer *timer)
{
  gst_element_set_loop_function (GST_ELEMENT (timer), gnl_timeline_timer_loop);
}

static GstPadLinkReturn
timer_connect (GstPad *pad, const GstCaps *caps)
{
  GstPad *otherpad;
  TimerGroupConnection *connection;

  connection = gst_pad_get_element_private (pad);
	        
  otherpad = (GST_PAD_IS_SRC (pad)? connection->sinkpad : connection->srcpad);
		  
/*  if (GST_CAPS_IS_FIXED (caps)) */
    return gst_pad_try_set_caps (otherpad, caps);
/*  else
    return GST_PAD_CONNECT_DELAYED; */
}

static TimerGroupConnection*
gnl_timeline_timer_create_pad (GnlTimelineTimer *timer, GnlGroup *group)
{
  TimerGroupConnection *connection;
  gchar *padname;
  const gchar *objname;

  connection = g_new0 (TimerGroupConnection, 1);
  connection->group = group;

  objname = gst_object_get_name (GST_OBJECT (group));
  padname = g_strdup_printf ("%s_sink", objname);
  connection->sinkpad = gst_pad_new (padname, GST_PAD_SINK);
  g_free (padname);
  gst_element_add_pad (GST_ELEMENT (timer), connection->sinkpad);
  gst_pad_set_element_private (connection->sinkpad, connection);
  gst_pad_set_link_function (connection->sinkpad, timer_connect);
  
  padname = g_strdup_printf ("%s_src", objname);
  connection->srcpad = gst_pad_new (padname, GST_PAD_SRC);
  g_free (padname);
  gst_element_add_pad (GST_ELEMENT (timer), connection->srcpad);
  gst_pad_set_element_private (connection->srcpad, connection);
  gst_pad_set_link_function (connection->srcpad, timer_connect);

  timer->connections = g_list_prepend (timer->connections, connection);

  return connection;
}

static void
gnl_timeline_timer_loop (GstElement *element)
{
  GnlTimelineTimer *timer = GNL_TIMELINE_TIMER (element);
  GList *walk = timer->connections;
  GstClockTime current = -1;
  TimerGroupConnection* to_schedule = NULL;

  while (walk) {
    TimerGroupConnection* connection = (TimerGroupConnection *) walk->data;
    GstPad *sinkpad = connection->sinkpad;

    if (GST_PAD_IS_USABLE (sinkpad)) {
      if (connection->time <= current) {
        to_schedule = connection;
        current = connection->time;
      }
    }

    walk = g_list_next (walk);
  }

  if (to_schedule) {
    GstPad *sinkpad = to_schedule->sinkpad;
    GstData *buf;
    
    timer->current = to_schedule;
      
    buf = gst_pad_pull (sinkpad);

    if (GST_IS_EVENT (buf) && GST_EVENT_TYPE (buf) == GST_EVENT_EOS) {
      GstClockTime time;
      GstPad *srcpad;
      GnlGroup *group;

      group = to_schedule->group;
      time = gnl_layer_get_time (GNL_LAYER (group));

      g_print ("got EOS on group %s, time %lld\n",
		 gst_element_get_name (GST_ELEMENT (group)),
	         time);

      if (gnl_layer_covers (GNL_LAYER (group), time, G_MAXUINT64, GNL_COVER_START)) {
        gst_pad_disconnect (to_schedule->sinkpad, GST_PAD_PEER (to_schedule->sinkpad));

        g_print ("reactivating group %s, seek to time %lld %lld\n",
		 gst_element_get_name (GST_ELEMENT (group)),
	         time, G_MAXUINT64);

	gst_element_send_event (GST_ELEMENT (group),
	                          gst_event_new_segment_seek (
	                            GST_FORMAT_TIME |
	                            GST_SEEK_METHOD_SET |
	                            GST_SEEK_FLAG_FLUSH |
	                            GST_SEEK_FLAG_ACCURATE,
	                            time,  G_MAXUINT64));

        srcpad = gst_element_get_pad (GST_ELEMENT (group), "src");
	if (srcpad) {
          gst_pad_connect (srcpad, to_schedule->sinkpad);
          gst_element_set_state (GST_ELEMENT (group), GST_STATE_PLAYING);
	}
	else  {
	  g_warning ("group %s has no pad\n", 
		 gst_element_get_name (GST_ELEMENT (group)));
	}
      }
      else {
        gst_pad_set_active (sinkpad, FALSE);
        gst_pad_push (to_schedule->srcpad, buf);
      }
    }
    else {
      if (GST_IS_BUFFER (buf)) {
	to_schedule->time = GST_BUFFER_TIMESTAMP (buf);
      }
      if (to_schedule->time < G_MAXINT64) {
        gst_pad_push (to_schedule->srcpad, buf);
      }
      else {
        gst_data_unref (GST_DATA (buf));
      }
    }
  }
  else {
    GList *walk = timer->connections;

    while (walk) {
      TimerGroupConnection* connection = (TimerGroupConnection *) walk->data;

      gst_pad_push (connection->srcpad, GST_DATA (gst_event_new (GST_EVENT_EOS)));

      walk = g_list_next (walk);
    }
    gst_element_set_eos (element);
  }
}

/*
 * timeline
 */

static void 		gnl_timeline_class_init 	(GnlTimelineClass *klass);
static void 		gnl_timeline_init 		(GnlTimeline *timeline);

static gboolean 	gnl_timeline_prepare 		(GnlLayer *layer, GstClockTime start, GstClockTime stop);
static GstElementStateReturn
			gnl_timeline_change_state 	(GstElement *element);

static GnlVLayerClass *parent_class = NULL;

GType
gnl_timeline_get_type (void)
{
  static GType timeline_type = 0;

  if (!timeline_type) {
    static const GTypeInfo timeline_info = {
      sizeof (GnlTimelineClass),
      NULL,
      NULL,
      (GClassInitFunc) gnl_timeline_class_init,
      NULL,
      NULL,
      sizeof (GnlTimeline),
      4,
      (GInstanceInitFunc) gnl_timeline_init,
    };
    timeline_type = g_type_register_static (GNL_TYPE_VLAYER, "GnlTimeline", &timeline_info, 0);
  }
  return timeline_type;
}

static void
gnl_timeline_class_init (GnlTimelineClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GnlLayerClass *gnllayer_class;

  gobject_class 	= (GObjectClass*)klass;
  gstelement_class	= (GstElementClass*)klass;
  gnllayer_class 	= (GnlLayerClass*)klass;

  parent_class = g_type_class_ref (GNL_TYPE_VLAYER);

  gstelement_class->change_state		= gnl_timeline_change_state;

  gnllayer_class->prepare               = gnl_timeline_prepare;
}


static void
gnl_timeline_init (GnlTimeline *timeline)
{
  timeline->groups = NULL;
  
}

GnlTimeline*
gnl_timeline_new (const gchar *name)
{
  GnlTimeline *timeline;

  g_return_val_if_fail (name != NULL, NULL);

  timeline = g_object_new (GNL_TYPE_TIMELINE, NULL);
  gst_object_set_name (GST_OBJECT (timeline), name);
  timeline->timer = g_object_new (GNL_TYPE_TIMELINE_TIMER, NULL);
  gst_object_set_name (GST_OBJECT (timeline->timer), g_strdup_printf ("%s_timer", name));

  gst_bin_add (GST_BIN (timeline), GST_ELEMENT (timeline->timer));

  return timeline;
}

void
gnl_timeline_add_group (GnlTimeline *timeline, GnlGroup *group)
{
  GstElement *pipeline;
  const gchar *groupname;
  gchar *pipename;
  
  timeline->groups = g_list_prepend (timeline->groups, group);

  gnl_timeline_timer_create_pad (timeline->timer, group);

  groupname = gst_object_get_name (GST_OBJECT (group));
  pipename = g_strdup_printf ("%s_pipeline", groupname);
  pipeline = gst_pipeline_new (pipename);
  g_free (pipename);

  gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (group));
  gst_bin_add (GST_BIN (timeline), GST_ELEMENT (pipeline));
}

static TimerGroupConnection*
gnl_timeline_get_connection_for_group (GnlTimeline *timeline, GnlGroup *group)
{
  GList *walk = timeline->timer->connections;

  while (walk) {
    TimerGroupConnection *connection = (TimerGroupConnection *) walk->data;
    
    if (connection->group == group) {
      return connection;
    }
    walk = g_list_next (walk);
  }
  return NULL;
}

GstPad*
gnl_timeline_get_pad_for_group (GnlTimeline *timeline, GnlGroup *group)
{
  TimerGroupConnection *connection;

  connection = gnl_timeline_get_connection_for_group (timeline, group);
  if (connection)
    return connection->srcpad;

  return NULL;
}

static gboolean
gnl_timeline_prepare (GnlLayer *layer, GstClockTime start, GstClockTime stop)
{
  GnlTimeline *timeline = GNL_TIMELINE (layer);
  GList *walk = timeline->groups;
  gboolean res = TRUE;
  
  while (walk && res) {
    GnlGroup *group = GNL_GROUP (walk->data);
    GstEvent *event;
    GstSeekType seek_type;
    GstPad *srcpad;

    seek_type = GST_FORMAT_TIME |
                GST_SEEK_METHOD_SET |
                GST_SEEK_FLAG_FLUSH |
                GST_SEEK_FLAG_ACCURATE;
		
    event = gst_event_new_segment_seek (seek_type, 0, G_MAXINT64);

    //gst_element_set_state (GST_ELEMENT (group), GST_STATE_PAUSED);

    res &= gst_element_send_event (GST_ELEMENT (group), event);

    srcpad = gst_element_get_pad (GST_ELEMENT (group), "src");
    if (srcpad) {
      TimerGroupConnection *connection;

      connection = gnl_timeline_get_connection_for_group (timeline, group);
      gst_pad_connect (srcpad, connection->sinkpad);
    }
    else {
      g_warning ("group %s does not have a pad", 
		 gst_element_get_name (GST_ELEMENT (group)));
    }

    walk = g_list_next (walk);
  }

  return res;
}


static GstElementStateReturn
gnl_timeline_change_state (GstElement *element)
{
  GnlTimeline *timeline = GNL_TIMELINE (element);
  gint transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      g_print ("%s: 1 ready->paused\n", gst_element_get_name (element));
      gnl_timeline_prepare (GNL_LAYER (timeline), 0, G_MAXUINT64);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      g_print ("%s: 1 paused->playing\n", gst_element_get_name (element));
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      g_print ("%s: 1 playing->paused\n", gst_element_get_name (element));
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    default:
      break;
  }
  return GST_ELEMENT_CLASS (parent_class)->change_state (element);
}

