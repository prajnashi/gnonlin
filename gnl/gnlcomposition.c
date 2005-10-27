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
     objects_lock : mutex to acces/modify any of those lists
  */
  GList			*objects_start;
  GList			*objects_stop;
  GStaticMutex		*objects_lock;  
  
  GHashTable		*objects_hash;

  /* current seek */
  gint64		seek_start;
  gint64		seek_stop;

  /* current top-level object, whose srcpad is ghost-ed on the composition */
  GnlObject		*current;
};

static void
gnl_composition_dispose		(GObject *object);
static void
gnl_composition_finalize 	(GObject *object);

static gboolean
gnl_composition_add_object	(GstBin *bin, GstElement *element);

static gboolean
gnl_composition_remove_object	(GstBin *bin, GstElement *element);

/* static GstElementStateReturn */
/* 			gnl_composition_change_state 		(GstElement *element); */
	
/* void			gnl_composition_show	 		(GnlComposition *comp); */

/* #define CLASS(comp)  GNL_COMPOSITION_CLASS (G_OBJECT_GET_CLASS (comp)) */

/* static gboolean         gnl_composition_query                	(GstElement *element, GstQueryType type, */
/* 		                                                 GstFormat *format, gint64 *value); */

/* static gboolean 	gnl_composition_covers_func		(GnlObject *object,  */
/* 								 GstClockTime start, GstClockTime stop, */
/* 							 	 GnlCoverType type); */
/* static gboolean 	gnl_composition_prepare			(GnlObject *object, GstEvent *event); */
/* static GstClockTime 	gnl_composition_nearest_cover_func 	(GnlComposition *comp, GstClockTime start,  */
/* 								 GnlDirection direction); */

/* static gboolean 	gnl_composition_schedule_entries 	(GnlComposition *comp, GstClockTime start, */
/* 								 GstClockTime stop, gint minprio, GstPad **pad); */

/* void	composition_update_start_stop(GnlComposition *comp); */

typedef struct _GnlCompositionEntry GnlCompositionEntry;

struct _GnlCompositionEntry
{
  GnlObject	*object;
  gulong	starthandler;
  gulong	stophandler;
  gulong	priorityhandler;
  gulong	activehandler;
};

/* #define GNL_COMP_ENTRY(entry)		((GnlCompositionEntry *)entry) */
/* #define GNL_COMP_ENTRY_OBJECT(entry)	(GNL_OBJECT (GNL_COMP_ENTRY (entry)->object)) */

/* GType */
/* gnl_composition_get_type (void) */
/* { */
/*   static GType composition_type = 0; */

/*   if (!composition_type) { */
/*     static const GTypeInfo composition_info = { */
/*       sizeof (GnlCompositionClass), */
/*       (GBaseInitFunc) gnl_composition_base_init, */
/*       NULL, */
/*       (GClassInitFunc) gnl_composition_class_init, */
/*       NULL, */
/*       NULL, */
/*       sizeof (GnlComposition), */
/*       32, */
/*       (GInstanceInitFunc) gnl_composition_init, */
/*     }; */
/*     composition_type = g_type_register_static (GNL_TYPE_OBJECT, "GnlComposition", &composition_info, 0); */
/*   } */
/*   return composition_type; */
/* } */

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

/*   gstelement_class->change_state = gnl_composition_change_state; */
/*   gstelement_class->query	 = gnl_composition_query; */

  gstbin_class->add_element      = GST_DEBUG_FUNCPTR (gnl_composition_add_object);
  gstbin_class->remove_element   = GST_DEBUG_FUNCPTR (gnl_composition_remove_object);

/*   gnlobject_class->prepare       = gnl_composition_prepare; */
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

  comp->private->objects_hash = g_hash_table_new_full (g_direct_hash,
						       g_direct_equal,
						       NULL,
						       (GDestroyNotify) hash_value_destroy);

  comp->private->seek_start = 0;
  comp->private->seek_stop = G_MAXUINT64;

  comp->private->current = NULL;
}

