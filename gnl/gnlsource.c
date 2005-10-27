/* Gnonlin
 * Copyright (C) <2001> Wim Taymans <wim.taymans@chello.be>
 *               <2004> Edward Hervey <bilboed@bilboed.com>
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
#include "gnlmarshal.h"

GST_DEBUG_CATEGORY_STATIC (gnlsource);
#define GST_CAT_DEFAULT gnlsource

GST_BOILERPLATE (GnlSource, gnl_source, GnlObject, GNL_TYPE_OBJECT);

static GstElementDetails gnl_source_details = GST_ELEMENT_DETAILS
(
  "GNonLin Source",
  "Filter/Editor",
  "Manages source elements",
  "Wim Taymans <wim.taymans@chello.be>, Edward Hervey <edward@fluendo.com>"
);

struct _GnlSourcePrivate {
  gboolean	dispose_has_run;
  GstPad	*ghostpad;
};

static gboolean
gnl_source_add_element	(GstBin *bin, GstElement *element);

static gboolean
gnl_source_remove_element	(GstBin *bin, GstElement *element);

static void 		gnl_source_dispose 		(GObject *object);
static void 		gnl_source_finalize 		(GObject *object);

static void		gnl_source_set_property 	(GObject *object, guint prop_id,
							 const GValue *value, GParamSpec *pspec);
static void		gnl_source_get_property 	(GObject *object, guint prop_id, GValue *value,
		                                         GParamSpec *pspec);
static gboolean		gnl_source_prepare		(GnlObject *object);

/* static GstElementStateReturn */
/* 			gnl_source_change_state 	(GstElement *element); */

static void
gnl_source_base_init (gpointer g_class)
{
  GstElementClass *gstclass = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstclass, &gnl_source_details);
}

static void
gnl_source_class_init (GnlSourceClass *klass)
{
  GObjectClass 		*gobject_class;
  GstElementClass 	*gstelement_class;
  GstBinClass		*gstbin_class;
  GnlObjectClass 	*gnlobject_class;

  gobject_class = 	(GObjectClass*)klass;
  gstelement_class = 	(GstElementClass*)klass;
  gstbin_class =	(GstBinClass*)klass;
  gnlobject_class = 	(GnlObjectClass*)klass;

  parent_class = g_type_class_ref (GNL_TYPE_OBJECT);

  GST_DEBUG_CATEGORY_INIT (gnlsource, "gnlsource", 0, "GNonLin Source Element");

  gstbin_class->add_element = GST_DEBUG_FUNCPTR (gnl_source_add_element);
  gstbin_class->remove_element = GST_DEBUG_FUNCPTR (gnl_source_remove_element);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gnl_source_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gnl_source_get_property);

/*   gnlobject_class->prepare = GST_DEBUG_FUNCPTR (gnl_source_prepare); */

  gobject_class->dispose      = GST_DEBUG_FUNCPTR (gnl_source_dispose);
  gobject_class->finalize     = GST_DEBUG_FUNCPTR (gnl_source_finalize);

/*   gstelement_class->change_state 	= gnl_source_change_state; */

}


static void
gnl_source_init (GnlSource *source, GnlSourceClass *klass)
{
  GST_OBJECT_FLAG_SET (source, GNL_OBJECT_SOURCE);
  source->element = NULL;
  source->private = g_new0(GnlSourcePrivate, 1);
}

static void
gnl_source_dispose (GObject *object)
{
  GnlSource *source = GNL_SOURCE (object);

  if (source->private->dispose_has_run)
    return;

  GST_INFO_OBJECT (object, "dispose");
  source->private->dispose_has_run = TRUE;
  
  G_OBJECT_CLASS (parent_class)->dispose (object);
  GST_INFO_OBJECT (object, "dispose END");
}

