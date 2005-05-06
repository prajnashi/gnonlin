/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@chello.be>
 *               2004 Edward Hervey <bilboed@bilboed.com>
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


#include "gnl.h"

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
  GnlComposition 	*comp;
  GstPad	*srcpad;
  GstPad	*sinkpad;
  GstClockTime	 time;
} TimerGroupLink;

struct _GnlTimelineTimer {
  GstElement		 parent;

  GList			*links;
  TimerGroupLink	*current;
};

struct _GnlTimelineTimerClass {
  GstElementClass	parent_class;
};

static GstElementClass *timer_parent_class = NULL;

static void 		gnl_timeline_timer_class_init 		(GnlTimelineTimerClass *klass);
static void 		gnl_timeline_timer_init 		(GnlTimelineTimer *timer);

static void		gnl_timeline_timer_dispose		(GObject *object);
static void		gnl_timeline_timer_finalize		(GObject *object);

static GstElementStateReturn
gnl_timeline_timer_change_state (GstElement *element);

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

  gobject_class->dispose = gnl_timeline_timer_dispose;
  gobject_class->finalize = gnl_timeline_timer_finalize;

  gstelement_class->change_state = gnl_timeline_timer_change_state;
  
  timer_parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
}

static void
gnl_timeline_timer_dispose (GObject *object)
{
  GnlTimelineTimer	*timer = GNL_TIMELINE_TIMER (object);
  GList			*walk = timer->links;
  TimerGroupLink	*link;

  GST_INFO("dispose");
  while (walk) {
    link = (TimerGroupLink *) walk->data;

    if (link->comp) {
      GST_INFO ("Doing composition %p:%s", 
		link->comp,
		gst_element_get_name (GST_ELEMENT (link->comp)));
      if (link->srcpad)
	gst_object_unref (GST_OBJECT (link->srcpad));
      if (link->sinkpad)
	gst_object_unref (GST_OBJECT (link->sinkpad));
    }
    walk = g_list_next (walk);
  }
  GST_INFO ("Calling parent dispose");
  G_OBJECT_CLASS (timer_parent_class)->dispose (object);
}

static void
gnl_timeline_timer_finalize (GObject *object)
{
  GnlTimelineTimer	*timer = GNL_TIMELINE_TIMER (object);
  GList			*walk = timer->links;
  TimerGroupLink	*link;

  GST_INFO ("finalize");
  while (walk) {
    link = (TimerGroupLink *) walk->data;
    g_free (link);
    walk = g_list_next (walk);
  }
  g_list_free (timer->links);

  G_OBJECT_CLASS (timer_parent_class)->finalize (object);
}

static void
gnl_timeline_timer_init (GnlTimelineTimer *timer)
{
  timer->links	= NULL;
  timer->current= NULL;
  gst_element_set_loop_function (GST_ELEMENT (timer), gnl_timeline_timer_loop);
}

static void
gnl_timeline_timer_reset (GnlTimelineTimer *timer)
{
  GList	*walk = timer->links;

  while (walk) {
    TimerGroupLink *link = (TimerGroupLink *) walk->data;

    link->time = 0LL;
    
    walk = g_list_next (walk);
  }
}

static GstElementStateReturn
gnl_timeline_timer_change_state (GstElement *element)
{
  GnlTimelineTimer *timer = GNL_TIMELINE_TIMER (element);
  gint transition = GST_STATE_TRANSITION (element);

  switch (transition) {
  case GST_STATE_PAUSED_TO_READY:
    GST_INFO ("%s: 1 null->ready", gst_element_get_name (element));
    gnl_timeline_timer_reset (timer);
    break;
  default:
    break;
  }
  return GST_ELEMENT_CLASS (timer_parent_class)->change_state (element);
}


/*
 * TimelineTimer GstPad getcaps function
 */
static GstCaps *
timer_getcaps (GstPad *pad)
{
  GstPad *otherpad;
  TimerGroupLink *link;

  link = gst_pad_get_element_private (pad);
	        
  otherpad = (GST_PAD_IS_SRC (pad)? link->sinkpad : link->srcpad);
		  
  return gst_pad_get_allowed_caps (otherpad);
}

/*
  TimelineTimer GstPad link function
*/

