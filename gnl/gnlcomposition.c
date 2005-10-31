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

static GstElementDetails gnl_composition_details = GST_ELEMENT_DETAILS (
   "GNonLin Composition",
   "Filter/Editor",
   "Combines GNL objects",
   "Wim Taymans <wim.taymans@chello.be>, Edward Hervey <bilboed@bilboed.com>"
   );

GST_DEBUG_CATEGORY_STATIC (gnlcomposition);
#define GST_CAT_DEFAULT gnlcomposition

struct _GnlCompositionPrivate {
  gboolean	dispose_has_run;
  
  /* 
     Sorted List of GnlObjects , ThreadSafe 
     objects_start : sorted by start-time then priority
     objects_stop : sorted by stop-time then priority
     objects_hash : contains signal handlers id for controlled objects
     objects_lock : mutex to acces/modify any of those lists/hashtable
  */
  GList			*objects_start;
  GList			*objects_stop;
  GStaticMutex		*objects_lock;  
  
  GHashTable		*objects_hash;
  
  /* composition bus watch id */
  guint			watchid;
  
  /* source ghostpad */
  GstPad		*ghostpad;

  /* 
     current seek boundaries.
     If GST_CLOCK_TIME_NONE, use the GnlObject start/stop values
  */
  gint64		seek_start;
  gint64		seek_stop;
  
  /* current stack */
  GList			*current;
  /*
    current segment seek start/stop time. 
    Reconstruct pipeline ONLY if seeking outside of those values
  */
  GstClockTime		segment_start;
  GstClockTime		segment_stop;
};

static void
gnl_composition_dispose		(GObject *object);
static void
gnl_composition_finalize 	(GObject *object);
static void
gnl_composition_reset		(GnlComposition *comp);

static gboolean
gnl_composition_add_object	(GstBin *bin, GstElement *element);

static gboolean
gnl_composition_remove_object	(GstBin *bin, GstElement *element);

static GstStateChangeReturn
gnl_composition_change_state	(GstElement *element, 
				 GstStateChange transition);

static gboolean
gnl_composition_bus_watch	(GstBus *bus, GstMessage *message,
				 GnlComposition *comp);

static GnlObject *
gnl_composition_find_object	(GnlComposition *comp, GstClockTime start,
				 guint priority);

static GnlObject *
gnl_composition_find_object_full	(GnlComposition *comp,
					 GstClockTime timestamp,
					 guint priority,
					 gboolean active);

static gboolean
update_pipeline		(GnlComposition *comp, GstClockTime currenttime);

#define COMP_REAL_START(comp) \
  ((GST_CLOCK_TIME_IS_VALID (comp->private->seek_start)) ? \
   (comp->private->seek_start) : (GNL_OBJECT (comp)->start))

/* void			gnl_composition_show	 		(GnlComposition *comp); */

/* #define CLASS(comp)  GNL_COMPOSITION_CLASS (G_OBJECT_GET_CLASS (comp)) */

/* static gboolean         gnl_composition_query                	(GstElement *element, GstQueryType type, */
/* 		                                                 GstFormat *format, gint64 *value); */

/* static gboolean 	gnl_composition_covers_func		(GnlObject *object,  */
/* 								 GstClockTime start, GstClockTime stop, */
/* 							 	 GnlCoverType type); */

static gboolean
gnl_composition_prepare		(GnlObject *object);

/* static GstClockTime 	gnl_composition_nearest_cover_func 	(GnlComposition *comp, GstClockTime start,  */
/* 								 GnlDirection direction); */


typedef struct _GnlCompositionEntry GnlCompositionEntry;

struct _GnlCompositionEntry
{
  GnlObject	*object;
  gulong	starthandler;
  gulong	stophandler;
  gulong	priorityhandler;
  gulong	activehandler;
};

static void
gnl_composition_base_init (gpointer g_class)
{
  GstElementClass *gstclass = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstclass, &gnl_composition_details);
}