static void
gnl_source_finalize (GObject *object)
{
  GnlSource *source = GNL_SOURCE (object);

  GST_INFO_OBJECT (object, "finalize");
  g_free (source->private);
  
  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static gint
compare_src_pad (GstPad *pad, GstCaps *caps)
{
  gint ret;

  if (gst_pad_accept_caps (pad, caps))
    ret = 0;
  else {
    gst_object_unref (pad);
    ret = 1;
  }
  return ret;
}

/*
  get_valid_src_pad

  Returns True if there's a src pad compatible with the GnlObject caps in the
  given element. Fills in pad if so.
*/

static gboolean
get_valid_src_pad (GnlSource *source, GstElement *element, GstPad **pad)
{
  GstIterator	*srcpads;

  g_return_val_if_fail (pad, FALSE);

  srcpads = gst_element_iterate_src_pads (element);
  *pad = (GstPad*) gst_iterator_find_custom (srcpads,
     (GCompareFunc) compare_src_pad, GNL_OBJECT(source)->caps);
  gst_iterator_free (srcpads);

  if (*pad)
    return TRUE;
  return FALSE;
}

static void
no_more_pads_in_child (GstElement *element, GnlSource *source)
{
  GstPad	*pad;

  /* check if we can get a valid src pad to ghost */
  GST_DEBUG_OBJECT (element, "let's find a suitable pad");

  if (get_valid_src_pad (source, element, &pad)) {
    source->private->ghostpad = gnl_object_ghost_pad (GNL_OBJECT (source),
						      GST_PAD_NAME (pad),
						      pad);
  };

  if (!(source->private->ghostpad))
    GST_WARNING_OBJECT (source, "Couldn't get a valid source pad");
  
}

static gboolean
gnl_source_add_element	(GstBin *bin, GstElement *element)
{
  GnlSource	*source = GNL_SOURCE (bin);
  gboolean	pret;

  if (source->element) {
    GST_WARNING_OBJECT (bin, "GnlSource can only handle one element at a time");
    return FALSE;
  }
  
  /* call parent add_element */
  pret = GST_BIN_CLASS (parent_class)->add_element (bin, element);

  if (pret) {
    GstPad	*pad;

    source->element = element;
    gst_object_ref (element);

    /* need to get the src pad */
    if (get_valid_src_pad (source, element, &pad)) {
      GST_DEBUG_OBJECT (bin, "We have a valid src pad: %s:%s",
			GST_DEBUG_PAD_NAME (pad));
      source->private->ghostpad = gnl_object_ghost_pad (GNL_OBJECT (source),
							GST_PAD_NAME (pad),
							pad);
      if (!(source->private->ghostpad))
	return FALSE;
    } else {
      GST_DEBUG_OBJECT (bin, "no src pads available yet, connecting callback");
      /* we'll get the pad later */
      g_signal_connect (G_OBJECT (element), "no-more-pads",
			G_CALLBACK (no_more_pads_in_child), source);
    }
  }
  
  return pret;
}

static gboolean
gnl_source_remove_element	(GstBin *bin, GstElement *element)
{
  GnlSource	*source = GNL_SOURCE (bin);
  gboolean	pret;

  if ((!source->element) || (source->element != element)) {
    return FALSE;
  }


  /* try to remove it */
  pret = GST_BIN_CLASS (parent_class)->remove_element (bin, element);

  if (pret) {
    /* remove ghostpad */
    if (source->private->ghostpad) {
      gst_element_remove_pad (GST_ELEMENT (bin), source->private->ghostpad);
      source->private->ghostpad = NULL;
    }
    gst_object_unref (element);
    source->element = NULL;
  }
  return pret;
}

/* static gboolean */
/* gnl_source_prepare	(GnlObject *object) */
/* { */
/*   GnlSource	*source = GNL_SOURCE (object); */
/*   gboolean	ret = TRUE; */

/*   g_return_val_if_fail (source->element, FALSE); */

/*   /\* send initial seek *\/ */
/*   if ((!source->private->initial_seek) && (source->private->ghostpad)) { */
/*     GstEvent	*event; */

/*     GST_LOG_OBJECT (object, "Sending initial seek to ghostpad"); */
/*     event = gst_event_new_seek (1.0, GST_FORMAT_TIME, */
/* 				GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_SEGMENT, */
/* 				GST_SEEK_TYPE_SET, object->start, */
/* 				GST_SEEK_TYPE_SET, object->stop); */
/*     ret = gst_pad_send_event (source->private->ghostpad, event); */
/*     source->private->initial_seek = TRUE; */
/*   } */

/*   return ret; */
/* } */
/* /\**  */
/*  * gnl_source_new: */
/*  * @name: The name of the new #GnlSource */
/*  * @element: The element managed by this source */
/*  * */
/*  * Creates a new source object with the given name. The */
/*  * source will manage the given GstElement */
/*  * */
/*  * Returns: a new #GnlSource object or NULL in case of */
/*  * an error. */
/*  *\/ */
/* GnlSource* */
/* gnl_source_new (const gchar *name, GstElement *element) */
/* { */
/*   GnlSource *source; */

/*   g_return_val_if_fail (name != NULL, NULL); */
/*   g_return_val_if_fail (element != NULL, NULL); */

/*   source = g_object_new(GNL_TYPE_SOURCE, NULL); */
 
/*   gst_object_set_name(GST_OBJECT(source), name); */
/*   gnl_source_set_element(source, element); */

/*   GST_INFO("sched source[%p] bin[%p]",  */
/* 	   GST_ELEMENT_SCHED(source), */
/* 	   GST_ELEMENT_SCHED(source->bin)); */

/*   return source; */
/* } */

/* /\**  */
/*  * gnl_source_get_element: */
/*  * @source: The source element to get the element of */
/*  * */
/*  * Get the element managed by this source. */
/*  * */
/*  * Returns: The element managed by this source. */
/*  *\/ */
/* GstElement* */
/* gnl_source_get_element (GnlSource *source) */
/* { */
/*   g_return_val_if_fail (GNL_IS_SOURCE (source), NULL); */

/*   return source->element; */
/* } */

/* /\** */
/*  * gnl_source_set_element: */
/*  * @source: The source element to set the element on */
/*  * @element: The element that should be managed by the source */
/*  * */
/*  * Set the given element on the given source. If the source */
/*  * was managing another element, it will be removed first. */
/*  *\/ */
/* static void */
/* gnl_source_set_element (GnlSource *source, GstElement *element) */
/* { */
/*   gchar	*tmp; */

/*   g_return_if_fail (GNL_IS_SOURCE (source)); */
/*   g_return_if_fail (GST_IS_ELEMENT (element)); */

/*   if (source->element) { */
/*     gst_bin_remove (GST_BIN (source->bin), source->element); */
/*     gst_object_unref (GST_OBJECT (source->element)); */
/*   } */
  
/*   source->element = element; */
/*   source->linked_pads = 0; */
/*   source->total_pads = 0; */
/*   source->links = NULL; */
/*   if (source->pending_seek) { */
/*     gst_event_unref(source->pending_seek); */
/*     source->pending_seek = NULL; */
/*   } */
/*   source->private->seek_start = GST_CLOCK_TIME_NONE; */
/*   source->private->seek_stop = GST_CLOCK_TIME_NONE; */

/*   tmp = g_strdup_printf ("gnlsource_pipeline_%s", gst_element_get_name(element)); */
/*   gst_element_set_name (source->bin, tmp); */
/*   g_free (tmp); */

/*   gst_bin_add (GST_BIN (source->bin), source->element); */
/* } */

/* static GstStateChangeReturn */
/* gnl_source_change_state (GstElement *element, GstStateChange transition) */
/* { */
/*   GnlSource *source = GNL_SOURCE (element); */
/*   GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS; */
/* /\*   GstElementStateReturn	res = GST_STATE_SUCCESS; *\/ */
/* /\*   GstElementStateReturn	res2 = GST_STATE_SUCCESS; *\/ */

/*   switch (transition) { */
/*   case GST_STATE_CHANGE_NULL_TO_READY: */
/*   case GST_STATE_CHANGE_READY_TO_PAUSED: */
/*   case GST_STATE_CHANGE_PAUSED_TO_PLAYING: */
/*     break; */
/*   } */

/*   /\* Call parent change_state *\/ */



/*   GST_DEBUG ("Calling parent change_state"); */
/*   res2 = GST_ELEMENT_CLASS (parent_class)->change_state (element); */
/*   if (!res2) */
/*     return GST_STATE_FAILURE; */
/*   GST_DEBUG ("doing our own stuff %d", transition); */
/*   switch (transition) { */
/*   case GST_STATE_NULL_TO_READY: */
/*     break; */
/*   case GST_STATE_READY_TO_PAUSED: */
/*     if (!source_queue_media (source)) */
/*       res = GST_STATE_FAILURE; */
/*     break; */
/*   case GST_STATE_PAUSED_TO_PLAYING: */
/*     if (!GNL_OBJECT(source)->active) */
/*       GST_WARNING("Trying to change state but Source %s is not active ! This might be normal...", */
/* 		  gst_element_get_name(element)); */
/* /\*     else if (!gst_element_set_state (source->bin, GST_STATE_PLAYING)) *\/ */
/* /\*       res = GST_STATE_FAILURE; *\/ */
/*     break; */
/*   case GST_STATE_PLAYING_TO_PAUSED: */
/*     /\* done by GstBin->change_state *\/ */
/*     /\*     if (!gst_element_set_state (source->bin, GST_STATE_PAUSED)) *\/ */
/*     /\*       res = GST_STATE_FAILURE; *\/ */
/*     break; */
/*   case GST_STATE_PAUSED_TO_READY: */
/*     source->private->queued = FALSE; */
/*     source->queueing = FALSE; */
/*     break; */
/*   case GST_STATE_READY_TO_NULL: */
/*     break; */
/*   default: */
/*     GST_INFO ("TRANSITION NOT HANDLED ???"); */
/*     break; */
/*   } */
  
/*   if ((res != GST_STATE_SUCCESS) || (res2 != GST_STATE_SUCCESS)) { */
/*     GST_WARNING("%s : something went wrong", */
/* 		gst_element_get_name(element)); */
/*     return GST_STATE_FAILURE; */
/*   } */
/*   GST_INFO("%s : change_state returns %d/%d", */
/* 	   gst_element_get_name(element), */
/* 	   res, res2); */
/*   return res2; */
/* } */

static void
gnl_source_set_property (GObject *object, guint prop_id,
			 const GValue *value, GParamSpec *pspec)
{
  GnlSource *source;

  g_return_if_fail (GNL_IS_SOURCE (object));

  source = GNL_SOURCE (object);

  switch (prop_id) {
/*     case ARG_ELEMENT: */
/*       gnl_source_set_element (source, GST_ELEMENT (g_value_get_object (value))); */
/*       break; */
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gnl_source_get_property (GObject *object, guint prop_id,
			 GValue *value, GParamSpec *pspec)
{
  GnlSource *source;
  
  g_return_if_fail (GNL_IS_SOURCE (object));

  source = GNL_SOURCE (object);

  switch (prop_id) {
/*     case ARG_ELEMENT: */
/*       g_value_set_object (value, source->element); */
/*       break; */
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