static GstPadLinkReturn
timer_link (GstPad *pad, const GstCaps *caps)
{
  GstPad *otherpad;
  TimerGroupLink *link;

  GST_INFO ("timer_link");
  link = gst_pad_get_element_private (pad);
  if (!link)
    GST_WARNING ("No TimerGroupLink in pad data !!!!");
  otherpad = (GST_PAD_IS_SRC (pad)? link->sinkpad : link->srcpad);

  GST_INFO("trying to link pad %s:%s to peerpad %s:%s with caps %s", 
	   GST_DEBUG_PAD_NAME(pad),
	   GST_DEBUG_PAD_NAME(otherpad),
	   gst_caps_to_string(caps));
		  
  return gst_pad_try_set_caps (otherpad, caps);
}

/*
  Adds the given GnlGroup to the list of groups to be handled by the TimelineTimer

  Returns a TimerGroupLink containing the group's information in the timer
*/

static TimerGroupLink*
gnl_timeline_timer_create_pad (GnlTimelineTimer *timer, GnlComposition *comp)
{
  TimerGroupLink *link;
  gchar *padname;
  const gchar *objname;

  GST_INFO("timer[%s], composition[%s]",
	   gst_element_get_name(GST_ELEMENT(timer)),
	   gst_element_get_name(GST_ELEMENT(comp)));

  link = g_new0 (TimerGroupLink, 1);
  link->comp = comp;
  g_object_add_weak_pointer (G_OBJECT (comp),
			     (gpointer *) &(link->comp));

  objname = gst_object_get_name (GST_OBJECT (comp));
  padname = g_strdup_printf ("%s_sink", objname);
  link->sinkpad = gst_pad_new (padname, GST_PAD_SINK);
  g_free (padname);
  gst_element_add_pad (GST_ELEMENT (timer), link->sinkpad);
  gst_pad_set_element_private (link->sinkpad, link);
  gst_pad_set_link_function (link->sinkpad, timer_link);
  gst_pad_set_getcaps_function (link->sinkpad, timer_getcaps);
  
  padname = g_strdup_printf ("%s_src", objname);
  link->srcpad = gst_pad_new (padname, GST_PAD_SRC);
  g_free (padname);
  gst_element_add_pad (GST_ELEMENT (timer), link->srcpad);
  gst_pad_set_element_private (link->srcpad, link);
  gst_pad_set_link_function (link->srcpad, timer_link);
  gst_pad_set_getcaps_function (link->srcpad, timer_getcaps);

  timer->links = g_list_prepend (timer->links, link);

  return link;
}

/*
  gnl_timeline_timer_loop

  What makes the world (i.e. timeline) go round...

  Called at every iteration
*/