static void
gnl_composition_class_init (GnlCompositionClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;
  GnlObjectClass *gnlobject_class;

  gobject_class    = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;
  gstbin_class     = (GstBinClass*)klass;
  gnlobject_class  = (GnlObjectClass*)klass;

  GST_DEBUG_CATEGORY_INIT (gnlcomposition, "gnlcomposition", 0, "GNonLin Composition");

  gobject_class->dispose	= GST_DEBUG_FUNCPTR (gnl_composition_dispose);
  gobject_class->finalize 	= GST_DEBUG_FUNCPTR (gnl_composition_finalize);

  gstelement_class->change_state = gnl_composition_change_state;
/*   gstelement_class->query	 = gnl_composition_query; */

  gstbin_class->add_element      = 
    GST_DEBUG_FUNCPTR (gnl_composition_add_object);
  gstbin_class->remove_element   = 
    GST_DEBUG_FUNCPTR (gnl_composition_remove_object);

  gnlobject_class->prepare       = 
    GST_DEBUG_FUNCPTR (gnl_composition_prepare);
/*   gnlobject_class->covers	 = gnl_composition_covers_func; */

/*   klass->nearest_cover	 	 = gnl_composition_nearest_cover_func; */
}

static void
hash_value_destroy	(GnlCompositionEntry *entry)
{
  g_signal_handler_disconnect (entry->object, entry->starthandler);
  g_signal_handler_disconnect (entry->object, entry->stophandler);
  g_signal_handler_disconnect (entry->object, entry->priorityhandler);
  g_signal_handler_disconnect (entry->object, entry->activehandler);
  g_free(entry);
}

static void
gnl_composition_init (GnlComposition *comp, GnlCompositionClass *klass)
{
  GST_OBJECT_FLAG_SET (comp, GNL_OBJECT_SOURCE);


  comp->private = g_new0 (GnlCompositionPrivate, 1);
  comp->private->objects_lock = g_new0 (GStaticMutex, 1);
  g_static_mutex_init (comp->private->objects_lock);
  comp->private->objects_start = NULL;
  comp->private->objects_stop = NULL;

  comp->private->objects_hash = g_hash_table_new_full
    (g_direct_hash,
     g_direct_equal,
     NULL,
     (GDestroyNotify) hash_value_destroy);
  
  comp->private->watchid = gst_bus_add_watch
    (GST_BIN (comp)->child_bus,
     (GstBusFunc) gnl_composition_bus_watch,
     comp);
  
  comp->private->ghostpad = 
    gnl_object_ghost_pad_notarget (GNL_OBJECT (comp),
				   "src",
				   GST_PAD_SRC);

  gst_element_add_pad (GST_ELEMENT (comp),
		       comp->private->ghostpad);
  
  gnl_composition_reset (comp);
}

static void
gnl_composition_dispose (GObject *object)
{
  GnlComposition	*comp = GNL_COMPOSITION (object);

  if (comp->private->dispose_has_run)
    return;

  comp->private->dispose_has_run = TRUE;

  if (comp->private->watchid)
    g_source_remove (comp->private->watchid);

  if (comp->private->ghostpad)
    gst_element_remove_pad (GST_ELEMENT (object),
			    comp->private->ghostpad);

  /* FIXME: all of a sudden I can't remember what we have to do here... */
}