static void
gnl_composition_dispose (GObject *object)
{
  GnlComposition	*comp = GNL_COMPOSITION (object);

  if (comp->private->dispose_has_run)
    return;

  comp->private->dispose_has_run = TRUE;
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

/* /\* */
/*  * gnl_composition_find_entry_priority: */
/*  * @comp: The composition in which we're looking for an entry */
/*  * @time: The time to start the search */
/*  * @method: the #GnlFindMethod to use */
/*  * @minpriority: The minimum priority to use */
/*  * */
/*  * Returns: The #GnlCompositionEntry found, or NULL if nothing was found */
/* *\/ */

/* static GnlCompositionEntry * */
/* gnl_composition_find_entry_priority (GnlComposition *comp, GstClockTime time, */
/* 				     GnlFindMethod method, gint minpriority) { */
/*   GList	*objects = comp->objects; */
/*   GnlCompositionEntry	*tmp = NULL; */

/*   GST_INFO ("Composition[%s], time[%" GST_TIME_FORMAT "], Method[%d], minpriority[%d]", */
/* 	    gst_element_get_name(GST_ELEMENT(comp)), */
/* 	    GST_TIME_ARGS(time), method, minpriority); */

/*   /\* */
/*     Take into account the fact that we now have to search for the lowest priority */
/*   *\/ */

/*   if (method == GNL_FIND_AT) { */
/*     while (objects) { */
/*       GnlCompositionEntry *entry = (GnlCompositionEntry *) (objects->data); */
/*       GstClockTime start, stop; */
      
/*       if (entry->object->priority >= minpriority) { */
/* 	gnl_object_get_start_stop (entry->object, &start, &stop); */

/* 	if ((start <= time && start + (stop - start) > time) */
/* 	    && (!tmp || (tmp && tmp->object->priority > entry->object->priority))) { */
/* 	  tmp = entry; */
/* 	} */
/*       } */
/*       objects = g_list_next(objects); */
/*     } */

/*     if (tmp) { */
/* 	GST_INFO ("Returning [%s] [%" GST_TIME_FORMAT "]->[%" GST_TIME_FORMAT "] priority:%d", */
/* 		  gst_element_get_name (GST_ELEMENT (tmp->object)), */
/* 		  GST_TIME_ARGS (tmp->object->start), */
/* 		  GST_TIME_ARGS (tmp->object->stop), */
/* 		  tmp->object->priority); */
/*     } else { */
/* 	GST_INFO ("No matching entry found"); */
/*     } */
/*     return tmp; */
/*   } else */
/*     while (objects) { */
/*       GnlCompositionEntry *entry = (GnlCompositionEntry *) (objects->data); */
/*       GstClockTime start, stop; */
      
/*       gnl_object_get_start_stop (entry->object, &start, &stop); */
      
/*       if (entry->object->priority >= minpriority) */
/* 	switch (method) { */
/* 	case GNL_FIND_AFTER: */
/* 	  if (start >= time) */
/* 	    return entry; */
/* 	  break; */
/* 	case GNL_FIND_START: */
/* 	  if (start == time) */
/* 	    return entry; */
/* 	  break; */
/* 	default: */
/* 	  GST_WARNING ("%s: unkown find method", gst_element_get_name (GST_ELEMENT (comp))); */
/* 	  break; */
/* 	} */
/*       objects = g_list_next(objects); */
/*     } */
/*   return NULL; */
/* } */

/* /\* */
/*   gnl_composition_find_entry */

/*   Find the GnlCompositionEntry located AT/AFTER/START */

/* *\/ */

/* static GnlCompositionEntry* */
/* gnl_composition_find_entry (GnlComposition *comp, GstClockTime time, GnlFindMethod method) */
/* { */
/* /\*   GList *objects = comp->objects; *\/ */

/*   GST_INFO ("Composition[%s], time[%lld], Method[%d]", */
/* 	    gst_element_get_name(GST_ELEMENT(comp)), */
/* 	    time, method); */

/*   return gnl_composition_find_entry_priority(comp, time, method, 1); */
/* } */

/* /\** */
/*  * gnl_composition_find_object: */
/*  * @comp: The #GnlComposition to look into */
/*  * @time: The time to start looking at */
/*  * @method: The #GnlFindMethod used to look to the object */
/*  * */
/*  * Returns: The #GnlObject found , or NULL if none */
/*  *\/ */

/* GnlObject* */
/* gnl_composition_find_object (GnlComposition *comp, GstClockTime time, GnlFindMethod method) */
/* { */
/*   GnlCompositionEntry *entry; */

/*   GST_INFO ("Composition[%s], time[%" GST_TIME_FORMAT "], Method[%d]", */
/* 	    gst_element_get_name(GST_ELEMENT(comp)), */
/* 	    GST_TIME_ARGS(time), method); */

/*   entry = gnl_composition_find_entry (comp, time, method); */
/*   if (entry) { */
/*     return entry->object; */
/*   } */

/*   return NULL; */
/* } */

/* /\* */
/*   GnlCompositionEntry comparision function */

/*   Allows to sort by priority and THEN by time */

/*   MODIFIED : sort by time and then by priority */
/* *\/ */

/* static gint  */
/* _entry_compare_func (gconstpointer a, gconstpointer b) */
/* { */
/*   GnlObject *object1, *object2; */
/*   GstClockTime start1, start2; */
/*   gint res; */
/*   long long int lres; */

/*   object1 = ((GnlCompositionEntry *) a)->object; */
/*   object2 = ((GnlCompositionEntry *) b)->object; */

/*   start1 = object1->start; */
/*   start2 = object2->start; */

/*   lres = start1 - start2; */

/*   if (lres < 0) */
/*     res = -1; */
/*   else { */
/*     if (lres > 0) */
/*       res = 1; */
/*     else */
/*       res = gnl_object_get_priority (object1) - */
/* 	gnl_object_get_priority (object2);  */
/*   } */

/*   return res; */
/* } */

/* /\** */
/*  * gnl_composition_new: */
/*  * @name: the name of the composition */
/*  * */
/*  * Returns: an initialized #GnlComposition */
/*  *\/ */

/* GnlComposition* */
/* gnl_composition_new (const gchar *name) */
/* { */
/*   GnlComposition *comp; */

/*   g_return_val_if_fail (name != NULL, NULL); */

/*   comp = g_object_new (GNL_TYPE_COMPOSITION, NULL); */
/*   gst_object_set_name (GST_OBJECT (comp), name); */

/*   return comp; */
/* } */

/* void */
/* child_active_changed (GnlObject *object, GParamSpec *arg, gpointer udata) */
/* { */
/*   GnlComposition *comp = GNL_COMPOSITION (udata); */

/*   GST_INFO("%s [State:%d]: State of child %s has changed to %s", */
/* 	   gst_element_get_name(GST_ELEMENT (comp)), */
/* 	   gst_element_get_state (GST_ELEMENT (comp)), */
/* 	   gst_element_get_name(GST_ELEMENT (object)), */
/* 	   (object->active) ? "active" : "NOT active"); */
/*   if (object->active) { */
/*     GST_FLAG_UNSET (GST_ELEMENT (object), GST_ELEMENT_LOCKED_STATE); */
/*     comp->active_objects = g_list_append (comp->active_objects, object); */
/*     comp->to_remove = g_list_remove (comp->to_remove, object); */
/*   } else { */
/*     GST_FLAG_SET (GST_ELEMENT (object), GST_ELEMENT_LOCKED_STATE); */
/*     comp->active_objects = g_list_remove(comp->active_objects, object); */
/*   } */
/* } */

/* /\** */
/*  * gnl_composition_set_default_source */
/*  * @comp: The #GnlComposition */
/*  * @source: The #GnlSource we want to set as default source for this composition */
/*  * */
/*  *\/ */

/* void */
/* gnl_composition_set_default_source (GnlComposition *comp, */
/* 				    GnlSource *source) */
/* { */
/*   GnlCompositionEntry	*entry; */

/*   gnl_object_set_priority (GNL_OBJECT (source), G_MAXINT); */
/*   gnl_object_set_start_stop (GNL_OBJECT (source), 0LL, G_MAXUINT64); */

/*   entry = g_new0(GnlCompositionEntry, 1); */
/*   gst_object_ref (GST_OBJECT (source)); */
/*   gst_object_sink (GST_OBJECT (source)); */
/*   entry->object = GNL_OBJECT (source); */

/*   GNL_OBJECT(source)->comp_private = entry; */
/*   if (gst_element_get_pad (GST_ELEMENT (source), "src") == NULL) { */
/*     gnl_source_get_pad_for_stream (source, "src"); */
/*   } */
/*   entry->activehandler = g_signal_connect(GNL_OBJECT (source), "notify::active", G_CALLBACK (child_active_changed), comp); */
/*   comp->objects = g_list_insert_sorted (comp->objects, entry, _entry_compare_func); */

/*   GST_FLAG_SET (GST_ELEMENT (source), GST_ELEMENT_LOCKED_STATE); */
  
/*   GST_BIN_CLASS (parent_class)->add_element (GST_BIN (comp), GST_ELEMENT (source)); */
/*   GST_INFO ("Added default source to composition"); */
/* } */


/* static gint */
/* find_function (GnlCompositionEntry *entry, GnlObject *to_find)  */
/* { */
/*   GST_INFO("comparing object:%p to_find:%p", */
/* 	   entry->object, to_find); */
/*   if (entry->object == to_find) */
/*     return 0; */

/*   return 1; */
/* } */

/* /\** */
/*  * gnl_composition_remove_object: */
/*  * @comp: The #GnlComposition to remove an object from */
/*  * @object: The #GnlObject to remove from the composition */
/*  *\/ */

/* /\* */
/*   gnl_composition_schedule_object */

/*   Schedules the give object from start to stop and sets the output pad to *pad. */

/*   Returns : TRUE if the object was properly scheduled, FALSE otherwise */
/* *\/ */

/* static gboolean */
/* gnl_composition_schedule_object (GnlComposition *comp, GnlObject *object, */
/* 				 GstClockTime start, GstClockTime stop, */
/* 				 GstPad **pad) */
/* { */

/*   GST_INFO("Comp[%s]/sched=%p  Object[%s] Start [%lld] Stop[%lld] sched(object)=%p IS_SCHED:%d", */
/* 	   gst_element_get_name(GST_ELEMENT(comp)), */
/* 	   GST_ELEMENT_SCHED(GST_ELEMENT(comp)), */
/* 	   gst_element_get_name(GST_ELEMENT(object)), */
/* 	   start, stop, GST_ELEMENT_SCHED(object), GST_IS_SCHEDULER(GST_ELEMENT_SCHED(object))); */

/*   g_assert (start < stop); */

/*   if (!pad) */
/*     return TRUE; */
/*   /\* Activate object *\/ */
/*   gnl_object_set_active(object, TRUE); */

/*   if (gst_element_get_parent (GST_ELEMENT (object)) == NULL) {  */

/*     GST_INFO("Object has no parent, adding it to %s[Sched:%p]", */
/* 	     gst_element_get_name(GST_ELEMENT(comp)), GST_ELEMENT_SCHED(GST_ELEMENT(comp))); */

/*     GST_BIN_CLASS (parent_class)->add_element (GST_BIN (comp), GST_ELEMENT (object)); */
/*   } */

/*   gst_element_send_event (GST_ELEMENT (object), */
/* 			  gst_event_new_segment_seek ( */
/* 						      GST_FORMAT_TIME | */
/* 						      GST_SEEK_METHOD_SET | */
/* 						      GST_SEEK_FLAG_FLUSH | */
/* 						      GST_SEEK_FLAG_ACCURATE, */
/* 						      start, */
/* 						      stop) */
/* 			  ); */
/*   *pad = gst_element_get_pad (GST_ELEMENT (object), "src"); */
  
/*   GST_INFO("end of gnl_composition_schedule_object"); */

/*   return TRUE; */
/* } */

/* /\* */
/*   gnl_composition_schedule_object */

/*   Schedules the given operation from start to stop and sets *pad to the output pad */

/*   Returns : TRUE if the operation was properly scheduled, FALSE otherwise */
/* *\/ */

/* static gboolean */
/* gnl_composition_schedule_operation (GnlComposition *comp, GnlOperation *oper,  */
/* 				    GstClockTime start, GstClockTime stop, */
/* 				    GstPad **pad) */
/* { */
/*   const GList *pads; */
/*   gint	minprio = GNL_OBJECT(oper)->priority; */

/*   GST_INFO("Composition[%s]  Operation[%s] Start[%lld] Stop[%lld]", */
/* 	   gst_element_get_name(GST_ELEMENT(comp)), */
/* 	   gst_element_get_name(GST_ELEMENT(oper)), */
/* 	   start, stop); */

/*   gnl_composition_schedule_object (comp, GNL_OBJECT (oper), start, stop, pad); */

/*   pads = gst_element_get_pad_list (GST_ELEMENT (oper)); */
/*   while (pads) { */
/*     GstPad *newpad = NULL; */
/*     GstPad *sinkpad = GST_PAD (pads->data); */

/* /\*     GST_INFO ("Trying pad %s:%s Real[%s:%s]", *\/ */
/* /\* 	      GST_DEBUG_PAD_NAME(sinkpad), *\/ */
/* /\* 	      GST_DEBUG_PAD_NAME(GST_REAL_PAD (sinkpad))); *\/ */
/*     pads = g_list_next (pads); */

/*     if (GST_PAD_IS_SRC (sinkpad)) */
/*       continue; */

/*     minprio += 1; */
/*     if (!pad) { */
/*       gnl_composition_schedule_entries (comp, start, stop, minprio, NULL); */
/*       continue; */
/*     } */
/*     if (!gnl_composition_schedule_entries (comp, start, stop, minprio, &newpad)) */
/*       return FALSE; */

/*     /\* fixes scheduling where there's a gap between the oper's priority and the */
/*      * child objects's priorities  */
/*      *\/ */
/*     minprio = GNL_OBJECT(GST_PAD_PARENT(newpad))->priority; */

/*     GST_INFO ("Linking source pad %s:%s to operation pad %s:%s", */
/* 	      GST_DEBUG_PAD_NAME (newpad), */
/* 	      GST_DEBUG_PAD_NAME (sinkpad)); */
/*     if (GST_PAD_PEER(newpad)) { */
/*       GST_WARNING ("newpad %s:%s is still connected to %s:%s. Unlinking them !!", */
/* 		   GST_DEBUG_PAD_NAME(newpad), */
/* 		   GST_DEBUG_PAD_NAME(GST_PAD_PEER (newpad))); */
/*       gst_pad_unlink (newpad, GST_PAD_PEER (newpad)); */
/*     } */
/*     if (GST_PAD_PEER(sinkpad)) { */
/*       GST_WARNING ("sinkpad %s:%s is still connectd to %s:%s. Unlinking them !!", */
/* 		   GST_DEBUG_PAD_NAME(sinkpad), */
/* 		   GST_DEBUG_PAD_NAME (GST_PAD_PEER(sinkpad))); */
/*       gst_pad_unlink (GST_PAD_PEER(sinkpad), sinkpad); */
/*     } */
/*     if (!gst_pad_link (newpad, sinkpad)) { */
/*       GST_WARNING ("Couldn't link source pad %s:%s to operation pad %s:%s", */
/* 		   GST_DEBUG_PAD_NAME (newpad), */
/* 		   GST_DEBUG_PAD_NAME (sinkpad)); */
/*       return FALSE; */
/*     } */
/*     GST_INFO ("pads were linked with caps:%s", */
/* 	      gst_caps_to_string(gst_pad_get_caps(sinkpad))); */
/*   } */

/*   GST_INFO("Finished"); */
/*   return TRUE; */
/* } */

/* /\* */
/*   de-activates all active_objects */
/* *\/ */

/* void */
/* gnl_composition_deactivate_childs (GList	*childs) */
/* { */
/*   GList	*tmp, *next; */

/*   GST_INFO("deactivate childs %p", childs); */
/*   for (next = NULL, tmp = childs; tmp; tmp = next) { */
/*     next = g_list_next (tmp); */
/* /\*     gst_element_set_state(GST_ELEMENT (tmp->data), GST_STATE_READY ); *\/ */
/*     gnl_object_set_active(GNL_OBJECT (tmp->data), FALSE); */
/*   } */
/* } */

/* void */
/* gnl_composition_activate_entries (GList	*entries) */
/* { */
/*   GList	*tmp; */

/*   for (tmp = entries; tmp ; tmp = g_list_next(tmp)) { */
/*     GnlCompositionEntry	*entry = (GnlCompositionEntry *) tmp->data; */
/*     gnl_object_set_active (GNL_OBJECT (entry->object), TRUE); */
/*   } */
/* } */

/* void */
/* gnl_composition_deactivate_entries (GList	*entries) */
/* { */
/*   GList	*tmp; */

/*   for (tmp = entries; tmp ; tmp = g_list_next(tmp)) { */
/*     GnlCompositionEntry	*entry = (GnlCompositionEntry *) tmp->data; */
/*     gnl_object_set_active (GNL_OBJECT (entry->object), FALSE); */
/*   } */
/* } */

/* void */
/* gnl_composition_activate_childs (GList *childs) */
/* { */
/*   GList	*tmp, *next; */

/*   GST_INFO ("Activating childs"); */
/*   for (next = NULL, tmp = childs; tmp ; tmp = next) { */
/*     next = g_list_next (tmp); */
/*     gnl_object_set_active (GNL_OBJECT (tmp->data), TRUE); */
/*   } */
/* } */

/* /\* */
/*   gnl_composition_schedule_entries NEW_VERSION */

/*   comp : the composition whose entries to schedule */
/*   start, stop : the start and stop time of what to schedule */
/*   minprio : the minimum priority to schedule */
/*   *pad : the output pad */

/*   Returns TRUE if the entries are scheduled and the pad is set */
/*   Only schedules the next entry. If no entries left, reset comp->next_stop */
/* *\/ */

/* static gboolean */
/* gnl_composition_schedule_entries(GnlComposition *comp, GstClockTime start,  */
/* 				 GstClockTime stop, gint minprio, GstPad **pad) */
/* { */
/*   gboolean res = TRUE; */
/*   GnlObject	*obj, *tmp = NULL; */
/*   GnlObject	*keep = NULL; */
/*   GList		*list; */
/*   GnlCompositionEntry	*compentry; */

/*   GST_INFO("%s [%lld]->[%lld]  minprio[%d]", */
/* 	   gst_element_get_name(GST_ELEMENT(comp)), */
/* 	   start, stop, minprio); */
/*   g_assert (start < stop); */
/*   /\* Find the object to schedule with a suitable priority *\/ */
/*   compentry = gnl_composition_find_entry_priority(comp, start, GNL_FIND_AT, minprio); */

/*   if (!compentry) */
/*     return FALSE; */

/*   obj = compentry->object; */

/*   /\*  */
/*      Find the following object */
     
/*      Doesn't handle the GnlOperation correctly  */
/*      The trick is to use the mininum priority to find the next stop for */
/*        GnlOperation's input(s). */
/*   *\/ */

/*   for ( list = comp->objects; list; list = g_list_next(list)) { */
/*     tmp = (GnlObject *) ((GnlCompositionEntry *) list->data)->object; */

/*     if (tmp == obj) */
/*       continue; */
    
/*     if (tmp->priority >= minprio) { /\* Don't take objects less important than minprio*\/ */

/*       if (tmp->start >= obj->stop) { /\* There is a gap before the next object *\/ */
/* 	GST_INFO("Gap before next object"); */
/* 	break; */
/*       } */

/*       /\* fact : tmp->start < obj->stop *\/ */
/*       GST_INFO ("Testing [%20s] against [%20s] [%" GST_TIME_FORMAT "]->[%" GST_TIME_FORMAT "] prio[%d]", */
/* 		gst_element_get_name (GST_ELEMENT (obj)), */
/* 		gst_element_get_name (GST_ELEMENT (tmp)), */
/* 		GST_TIME_ARGS (tmp->start), */
/* 		GST_TIME_ARGS (tmp->stop), */
/* 		tmp->priority); */
/*       if (((tmp->priority < obj->priority) && (tmp->stop > start)) */
/* 	  || */
/* 	  ((tmp->priority > obj->priority) && (tmp->stop >= obj->stop))) { */
/* 	/\* There isn't any gap *\/ */
/* 	GST_INFO("Obj - Tmp : %d || No gap, it's ok",  */
/* 		 obj->priority - tmp->priority); */
/* 	if (!keep) */
/* 	  keep = tmp; */
/* 	else if (tmp->priority < keep->priority) */
/* 	  keep = tmp; */
/*       } */

/*     } */
/*   } */

/*   if (keep) { */

/*     GST_INFO("next[%s]keep[%s] [%lld]->[%lld]", */
/* 	     gst_element_get_name (GST_ELEMENT (obj)), */
/* 	     gst_element_get_name(GST_ELEMENT(keep)), */
/* 	     keep->start, keep->stop); */
/*     if (keep->priority > obj->priority) */
/*       stop = obj->stop; */
/*     else */
/*       stop = MIN(keep->start, stop); */
/*   } else { */
/*     stop = MIN(obj->stop, stop); */
/*   } */

/*   comp->next_stop = MIN(comp->next_stop, stop); */

/*   GST_INFO("next_stop [%lld]", comp->next_stop); */
  
/*   if (GNL_IS_OPERATION(obj)) */
/*     res = gnl_composition_schedule_operation(comp, GNL_OPERATION(obj),  */
/* 					     start, comp->next_stop, pad); */
/*   else */
/*     res = gnl_composition_schedule_object(comp, obj, start, comp->next_stop, pad); */
 
/*   return res; */
/* } */

/* static gboolean */
/* probe_fired (GstProbe *probe, GstData **data, gpointer user_data) */
/* { */
/*   GnlComposition *comp = GNL_COMPOSITION (user_data); */
/*   gboolean res = TRUE; */
  
/*   if (GST_IS_BUFFER (*data)) { */
/*     GST_INFO ("Got a buffer, updating current_time"); */
/*     GNL_OBJECT (comp)->current_time = GST_BUFFER_TIMESTAMP (*data); */
/*   } */
/*   else { */
/*     GST_INFO ("Got an Event : %d", */
/* 	      GST_EVENT_TYPE (*data)); */
/*     if (GST_EVENT_TYPE (*data) == GST_EVENT_EOS) { */
/*       GST_INFO ("Got EOS, current_time is now previous stop", */
/* 		gst_element_get_name (GST_ELEMENT (comp))); */
/*       GNL_OBJECT (comp)->current_time = comp->next_stop; */
/*     } else if (GST_EVENT_TYPE (*data) == GST_EVENT_DISCONTINUOUS) */
/*       if (!gst_event_discont_get_value (GST_EVENT(*data), GST_FORMAT_TIME, (gint64 *) &(GNL_OBJECT(comp)->current_time))) */
/* 	GST_WARNING ("Got discont, but couldn't get GST_TIME value..."); */
/*   } */
/*   GST_INFO("[Probe:%p] %s current_time [%lld] -> [%3lldH:%3lldm:%3llds:%3lld]",  */
/* 	   probe, */
/* 	   gst_element_get_name(GST_ELEMENT(comp)), */
/* 	   GNL_OBJECT (comp)->current_time, */
/* 	   GNL_OBJECT (comp)->current_time / (3600 * GST_SECOND), */
/* 	   GNL_OBJECT (comp)->current_time % (3600 * GST_SECOND) / (60 * GST_SECOND), */
/* 	   GNL_OBJECT (comp)->current_time % (60 * GST_SECOND) / GST_SECOND, */
/* 	   GNL_OBJECT (comp)->current_time % GST_SECOND / GST_MSECOND); */

/*   return res; */
/* } */

/* static gboolean */
/* gnl_composition_prepare (GnlObject *object, GstEvent *event) */
/* { */
/*   GnlComposition *comp = GNL_COMPOSITION (object); */
/*   gboolean res; */
/*   GstPad *pad = NULL; */
/*   GstPad *ghost; */
/*   GstClockTime	start_pos, stop_pos; */
/*   GstProbe *probe; */

/*   GST_INFO("BEGIN Object[%s] Event[%lld]->[%lld]", */
/* 	   gst_element_get_name(GST_ELEMENT(object)), */
/* 	   GST_EVENT_SEEK_OFFSET(event), */
/* 	   GST_EVENT_SEEK_ENDOFFSET(event)); */

/*   if (gst_element_get_state (GST_ELEMENT (comp)) != GST_STATE_PAUSED) { */
/*     GST_WARNING ("%s: Prepare while not in PAUSED", */
/* 		 gst_element_get_name (GST_ELEMENT (comp))); */
/*     return FALSE; */
/*   } */
/*   start_pos = GST_EVENT_SEEK_OFFSET (event); */
/*   stop_pos  = GST_EVENT_SEEK_ENDOFFSET (event); */
/*   comp->next_stop  = stop_pos; */
  
/*   ghost = gst_element_get_pad (GST_ELEMENT (comp), "src"); */
/*   if (ghost) {     */
/*     GST_INFO("Existing ghost pad and probe, removing"); */
/*     /\* Remove the GstProbe attached to this pad before deleting it *\/ */
/*     probe = gst_pad_get_element_private(ghost); */
/*     gst_pad_remove_probe(GST_PAD (ghost), probe); */
/*     gst_probe_destroy (probe); */
/*     gst_element_remove_pad (GST_ELEMENT (comp), ghost); */
/*     if (gst_element_get_pad (GST_ELEMENT (comp), "src")) */
/*       g_error ("We removed the ghost pad from the composition and it's still there !!!!"); */
/*   } */

/*   gnl_composition_deactivate_childs (comp->active_objects); */
/*   comp->active_objects = NULL; */

/*   /\* Do a first run to establish what the real stop_pos is *\/ */
/*   GST_INFO ("Doing first run(s) to establish what the real stop time is"); */
/*   gnl_composition_schedule_entries (comp, start_pos, stop_pos, 0, NULL); */
/*   while (stop_pos != comp->next_stop) { */
/*     stop_pos = comp->next_stop; */
/*     gnl_composition_schedule_entries (comp, start_pos, stop_pos, 0, NULL); */
/*   } */

/*   /\* Scbedule the entries from start_pos *\/ */
  
/*   GST_INFO ("%s : Got the real time, now REALLY scheduling", */
/* 	    gst_element_get_name (GST_ELEMENT (comp))); */
/*   res = gnl_composition_schedule_entries (comp, start_pos, */
/* 					  comp->next_stop, 0, &pad); */
/*   GST_INFO ("%s : Finished really scheduling", */
/* 	    gst_element_get_name (GST_ELEMENT (comp))); */
/*   if (!res) { */
/*     GST_ERROR ("Something went awfully wrong while preparing %s", */
/* 	       gst_element_get_name (GST_ELEMENT (comp))); */
/*     return FALSE; */
/*   } */
/*   if (pad) { */

/*     GST_INFO("%s : Have a pad", */
/* 	     gst_element_get_name(GST_ELEMENT(object))); */

/*     if (GST_PAD_IS_LINKED(pad)) { */
/*       GST_WARNING ("pad %s:%s returned by scheduling is connected to %s:%s", */
/* 		   GST_DEBUG_PAD_NAME(pad), */
/* 		   GST_DEBUG_PAD_NAME(GST_PAD_PEER(pad))); */
/*       gst_pad_unlink (pad, GST_PAD_PEER (pad)); */
/*     } */
    

/*     GST_INFO ("Putting probe and ghost pad back"); */
/*     probe = gst_probe_new (FALSE, probe_fired, comp); */
/*     ghost = gst_element_add_ghost_pad (GST_ELEMENT (comp),  */
/* 				       pad, */
/* 				       "src"); */
/*     if (!ghost) { */
/*       GST_WARNING ("Wasn't able to create ghost src pad for composition %s, there is already [%s:%s]", */
/* 		   gst_element_get_name (GST_ELEMENT (comp)), */
/* 		   GST_DEBUG_PAD_NAME (gst_element_get_pad (GST_ELEMENT (comp), "src"))); */
/* /\*       res = FALSE; *\/ */
/*     } else { */
/*       gst_pad_set_element_private(ghost, (gpointer) probe); */
/*       gst_pad_add_probe (GST_PAD (ghost), probe); */
/*       GST_INFO ("Ghost src pad and probe created"); */
/*     } */
/*   } */
/*   else { */
/*     GST_WARNING("Haven't got a pad :("); */
/*     res = FALSE; */
/*   } */

/*   GST_INFO ( "END %s: configured",  */
/* 	     gst_element_get_name (GST_ELEMENT (comp))); */

/*   return res; */
/* } */


/* static GstClockTime */
/* gnl_composition_nearest_cover_func (GnlComposition *comp, GstClockTime time, GnlDirection direction) */
/* { */
/*   GList			*objects = comp->objects; */
/*   GstClockTime start; */
  
/*   GST_INFO("Object:%s , Time[%lld], Direction:%d", */
/* 	   gst_element_get_name(GST_ELEMENT(comp)), */
/* 	   time, direction); */
  
/*   if (direction == GNL_DIRECTION_BACKWARD) { */
/*     GnlCompositionEntry	*entry; */
/*     GnlObject	*endobject = NULL; */
    
/*     /\*  */
/*        Look for the last object whose stop is < time */
/*        return the stop time for that object */
/*     *\/ */
/*     for (objects = g_list_last(comp->objects); objects; objects = objects->prev) { */
/*       entry = (GnlCompositionEntry *) (objects->data); */

/*       if (entry->object->priority == G_MAXINT) */
/* 	continue ; */
/*       if (endobject) { */
/* 	if (entry->object->stop < endobject->start) */
/* 	  break; */
/* 	if (entry->object->stop > endobject->stop) */
/* 	  endobject = entry->object; */
/*       } else if (entry->object->stop < time) */
/* 	endobject = entry->object; */
/*       // if theres a endobject */
/*       //   if the object ends later than the endobject it becomes the endobject */
/*       //   if the object ends earlier than the endobject->start break ! */
/*       // else */
/*       //   if object->stop < time */
/*       //     it becomes the end object */
/*     } */
/*     if (endobject) { */
/*       GST_INFO("endobject [%lld]->[%lld]", */
/* 	       endobject->start, */
/* 	       endobject->stop); */
/*       return (endobject->stop); */
/*     } else */
/*       GST_INFO("no endobject"); */
/*   } else { */
/*     GnlCompositionEntry *entry; */
/*     GstClockTime	last = G_MAXINT64; */
/*     GST_INFO ("starting"); */
/*     while (objects) { */
/*       entry = (GnlCompositionEntry *) (objects->data); */

/*       if (entry->object->priority == G_MAXINT) { */
/* 	objects = g_list_next (objects); */
/* 	continue ; */
/*       } */
/*       start = entry->object->start; */
      
/*       GST_INFO("Object[%s] Start[%lld]", */
/* 	       gst_element_get_name(GST_ELEMENT(entry->object)), */
/* 	       start); */
      
/*       if (start >= time) { */
/* 	if (direction == GNL_DIRECTION_FORWARD) */
/* 	  return start; */
/* 	else */
/* 	  return last; */
/*       } */
/*       last = start; */
      
/*       objects = g_list_next (objects); */
/*     } */
/*   } */
  
/*   return GST_CLOCK_TIME_NONE; */
/* } */


/* static GstElementStateReturn */
/* gnl_composition_change_state (GstElement *element) */
/* { */
/*   GnlComposition *comp = GNL_COMPOSITION (element); */
/*   gint transition = GST_STATE_TRANSITION (comp); */
/*   GstElementStateReturn	res = GST_STATE_SUCCESS; */

/* /\*   if (GST_FLAG_IS_SET (element, GST_BIN_STATE_LOCKED)) { *\/ */
/* /\*     GST_WARNING ("Recursive call on %s", *\/ */
/* /\* 		 gst_element_get_name (element)); *\/ */
/* /\*     return GST_STATE_SUCCESS; *\/ */
/* /\*   } else *\/ */
/* /\*     GST_INFO ("Non-recursive call on %s", *\/ */
/* /\* 	      gst_element_get_name (element)); *\/ */

/*   if (transition != GST_STATE_PAUSED_TO_READY) */
/*     res = GST_ELEMENT_CLASS (parent_class)->change_state (element); */
  
/*   switch (transition) { */
/*   case GST_STATE_NULL_TO_READY: */
/*     //composition_update_start_stop(comp); */
/*     break; */
/*   case GST_STATE_READY_TO_PAUSED: */
/*     GST_INFO ( "%s: 1 ready->paused", gst_element_get_name (GST_ELEMENT (comp))); */
/*     gnl_composition_deactivate_entries (comp->objects); */
/*     break; */
/*   case GST_STATE_PAUSED_TO_PLAYING: */
/*     GST_INFO ( "%s: 1 paused->playing", gst_element_get_name (GST_ELEMENT (comp))); */
/*     break; */
/*   case GST_STATE_PLAYING_TO_PAUSED: */
/*     GST_INFO ( "%s: 1 playing->paused", gst_element_get_name (GST_ELEMENT (comp))); */
/*     gnl_composition_deactivate_childs (comp->active_objects); */
/*     comp->active_objects = NULL; */
/*     break; */
/*   case GST_STATE_PAUSED_TO_READY: */
/*     gnl_composition_activate_entries (comp->objects); */
/* /\*     gnl_composition_deactivate_childs (comp->active_objects); *\/ */
/*     /\* De-activate ghost pad *\/ */
/*     if (gst_element_get_pad (element, "src")) { */
/*       gst_pad_remove_probe (GST_PAD_REALIZE (gst_element_get_pad (element, "src")), */
/* 			    (GstProbe *) gst_pad_get_element_private (gst_element_get_pad (element, "src"))); */
/*       gst_element_remove_pad (element, gst_element_get_pad (element, "src")); */
/*     } */
/* /\*     comp->active_objects = NULL; *\/ */
/*     break; */
/*   default: */
/*     break; */
/*   } */
  
/*   GST_INFO ("Calling parent change_state method"); */
  
/*   if (transition == GST_STATE_PAUSED_TO_READY) */
/*     res = GST_ELEMENT_CLASS (parent_class)->change_state (element); */
/*   GST_INFO("%s : change_state returns %d", */
/* 	   gst_element_get_name(element), */
/* 	   res); */
/*   return res; */
/* } */

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

static void
object_start_changed	(GnlObject *object, GParamSpec *arg, GnlComposition *comp)
{
  GST_DEBUG_OBJECT (object, "...");

  comp->private->objects_start = g_list_sort (comp->private->objects_start, 
					      (GCompareFunc) objects_start_compare);
  update_start_stop_duration (comp);
}

static void
object_stop_changed	(GnlObject *object, GParamSpec *arg, GnlComposition *comp)
{
  GST_DEBUG_OBJECT (object, "...");
  
  comp->private->objects_stop = g_list_sort (comp->private->objects_stop, 
					     (GCompareFunc) objects_stop_compare);
  update_start_stop_duration (comp);
}

static void
object_priority_changed	(GnlObject *object, GParamSpec *arg, GnlComposition *comp)
{
  GST_DEBUG_OBJECT (object, "...");

  comp->private->objects_start = g_list_sort (comp->private->objects_start, 
					      (GCompareFunc) objects_start_compare);
  comp->private->objects_stop = g_list_sort (comp->private->objects_stop, 
					     (GCompareFunc) objects_stop_compare);
  update_start_stop_duration (comp);
}

static void
object_active_changed	(GnlObject *object, GParamSpec *arg, GnlComposition *comp)
{
  GST_DEBUG_OBJECT (object, "...");

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
  comp->private->objects_start = g_list_insert_sorted (comp->private->objects_start,
						       element,
						       (GCompareFunc) objects_start_compare);
  comp->private->objects_stop = g_list_insert_sorted (comp->private->objects_stop,
						      element,
						      (GCompareFunc) objects_stop_compare);

  /* check if we have to modify the current pipeline setup */

  /* update start/duration/stop */
  update_start_stop_duration (comp);

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
  comp->private->objects_start = g_list_remove (comp->private->objects_start, element);
  comp->private->objects_start = g_list_sort (comp->private->objects_start, 
					      (GCompareFunc) objects_start_compare);
  comp->private->objects_stop = g_list_remove (comp->private->objects_stop, element);
  comp->private->objects_stop = g_list_sort (comp->private->objects_stop, 
					     (GCompareFunc) objects_stop_compare);

  /* check if we have to modify the current pipeline setup */

  /* update start/duration/stop */
  update_start_stop_duration (comp);

 beach:
  gst_object_unref (element);
  g_static_mutex_unlock (comp->private->objects_lock);
  return ret;
}