static void
gnl_timeline_timer_loop (GstElement *element)
{
  GnlTimelineTimer *timer = GNL_TIMELINE_TIMER (element);
  GList *walk = timer->links;
  GstClockTime current = -1;
  TimerGroupLink* to_schedule = NULL;
  
  /* Check if there is a usable composition */

  while (walk) {
    TimerGroupLink* link = (TimerGroupLink *) walk->data;
    GstPad *sinkpad = link->sinkpad;

    if (!GST_PAD_PEER (link->sinkpad))
      GST_INFO("WALK comp[%s] time[%lld] Trying pad %s:%s",
	       gst_element_get_name(GST_ELEMENT(link->comp)),
	       link->time,
	       GST_DEBUG_PAD_NAME(sinkpad));
    else
      GST_INFO("WALK comp[%s] time[%lld] Trying pad %s:%s LINKED TO %s:%s",
	       gst_element_get_name(GST_ELEMENT(link->comp)),
	       link->time,
	       GST_DEBUG_PAD_NAME(sinkpad),
	       GST_DEBUG_PAD_NAME (GST_PAD_PEER (link->sinkpad)));      

    if (GST_PAD_IS_ACTIVE (sinkpad)) {
      if (link->time <= current) {
        to_schedule = link;
        current = link->time;
      }
    }

    walk = g_list_next (walk);
  }

  if (to_schedule) {
    GstPad *sinkpad = to_schedule->sinkpad;
    GstBuffer *buf;
    
    /* If there is a usable composition */

    GST_INFO("to_schedule[%s]", gst_element_get_name(GST_ELEMENT(to_schedule->comp)));

    if (!GST_PAD_IS_ACTIVE (to_schedule->srcpad)) {
      GST_INFO ("to_schedule->srcpad is not active, returning...");
      return;
    }

    timer->current = to_schedule;
    GST_INFO("Pulling a buffer");

    buf = GST_BUFFER (gst_pad_pull (sinkpad));

    GST_INFO("Buffer pulled");

    if (GST_IS_EVENT(buf))
      GST_INFO ("Buffer is an Event : %d", GST_EVENT_TYPE (GST_EVENT (buf)));

    if (GST_IS_EVENT (buf) && GST_EVENT_TYPE (buf) == GST_EVENT_EOS) {
      GstClockTime time;
      GstPad *srcpad;
      GnlComposition *comp;
/*       GstFormat format; */

      /* if the buffer is an EOS event */
    
      comp = to_schedule->comp;

      /* 
	 Get the selected composition's position 
	 (should in fact be the next useful position)
      */
      
/*       format = GST_FORMAT_TIME; */
/*       gst_element_query (GST_ELEMENT (group), GST_QUERY_POSITION, &format, &time); */

      time = GNL_OBJECT (comp)->current_time;

      GST_INFO ("got EOS on composition %s, time %lld",
		 gst_element_get_name (GST_ELEMENT (comp)),
	         time);

/*       if (gnl_object_covers (GNL_OBJECT (group), time, G_MAXINT64, GNL_COVER_START)) { */
      if (time < GNL_OBJECT (comp)->stop) {
	/* if there is something else at the given position */
	if (GST_PAD_IS_LINKED(to_schedule->sinkpad))
	  gst_pad_unlink (to_schedule->sinkpad, GST_PAD_PEER (to_schedule->sinkpad));

        GST_INFO ("reactivating composition %s, seek to time %" GST_TIME_FORMAT,
		  gst_element_get_name (GST_ELEMENT (comp)),
		  GST_TIME_ARGS(time));
	gst_element_set_state (GST_ELEMENT (comp), GST_STATE_PAUSED);

	gst_element_send_event (GST_ELEMENT (comp),
	                          gst_event_new_segment_seek (
	                            GST_FORMAT_TIME |
	                            GST_SEEK_METHOD_SET |
	                            GST_SEEK_FLAG_FLUSH |
	                            GST_SEEK_FLAG_ACCURATE,
	                            time,  G_MAXINT64));
	gst_element_set_state (GST_ELEMENT (comp), GST_STATE_PLAYING);
        srcpad = gst_element_get_pad (GST_ELEMENT (comp), "src");
	if (srcpad) {
	  GST_INFO("linking %s:%s to %s:%s",
		   GST_DEBUG_PAD_NAME (srcpad),
		   GST_DEBUG_PAD_NAME (to_schedule->sinkpad));
          if (!(gst_pad_link (srcpad, to_schedule->sinkpad)))
	    GST_WARNING ("Couldn't link %s:%s to %s:%s !!",
			 GST_DEBUG_PAD_NAME(srcpad),
			 GST_DEBUG_PAD_NAME(to_schedule->sinkpad));
          gst_element_set_state (GST_ELEMENT (comp), GST_STATE_PLAYING);
	} else  {
	  GST_WARNING ("composition %s has no pad", 
		       gst_element_get_name (GST_ELEMENT (comp)));
	}
      } else {
	/* If there isn't anything else in that composition (real EOS) */
	GST_INFO("Nothing else in that composition, sending real EOS and setting timegrouplink time to 0");
	gst_pad_unlink (GST_PAD_PEER (sinkpad), sinkpad);
        gst_pad_set_active (sinkpad, FALSE);
        gst_pad_push (to_schedule->srcpad, (GstData *) buf);
	to_schedule->time = 0LL;
      }
    } else {
      /* 
	 if not EOS event
	 _ update the TimerGroupLink->time
	 _ forward the Buffer/Event
      */

      if (!GST_IS_EVENT (buf)) {
	if (GST_BUFFER_DURATION (buf) == GST_CLOCK_TIME_NONE )
	  to_schedule->time = GST_BUFFER_TIMESTAMP (buf);
	else
	  to_schedule->time = GST_BUFFER_TIMESTAMP (buf) + GST_BUFFER_DURATION (buf);
      } else if (GST_IS_EVENT (buf) && (GST_EVENT_TYPE (GST_EVENT (buf)) == GST_EVENT_DISCONTINUOUS)) {
	if (!gst_event_discont_get_value (GST_EVENT(buf), GST_FORMAT_TIME, &(to_schedule->time)))
	  GST_WARNING ("Couldn't get time value for discont event !!");
	else
	  GST_DEBUG ("Got value from discont event, now %lld", to_schedule->time);
      }
      if (to_schedule->time < G_MAXINT64) {
        gst_pad_push (to_schedule->srcpad, (GstData *) buf);
      } else {
	GST_WARNING ("Not forwarding buffer/event because to_schedule->time >= G_MAXINT64");
        gst_data_unref (GST_DATA (buf));
      }
    }
  }
  else {
    /* If no usable group EOS all the groups */
    /* ERRATA : in fact we EOS the GnlTimeline */

    GST_INFO("Nothing more to schedule");

    for (walk = timer->links; walk; walk = g_list_next(walk)) {
      TimerGroupLink* link = (TimerGroupLink *) walk->data;
      
      GST_INFO ("pushing EOS on pad %s:%s", GST_DEBUG_PAD_NAME (link->srcpad));
      gst_pad_push (link->srcpad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
      
    }
    /*     gst_element_set_eos(element); */
    gst_element_set_eos (element);
    gst_element_set_eos (GST_ELEMENT(gst_element_get_parent(element)));
    
  }
  GST_INFO("End of Loop Parent[%s]",
	   gst_element_get_name(gst_element_get_parent(element)));
}

/*
 * timeline
 */

static GstElementDetails gnl_timeline_details = GST_ELEMENT_DETAILS ( 
  "GNL Timeline",
  "Filter Editor",
  "Combines GNL Composition",
  "Wim Taymans <wim.taymans@chello.be>, Edward Hervey <bilboed@bilboed.com>"
  );

static void		gnl_timeline_base_init		(gpointer g_class);
static void 		gnl_timeline_class_init 	(GnlTimelineClass *klass);
static void 		gnl_timeline_init 		(GnlTimeline *timeline);

static void		gnl_timeline_dispose		(GObject *object);
static void		gnl_timeline_finalize		(GObject *object);

static gboolean 	gnl_timeline_prepare 		(GnlObject *object, GstEvent *event);
static GstElementStateReturn
			gnl_timeline_change_state 	(GstElement *element);
static gboolean 	gnl_timeline_query 		(GstElement *element, GstQueryType type,
		                                         GstFormat *format, gint64 *value);
static GstPad *		gnl_timeline_request_new_pad	(GstElement *element, GstPadTemplate *templ,
							 const gchar *name);

static GnlCompositionClass *parent_class = NULL;

GType
gnl_timeline_get_type (void)
{
  static GType timeline_type = 0;

  if (!timeline_type) {
    static const GTypeInfo timeline_info = {
      sizeof (GnlTimelineClass),
      (GBaseInitFunc) gnl_timeline_base_init,
      NULL,
      (GClassInitFunc) gnl_timeline_class_init,
      NULL,
      NULL,
      sizeof (GnlTimeline),
      4,
      (GInstanceInitFunc) gnl_timeline_init,
    };
    timeline_type = g_type_register_static (GNL_TYPE_COMPOSITION, "GnlTimeline", &timeline_info, 0);
  }
  return timeline_type;
}

static void
gnl_timeline_base_init (gpointer g_class)
{
  GstElementClass *gstclass = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstclass, &gnl_timeline_details);
}