static void
gnl_composition_finalize (GObject *object)
{
  GnlComposition *comp = GNL_COMPOSITION (object);

  GST_INFO("finalize");

  g_static_mutex_lock (comp->private->objects_lock);
  g_list_free (comp->private->objects_start);
  g_list_free (comp->private->objects_stop);
  g_hash_table_destroy (comp->private->objects_hash);
  g_static_mutex_unlock (comp->private->objects_lock);

  g_free (comp->private);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gnl_composition_reset	(GnlComposition *comp)
{
  comp->private->seek_start = GST_CLOCK_TIME_NONE;
  comp->private->seek_stop = GST_CLOCK_TIME_NONE;

  comp->private->segment_start = GST_CLOCK_TIME_NONE;
  comp->private->segment_stop = GST_CLOCK_TIME_NONE;

  if (comp->private->current)
    g_list_free (comp->private->current);
  comp->private->current = NULL;

  /* FIXME: uncomment the following when setting a NULL target is allowed */
/*   gnl_object_ghost_pad_set_target (GNL_OBJECT (comp), */
/* 				   comp->private->ghostpad, */
/* 				   NULL); */
}

static gboolean
gnl_composition_bus_watch	(GstBus *bus, GstMessage *message,
				 GnlComposition *comp)
{
  GST_DEBUG_OBJECT (comp, "saw message : %s",
		    gst_message_type_get_name (GST_MESSAGE_TYPE (message)));
  return TRUE;
}

static gint
priority_comp (GnlObject *a, GnlObject *b)
{
  return a->priority - b->priority;
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
get_stack_list	(GnlComposition *comp, GstClockTime timestamp,
		 guint priority, gboolean activeonly)
{
  GList	*tmp = comp->private->objects_start;
  GList	*stack = NULL;

  GST_DEBUG_OBJECT (comp, "timestamp:%lld, priority:%d, activeonly:%d",
		    timestamp, priority, activeonly);

  /*
    Iterate the list with a stack:
    Either :
      _ ignore
      _ add to the stack (object.start <= timestamp < object.stop)
        _ add in priority order
      _ stop iteration (object.start > timestamp)
  */

  for ( ; tmp ; tmp = g_list_next (tmp)) {
    GnlObject	*object = GNL_OBJECT (tmp->data);

    if (object->start <= timestamp) {
      if (object->stop < timestamp) {
	GST_LOG_OBJECT (comp, "too far, stopping iteration");
	break;
      } else if ( (object->priority >= priority) 
		  && ((!activeonly) || (object->active)) ) {
	GST_LOG_OBJECT (comp, "adding %s sorted to the stack", 
			GST_OBJECT_NAME (object));
	stack = g_list_insert_sorted (stack, object, 
				      (GCompareFunc) priority_comp);
      }
    }
  }

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
get_clean_toplevel_stack (GnlComposition *comp, GstClockTime timestamp,
			  GstClockTime *stop_time)
{
  GList	*stack, *tmp;
  GList	*ret = NULL;
  gint	size = 1;
  GstClockTime	stop = G_MAXUINT64;

  GST_DEBUG_OBJECT (comp, "timestamp:%lld", timestamp);

  stack = get_stack_list (comp, timestamp, 0, TRUE);
  for (tmp = stack; tmp && size; tmp = g_list_next(tmp), size--) {
    GnlObject	*object = tmp->data;

    ret = g_list_append (ret, object);
    if (stop > object->stop)
      stop = object->stop;
    /* FIXME : ADD CASE FOR OPERATION */
    /* size += number of input for operation */
  }
  
  g_list_free(stack);

  if (stop_time) {
    if (ret)
      *stop_time = stop;
    else
      *stop_time = 0;
  }
  
  return ret;
}

/**
 * gnl_composition_find_object_full:
 * @comp: The #GnlComposition to look in.
 * @timetstamp: the #GstClockTime to look at.
 * @priority: The priority to start looking from.
 * @activeonly: Look only for active elements if TRUE.
 *
 * Returns: The #GnlObject corresponding to the given parameters, or NULL if
 * there wasn't any.
 */

static GnlObject *
gnl_composition_find_object_full (GnlComposition *comp,
				  GstClockTime timestamp,
				  guint priority,
				  gboolean activeonly)
{
  GnlObject *object = NULL;
  GList	*stack = NULL;

  g_static_mutex_lock (comp->private->objects_lock);

  stack = get_stack_list (comp, timestamp, priority, activeonly);
  if (stack) {
    object = GNL_OBJECT (stack->data);
    g_list_free (stack);
  }

  g_static_mutex_unlock (comp->private->objects_lock);
  return object;
}


/**
 * gnl_composition_find_object:
 * @comp: The #GnlComposition to look in.
 * @timestamp: The #GstClockTime to look at.
 */
static GnlObject *
gnl_composition_find_object	(GnlComposition *comp,
				 GstClockTime timestamp,
				 guint priority)
{
  return gnl_composition_find_object_full (comp, timestamp, priority, FALSE);
}



static GstPad *
get_src_pad (GstElement *element)
{
  GstIterator		*it;
  GstIteratorResult	itres;
  GstPad		*srcpad;

  it = gst_element_iterate_src_pads (element);
  itres = gst_iterator_next (it, (gpointer) &srcpad);
  if (itres != GST_ITERATOR_OK) {
    GST_WARNING ("%s doesn't have a src pad !",
		 GST_ELEMENT_NAME (element));
    srcpad = NULL;
  }
  gst_iterator_free (it);
  return srcpad;
}

static gboolean
gnl_composition_prepare	(GnlObject *object)
{
  gboolean		ret = TRUE;

  return ret;
}

static GstStateChangeReturn
gnl_composition_change_state (GstElement *element, GstStateChange transition)
{
  GnlComposition *comp = GNL_COMPOSITION (element);
  GstStateChangeReturn	ret;

  switch (transition) {
  case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    /* state-lock elements not used */
/*     update_pipeline (comp, COMP_REAL_START (comp)); */
    break;
  default:
    break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
  case GST_STATE_CHANGE_READY_TO_PAUSED:
    GST_DEBUG_OBJECT (comp, "ghosting pad");
    /* set ghostpad target */
    update_pipeline (comp,
		     COMP_REAL_START(comp));
    
    /* send initial seek */
    
    break;
  case GST_STATE_CHANGE_PAUSED_TO_READY:
    gnl_composition_reset (comp);
    break;
  default:
    break;
  }

  return ret;
}

static gint
objects_start_compare	(GnlObject *a, GnlObject *b)
{
  /* return positive value if a is after b */
  if (a->start == b->start)
    return a->priority - b->priority;
  return a->start - b->start;
}

static gint
objects_stop_compare	(GnlObject *a, GnlObject *b)
{
  /* return positive value if b is after a */
  if (a->stop == b->stop)
    return a->priority - b->priority;
  return b->stop - a->stop;
}

static void
update_start_stop_duration	(GnlComposition *comp)
{
  GnlObject	*obj;
  GnlObject	*cobj = GNL_OBJECT (comp);

  if (!(comp->private->objects_start)) {
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
    cobj->start = obj->start;
    g_object_notify (G_OBJECT (cobj), "start");
  }

  obj = GNL_OBJECT (comp->private->objects_stop->data);
  if (obj->stop != cobj->stop) {
    cobj->stop = obj->stop;
    g_object_notify (G_OBJECT (cobj), "stop");
  }

  if ((cobj->stop - cobj->start) != cobj->duration) {
    cobj->duration = cobj->stop - cobj->start;
    g_object_notify (G_OBJECT (cobj), "duration");
  }
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
compare_relink_stack	(GnlComposition *comp, GList *stack)
{
  GList	*curo, *curn;
  GList	*deactivate = NULL;
  GnlObject	*oldobj = NULL;
  GnlObject	*newobj = NULL;
  GnlObject	*pold = NULL; 
  GnlObject	*pnew = NULL;

  for (curo = comp->private->current, curn = stack; curo && curn; ) {
    oldobj = GNL_OBJECT (curo);
    newobj = GNL_OBJECT (curn);

    if ((oldobj == newobj) && GNL_OBJECT_IS_OPERATION (oldobj)) {
      /* guint	size = GNL_OPERATION_SINKS (oldobj); */
      /* FIXME : check objects the operation works on */
      /*
	while (size) {

	size--;
	}
      */
    } else if (oldobj != newobj) {
      deactivate = g_list_append (deactivate, oldobj);
      if (pold) {
	/* unlink old object from previous old obj */
	gst_element_unlink (GST_ELEMENT (pold), GST_ELEMENT (oldobj));
      }
      if (pnew) {
	/* link new object to previous new obj */
	gst_element_link (GST_ELEMENT (pnew), GST_ELEMENT (newobj));
      }
    }
    
    curo = g_list_next (curo);
    curn = g_list_next (curn);
    pold = oldobj;
    pnew = newobj;
  }
  
  GST_DEBUG_OBJECT (comp, "curo:%p, curn:%p",
		    curo, curn);
  for (; curn; curn = g_list_next (curn)) {
    GST_DEBUG_OBJECT (curn->data, "...");
    newobj = GNL_OBJECT (curn->data);

    if (pnew)
      gst_element_link (GST_ELEMENT (pnew), GST_ELEMENT (newobj));

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
 *
 * Updates the internal pipeline and properties. If @currenttime is 
 * GST_CLOCK_TIME_NONE, it will not modify the current pipeline
 *
 * Returns: TRUE if the pipeline was modified.
 */

static gboolean
update_pipeline	(GnlComposition *comp, GstClockTime currenttime)
{
  gboolean	ret = FALSE;

  GST_DEBUG_OBJECT (comp, "currenttime:%lld", currenttime);

  g_static_mutex_lock (comp->private->objects_lock);

  update_start_stop_duration (comp);

  /* only modify the pipeline if we're in state != PLAYING */
  if ( (GST_CLOCK_TIME_IS_VALID (currenttime))
       && (GST_STATE (comp) == GST_STATE_PAUSED)) {
    GList	*stack = NULL;
    GList	*deactivate;
    GstClockTime	new_stop;

    GST_DEBUG_OBJECT (comp, "now really updating the pipeline");

    /* rebuild the stack and relink new elements */
    stack = get_clean_toplevel_stack (comp, currenttime, &new_stop);    
    deactivate = compare_relink_stack (comp, stack);
    if (deactivate)
      ret = TRUE;

    /* if toplevel element has changed, redirect ghostpad to it */
    if ((stack) && ((!(comp->private->current)) || (comp->private->current->data != stack->data))) {
      GstPad	*pad;

      pad = get_src_pad (GST_ELEMENT (stack->data));
      if (pad) 
	gnl_object_ghost_pad_set_target (GNL_OBJECT (comp), 
					 comp->private->ghostpad,
					 pad);
    }

    GST_DEBUG_OBJECT (comp, "activating objects in new stack");
    /* activate new stack */
    comp->private->current = stack;
    while (stack) {
      GST_OBJECT_FLAG_UNSET (stack->data, GST_ELEMENT_LOCKED_STATE);
      stack = g_list_next (stack);
    }

    GST_DEBUG_OBJECT (comp, "De-activating objects no longer used");
    /* state-lock elements no more used */
    while (deactivate) {
      GST_OBJECT_FLAG_SET (deactivate->data, GST_ELEMENT_LOCKED_STATE);
      deactivate = g_list_next (deactivate);
    }

    /* set new segment_start/stop */
    comp->private->segment_start = currenttime;
    comp->private->segment_stop = new_stop;
  }

  g_static_mutex_unlock (comp->private->objects_lock);
  return ret;
}

static void
object_start_changed	(GnlObject *object, GParamSpec *arg,
			 GnlComposition *comp)
{
  GST_DEBUG_OBJECT (object, "...");

  comp->private->objects_start = g_list_sort 
    (comp->private->objects_start, 
     (GCompareFunc) objects_start_compare);

  update_pipeline (comp, GST_CLOCK_TIME_NONE);
}

static void
object_stop_changed	(GnlObject *object, GParamSpec *arg,
			 GnlComposition *comp)
{
  GST_DEBUG_OBJECT (object, "...");
  
  comp->private->objects_stop = g_list_sort 
    (comp->private->objects_stop, 
     (GCompareFunc) objects_stop_compare);
  
  update_pipeline (comp, GST_CLOCK_TIME_NONE);
}

static void
object_priority_changed	(GnlObject *object, GParamSpec *arg, 
			 GnlComposition *comp)
{
  GST_DEBUG_OBJECT (object, "...");

  comp->private->objects_start = g_list_sort
    (comp->private->objects_start, 
     (GCompareFunc) objects_start_compare);
  
  comp->private->objects_stop = g_list_sort
    (comp->private->objects_stop, 
     (GCompareFunc) objects_stop_compare);
  
  update_pipeline (comp, GST_CLOCK_TIME_NONE);
}

static void
object_active_changed	(GnlObject *object, GParamSpec *arg,
			 GnlComposition *comp)
{
  GST_DEBUG_OBJECT (object, "...");

  update_pipeline (comp, GST_CLOCK_TIME_NONE);
}

static gboolean
gnl_composition_add_object	(GstBin *bin, GstElement *element)
{
  gboolean	ret;
  GnlCompositionEntry	*entry;
  GnlComposition	*comp = GNL_COMPOSITION (bin);

  GST_DEBUG_OBJECT (bin, "element %s", GST_OBJECT_NAME (element));
  /* we only accept GnlObject */
  g_return_val_if_fail (GNL_IS_OBJECT (element), FALSE);

  g_static_mutex_lock (comp->private->objects_lock);
  gst_object_ref (element);
  ret = GST_BIN_CLASS (parent_class)->add_element (bin, element);
  if (!ret)
    goto beach;

  /* wrap it and add it to the hash table */
  entry = g_new0 (GnlCompositionEntry, 1);
  entry->object = GNL_OBJECT (element);
  entry->starthandler = g_signal_connect (G_OBJECT (element),
					  "notify::start",
					  G_CALLBACK (object_start_changed),
					  comp);
  entry->stophandler = g_signal_connect (G_OBJECT (element),
					  "notify::stop",
					  G_CALLBACK (object_stop_changed),
					  comp);
  entry->priorityhandler = g_signal_connect (G_OBJECT (element),
					  "notify::priority",
					  G_CALLBACK (object_priority_changed),
					  comp);
  entry->activehandler = g_signal_connect (G_OBJECT (element),
					  "notify::active",
					  G_CALLBACK (object_active_changed),
					  comp);
  g_hash_table_insert (comp->private->objects_hash, element, entry);

  /* add it sorted to the objects list */
  comp->private->objects_start = g_list_insert_sorted 
    (comp->private->objects_start,
     element,
     (GCompareFunc) objects_start_compare);
  
  comp->private->objects_stop = g_list_insert_sorted
    (comp->private->objects_stop,
     element,
     (GCompareFunc) objects_stop_compare);
  
  /* update pipeline */
  g_static_mutex_unlock (comp->private->objects_lock);
  update_pipeline (comp, GST_CLOCK_TIME_NONE);
  g_static_mutex_lock (comp->private->objects_lock);

 beach:
  gst_object_unref (element);
  g_static_mutex_unlock (comp->private->objects_lock);
  return ret;
}


static gboolean
gnl_composition_remove_object	(GstBin *bin, GstElement *element)
{
  gboolean	ret;
  GnlComposition	*comp = GNL_COMPOSITION (bin);

  GST_DEBUG_OBJECT (bin, "element %s", GST_OBJECT_NAME (element));
  /* we only accept GnlObject */
  g_return_val_if_fail (GNL_IS_OBJECT (element), FALSE);

  g_static_mutex_lock (comp->private->objects_lock);
  gst_object_ref (element);
  ret = GST_BIN_CLASS (parent_class)->remove_element (bin, element);
  if (!ret)
    goto beach;

  if (!(g_hash_table_remove (comp->private->objects_hash, element)))
    goto beach;

  /* remove it from the objects list and resort the lists */
  comp->private->objects_start = g_list_remove 
    (comp->private->objects_start, element);
  comp->private->objects_start = g_list_sort
    (comp->private->objects_start, 
     (GCompareFunc) objects_start_compare);
  
  comp->private->objects_stop = g_list_remove
    (comp->private->objects_stop, element);
  comp->private->objects_stop = g_list_sort
    (comp->private->objects_stop, 
     (GCompareFunc) objects_stop_compare);

  g_static_mutex_unlock (comp->private->objects_lock);
  update_pipeline (comp, GST_CLOCK_TIME_NONE);
  g_static_mutex_lock (comp->private->objects_lock);

 beach:
  gst_object_unref (element);
  g_static_mutex_unlock (comp->private->objects_lock);
  return ret;
}


