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

  GMutex *flushing_lock;
  gboolean flushing;
  guint pending_idle;

  /* source ghostpad */
  GstPad *ghostpad;

  /* current stack, list of GnlObject* */
  GList *current;

  GnlObject *defaultobject;

  /*
     current segment seek start/stop time. 
     Reconstruct pipeline ONLY if seeking outside of those values
   */
  GstClockTime segment_start;
  GstClockTime segment_stop;

  /* pending child seek */
  GstEvent *childseek;

  /* Seek segment handler */
  GstSegment *segment;

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
    g_list_free (comp->private->current);
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
  g_list_free (comp->private->current);
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
    g_list_free (comp->private->current);
  comp->private->current = NULL;

  if (comp->private->ghostpad) {
    gnl_object_remove_ghost_pad (GNL_OBJECT (comp), comp->private->ghostpad);
    comp->private->ghostpad = NULL;
  }

  if (comp->private->childseek) {
    gst_event_unref (comp->private->childseek);
    comp->private->childseek = NULL;
  }

  unlock_childs(comp);

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
  GST_DEBUG_OBJECT (comp, "Setting segment->start to segment_stop:%"GST_TIME_FORMAT,
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
	epos =
	  (MIN (comp->private->segment->stop, GNL_OBJECT (comp)->stop));
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
		    GST_MESSAGE_SRC (message) ? GST_ELEMENT_NAME (GST_MESSAGE_SRC (message)) : "UNKNOWN");

  switch (GST_MESSAGE_TYPE (message)) {
  case GST_MESSAGE_SEGMENT_START: {
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
	GST_WARNING_OBJECT (comp, "There was already a pending segment_done in main thread !");
	g_source_remove (comp->private->pending_idle);
      }
      comp->private->pending_idle = g_idle_add ((GSourceFunc) segment_done_main_thread, (gpointer)comp);

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
    flags = GST_SEEK_FLAG_SEGMENT | GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH;

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
  Figures out if pipeline needs updating. Updates it and sends the seek event.
  can be called by user_seek or segment_done
*/

static gboolean
seek_handling (GnlComposition * comp, gboolean initial, gboolean update)
{
  GST_DEBUG_OBJECT (comp, "initial:%d, update:%d", initial, update);

  COMP_FLUSHING_LOCK (comp);
  GST_DEBUG_OBJECT (comp, "Setting flushing to TRUE");
  comp->private->flushing = TRUE;
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
      handle_seek_event (comp, event);
      break;
    }
    default:
      break;
  }

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

  GST_DEBUG_OBJECT (comp, "%s:%s , hadghost:%d",
      GST_DEBUG_PAD_NAME (target), hadghost);

  if (!(hadghost)) {
    comp->private->ghostpad = gnl_object_ghost_pad_no_target (GNL_OBJECT (comp),
        "src", GST_PAD_SRC);
  } else {
    GstPad *ptarget =
        gst_ghost_pad_get_target (GST_GHOST_PAD (comp->private->ghostpad));

    if (ptarget == target) {
      GST_DEBUG_OBJECT (comp,
          "Target of ghostpad is the same as existing one, not changing");
      gst_object_unref (ptarget);
      return;
    }

    GST_DEBUG_OBJECT (comp, "Previous target was %s:%s, blocking that pad",
        GST_DEBUG_PAD_NAME (ptarget));
    gst_pad_set_blocked_async (ptarget, TRUE, (GstPadBlockCallback) pad_blocked, comp);

    gst_object_unref (ptarget);
  }

  gnl_object_ghost_pad_set_target (GNL_OBJECT (comp),
      comp->private->ghostpad, target);
  if (!(hadghost)) {
    GST_DEBUG_OBJECT (comp->private->ghostpad, "About to replace event_pad_func");
    comp->private->gnl_event_pad_func =
      GST_PAD_EVENTFUNC (comp->private->ghostpad);
    gst_pad_set_event_function (comp->private->ghostpad,
				GST_DEBUG_FUNCPTR (gnl_composition_event_handler));
    GST_DEBUG_OBJECT (comp->private->ghostpad, "eventfunc is now %s",
      GST_DEBUG_FUNCPTR_NAME (GST_PAD_EVENTFUNC (comp->private->ghostpad)));
    if (!(gst_element_add_pad (GST_ELEMENT (comp), comp->private->ghostpad)))
      GST_WARNING ("Couldn't add the ghostpad");
    else
      gst_element_no_more_pads (GST_ELEMENT (comp));
  }
  GST_DEBUG_OBJECT (comp, "END");
}

