/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@chello.be>
 *               2004 Edward Hervey <edward@fluendo.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gnl.h"

GST_BOILERPLATE (GnlComposition, gnl_composition, GnlObject, GNL_TYPE_OBJECT);

static GstElementDetails gnl_composition_details =
GST_ELEMENT_DETAILS ("GNonLin Composition",
    "Filter/Editor",
    "Combines GNL objects",
    "Wim Taymans <wim.taymans@chello.be>, Edward Hervey <bilboed@bilboed.com>");

static GstStaticPadTemplate gnl_composition_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gnlcomposition);
#define GST_CAT_DEFAULT gnlcomposition

struct _GnlCompositionPrivate
{
  gboolean dispose_has_run;

  /* 
     Sorted List of GnlObjects , ThreadSafe 
     objects_start : sorted by start-time then priority
     objects_stop : sorted by stop-time then priority
     objects_hash : contains signal handlers id for controlled objects
     objects_lock : mutex to acces/modify any of those lists/hashtable
   */
  GList *objects_start;
  GList *objects_stop;
  GHashTable *objects_hash;
  GMutex *objects_lock;

  /*
    thread-safe Seek handling.
    flushing_lock : mutex to access flushing and pending_idle
    flushing : 
    pending_idle :
  */
  GMutex *flushing_lock;
  gboolean flushing;
  guint pending_idle;

  /* source top-level ghostpad */
  GstPad *ghostpad;

  /* current stack, list of GnlObject* */
  GNode *current;

  GnlObject *defaultobject;

  /*
     current segment seek start/stop time. 
     Reconstruct pipeline ONLY if seeking outside of those values
     FIXME : segment_start isn't always the earliest time before which the
		timeline doesn't need to be modified
  */
  GstClockTime segment_start;
  GstClockTime segment_stop;

  /* pending child seek */
  GstEvent *childseek;

  /* Seek segment handler */
  GstSegment *segment;

  /* number of pads we are waiting to appear so be can do proper linking */
  guint	waitingpads;

  /*
     OUR sync_handler on the child_bus 
     We are called before gnl_object_sync_handler
   */
  GstPadEventFunction gnl_event_pad_func;
};

#define OBJECT_IN_ACTIVE_SEGMENT(comp,element) \
  (((GNL_OBJECT (element)->start >= comp->private->segment_start) &&	\
    (GNL_OBJECT (element)->start < comp->private->segment_stop)) ||	\
   ((GNL_OBJECT (element)->stop > comp->private->segment_start) &&	\
    (GNL_OBJECT (element)->stop <= comp->private->segment_stop)))	\

static void gnl_composition_dispose (GObject * object);
static void gnl_composition_finalize (GObject * object);
static void gnl_composition_reset (GnlComposition * comp);

static gboolean gnl_composition_add_object (GstBin * bin, GstElement * element);

static void gnl_composition_handle_message (GstBin * bin, GstMessage * message);

static gboolean
gnl_composition_remove_object (GstBin * bin, GstElement * element);

static GstStateChangeReturn
gnl_composition_change_state (GstElement * element, GstStateChange transition);


static gboolean
seek_handling (GnlComposition * comp, gboolean initial, gboolean update);

static gboolean
update_pipeline (GnlComposition * comp, GstClockTime currenttime,
    gboolean initial, gboolean change_state);

#define COMP_REAL_START(comp) \
  (MAX (comp->private->segment->start, GNL_OBJECT (comp)->start))

#define COMP_REAL_STOP(comp) \
  ((GST_CLOCK_TIME_IS_VALID (comp->private->segment->stop) \
    ? (MIN (comp->private->segment->stop, GNL_OBJECT (comp)->stop))) \
   : (GNL_OBJECT (comp)->stop))

#define COMP_ENTRY(comp, object) \
  (g_hash_table_lookup (comp->private->objects_hash, (gconstpointer) object))

#define COMP_OBJECTS_LOCK(comp) G_STMT_START {				\
    GST_LOG_OBJECT (comp, "locking objects_lock from thread %p",		\
      g_thread_self());							\
    g_mutex_lock (comp->private->objects_lock);				\
    GST_LOG_OBJECT (comp, "locked object_lock from thread %p",		\
		    g_thread_self());					\
  } G_STMT_END

#define COMP_OBJECTS_UNLOCK(comp) G_STMT_START {			\
    GST_LOG_OBJECT (comp, "unlocking objects_lock from thread %p",		\
		    g_thread_self());					\
    g_mutex_unlock (comp->private->objects_lock);			\
  } G_STMT_END

#define COMP_FLUSHING_LOCK(comp) G_STMT_START {				\
    GST_LOG_OBJECT (comp, "locking flushing_lock from thread %p",		\
      g_thread_self());							\
    g_mutex_lock (comp->private->flushing_lock);				\
    GST_LOG_OBJECT (comp, "locked flushing_lock from thread %p",		\
		    g_thread_self());					\
  } G_STMT_END

#define COMP_FLUSHING_UNLOCK(comp) G_STMT_START {			\
    GST_LOG_OBJECT (comp, "unlocking flushing_lock from thread %p",		\
		    g_thread_self());					\
    g_mutex_unlock (comp->private->flushing_lock);			\
  } G_STMT_END

static gboolean gnl_composition_prepare (GnlObject * object);


typedef struct _GnlCompositionEntry GnlCompositionEntry;

struct _GnlCompositionEntry
{
  GnlObject *object;

  /* handler ids for property notifications */
  gulong starthandler;
  gulong stophandler;
  gulong priorityhandler;
  gulong activehandler;

  /* handler id for 'no-more-pads' signal */
  gulong nomorepadshandler;
  gulong padaddedhandler;
  gulong padremovedhandler;
};

static void
gnl_composition_base_init (gpointer g_class)
{
  GstElementClass *gstclass = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstclass, &gnl_composition_details);
}

static void
gnl_composition_class_init (GnlCompositionClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;
  GnlObjectClass *gnlobject_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbin_class = (GstBinClass *) klass;
  gnlobject_class = (GnlObjectClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gnlcomposition, "gnlcomposition",
      GST_DEBUG_FG_BLUE | GST_DEBUG_BOLD, "GNonLin Composition");

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gnl_composition_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gnl_composition_finalize);

  gstelement_class->change_state = gnl_composition_change_state;
/*   gstelement_class->query	 = gnl_composition_query; */

  gstbin_class->add_element = GST_DEBUG_FUNCPTR (gnl_composition_add_object);
  gstbin_class->remove_element =
      GST_DEBUG_FUNCPTR (gnl_composition_remove_object);
  gstbin_class->handle_message =
      GST_DEBUG_FUNCPTR (gnl_composition_handle_message);

  gnlobject_class->prepare = GST_DEBUG_FUNCPTR (gnl_composition_prepare);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gnl_composition_src_template));
}

static void
hash_value_destroy (GnlCompositionEntry * entry)
{
  if (entry->starthandler)
    g_signal_handler_disconnect (entry->object, entry->starthandler);
  if (entry->stophandler)
    g_signal_handler_disconnect (entry->object, entry->stophandler);
  if (entry->priorityhandler)
    g_signal_handler_disconnect (entry->object, entry->priorityhandler);
  g_signal_handler_disconnect (entry->object, entry->activehandler);
  g_signal_handler_disconnect (entry->object, entry->padremovedhandler);
  g_signal_handler_disconnect (entry->object, entry->padaddedhandler);

  if (entry->nomorepadshandler)
    g_signal_handler_disconnect (entry->object, entry->nomorepadshandler);
  g_free (entry);
}

static void
gnl_composition_init (GnlComposition * comp, GnlCompositionClass * klass)
{
  GST_OBJECT_FLAG_SET (comp, GNL_OBJECT_SOURCE);

  comp->private = g_new0 (GnlCompositionPrivate, 1);
  comp->private->objects_lock = g_mutex_new ();
  comp->private->objects_start = NULL;
  comp->private->objects_stop = NULL;

  comp->private->flushing_lock = g_mutex_new ();
  comp->private->flushing = FALSE;
  comp->private->pending_idle = 0;

  comp->private->segment = gst_segment_new ();

  comp->private->waitingpads = 0;

  comp->private->defaultobject = NULL;

  comp->private->objects_hash = g_hash_table_new_full
      (g_direct_hash,
      g_direct_equal, NULL, (GDestroyNotify) hash_value_destroy);

  gnl_composition_reset (comp);
}