static void
gnl_timeline_class_init (GnlTimelineClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass	*gstbin_class;
  GnlCompositionClass *gnlcomposition_class;
  GnlObjectClass *gnlobject_class;

  gobject_class 	= (GObjectClass*)klass;
  gstelement_class	= (GstElementClass*)klass;
  gstbin_class		= (GstBinClass*)klass;
  gnlcomposition_class 	= (GnlCompositionClass*)klass;
  gnlobject_class 	= (GnlObjectClass*)klass;

  parent_class = g_type_class_ref (GNL_TYPE_COMPOSITION);

  gobject_class->dispose = gnl_timeline_dispose;
  gobject_class->finalize = gnl_timeline_finalize;

  gstelement_class->change_state	= gnl_timeline_change_state;
  gstelement_class->query		= gnl_timeline_query;
  gstelement_class->request_new_pad	= gnl_timeline_request_new_pad;

  gstbin_class->add_element		= (void (*) (GstBin *, GstElement *))gnl_timeline_add_composition;

  gnlobject_class->prepare              = gnl_timeline_prepare;
}

static void
gnl_timeline_dispose (GObject *object)
{
  GnlTimeline *timeline = GNL_TIMELINE (object);
  GList	*groups = timeline->groups;
  GnlComposition	*comp;

  GST_INFO ("dispose");
  while (groups) {
    gchar	*pipename;
    GstElement	*pipe;
    
    comp = groups->data;
    pipename = g_strdup_printf ("%s_pipeline",
				gst_object_get_name (GST_OBJECT(comp)));
    pipe = gst_bin_get_by_name (GST_BIN (timeline),
				pipename);
    g_free (pipename);

    gst_bin_remove (GST_BIN (pipe), GST_ELEMENT (comp));
    gst_bin_remove (GST_BIN (timeline), pipe);

    groups = g_list_next (groups);
  }
  gst_bin_remove (GST_BIN (timeline),
		  GST_ELEMENT (timeline->timer));
  gst_object_unref (GST_OBJECT (timeline->timer));

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gnl_timeline_finalize (GObject *object)
{
  GnlTimeline	*timeline = GNL_TIMELINE (object);

  GST_INFO ("finalize");
  g_list_free (timeline->groups);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gnl_timeline_init (GnlTimeline *timeline)
{
  timeline->groups = NULL;
}

/**
 * gnl_timeline_new:
 * @name: The name of the instance
 *
 * Returns: a newly allocated #GnlTimeline, or NULL if the creation failed
 */

GnlTimeline*
gnl_timeline_new (const gchar *name)
{
  GnlTimeline *timeline;

  g_return_val_if_fail (name != NULL, NULL);

  GST_INFO("name:%s", name);

  timeline = g_object_new (GNL_TYPE_TIMELINE, NULL);
  gst_object_set_name (GST_OBJECT (timeline), name);

  timeline->timer = g_object_new (GNL_TYPE_TIMELINE_TIMER, NULL);
  gst_object_set_name (GST_OBJECT (timeline->timer), g_strdup_printf ("%s_timer", name));
  gst_object_ref (GST_OBJECT (timeline->timer));
  gst_bin_add (GST_BIN (timeline), GST_ELEMENT (timeline->timer));

  return timeline;
}

void
timeline_update_start_stop(GnlTimeline *timeline)
{
  GList		*tmp;
  GnlObject	*obj;
  GstClockTime	start = G_MAXINT64;
  GstClockTime	stop = 0LL;
  
  if (!timeline->groups) {
    gnl_object_set_start_stop (GNL_OBJECT(timeline), 0, G_MAXINT64);
    return;
  }
  for (tmp = timeline->groups; tmp; tmp = tmp->next) {
    obj = GNL_OBJECT (tmp->data);
    if (obj->start < start)
      start = obj->start;
    if (obj->stop > stop)
      stop = obj->stop;
  }
  gnl_object_set_start_stop (GNL_OBJECT(timeline), start, stop);
}

void
group_start_stop_changed (GnlComposition *comp, GParamSpec *arg, gpointer udata)
{
  timeline_update_start_stop(GNL_TIMELINE(udata));
}

/**
 * gnl_timeline_add_composition:
 * @timeline: The #GnlTimeline to add a group to
 * @comp: The #GnlComposition to add to the timeline
 */

void
gnl_timeline_add_composition (GnlTimeline *timeline, GnlComposition *composition)
{
  GstElement *pipeline;
  const gchar *groupname;
  gchar *pipename;
  
  if (GNL_IS_COMPOSITION(composition)) {
    GST_INFO("timeline[%s](Sched:%p), comp[%s](Sched:%p)",
	     gst_element_get_name(GST_ELEMENT(timeline)),
	     GST_ELEMENT_SCHED (GST_ELEMENT (timeline)),
	     gst_element_get_name(GST_ELEMENT(composition)),
	     GST_ELEMENT_SCHED (GST_ELEMENT (composition)));
    
    timeline->groups = g_list_prepend (timeline->groups, composition);
    
    gnl_timeline_timer_create_pad (timeline->timer, composition);
    
    groupname = gst_object_get_name (GST_OBJECT (composition));
    pipename = g_strdup_printf ("%s_pipeline", groupname);
    pipeline = gst_bin_new (pipename);
    g_free (pipename);
    
    g_signal_connect (composition, "notify::start", G_CALLBACK (group_start_stop_changed), timeline);
    g_signal_connect (composition, "notify::stop", G_CALLBACK (group_start_stop_changed), timeline);
    
    gst_bin_add (GST_BIN (pipeline), GST_ELEMENT (composition));
    gst_bin_add (GST_BIN (timeline), GST_ELEMENT (pipeline));
    
    GST_INFO ("Composition(Sched:%p) added to timeline(Sched:%p)",
	      GST_ELEMENT_SCHED (GST_ELEMENT (composition)),
	      GST_ELEMENT_SCHED (GST_ELEMENT (timeline)));
    
    timeline_update_start_stop (timeline);
  } else {
    gst_bin_add (GST_BIN (timeline), GST_ELEMENT (composition));
  }
}

static TimerGroupLink*
gnl_timeline_get_link_for_group (GnlTimeline *timeline, GnlComposition *comp)
{
  GList *walk = timeline->timer->links;

  while (walk) {
    TimerGroupLink *link = (TimerGroupLink *) walk->data;
    
    if (link->comp == comp) {
      return link;
    }
    walk = g_list_next (walk);
  }
  return NULL;
}

/**
 * gnl_timeline_get_pad_for_composition:
 * @timeline: The #GnlTimeline
 * @comp: The #GnlComposition we want a #GstPad to
 *
 * Returns: The corresponding #GstPad, or NULL if the composition couldn't be found
 */

GstPad*
gnl_timeline_get_pad_for_composition (GnlTimeline *timeline, GnlComposition *comp)
{
  TimerGroupLink *link;

  GST_INFO("timeline[%s], composition[%s]",
	   gst_element_get_name(GST_ELEMENT(timeline)),
	   gst_element_get_name(GST_ELEMENT(comp)));


  link = gnl_timeline_get_link_for_group (timeline, comp);
  if (link) {
    GST_INFO ("Found pad, returning %s:%s",
	      GST_DEBUG_PAD_NAME (link->srcpad));
    return link->srcpad;
  }

  return NULL;
}


static GstPad *
gnl_timeline_request_new_pad	(GstElement *element, GstPadTemplate *templ,
				 const gchar *name)
{
  GnlTimeline	*timeline = GNL_TIMELINE (element);
  GList	*walk = timeline->groups;

  /* look for the composition called name */
  while (walk) {
    GnlComposition	*comp = GNL_COMPOSITION (walk->data);

    /* if there's one return gnl_timeline_get_pad_for_composition() */

    if (!g_ascii_strcasecmp(gst_element_get_name(comp), name))
      return gnl_timeline_get_pad_for_composition (timeline, comp);
  }
  return NULL;
}


static gboolean
gnl_timeline_prepare (GnlObject *object, GstEvent *event)
{
  GnlTimeline *timeline = GNL_TIMELINE (object);
  GList *walk = timeline->groups;
  gboolean res = TRUE;
  
  GST_INFO("prepare in timeline[%p] [%lld]->[%lld]",
	   object,
	   GST_EVENT_SEEK_OFFSET(event),
	   GST_EVENT_SEEK_ENDOFFSET(event));
    
  if (gst_element_get_state (GST_ELEMENT (object)) != GST_STATE_PAUSED) {
    GST_WARNING ("%s: Prepare while not in PAUSED",
		 gst_element_get_name (GST_ELEMENT (object)));
    return FALSE;
  }

  while (walk && res) {
    GnlComposition *comp = GNL_COMPOSITION (walk->data);
    GstPad *srcpad;
    
    gst_event_ref (event);
    res &= gst_element_send_event (GST_ELEMENT (comp), event);

    srcpad = gst_element_get_pad (GST_ELEMENT (comp), "src");
    if (srcpad) {
      TimerGroupLink *link;

      link = gnl_timeline_get_link_for_group (timeline, comp);

      /* If there is already something linked, unlink it ! Pad'pitie ! */
      if (GST_PAD_IS_LINKED(link->sinkpad))
	gst_pad_unlink (GST_PAD_PEER(link->sinkpad), link->sinkpad);
      
      GST_INFO ("About to link composition %s(sched:%p) to TimelineTimer(sched:%p). TimelineSched:%p",
		gst_element_get_name (GST_ELEMENT(comp)),
		GST_ELEMENT_SCHED(GST_ELEMENT (comp)),
		GST_ELEMENT_SCHED (GST_ELEMENT (timeline->timer)),
		GST_ELEMENT_SCHED (GST_ELEMENT (timeline)));

      if (!gst_pad_link (srcpad, link->sinkpad))
	GST_WARNING ("Couldn't link composition [%s] to the Timeline Timer !!",
		     gst_element_get_name (GST_ELEMENT (comp)));
    }
    else {
      GST_WARNING ("composition %s does not have a 'src' pad",
		   gst_element_get_name (GST_ELEMENT (comp)));
    }

    walk = g_list_next (walk);
  }
  
  gnl_timeline_timer_reset (timeline->timer);

  GST_INFO("END");
  return res;
}

static gboolean
gnl_timeline_query (GstElement *element, GstQueryType type,
		    GstFormat *format, gint64 *value)
{
  GnlTimeline	*timeline = GNL_TIMELINE(element);

  if (*format != GST_FORMAT_TIME)
    return FALSE;

  if (type == GST_QUERY_POSITION) {
    
    /* when the element is queried before anything is scheduled this
     * would cause a segfault because timeline->timer->current == NULL
     */
    if ( ! timeline->timer->current ) {
      *value = 0;
      return FALSE;
    }
    
    *value = timeline->timer->current->time;
    return TRUE;
  }
  return GST_ELEMENT_CLASS (parent_class)->query (element, type, format, value);
}

static GstElementStateReturn
gnl_timeline_change_state (GstElement *element)
{
  GstElementStateReturn	res = GST_STATE_SUCCESS;
  GstElementStateReturn	res2 = GST_STATE_SUCCESS;
  GnlTimeline *timeline = GNL_TIMELINE (element);
  gint transition = GST_STATE_TRANSITION (element);

  if (transition == GST_STATE_READY_TO_PAUSED)
    res2 = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (transition) {
  case GST_STATE_NULL_TO_READY:
    GST_INFO ("%s: 1 null->ready", gst_element_get_name (element));
    break;
  case GST_STATE_READY_TO_PAUSED:
    {
      GstEvent *event;
      GstSeekType seek_type;

      seek_type = GST_FORMAT_TIME |
	GST_SEEK_METHOD_SET |
	GST_SEEK_FLAG_FLUSH |
	GST_SEEK_FLAG_ACCURATE;

      GST_INFO ("%s: 1 ready->paused", gst_element_get_name (element));

      event = gst_event_new_segment_seek (seek_type, 0, G_MAXINT64);
      if (!gnl_timeline_prepare (GNL_OBJECT (timeline), event))
	res = GST_STATE_FAILURE;
      break;
    }
  case GST_STATE_PAUSED_TO_PLAYING:
/*     { */
/*       GstEvent *event; */
/*       GstSeekType seek_type; */

/*       seek_type = GST_FORMAT_TIME | */
/* 	GST_SEEK_METHOD_SET | */
/* 	GST_SEEK_FLAG_FLUSH | */
/* 	GST_SEEK_FLAG_ACCURATE; */

    GST_INFO ("%s: 1 paused->playing", gst_element_get_name (element));

/*       event = gst_event_new_segment_seek (seek_type, 0, G_MAXINT64); */
/*       if (!gnl_timeline_prepare (GNL_OBJECT (timeline), event)) */
/* 	res = GST_STATE_FAILURE; */
    break;
  case GST_STATE_PLAYING_TO_PAUSED:
    GST_INFO ("%s: 1 playing->paused", gst_element_get_name (element));
    break;
  case GST_STATE_PAUSED_TO_READY:
    break;
  default:
    break;
  }
  GST_INFO ("Calling parent change_state function");
  if (transition != GST_STATE_READY_TO_PAUSED)
    res2 = GST_ELEMENT_CLASS (parent_class)->change_state (element);
  GST_INFO ("Really finished");
  return ((res2 && res) ? res2 : GST_STATE_FAILURE);
}