static GstClockTime
next_stop_in_region_above_priority (GnlComposition * composition,
				    GstClockTime start,
				    GstClockTime stop, guint priority)
{
  GList * tmp = composition->private->objects_start;
  GnlObject * object;
  GstClockTime res = stop;

  GST_DEBUG_OBJECT (composition, "start: %" GST_TIME_FORMAT " stop: %"
		    GST_TIME_FORMAT " priority:%d",
		    GST_TIME_ARGS (start),
		    GST_TIME_ARGS (stop),
		    priority);

  /* Find first object with priority < priority, and start < start_position <= stop */
  for (; tmp ; tmp = g_list_next (tmp)) {
    object = tmp->data;

    if (object->priority >= priority)
      continue;

    if (object->stop <= start)
      continue;

    if (object->start > stop)
      break;
    
    res = object->start;
    GST_DEBUG_OBJECT (composition, "Found %s [prio:%d] at %" GST_TIME_FORMAT,
		      GST_OBJECT_NAME (object),
		      object->priority,
		      GST_TIME_ARGS (object->start));
    break;
  }
  return res;
}

/**
 * get_stack_list:
 * @comp: The #GnlComposition
 * @timestamp: The #GstClockTime to look at
 * @priority: The priority level to start looking from
 * @activeonly: Only look for active elements if TRUE
 *
 * Not MT-safe, you should take the objects lock before calling it.
 * Returns: A #GList of #GnlObject sorted in priority order, corresponding
 * to the given search arguments. The list can be empty.
 */