static void
gnl_composition_dispose (GObject * object)
{
  GnlComposition *comp = GNL_COMPOSITION (object);

  if (comp->private->dispose_has_run)
    return;

  comp->private->dispose_has_run = TRUE;

  if (comp->private->ghostpad) {
    gnl_object_remove_ghost_pad (GNL_OBJECT (object), comp->private->ghostpad);
    comp->private->ghostpad = NULL;
  }

  if (comp->private->childseek) {
    gst_event_unref (comp->private->childseek);
    comp->private->childseek = NULL;
  }

  if (comp->private->current) {
    g_node_destroy (comp->private->current);
    comp->private->current = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gnl_composition_finalize (GObject * object)
{
  GnlComposition *comp = GNL_COMPOSITION (object);

  GST_INFO ("finalize");

  COMP_OBJECTS_LOCK (comp);
  g_list_free (comp->private->objects_start);
  g_list_free (comp->private->objects_stop);
  if (comp->private->current)
    g_node_destroy (comp->private->current);
  g_hash_table_destroy (comp->private->objects_hash);
  COMP_OBJECTS_UNLOCK (comp);

  g_mutex_free (comp->private->objects_lock);
  gst_segment_free (comp->private->segment);

  g_mutex_free (comp->private->flushing_lock);

  g_free (comp->private);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
unlock_child_state (GstElement * child, GValue * ret, gpointer udata)
{
  GST_DEBUG_OBJECT (child, "unlocking state");
  gst_element_set_locked_state (child, FALSE);
  return TRUE;
}

static gboolean
lock_child_state (GstElement * child, GValue * ret, gpointer udata)
{
  GST_DEBUG_OBJECT (child, "locking state");
  gst_element_set_locked_state (child, TRUE);
  return TRUE;
}

static void
unlock_childs (GnlComposition * comp)
{
  GstIterator *childs;
  GstIteratorResult res;
  GValue val = { 0 };

  g_value_init (&val, G_TYPE_BOOLEAN);
  g_value_set_boolean (&val, FALSE);
  childs = gst_bin_iterate_elements (GST_BIN (comp));
  res =
      gst_iterator_fold (childs, (GstIteratorFoldFunction) unlock_child_state,
      &val, NULL);
  gst_iterator_free (childs);
}

static void
gnl_composition_reset (GnlComposition * comp)
{
  GST_DEBUG_OBJECT (comp, "resetting");

  comp->private->segment_start = GST_CLOCK_TIME_NONE;
  comp->private->segment_stop = GST_CLOCK_TIME_NONE;

  gst_segment_init (comp->private->segment, GST_FORMAT_TIME);

  if (comp->private->current)
    g_node_destroy (comp->private->current);
  comp->private->current = NULL;

  if (comp->private->ghostpad) {
    gnl_object_remove_ghost_pad (GNL_OBJECT (comp), comp->private->ghostpad);
    comp->private->ghostpad = NULL;
  }

  if (comp->private->childseek) {
    gst_event_unref (comp->private->childseek);
    comp->private->childseek = NULL;
  }
  
  comp->private->waitingpads = 0;

  unlock_childs (comp);

  COMP_FLUSHING_LOCK (comp);
  if (comp->private->pending_idle)
    g_source_remove (comp->private->pending_idle);
  comp->private->pending_idle = 0;
  comp->private->flushing = FALSE;
  COMP_FLUSHING_UNLOCK (comp);

  GST_DEBUG_OBJECT (comp, "Composition now resetted");
}

static gboolean
segment_done_main_thread (GnlComposition * comp)
{
  /* Set up a non-initial seek on segment_stop */
  GST_DEBUG_OBJECT (comp,
      "Setting segment->start to segment_stop:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (comp->private->segment_stop));
  comp->private->segment->start = comp->private->segment_stop;

  seek_handling (comp, TRUE, TRUE);

  if (!comp->private->current) {
    /* If we're at the end, post SEGMENT_DONE, or push EOS */
    GST_DEBUG_OBJECT (comp, "Nothing else to play");

    if (!(comp->private->segment->flags & GST_SEEK_FLAG_SEGMENT)
        && comp->private->ghostpad)
      gst_pad_push_event (comp->private->ghostpad, gst_event_new_eos ());
    else if (comp->private->segment->flags & GST_SEEK_FLAG_SEGMENT) {
      gint64 epos;

      if (GST_CLOCK_TIME_IS_VALID (comp->private->segment->stop))
        epos = (MIN (comp->private->segment->stop, GNL_OBJECT (comp)->stop));
      else
        epos = (GNL_OBJECT (comp)->stop);

      GST_BIN_CLASS (parent_class)->handle_message
          (GST_BIN (comp),
          gst_message_new_segment_done (GST_OBJECT (comp),
              comp->private->segment->format, epos));
    }
  }
  return FALSE;
}

static void
gnl_composition_handle_message (GstBin * bin, GstMessage * message)
{
  GnlComposition *comp = GNL_COMPOSITION (bin);
  gboolean dropit = FALSE;

  GST_DEBUG_OBJECT (comp, "message:%s from %s",
      gst_message_type_get_name (GST_MESSAGE_TYPE (message)),
      GST_MESSAGE_SRC (message) ? GST_ELEMENT_NAME (GST_MESSAGE_SRC (message)) :
      "UNKNOWN");

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_SEGMENT_START:{
      COMP_FLUSHING_LOCK (comp);
      if (comp->private->pending_idle) {
        GST_DEBUG_OBJECT (comp, "removing pending seek for main thread");
        g_source_remove (comp->private->pending_idle);
      }
      comp->private->pending_idle = 0;
      comp->private->flushing = FALSE;
      COMP_FLUSHING_UNLOCK (comp);
      dropit = TRUE;
      break;
    }
    case GST_MESSAGE_SEGMENT_DONE:{
      COMP_FLUSHING_LOCK (comp);
      if (comp->private->flushing) {
        GST_DEBUG_OBJECT (comp, "flushing, bailing out");
        COMP_FLUSHING_UNLOCK (comp);
        dropit = TRUE;
        break;
      }
      COMP_FLUSHING_UNLOCK (comp);


      GST_DEBUG_OBJECT (comp, "Adding segment_done handling to main thread");
      if (comp->private->pending_idle) {
        GST_WARNING_OBJECT (comp,
            "There was already a pending segment_done in main thread !");
        g_source_remove (comp->private->pending_idle);
      }
      comp->private->pending_idle =
          g_idle_add ((GSourceFunc) segment_done_main_thread, (gpointer) comp);

      dropit = TRUE;
      break;
    }
    default:
      break;
  }

  if (dropit)
    gst_message_unref (message);
  else
    GST_BIN_CLASS (parent_class)->handle_message (bin, message);
}

static gint
priority_comp (GnlObject * a, GnlObject * b)
{
  return a->priority - b->priority;
}

static gboolean
have_to_update_pipeline (GnlComposition * comp)
{
  GST_DEBUG_OBJECT (comp,
      "segment[%" GST_TIME_FORMAT "--%" GST_TIME_FORMAT "] current[%"
      GST_TIME_FORMAT "--%" GST_TIME_FORMAT "]",
      GST_TIME_ARGS (comp->private->segment->start),
      GST_TIME_ARGS (comp->private->segment->stop),
      GST_TIME_ARGS (comp->private->segment_start),
      GST_TIME_ARGS (comp->private->segment_stop));

  if (comp->private->segment->start < comp->private->segment_start)
    return TRUE;
  if (comp->private->segment->start >= comp->private->segment_stop)
    return TRUE;
  return FALSE;
}

/**
 * get_new_seek_event:
 *
 * Returns a seek event for the currently configured segment
 * and start/stop values
 *
 * The GstSegment and segment_start|stop must have been configured
 * before calling this function.
 */
static GstEvent *
get_new_seek_event (GnlComposition * comp, gboolean initial)
{
  GstSeekFlags flags;
  gint64 start, stop;

  GST_DEBUG_OBJECT (comp, "initial:%d", initial);
  /* remove the seek flag */
  if (!(initial))
    flags = comp->private->segment->flags;
  else
    flags =
        GST_SEEK_FLAG_SEGMENT | GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH;

  start = MAX (comp->private->segment->start, comp->private->segment_start);
  stop = GST_CLOCK_TIME_IS_VALID (comp->private->segment->stop)
      ? MIN (comp->private->segment->stop, comp->private->segment_stop)
      : comp->private->segment_stop;

  GST_DEBUG_OBJECT (comp,
      "Created new seek event. Flags:%d, start:%" GST_TIME_FORMAT ", stop:%"
      GST_TIME_FORMAT, flags, GST_TIME_ARGS (start), GST_TIME_ARGS (stop));
  return gst_event_new_seek (comp->private->segment->rate,
      comp->private->segment->format, flags, GST_SEEK_TYPE_SET, start,
      GST_SEEK_TYPE_SET, stop);
}

/*
  Figures out if pipeline needs updating.
  Updates it and sends the seek event.
  Sends flush events downstream if needed.
  can be called by user_seek or segment_done
*/

static gboolean
seek_handling (GnlComposition * comp, gboolean initial, gboolean update)
{
  GST_DEBUG_OBJECT (comp, "initial:%d, update:%d", initial, update);

  COMP_FLUSHING_LOCK (comp);

  GST_DEBUG_OBJECT (comp, "Setting flushing to TRUE");
  comp->private->flushing = TRUE;

  /* Send downstream flush start/stop if needed */
  if (comp->private->ghostpad
      && (comp->private->segment->flags & GST_SEEK_FLAG_FLUSH)
      && (!update)) {
    GST_LOG_OBJECT (comp, "Sending downstream flush start/stop");
    gst_pad_push_event (comp->private->ghostpad, gst_event_new_flush_start ());
    gst_pad_push_event (comp->private->ghostpad, gst_event_new_flush_stop ());
  }

  COMP_FLUSHING_UNLOCK (comp);

  if (update || have_to_update_pipeline (comp)) {
    update_pipeline (comp, comp->private->segment->start, initial, TRUE);
  }

  return TRUE;
}

static void
handle_seek_event (GnlComposition * comp, GstEvent * event)
{
  gboolean update;
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;

  gst_event_parse_seek (event, &rate, &format, &flags,
      &cur_type, &cur, &stop_type, &stop);

  GST_DEBUG_OBJECT (comp,
      "start:%" GST_TIME_FORMAT " -- stop:%" GST_TIME_FORMAT "  flags:%d",
      GST_TIME_ARGS (cur), GST_TIME_ARGS (stop), flags);

  gst_segment_set_seek (comp->private->segment,
      rate, format, flags, cur_type, cur, stop_type, stop, &update);

  GST_DEBUG_OBJECT (comp, "Segment now has flags:%d",
      comp->private->segment->flags);

  /* crop the segment start/stop values */
  /* Only crop segment start value if we don't have a default object */
  if (comp->private->defaultobject == NULL)
    comp->private->segment->start = MAX (comp->private->segment->start,
					 GNL_OBJECT (comp)->start);
  comp->private->segment->stop = MIN (comp->private->segment->stop,
      GNL_OBJECT (comp)->stop);

  seek_handling (comp, TRUE, FALSE);
}

static gboolean
gnl_composition_event_handler (GstPad * ghostpad, GstEvent * event)
{
  GnlComposition *comp = GNL_COMPOSITION (gst_pad_get_parent (ghostpad));
  gboolean res = TRUE;


  GST_DEBUG_OBJECT (comp, "event type:%s", GST_EVENT_TYPE_NAME (event));
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      GstEvent * nevent;

      handle_seek_event (comp, event);

      /* the incoming event might not be quite correct, we get a new proper
       * event to pass on to the childs. */
      nevent = get_new_seek_event (comp, FALSE);
      gst_event_unref (event);
      event = nevent;
      break;
    }
    default:
      break;
  }

  /* FIXME : What should we do here if waitingpads != 0 ?? */
  /*		Delay ? Ignore ? Refuse ?*/

  if (res) {
    GST_DEBUG_OBJECT (comp, "About to call gnl_event_pad_func()");
    COMP_OBJECTS_LOCK (comp);
    res = comp->private->gnl_event_pad_func (ghostpad, event);
    COMP_OBJECTS_UNLOCK (comp);
    GST_DEBUG_OBJECT (comp, "Done calling gnl_event_pad_func() %d", res);
  }
  gst_object_unref (comp);
  return res;
}

static void
pad_blocked (GstPad * pad, gboolean blocked, GnlComposition * comp)
{
  GST_DEBUG_OBJECT (comp, "Pad : %s:%s , blocked:%d",
      GST_DEBUG_PAD_NAME (pad), blocked);
}

/* gnl_composition_ghost_pad_set_target:
 * target: The target #GstPad. The refcount will be decremented (given to the ghostpad).
 */

static void
gnl_composition_ghost_pad_set_target (GnlComposition * comp, GstPad * target)
{
  gboolean hadghost = (comp->private->ghostpad) ? TRUE : FALSE;

  if (target)
    GST_DEBUG_OBJECT (comp, "%s:%s , hadghost:%d",
		      GST_DEBUG_PAD_NAME (target), hadghost);
  else
    GST_DEBUG_OBJECT (comp, "Removing target, hadghost:%d", hadghost);

  if (!(hadghost)) {
    comp->private->ghostpad = gnl_object_ghost_pad_no_target (GNL_OBJECT (comp),
        "src", GST_PAD_SRC);
    GST_DEBUG_OBJECT (comp->private->ghostpad,
        "About to replace event_pad_func");
    comp->private->gnl_event_pad_func =
        GST_PAD_EVENTFUNC (comp->private->ghostpad);
    gst_pad_set_event_function (comp->private->ghostpad,
        GST_DEBUG_FUNCPTR (gnl_composition_event_handler));
    GST_DEBUG_OBJECT (comp->private->ghostpad, "eventfunc is now %s",
        GST_DEBUG_FUNCPTR_NAME (GST_PAD_EVENTFUNC (comp->private->ghostpad)));    
  } else {
    GstPad *ptarget =
        gst_ghost_pad_get_target (GST_GHOST_PAD (comp->private->ghostpad));

    if (ptarget && ptarget == target) {
      GST_DEBUG_OBJECT (comp,
          "Target of ghostpad is the same as existing one, not changing");
      gst_object_unref (ptarget);
      return;
    }

    if (ptarget) {
      GST_DEBUG_OBJECT (comp, "Previous target was %s:%s, blocking that pad",
			GST_DEBUG_PAD_NAME (ptarget));
      gst_pad_set_blocked_async (ptarget, TRUE, (GstPadBlockCallback) pad_blocked,
				 comp);
      gst_object_unref (ptarget);
    }
  }

  gnl_object_ghost_pad_set_target (GNL_OBJECT (comp),
      comp->private->ghostpad, target);
  if (!(hadghost)) {
    gst_pad_set_active (comp->private->ghostpad, TRUE);
    if (!(gst_element_add_pad (GST_ELEMENT (comp), comp->private->ghostpad)))
      GST_WARNING ("Couldn't add the ghostpad");
    else
      gst_element_no_more_pads (GST_ELEMENT (comp));
  }
  GST_DEBUG_OBJECT (comp, "END");
}

static GstClockTime
next_stop_in_region_above_priority (GnlComposition * composition,
    GstClockTime start, GstClockTime stop, guint priority)
{
  GList *tmp = composition->private->objects_start;
  GnlObject *object;
  GstClockTime res = stop;

  GST_DEBUG_OBJECT (composition, "start: %" GST_TIME_FORMAT " stop: %"
      GST_TIME_FORMAT " priority:%d",
      GST_TIME_ARGS (start), GST_TIME_ARGS (stop), priority);

  /* Find first object with priority < priority, and start < start_position <= stop */
  for (; tmp; tmp = g_list_next (tmp)) {
    object = (GnlObject *) tmp->data;

    if (object->priority >= priority)
      continue;

    if (object->stop <= start)
      continue;

    if (object->start <= start)
      continue;

    if (object->start > stop)
      break;

    res = object->start;
    GST_DEBUG_OBJECT (composition, "Found %s [prio:%d] at %" GST_TIME_FORMAT,
        GST_OBJECT_NAME (object),
        object->priority, GST_TIME_ARGS (object->start));
    break;
  }
  return res;
}


/*
 * Converts a sorted list to a tree
 * Recursive
 *
 * stack will be set to the next item to use in the parent.
 * If operations number of sinks is limited, it will only use that number.
 */

static GNode *
convert_list_to_tree (GList ** stack, GstClockTime * stop, guint * highprio)
{
  GNode *ret;
  guint nbsinks;
  gboolean limit;
  GList *tmp;
  GnlObject *object;

  if (!stack || !*stack)
    return NULL;

  object = (GnlObject *) (*stack)->data;

  GST_DEBUG ("object:%s , stop:%"GST_TIME_FORMAT"highprio:%d", GST_ELEMENT_NAME (object),
	     GST_TIME_ARGS (*stop), *highprio);

  /* update earliest stop */
  if (GST_CLOCK_TIME_IS_VALID (*stop)) {
    if (GST_CLOCK_TIME_IS_VALID (object->stop) && (*stop > object->stop))
      *stop = object->stop;
  } else {
    *stop = object->stop;
  }

  if (GNL_IS_SOURCE (object)) {
    *stack = g_list_next (*stack);
    /* update highest priority.
     * We do this here, since it's only used with sources (leafs of the tree) */
    if (object->priority > *highprio)
      *highprio = object->priority;
    ret = g_node_new (object);
    goto beach;
  } else {                      /* GnlOperation */
    GnlOperation *oper = (GnlOperation *) object;

    GST_LOG_OBJECT (oper, "operation, num_sinks:%d",
		    oper->num_sinks);
    ret = g_node_new (object);
    limit = (oper->num_sinks != -1);
    nbsinks = oper->num_sinks;

    /* FIXME : if num_sinks == -1 : request the proper number of pads */

    for (tmp = g_list_next (*stack); tmp && (!limit || nbsinks);) {
      g_node_append (ret, convert_list_to_tree (&tmp, stop, highprio));

      if (limit)
        nbsinks--;
    }
  }

 beach:
  GST_DEBUG_OBJECT (object, "*stop:%"GST_TIME_FORMAT" priority:%d",
		    GST_TIME_ARGS (*stop), *highprio);

  return ret;
}

/**
 * get_stack_list:
 * @comp: The #GnlComposition
 * @timestamp: The #GstClockTime to look at
 * @priority: The priority level to start looking from
 * @activeonly: Only look for active elements if TRUE
 * @stop: The smallest stop time of the objects in the stack
 * @highprio: The highest priority in the stack
 *
 * Not MT-safe, you should take the objects lock before calling it.
 * Returns: A tree of #GNode sorted in priority order, corresponding
 * to the given search arguments. The returned value can be #NULL.
 */

static GNode *
get_stack_list (GnlComposition * comp, GstClockTime timestamp,
		guint priority, gboolean activeonly, GstClockTime * stop,
		guint *highprio)
{
  GList *tmp = comp->private->objects_start;
  GList *stack = NULL;
  GNode *ret = NULL;
  GstClockTime nstop = GST_CLOCK_TIME_NONE;
  guint highest = 0;

  GST_DEBUG_OBJECT (comp,
      "timestamp:%" GST_TIME_FORMAT ", priority:%d, activeonly:%d",
      GST_TIME_ARGS (timestamp), priority, activeonly);

  for (; tmp; tmp = g_list_next (tmp)) {
    GnlObject *object = (GnlObject *) tmp->data;

    GST_LOG_OBJECT (object,
        "start: %" GST_TIME_FORMAT " , stop:%" GST_TIME_FORMAT " , duration:%"
        GST_TIME_FORMAT ", priority:%d", GST_TIME_ARGS (object->start),
        GST_TIME_ARGS (object->stop), GST_TIME_ARGS (object->duration),
        object->priority);

    if (object->start <= timestamp) {
      if ((object->stop > timestamp) &&
          (object->priority >= priority) &&
          ((!activeonly) || (object->active))) {
        GST_LOG_OBJECT (comp, "adding %s: sorted to the stack",
            GST_OBJECT_NAME (object));
        stack = g_list_insert_sorted (stack, object,
            (GCompareFunc) priority_comp);
      }
    } else {
      GST_LOG_OBJECT (comp, "too far, stopping iteration");
      break;
    }
  }

  /* append the default source if we have one */
  if ((timestamp < GNL_OBJECT (comp)->stop) && comp->private->defaultobject)
    stack = g_list_append (stack, comp->private->defaultobject);

  /* convert that list to a stack */
  tmp = stack;
  ret = convert_list_to_tree (&tmp, &nstop, &highest);
  if (nstop)
    *stop = nstop;
  if (highprio)
    *highprio = highest;

  g_list_free (stack);

  return ret;
}

/**
 * get_clean_toplevel_stack:
 * @comp: The #GnlComposition
 * @timestamp: The #GstClockTime to look at
 * @stop_time: Pointer to a #GstClockTime for min stop time of returned stack
 *
 * Returns: The new current stack for the given #GnlComposition and @timestamp.
 */

static GNode *
get_clean_toplevel_stack (GnlComposition * comp, GstClockTime * timestamp,
    GstClockTime * stop_time)
{
  GNode *stack = NULL;
  GList *tmp;
  GstClockTime stop = G_MAXUINT64;
  guint highprio;

  GST_DEBUG_OBJECT (comp, "timestamp:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (*timestamp));

  stack = get_stack_list (comp, *timestamp, 0, TRUE, &stop, &highprio);

  if (!stack) {
    GnlObject *object = NULL;

    /* Case for gaps, therefore no objects at specified *timestamp */

    GST_DEBUG_OBJECT (comp,
        "Got empty stack, checking if it really was after the last object");
    /* Find the first active object just after *timestamp */
    for (tmp = comp->private->objects_start; tmp; tmp = g_list_next (tmp)) {
      object = (GnlObject *) tmp->data;

      if ((object->start > *timestamp) && object->active)
        break;
    }

    if (tmp) {
      GST_DEBUG_OBJECT (comp,
          "Found a valid object after %" GST_TIME_FORMAT " : %s [%"
          GST_TIME_FORMAT "]", GST_TIME_ARGS (*timestamp),
          GST_ELEMENT_NAME (object), GST_TIME_ARGS (object->start));
      *timestamp = object->start;
      stack = get_stack_list (comp, *timestamp, 0, TRUE, &stop, &highprio);
    }
  }

  if (stack) {
    guint32 top_priority;
    
    top_priority = GNL_OBJECT (stack->data)->priority;
    
    /* Figure out if there's anything blocking us with smaller priority */
    stop =
        next_stop_in_region_above_priority (comp, *timestamp, stop,
					    (highprio == 0) ? top_priority : highprio);
  }

  if (stop_time) {
    if (stack)
      *stop_time = stop;
    else
      *stop_time = 0;
    GST_LOG_OBJECT (comp, "Setting stop_time to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (*stop_time));
  }

  return stack;
}


/*
 *
 * UTILITY FUNCTIONS
 *
 */

/**
 * get_src_pad:
 * #element: a #GstElement
 *
 * Returns: The src pad for the given element. A reference was added to the
 * returned pad, remove it when you don't need that pad anymore.
 * Returns NULL if there's no source pad.
 */

static GstPad *
get_src_pad (GstElement * element)
{
  GstIterator *it;
  GstIteratorResult itres;
  GstPad *srcpad;

  it = gst_element_iterate_src_pads (element);
  itres = gst_iterator_next (it, (gpointer) & srcpad);
  if (itres != GST_ITERATOR_OK) {
    GST_DEBUG ("%s doesn't have a src pad !", GST_ELEMENT_NAME (element));
    srcpad = NULL;
  }
  gst_iterator_free (it);
  return srcpad;
}


/*
 *
 * END OF UTILITY FUNCTIONS
 *
 */

static gboolean
gnl_composition_prepare (GnlObject * object)
{
  gboolean ret = TRUE;

  return ret;
}

static GstStateChangeReturn
gnl_composition_change_state (GstElement * element, GstStateChange transition)
{
  GnlComposition *comp = GNL_COMPOSITION (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:{
      GstIterator *childs;
      GstIteratorResult res;
      GValue val = { 0 };

      /* state-lock all elements */
      GST_DEBUG_OBJECT (comp,
          "Setting all childs to READY and locking their state");
      g_value_init (&val, G_TYPE_BOOLEAN);
      g_value_set_boolean (&val, FALSE);
      childs = gst_bin_iterate_elements (GST_BIN (comp));
      res = gst_iterator_fold (childs,
          (GstIteratorFoldFunction) lock_child_state, &val, NULL);
      gst_iterator_free (childs);
    }

      /* set ghostpad target */
      if (!(update_pipeline (comp, COMP_REAL_START (comp), TRUE, FALSE))) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto beach;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
      gnl_composition_reset (comp);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    default:
      break;
  }

beach:
  return ret;
}

static gint
objects_start_compare (GnlObject * a, GnlObject * b)
{
  if (a->start == b->start)
    return a->priority - b->priority;
  if (a->start < b->start)
    return -1;
  if (a->start > b->start)
    return 1;
  return 0;
}

static gint
objects_stop_compare (GnlObject * a, GnlObject * b)
{
  if (a->stop == b->stop)
    return a->priority - b->priority;
  if (b->stop < a->stop)
    return -1;
  if (b->stop > a->stop)
    return 1;
  return 0;
}

static void
update_start_stop_duration (GnlComposition * comp)
{
  GnlObject *obj;
  GnlObject *cobj = GNL_OBJECT (comp);

  if (!(comp->private->objects_start)) {
    GST_LOG ("no objects, resetting everything to 0");
    if (cobj->start) {
      cobj->start = 0;
      g_object_notify (G_OBJECT (cobj), "start");
    }
    if (cobj->duration) {
      cobj->duration = 0;
      g_object_notify (G_OBJECT (cobj), "duration");
    }
    if (cobj->stop) {
      cobj->stop = 0;
      g_object_notify (G_OBJECT (cobj), "stop");
    }
    return;
  }

  /* If we have a default object, the start position is 0 */
  if (comp->private->defaultobject) {
    if (cobj->start != 0) { 
      cobj->start = 0;
      g_object_notify (G_OBJECT (cobj), "start");
    }
  } else {
    /* Else it's the first object's start value */
    obj = GNL_OBJECT (comp->private->objects_start->data);
    if (obj->start != cobj->start) {
      GST_LOG_OBJECT (obj, "setting start from %s to %" GST_TIME_FORMAT,
		      GST_OBJECT_NAME (obj), GST_TIME_ARGS (obj->start));
      cobj->start = obj->start;
      g_object_notify (G_OBJECT (cobj), "start");
    }
  }

  obj = GNL_OBJECT (comp->private->objects_stop->data);
  if (obj->stop != cobj->stop) {
    GST_LOG_OBJECT (obj, "setting stop from %s to %" GST_TIME_FORMAT,
        GST_OBJECT_NAME (obj), GST_TIME_ARGS (obj->stop));
    if (comp->private->defaultobject) {
      g_object_set (comp->private->defaultobject, "duration", obj->stop, NULL);
      g_object_set (comp->private->defaultobject, "media-duration", obj->stop,
          NULL);
    }
    comp->private->segment->stop = obj->stop;
    cobj->stop = obj->stop;
    g_object_notify (G_OBJECT (cobj), "stop");
  }

  if ((cobj->stop - cobj->start) != cobj->duration) {
    cobj->duration = cobj->stop - cobj->start;
    g_object_notify (G_OBJECT (cobj), "duration");
  }

  GST_LOG_OBJECT (comp,
      "start:%" GST_TIME_FORMAT
      " stop:%" GST_TIME_FORMAT
      " duration:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (cobj->start),
      GST_TIME_ARGS (cobj->stop), GST_TIME_ARGS (cobj->duration));
}

static void
no_more_pads_object_cb (GstElement * element, GnlComposition * comp)
{
  GnlObject *object = GNL_OBJECT (element);
  GNode *tmp;
  GstPad *pad = NULL;

  GST_LOG_OBJECT (element, "no more pads");
  if (!(pad = get_src_pad (element)))
    return;

  COMP_OBJECTS_LOCK (comp);

  tmp =
      g_node_find (comp->private->current, G_IN_ORDER, G_TRAVERSE_ALL, object);

  if (tmp) {
    GnlCompositionEntry *entry = COMP_ENTRY (comp, object);

    comp->private->waitingpads--;
    GST_LOG_OBJECT (comp, "Number of waiting pads is now %d",
		    comp->private->waitingpads);

    if (tmp->parent) {
      /* child, link to parent */
      /* FIXME, shouldn't we check the order in which we link to the parent ? */
      if (!(gst_element_link (element, GST_ELEMENT (tmp->parent->data)))) {
	GST_WARNING_OBJECT (comp, "Couldn't link %s to %s",
			    GST_ELEMENT_NAME (element),
			    GST_ELEMENT_NAME (GST_ELEMENT (tmp->parent->data)));
	goto done;
      }
      gst_pad_set_blocked_async(pad, FALSE, (GstPadBlockCallback) pad_blocked, comp);
    }

    if (comp->private->current && comp->private->waitingpads == 0) {
      GstPad *tpad = get_src_pad (GST_ELEMENT (comp->private->current->data));

      /* There are no more waiting pads for the currently configured timeline */
      /* stack. */

      /* 1. set target of ghostpad to toplevel element src pad */
      gnl_composition_ghost_pad_set_target (comp, tpad);

      /* 2. send pending seek */
      if (comp->private->childseek) {
        GST_INFO_OBJECT (comp, "Sending pending seek for %s",
            GST_OBJECT_NAME (object));
        if (!(gst_pad_send_event (tpad, comp->private->childseek)))
          GST_ERROR_OBJECT (comp, "Sending seek event failed!");
      }
      comp->private->childseek = NULL;

      /* 3. unblock ghostpad */
      GST_LOG_OBJECT (comp, "About to unblock top-level pad");
      gst_pad_set_blocked_async (tpad, FALSE, (GstPadBlockCallback) pad_blocked,
          comp);
    }

    /* deactivate nomorepads handler */
    g_signal_handler_disconnect (object, entry->nomorepadshandler);
    entry->nomorepadshandler = 0;
  } else {
    GST_LOG_OBJECT (comp, "The following object is not in currently configured stack : %s",
		    GST_ELEMENT_NAME (object));
  }

 done:
  COMP_OBJECTS_UNLOCK (comp);


  GST_DEBUG_OBJECT (comp, "end");
}

/*
 * recursive depth-first relink stack function on new stack
 *
 * _ relink nodes with changed parent/order
 * _ links new nodes with parents
 * _ unblocks available source pads (except for toplevel)
 *
 * WITH OBJECTS LOCK TAKEN
 */

static void
compare_relink_single_node (GnlComposition * comp, GNode * node,
    GNode * oldstack)
{
  GNode *child;
  GNode *oldnode = NULL;
  GnlObject *newobj;
  GnlObject *newparent;
  GnlObject *oldparent = NULL;
  GstPad *srcpad;

  if (!node)
    return;

  newparent = G_NODE_IS_ROOT (node) ? NULL : (GnlObject *) node->parent->data;
  newobj = (GnlObject *) node->data;
  if (oldstack) {
    oldnode = g_node_find (oldstack, G_IN_ORDER, G_TRAVERSE_ALL, newobj);
    if (oldnode)
      oldparent =
          G_NODE_IS_ROOT (oldnode) ? NULL : (GnlObject *) oldnode->parent->data;
  }
  GST_DEBUG_OBJECT (comp, "newobj:%s",
      GST_ELEMENT_NAME ((GstElement *) newobj));

  srcpad = get_src_pad ((GstElement *) newobj);

  if (srcpad) {
    gst_pad_set_blocked_async (srcpad, TRUE, (GstPadBlockCallback) pad_blocked,
			       comp);
  }

  if (GNL_IS_OPERATION (newobj)) {
    GST_LOG_OBJECT (newobj, "is operation, analyzing the childs");
    for (child = node->children; child; child = child->next)
      compare_relink_single_node (comp, child, oldstack);
  } else {
    /* FIXME : do we need to do something specific for sources ? */
  }

  if (srcpad) {
    GST_LOG_OBJECT (newobj, "has a valid source pad");
    /* POST PROCESSING */
    if ((oldparent != newparent) ||
	(oldparent && newparent &&
	 (g_node_child_index (node, newobj) != g_node_child_index (oldnode,
								   newobj)))) {
      GST_LOG_OBJECT (newobj,
		      "not same parent, or same parent but in different order");
      
      /* relink to new parent in required order */
      if (newparent) {
	/* FIXME : do it in required order */
	if (!(gst_element_link ((GstElement *) newobj, (GstElement *) newparent)))
	  GST_ERROR_OBJECT (comp, "Couldn't link %s to %s",
			    GST_ELEMENT_NAME (newobj),
			    GST_ELEMENT_NAME (newparent));
      }
    } else
      GST_LOG_OBJECT (newobj, "Same parent and same position in the new stack");
    
    /* the new root handling is taken care of in the global compare_relink_stack() */
    if (!G_NODE_IS_ROOT (node))
      gst_pad_set_blocked_async (srcpad, FALSE, (GstPadBlockCallback) pad_blocked,
				 comp);
  } else {
    GnlCompositionEntry *entry = COMP_ENTRY (comp, newobj);

    GST_LOG_OBJECT (newobj, "no existing pad, connecting to 'no-more-pads'");
    comp->private->waitingpads++;
    if (!(entry->nomorepadshandler))
      entry->nomorepadshandler = g_signal_connect
          (G_OBJECT (newobj), "no-more-pads",
          G_CALLBACK (no_more_pads_object_cb), comp);
  }

  GST_LOG_OBJECT (newobj, "DONE");
}

/*
 * recursive depth-first compare stack function on old stack
 *
 * _ Add no-longer used objects to the deactivate list
 * _ unlink child-parent relations that have changed (not same parent, or not same order)
 * _ blocks available source pads
 *
 * WITH OBJECTS LOCK TAKEN
 */

static GList *
compare_deactivate_single_node (GnlComposition * comp, GNode * node,
    GNode * newstack)
{
  GNode *child;
  GNode *newnode = NULL;        /* Same node in newstack */
  GnlObject *oldparent;
  GList *deactivate = NULL;
  GnlObject *oldobj = NULL;
  GstPad *srcpad = NULL;

  if (!node)
    return NULL;

  oldparent = G_NODE_IS_ROOT (node) ? NULL : (GnlObject *) node->parent->data;
  oldobj = (GnlObject *) node->data;
  if (newstack)
    newnode = g_node_find (newstack, G_IN_ORDER, G_TRAVERSE_ALL, oldobj);

  GST_DEBUG_OBJECT (comp, "oldobj:%s",
      GST_ELEMENT_NAME ((GstElement *) oldobj));

  if ((!oldparent) && comp->private->ghostpad) {
    /* previous root of the tree, remove the target of the ghostpad */
    GST_DEBUG_OBJECT (comp, "Setting ghostpad target to NULL so oldobj srcpad is no longer linked");
    gnl_composition_ghost_pad_set_target (comp, NULL);
  }

  srcpad = get_src_pad ((GstElement *) oldobj);

  /* PRE PROCESSING */
  if (srcpad) {
    gst_pad_set_blocked_async (srcpad, TRUE, (GstPadBlockCallback) pad_blocked,
        comp);
  }

  /* Optionnal OPERATION PROCESSING */
  if (GNL_IS_OPERATION (oldobj)) {
    for (child = node->children; child; child = child->next) {
      GList * newdeac = compare_deactivate_single_node (comp, child, newstack);

      if (newdeac)
	deactivate = g_list_concat (deactivate, newdeac);
    }
  } else {
    /* FIXME : do we need to do something specific for sources ? */
  }

  /* POST PROCESSING */
  if (newnode) {
    GnlObject *newparent =
        G_NODE_IS_ROOT (newnode) ? NULL : (GnlObject *) newnode->parent->data;

    GST_LOG_OBJECT (oldobj, "exists in new stack");

    if ((oldparent != newparent) ||
        (oldparent && newparent &&
            (g_node_child_index (node, oldobj) != g_node_child_index (newnode,
                    oldobj)))) {
      GST_LOG_OBJECT (comp,
          "not same parent, or same parent but in different order");

      /* unlink */
      if (oldparent) {
	GstPad * peerpad = NULL;

	if (srcpad)
	  peerpad = gst_pad_get_peer (srcpad);
        gst_element_unlink ((GstElement *) oldobj, (GstElement *) oldparent);
	/* send flush start / flush stop */
	if (peerpad) {
	  GST_LOG_OBJECT (peerpad, "Sending flush start/stop");
	  gst_pad_send_event (peerpad, gst_event_new_flush_start());
	  gst_pad_send_event (peerpad, gst_event_new_flush_stop());
	  gst_object_unref (peerpad);
	}
      }

    } else {
      GST_LOG_OBJECT (comp, "same parent, same order");
    }

  } else {
    /* no longer used in new stack */
    GST_LOG_OBJECT (comp, "%s not used anymore",
		    GST_ELEMENT_NAME (oldobj));

    if (oldparent) {
      GstPad * peerpad = NULL;

      /* unlink from oldparent */
      GST_LOG_OBJECT (comp, "unlinking from previous parent");
      gst_element_unlink ((GstElement *) oldobj, (GstElement *) oldparent);
      if (srcpad && (peerpad = gst_pad_get_peer (srcpad))) {
	GST_LOG_OBJECT (peerpad, "Sending flush start/stop");
	gst_pad_send_event (peerpad, gst_event_new_flush_start());
	gst_pad_send_event (peerpad, gst_event_new_flush_stop());
	gst_object_unref (peerpad);
      }
    }

    GST_LOG_OBJECT (comp, "adding %s to deactivate list",
		    GST_ELEMENT_NAME (oldobj));
    deactivate = g_list_append (deactivate, oldobj);
  }
  /* only unblock if it's not the ROOT */

  if (srcpad)
    gst_object_unref (srcpad);

  return deactivate;
}

/**
 * compare_relink_stack:
 * @comp: The #GnlComposition
 * @stack: The new stack
 *
 * Compares the given stack to the current one and relinks it if needed.
 *
 * WITH OBJECTS LOCK TAKEN
 *
 * Returns: The #GList of #GnlObject no longer used
 */

static GList *
compare_relink_stack (GnlComposition * comp, GNode * stack)
{
  GList *deactivate = NULL;

  /* 1. reset waiting pads for new stack */
  comp->private->waitingpads = 0;
  
  /* 2. Traverse old stack to deactivate no longer used objects */

  deactivate =
      compare_deactivate_single_node (comp, comp->private->current, stack);

  /* 3. Traverse new stack to do needed (re)links */

  compare_relink_single_node (comp, stack, comp->private->current);

  return deactivate;
}

static void
unlock_activate_stack (GnlComposition * comp, GNode * node,
    gboolean change_state, GstState state)
{
  GNode *child;

  GST_LOG_OBJECT (comp, "object:%s",
      GST_ELEMENT_NAME ((GstElement *) (node->data)));

  gst_element_set_locked_state ((GstElement *) (node->data), FALSE);
  if (change_state)
    gst_element_set_state (GST_ELEMENT (node->data), state);
  for (child = node->children; child; child = child->next)
    unlock_activate_stack (comp, child, change_state, state);
}

/**
 * update_pipeline:
 * @comp: The #GnlComposition
 * @currenttime: The #GstClockTime to update at, can be GST_CLOCK_TIME_NONE.
 * @initial: TRUE if this is the first setup
 * @change_state: Change the state of the (de)activated objects if TRUE.
 *
 * Updates the internal pipeline and properties. If @currenttime is 
 * GST_CLOCK_TIME_NONE, it will not modify the current pipeline
 *
 * Returns: FALSE if there was an error updating the pipeline.
 */

static gboolean
update_pipeline (GnlComposition * comp, GstClockTime currenttime,
    gboolean initial, gboolean change_state)
{
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (comp, "currenttime:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (currenttime));

  COMP_OBJECTS_LOCK (comp);

  update_start_stop_duration (comp);

  if ((GST_CLOCK_TIME_IS_VALID (currenttime))) {
    GstState state = GST_STATE (comp);
    GstState nextstate =
        (GST_STATE_NEXT (comp) ==
        GST_STATE_VOID_PENDING) ? GST_STATE (comp) : GST_STATE_NEXT (comp);
    GNode *stack = NULL;
    GList *deactivate;
    GstClockTime new_stop;
    gboolean switchtarget = FALSE;

    GST_DEBUG_OBJECT (comp,
        "now really updating the pipeline, current-state:%s",
        gst_element_state_get_name (state));

    /* (re)build the stack and relink new elements */
    stack = get_clean_toplevel_stack (comp, &currenttime, &new_stop);
    deactivate = compare_relink_stack (comp, stack);

    /* set new segment_start/stop */
    comp->private->segment_start = currenttime;
    comp->private->segment_stop = new_stop;

    /* Clear pending child seek */
    if (comp->private->childseek) {
      gst_event_unref (comp->private->childseek);
      comp->private->childseek = NULL;
    }

    COMP_OBJECTS_UNLOCK (comp);

    if (deactivate) {
      GList * tmp;
      GST_DEBUG_OBJECT (comp, "De-activating objects no longer used");

      /* state-lock elements no more used */
      for (tmp = deactivate;tmp; tmp = g_list_next(tmp)) {
	GST_LOG ("%p", tmp->data);

        if (change_state)
          gst_element_set_state (GST_ELEMENT (tmp->data), state);
        gst_element_set_locked_state (GST_ELEMENT (tmp->data), TRUE);
      }
      g_list_free(deactivate);

      GST_DEBUG_OBJECT (comp, "Finished de-activating objects no longer used");
    }

    /* if toplevel element has changed, redirect ghostpad to it */
    if ((stack) && ((!(comp->private->current))
            || (comp->private->current->data != stack->data)))
      switchtarget = TRUE;

    GST_DEBUG_OBJECT (comp, "activating objects in new stack to %s",
        gst_element_state_get_name (nextstate));

    /* activate new stack */
    if (comp->private->current)
      g_node_destroy (comp->private->current);
    comp->private->current = stack;

    if (stack)
      unlock_activate_stack (comp, stack, change_state, nextstate);
    GST_DEBUG_OBJECT (comp, "Finished activating objects in new stack");

    if (comp->private->current) {
      GstEvent *event;

      /* There is a valid timeline stack */

      COMP_OBJECTS_LOCK (comp);

      /* 1. Create new seek event for newly configured timeline stack */
      event = get_new_seek_event (comp, initial);

      /* 2. Is the stack entirely ready ? */
      if (comp->private->waitingpads == 0) {
	GstPad *pad = NULL;
	/* 2.a. Stack is entirely ready */

	/* 3. Get toplevel object source pad */
	if ((pad = get_src_pad (GST_ELEMENT (comp->private->current->data)))) {
	  
	  GST_DEBUG_OBJECT (comp, "We have a valid toplevel element pad %s:%s",
			    GST_DEBUG_PAD_NAME (pad));

	  /* 4. Unconditionnaly set the ghostpad target to pad */
	  GST_LOG_OBJECT (comp,
			  "Setting the composition's ghostpad target to %s:%s",
			  GST_DEBUG_PAD_NAME (pad));
	  gnl_composition_ghost_pad_set_target (comp, pad);
	  
	  /* 5. send seek event */
	  GST_LOG_OBJECT (comp, "sending seek event");
	  if (!(gst_pad_send_event (pad, event))) {
	    ret = FALSE;
	  } else {
	    /* 6. unblock top-level pad */
	    GST_LOG_OBJECT (comp, "About to unblock top-level srcpad");
	    gst_pad_set_blocked_async (pad, FALSE,
				       (GstPadBlockCallback) pad_blocked, comp);
	    gst_object_unref (pad);
	  }
	  
	} else {
	  GST_WARNING_OBJECT (comp,
			      "Timeline is entirely linked, but couldn't get top-level element's source pad");
	  ret = FALSE;
	}
      } else {
	/* 2.b. Stack isn't entirely ready, save seek event for later on */
	GST_LOG_OBJECT (comp, "The timeline stack isn't entirely linked, delaying sending seek event");
	comp->private->childseek = event;
	ret = TRUE;
      }
      COMP_OBJECTS_UNLOCK (comp);

    } else {
      if ((!comp->private->objects_start) && comp->private->ghostpad) {
	GST_DEBUG_OBJECT (comp, "composition is now empty, removing ghostpad");
	gnl_object_remove_ghost_pad (GNL_OBJECT (comp), comp->private->ghostpad);
	comp->private->ghostpad = NULL;
      }
    }
  } else {
    COMP_OBJECTS_UNLOCK (comp);
  }

  GST_DEBUG_OBJECT (comp, "Returning %d", ret);
  return ret;
}

static void
object_start_changed (GnlObject * object, GParamSpec * arg,
    GnlComposition * comp)
{
  comp->private->objects_start = g_list_sort
      (comp->private->objects_start, (GCompareFunc) objects_start_compare);

  comp->private->objects_stop = g_list_sort
      (comp->private->objects_stop, (GCompareFunc) objects_stop_compare);

  if (comp->private->current && OBJECT_IN_ACTIVE_SEGMENT (comp, object))
    update_pipeline (comp, comp->private->segment_start, TRUE, TRUE);
  else
    update_start_stop_duration (comp);
}

static void
object_stop_changed (GnlObject * object, GParamSpec * arg,
    GnlComposition * comp)
{
  comp->private->objects_stop = g_list_sort
      (comp->private->objects_stop, (GCompareFunc) objects_stop_compare);

  comp->private->objects_start = g_list_sort
      (comp->private->objects_start, (GCompareFunc) objects_start_compare);

  if (comp->private->current && OBJECT_IN_ACTIVE_SEGMENT (comp, object))
    update_pipeline (comp, comp->private->segment_start, TRUE, TRUE);
  else
    update_start_stop_duration (comp);
}

static void
object_priority_changed (GnlObject * object, GParamSpec * arg,
    GnlComposition * comp)
{
  GST_DEBUG_OBJECT (object, "...");

  comp->private->objects_start = g_list_sort
      (comp->private->objects_start, (GCompareFunc) objects_start_compare);

  comp->private->objects_stop = g_list_sort
      (comp->private->objects_stop, (GCompareFunc) objects_stop_compare);

  if (comp->private->current && OBJECT_IN_ACTIVE_SEGMENT (comp, object))
    update_pipeline (comp, comp->private->segment_start, TRUE, TRUE);
  else
    update_start_stop_duration (comp);
}

static void
object_active_changed (GnlObject * object, GParamSpec * arg,
    GnlComposition * comp)
{
  GST_DEBUG_OBJECT (object, "...");

  update_start_stop_duration (comp);

  if (comp->private->current && OBJECT_IN_ACTIVE_SEGMENT (comp, object))
    update_pipeline (comp, comp->private->segment_start, TRUE, TRUE);
  else
    update_start_stop_duration (comp);
}

static void
object_pad_removed (GnlObject * object, GstPad * pad, GnlComposition * comp)
{
  GST_DEBUG_OBJECT (comp, "pad %s:%s was removed", GST_DEBUG_PAD_NAME (pad));
  /* remove ghostpad if it's the current top stack object */
  if (comp->private->current
      && ((GnlObject *) comp->private->current->data == object)
      && comp->private->ghostpad) {
    GST_DEBUG_OBJECT (comp, "Removing ghostpad");
    gnl_object_remove_ghost_pad (GNL_OBJECT (comp), comp->private->ghostpad);
    comp->private->ghostpad = NULL;
  }
}

static void
object_pad_added (GnlObject * object, GstPad * pad, GnlComposition * comp)
{
  GST_DEBUG_OBJECT (comp, "pad %s:%s was added, blocking it",
      GST_DEBUG_PAD_NAME (pad));

  gst_pad_set_blocked_async (pad, TRUE, (GstPadBlockCallback) pad_blocked,
      comp);
}

static gboolean
gnl_composition_add_object (GstBin * bin, GstElement * element)
{
  gboolean ret;
  GnlCompositionEntry *entry;
  GnlComposition *comp = GNL_COMPOSITION (bin);

  GST_DEBUG_OBJECT (bin, "element %s", GST_OBJECT_NAME (element));

  /* we only accept GnlObject */
  g_return_val_if_fail (GNL_IS_OBJECT (element), FALSE);

  COMP_OBJECTS_LOCK (comp);

  GST_DEBUG_OBJECT (element, "%" GST_TIME_FORMAT "--%" GST_TIME_FORMAT,
      GST_TIME_ARGS (GNL_OBJECT (element)->start),
      GST_TIME_ARGS (GNL_OBJECT (element)->stop));

  gst_object_ref (element);

  if ((GNL_OBJECT (element)->priority == G_MAXUINT32)
      && comp->private->defaultobject) {
    GST_WARNING_OBJECT (comp,
        "We already have a default source, remove it before adding new one");
    ret = FALSE;
    goto chiringuito;
  }

  ret = GST_BIN_CLASS (parent_class)->add_element (bin, element);
  if (!ret)
    goto chiringuito;

  /* wrap it and add it to the hash table */
  entry = g_new0 (GnlCompositionEntry, 1);
  entry->object = GNL_OBJECT (element);
  if ((GNL_OBJECT (element)->priority != G_MAXUINT32)) {
    /* Only react on non-default objects properties */
    entry->starthandler = g_signal_connect (G_OBJECT (element),
        "notify::start", G_CALLBACK (object_start_changed), comp);
    entry->stophandler = g_signal_connect (G_OBJECT (element),
        "notify::stop", G_CALLBACK (object_stop_changed), comp);
    entry->priorityhandler = g_signal_connect (G_OBJECT (element),
        "notify::priority", G_CALLBACK (object_priority_changed), comp);
  } else {
    /* We set the default source start/stop values to 0 and composition-stop */
    g_object_set (element, 
		  "start", (GstClockTime)0,
		  "media-start", (GstClockTime) 0,
		  "duration", (GstClockTimeDiff) GNL_OBJECT (comp)->stop,
		  "media-duration", (GstClockTimeDiff) GNL_OBJECT (comp)->stop,
		  NULL);
  }
  entry->activehandler = g_signal_connect (G_OBJECT (element),
      "notify::active", G_CALLBACK (object_active_changed), comp);
  entry->padremovedhandler = g_signal_connect (G_OBJECT (element),
      "pad-removed", G_CALLBACK (object_pad_removed), comp);
  entry->padaddedhandler = g_signal_connect (G_OBJECT (element),
      "pad-added", G_CALLBACK (object_pad_added), comp);
  g_hash_table_insert (comp->private->objects_hash, element, entry);

  /* Special case for default source */
  if (GNL_OBJECT (element)->priority == G_MAXUINT32) {
    comp->private->defaultobject = GNL_OBJECT (element);
    goto chiringuito;
  }

  /* add it sorted to the objects list */
  comp->private->objects_start = g_list_append
      (comp->private->objects_start, element);
  comp->private->objects_start = g_list_sort
      (comp->private->objects_start, (GCompareFunc) objects_start_compare);

  if (comp->private->objects_start)
    GST_LOG_OBJECT (comp,
        "Head of objects_start is now %s [%" GST_TIME_FORMAT "--%"
        GST_TIME_FORMAT "]",
        GST_OBJECT_NAME (comp->private->objects_start->data),
        GST_TIME_ARGS (GNL_OBJECT (comp->private->objects_start->data)->start),
        GST_TIME_ARGS (GNL_OBJECT (comp->private->objects_start->data)->stop));

  comp->private->objects_stop = g_list_append
      (comp->private->objects_stop, element);
  comp->private->objects_stop = g_list_sort
      (comp->private->objects_stop, (GCompareFunc) objects_stop_compare);

  if (comp->private->objects_stop)
    GST_LOG_OBJECT (comp,
        "Head of objects_stop is now %s [%" GST_TIME_FORMAT "--%"
        GST_TIME_FORMAT "]",
        GST_OBJECT_NAME (comp->private->objects_stop->data),
        GST_TIME_ARGS (GNL_OBJECT (comp->private->objects_stop->data)->start),
        GST_TIME_ARGS (GNL_OBJECT (comp->private->objects_stop->data)->stop));

  GST_DEBUG_OBJECT (comp,
      "segment_start:%" GST_TIME_FORMAT " segment_stop:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (comp->private->segment_start),
      GST_TIME_ARGS (comp->private->segment_stop));

  COMP_OBJECTS_UNLOCK (comp);

  /* If we added within currently configured segment OR the pipeline was *
   * previously empty, THEN update pipeline */
  if (OBJECT_IN_ACTIVE_SEGMENT (comp, element) || (!comp->private->current))
    update_pipeline (comp, comp->private->segment_start, TRUE, TRUE);
  else
    update_start_stop_duration (comp);

beach:
  gst_object_unref (element);
  return ret;

chiringuito:
  COMP_OBJECTS_UNLOCK (comp);
  update_start_stop_duration (comp);
  goto beach;
}


static gboolean
gnl_composition_remove_object (GstBin * bin, GstElement * element)
{
  gboolean ret = GST_STATE_CHANGE_FAILURE;
  GnlComposition *comp = GNL_COMPOSITION (bin);

  GST_DEBUG_OBJECT (bin, "element %s", GST_OBJECT_NAME (element));
  /* we only accept GnlObject */
  g_return_val_if_fail (GNL_IS_OBJECT (element), FALSE);

  COMP_OBJECTS_LOCK (comp);

  gst_object_ref (element);

  gst_element_set_locked_state (element, FALSE);

  /* handle default source */
  if (GNL_OBJECT (element)->priority == G_MAXUINT32) {
    comp->private->defaultobject = NULL;
  } else {

    /* remove it from the objects list and resort the lists */
    comp->private->objects_start = g_list_remove
        (comp->private->objects_start, element);
    comp->private->objects_start = g_list_sort
        (comp->private->objects_start, (GCompareFunc) objects_start_compare);

    comp->private->objects_stop = g_list_remove
        (comp->private->objects_stop, element);
    comp->private->objects_stop = g_list_sort
        (comp->private->objects_stop, (GCompareFunc) objects_stop_compare);

    GST_LOG_OBJECT (element, "Removed from the objects start/stop list");
  }

  if (!(g_hash_table_remove (comp->private->objects_hash, element)))
    goto chiringuito;

  COMP_OBJECTS_UNLOCK (comp);

  /* If we removed within currently configured segment, or it was the default source, *
   * update pipeline */
  if (OBJECT_IN_ACTIVE_SEGMENT (comp, element)
      || (GNL_OBJECT (element)->priority == G_MAXUINT32))
    update_pipeline (comp, comp->private->segment_start, TRUE, TRUE);
  else
    update_start_stop_duration (comp);

  ret = GST_BIN_CLASS (parent_class)->remove_element (bin, element);
  
  GST_LOG_OBJECT (element, "Done removing from the composition");

beach:
  gst_object_unref (element);
  return ret;

chiringuito:
  COMP_OBJECTS_UNLOCK (comp);
  goto beach;
}