static GList *
get_stack_list (GnlComposition * comp, GstClockTime timestamp,
    guint priority, gboolean activeonly)
{
  GList *tmp = comp->private->objects_start;
  GList *stack = NULL;

  GST_DEBUG_OBJECT (comp,
      "timestamp:%" GST_TIME_FORMAT ", priority:%d, activeonly:%d",
      GST_TIME_ARGS (timestamp), priority, activeonly);

  /*
     Iterate the list with a stack:
     Either :
     _ ignore
     _ add to the stack (object.start <= timestamp < object.stop)
     _ add in priority order
     _ stop iteration (object.start > timestamp)
   */

  for (; tmp; tmp = g_list_next (tmp)) {
    GnlObject *object = GNL_OBJECT (tmp->data);

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

  if ((timestamp < GNL_OBJECT(comp)->stop) && comp->private->defaultobject)
    stack = g_list_append(stack, comp->private->defaultobject);

  return stack;
}

/**
 * get_clean_toplevel_stack:
 * @comp: The #GnlComposition
 * @timestamp: The #GstClockTime to look at
 * @stop_time: Pointer to a #GstClockTime for min stop time of returned stack
 *
 * Returns: The new current stack for the given #GnlComposition
 */

static GList *
get_clean_toplevel_stack (GnlComposition * comp, GstClockTime * timestamp,
    GstClockTime * stop_time)
{
  GList *stack, *tmp;
  GList *ret = NULL;
  gint size = 1;
  GstClockTime stop = G_MAXUINT64;
  guint top_priority = -1;

  GST_DEBUG_OBJECT (comp, "timestamp:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (*timestamp));

  stack = get_stack_list (comp, *timestamp, 0, TRUE);

  /* Case for gaps, therefore no objects at specified *timestamp */
  if (!stack) {
    GnlObject *object = NULL;

    GST_DEBUG_OBJECT (comp, "Got empty stack, checking if it really was after the last object");
    /* Find the first active object just after *timestamp */    
    for (tmp = comp->private->objects_start; tmp; tmp = g_list_next (tmp)) {
      object = (GnlObject *) tmp->data;

      if ((object->start > *timestamp) && object->active)
	break;
    }
    
    if (tmp) {
      GST_DEBUG_OBJECT (comp, "Found a valid object after %"GST_TIME_FORMAT" : %s [%"GST_TIME_FORMAT"]",
			GST_TIME_ARGS (*timestamp), GST_ELEMENT_NAME (object),
			GST_TIME_ARGS (object->start));
      *timestamp = object->start;
      stack = get_stack_list (comp, *timestamp, 0, TRUE);
    }
  }

  for (tmp = stack; tmp && size; tmp = g_list_next (tmp), size--) {
    GnlObject *object = tmp->data;

    if (top_priority == -1)
      top_priority = object->priority;
    ret = g_list_append (ret, object);
    if (stop > object->stop)
      stop = object->stop;
    /* FIXME : ADD CASE FOR OPERATION */
    /* size += number of input for operation */
  }

  /* Figure out if there's anything blocking us with smaller priority */
  stop = next_stop_in_region_above_priority (comp, *timestamp, stop, top_priority);
  
  g_list_free (stack);

  if (stop_time) {
    if (ret)
      *stop_time = stop;
    else
      *stop_time = 0;
    GST_DEBUG_OBJECT (comp, "Setting stop_time to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (*stop_time));
  }

  return ret;
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
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      /* state-lock elements not used */
/*     update_pipeline (comp, COMP_REAL_START (comp)); */
      break;
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

  obj = GNL_OBJECT (comp->private->objects_start->data);
  if (obj->start != cobj->start) {
    GST_LOG_OBJECT (obj, "setting start from %s to %" GST_TIME_FORMAT,
        GST_OBJECT_NAME (obj), GST_TIME_ARGS (obj->start));
    cobj->start = obj->start;
    g_object_notify (G_OBJECT (cobj), "start");
  }

  obj = GNL_OBJECT (comp->private->objects_stop->data);
  if (obj->stop != cobj->stop) {
    GST_LOG_OBJECT (obj, "setting stop from %s to %" GST_TIME_FORMAT,
        GST_OBJECT_NAME (obj), GST_TIME_ARGS (obj->stop));
    if (comp->private->defaultobject) {
      g_object_set (comp->private->defaultobject, "duration", obj->stop, NULL);
      g_object_set (comp->private->defaultobject, "media-duration", obj->stop, NULL);
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
  GList *tmp, *prev;
  GstPad *pad = NULL;

  GST_LOG_OBJECT (element, "no more pads");
  if (!(pad = get_src_pad (element)))
    return;

  /* FIXME : ADD CASE FOR OPERATIONS */

  COMP_OBJECTS_LOCK (comp);

  for (tmp = comp->private->current, prev = NULL;
      tmp; prev = tmp, tmp = g_list_next (tmp)) {
    GnlObject *curobj = GNL_OBJECT (tmp->data);

    if (curobj == object) {
      GnlCompositionEntry *entry = COMP_ENTRY (comp, object);

      if (prev) {
        /* link that object to the previous */
        gst_element_link (element, GST_ELEMENT (prev->data));
        break;
      } else {
        /* toplevel element */
        gnl_composition_ghost_pad_set_target (comp, pad);
        if (comp->private->childseek) {
	  GST_INFO_OBJECT (comp, "Sending pending seek for %s",
			   GST_OBJECT_NAME (object));
          if (!(gst_pad_send_event (pad, comp->private->childseek)))
            GST_ERROR_OBJECT (comp, "Sending seek event failed!");
	}
        comp->private->childseek = NULL;
	gst_pad_set_blocked_async (pad, FALSE, (GstPadBlockCallback) pad_blocked, comp);
      }
      /* remove signal handler */
      g_signal_handler_disconnect (object, entry->nomorepadshandler);
      entry->nomorepadshandler = 0;
    }
  }

  COMP_OBJECTS_UNLOCK (comp);


  GST_DEBUG_OBJECT (comp, "end");
}

/**
 * compare_relink_stack:
 * @comp: The #GnlComposition
 * @stack: The new stack
 *
 * Compares the given stack to the current one and relinks it if needed
 *
 * Returns: The #GList of #GnlObject no longer used
 */

static GList *
compare_relink_stack (GnlComposition * comp, GList * stack)
{
  GList *deactivate = NULL;
  GnlObject *oldobj = NULL;
  GnlObject *newobj = NULL;
  GnlObject *pold = NULL;
  GnlObject *pnew = NULL;
  GList *curo = comp->private->current;
  GList *curn = stack;

  for (; curo && curn;) {
    oldobj = GNL_OBJECT (curo->data);
    newobj = GNL_OBJECT (curn->data);

    GST_DEBUG ("Comparing old:%s new:%s",
        GST_ELEMENT_NAME (oldobj), GST_ELEMENT_NAME (newobj));

    if ((oldobj == newobj) && GNL_OBJECT_IS_OPERATION (oldobj)) {
      /* guint      size = GNL_OPERATION_SINKS (oldobj); */
      /* FIXME : check objects the operation works on */
      /*
         while (size) {

         size--;
         }
       */
    } else if (oldobj != newobj) {
      GstPad *srcpad;
      GnlCompositionEntry *entry;

      deactivate = g_list_append (deactivate, oldobj);

      /* unlink from it's previous source */
      if (pold) {
        /* unlink old object from previous old obj */
        gst_element_unlink (GST_ELEMENT (pold), GST_ELEMENT (oldobj));
      }

      /* link to it's previous source */
      if (pnew) {
        GST_DEBUG_OBJECT (comp, "linking to previous source");
        /* link new object to previous new obj */
        entry = COMP_ENTRY (comp, newobj);
        srcpad = get_src_pad (GST_ELEMENT (newobj));

        if (!(srcpad)) {
          GST_LOG ("Adding callback on 'no-more-pads' for %s",
              GST_ELEMENT_NAME (entry->object));
          /* special case for elements who don't have their srcpad yet */
          if (entry->nomorepadshandler)
            g_signal_handler_disconnect (entry->object,
                entry->nomorepadshandler);
          entry->nomorepadshandler = g_signal_connect
              (G_OBJECT (newobj),
              "no-more-pads", G_CALLBACK (no_more_pads_object_cb), comp);
        } else {
          GST_LOG ("Linking %s to %s the standard way",
              GST_ELEMENT_NAME (pnew), GST_ELEMENT_NAME (newobj));
          gst_element_link (GST_ELEMENT (pnew), GST_ELEMENT (newobj));
          gst_object_unref (srcpad);
        }
      } else {
        /* There's no previous new, which means it's the toplevel element */
        GST_DEBUG_OBJECT (comp, "top-level element");

        srcpad = get_src_pad (GST_ELEMENT (newobj));

        if (!(srcpad)) {
          entry = COMP_ENTRY (comp, newobj);
          if (entry->nomorepadshandler)
            g_signal_handler_disconnect (entry->object,
                entry->nomorepadshandler);
          entry->nomorepadshandler = g_signal_connect
              (G_OBJECT (newobj),
              "no-more-pads", G_CALLBACK (no_more_pads_object_cb), comp);

        } else
          gst_object_unref (srcpad);
      }
    }

    curo = g_list_next (curo);
    curn = g_list_next (curn);
    pold = oldobj;
    pnew = newobj;
  }

  GST_DEBUG_OBJECT (comp, "curo:%p, curn:%p", curo, curn);

  for (; curn; curn = g_list_next (curn)) {
    newobj = GNL_OBJECT (curn->data);

    if (pnew)
      gst_element_link (GST_ELEMENT (pnew), GST_ELEMENT (newobj));
    else {
      GstPad *srcpad = get_src_pad (GST_ELEMENT (newobj));
      GnlCompositionEntry *entry;

      if (srcpad) {
        gnl_composition_ghost_pad_set_target (comp, srcpad);
        gst_object_unref (srcpad);
      } else {
        entry = COMP_ENTRY (comp, newobj);
        if (entry->nomorepadshandler)
          g_signal_handler_disconnect (entry->object, entry->nomorepadshandler);
        entry->nomorepadshandler = g_signal_connect
            (G_OBJECT (newobj),
            "no-more-pads", G_CALLBACK (no_more_pads_object_cb), comp);
      }
    }

    pnew = newobj;
  }

  for (; curo; curo = g_list_next (curo)) {
    oldobj = GNL_OBJECT (curo->data);

    deactivate = g_list_append (deactivate, oldobj);
    if (pold)
      gst_element_unlink (GST_ELEMENT (pold), GST_ELEMENT (oldobj));

    pold = oldobj;
  }

  /* 
     we need to remove from the deactivate lists, item which have changed priority
     but are still in the new stack.
     FIXME: This is damn ugly !
   */
  for (curo = comp->private->current; curo; curo = g_list_next (curo))
    for (curn = stack; curn; curn = g_list_next (curn))
      if (curo->data == curn->data)
        deactivate = g_list_remove (deactivate, curo->data);

  return deactivate;
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
    GList *stack = NULL;
    GList *deactivate;
    GstPad *pad = NULL;
    GstClockTime new_stop;
    gboolean switchtarget = FALSE;

    GST_DEBUG_OBJECT (comp,
        "now really updating the pipeline, current-state:%s",
        gst_element_state_get_name (state));

    /* rebuild the stack and relink new elements */
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
      GST_DEBUG_OBJECT (comp, "De-activating objects no longer used");

      /* state-lock elements no more used */
      while (deactivate) {
        if (change_state)
          gst_element_set_state (GST_ELEMENT (deactivate->data), state);
        gst_element_set_locked_state (GST_ELEMENT (deactivate->data), TRUE);
        deactivate = g_list_delete_link (deactivate, deactivate);
      }

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
      g_list_free (comp->private->current);
    comp->private->current = stack;

    while (stack) {
      gst_element_set_locked_state (GST_ELEMENT (stack->data), FALSE);
      if (change_state)
        gst_element_set_state (GST_ELEMENT (stack->data), nextstate);
      stack = g_list_next (stack);
    }

    GST_DEBUG_OBJECT (comp, "Finished activating objects in new stack");

    if (comp->private->current) {
      GstEvent *event;

      COMP_OBJECTS_LOCK (comp);

      event = get_new_seek_event (comp, initial);
      
      pad = get_src_pad (GST_ELEMENT (comp->private->current->data));

      if (pad) {
	GST_DEBUG_OBJECT (comp, "We have a valid toplevel element pad %s:%s",
			  GST_DEBUG_PAD_NAME (pad));

	if (switchtarget) {
	  GST_DEBUG_OBJECT (comp, "Top stack object has changed, switching pad");
	  if (pad) {
 	    GST_LOG_OBJECT (comp, "sending seek event");
 	    if (!(gst_pad_send_event (pad, event)))
 	      ret = FALSE;
 	    else {
 	      GST_LOG_OBJECT (comp, "seek event sent successfully to %s:%s",
 			      GST_DEBUG_PAD_NAME (pad));
 	      GST_LOG_OBJECT (comp, "Setting the composition's ghostpad target to %s:%s",
 			      GST_DEBUG_PAD_NAME (pad));
 	      gnl_composition_ghost_pad_set_target (comp, pad);
 	    }
	    
	  } else {
	    GST_LOG_OBJECT (comp, "No srcpad was available on stack's toplevel element");
	    /* The pad might be created dynamically */
	  }
	} else {
	  GST_DEBUG_OBJECT (comp,
			    "Top stack object is still the same, keeping existing pad");
	}

        if (!switchtarget) {
	  if (!(gst_pad_send_event (pad, event))) {
	    GST_ERROR_OBJECT (comp, "Couldn't send seek");
	    ret = FALSE;
	  }
	GST_LOG_OBJECT (comp, "seek event sent successfully to %s:%s",
			GST_DEBUG_PAD_NAME (pad));
	}
	gst_pad_set_blocked_async (pad, FALSE, (GstPadBlockCallback) pad_blocked, comp);
        gst_object_unref (pad);
      } else {
        GST_DEBUG_OBJECT (comp->private->current,
            "Couldn't get the source pad.. storing the pending child seek");
        comp->private->childseek = event;
        ret = TRUE;
      }

    COMP_OBJECTS_UNLOCK (comp);

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

  gst_pad_set_blocked_async (pad, TRUE, (GstPadBlockCallback) pad_blocked, comp);
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

  if ((GNL_OBJECT (element)->priority == G_MAXUINT32) && comp->private->defaultobject) {
    GST_WARNING_OBJECT (comp, "We already have a default source, remove it before adding new one");
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
		  "duration", (GstClockTimeDiff) GNL_OBJECT (comp)->stop,
		  "media-duration", (GstClockTimeDiff) GNL_OBJECT (comp)->stop, NULL);
    g_object_set (element, "start", 0, NULL);
    g_object_set (element, "media-start", 0, NULL);
  }
  entry->activehandler = g_signal_connect (G_OBJECT (element),
      "notify::active", G_CALLBACK (object_active_changed), comp);
  entry->padremovedhandler = g_signal_connect (G_OBJECT (element),
      "pad-removed", G_CALLBACK (object_pad_removed), comp);
  entry->padaddedhandler = g_signal_connect (G_OBJECT (element),
					     "pad-added", G_CALLBACK (object_pad_added), comp);
  g_hash_table_insert (comp->private->objects_hash, element, entry);

  /* Special case for default source */
  if (GNL_OBJECT(element)->priority == G_MAXUINT32) {
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

  GST_DEBUG_OBJECT (comp, "segment_start:%"GST_TIME_FORMAT" segment_stop:%"GST_TIME_FORMAT,
		    GST_TIME_ARGS (comp->private->segment_start),
		    GST_TIME_ARGS (comp->private->segment_stop));

  COMP_OBJECTS_UNLOCK (comp);

  /* If we added within currently configured segment, update pipeline */
  if (OBJECT_IN_ACTIVE_SEGMENT (comp, element))
    update_pipeline (comp, comp->private->segment_start, TRUE, TRUE);
  else
    update_start_stop_duration (comp);
  
beach:
  gst_object_unref (element);
  return ret;

 chiringuito:
  COMP_OBJECTS_UNLOCK (comp);
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
  if (GNL_OBJECT(element)->priority == G_MAXUINT32) {
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
  }

  if (!(g_hash_table_remove (comp->private->objects_hash, element)))
    goto chiringuito;

  COMP_OBJECTS_UNLOCK (comp);

  /* If we removed within currently configured segment, or it was the default source, *
   * update pipeline */
  if (OBJECT_IN_ACTIVE_SEGMENT (comp, element) || (GNL_OBJECT(element)->priority == G_MAXUINT32))
    update_pipeline (comp, comp->private->segment_start, TRUE, TRUE);
  else
    update_start_stop_duration (comp);

  ret = GST_BIN_CLASS (parent_class)->remove_element (bin, element);

beach:
  gst_object_unref (element);
  return ret;

 chiringuito:
  COMP_OBJECTS_UNLOCK (comp);
  goto beach;
}
