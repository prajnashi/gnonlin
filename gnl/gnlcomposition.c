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



#include "gnlcomposition.h"

static void 		gnl_composition_class_init 		(GnlCompositionClass *klass);
static void 		gnl_composition_init 			(GnlComposition *composition);

static guint64 		gnl_composition_next_change 		(GnlLayer *layer, guint64 time);
static gboolean 	gnl_composition_prepare_cut 		(GnlLayer *layer, guint64 start, guint64 stop,
								 GnlLayerCutDoneCallback func, gpointer user_data);
static gboolean 	gnl_composition_occupies_time 		(GnlLayer *layer, guint64 time);

static GstElementStateReturn
			gnl_composition_change_state 		(GstElement *element);

static GnlLayerClass *parent_class = NULL;

GType
gnl_composition_get_type (void)
{
  static GType composition_type = 0;

  if (!composition_type) {
    static const GTypeInfo composition_info = {
      sizeof (GnlCompositionClass),
      NULL,
      NULL,
      (GClassInitFunc) gnl_composition_class_init,
      NULL,
      NULL,
      sizeof (GnlComposition),
      32,
      (GInstanceInitFunc) gnl_composition_init,
    };
    composition_type = g_type_register_static (GNL_TYPE_LAYER, "GnlComposition", &composition_info, 0);
  }
  return composition_type;
}

static void
gnl_composition_class_init (GnlCompositionClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;
  GnlLayerClass *gnllayer_class;

  gobject_class =       (GObjectClass*)klass;
  gstelement_class =    (GstElementClass*)klass;
  gstbin_class =    	(GstBinClass*)klass;
  gnllayer_class =    	(GnlLayerClass*)klass;

  parent_class = g_type_class_ref (GNL_TYPE_LAYER);

  gstelement_class->change_state 	= gnl_composition_change_state;
  gnllayer_class->next_change 		= gnl_composition_next_change;
  gnllayer_class->prepare_cut 		= gnl_composition_prepare_cut;
  gnllayer_class->occupies_time  	= gnl_composition_occupies_time;

}

static void
gnl_composition_init (GnlComposition *composition)
{
  composition->layers = NULL;
  composition->active = NULL;
  composition->ocurrent = NULL;
}

GnlComposition*
gnl_composition_new (const gchar *name)
{
  GnlComposition *new;

  g_return_val_if_fail (name != NULL, NULL);

  new = g_object_new (GNL_TYPE_COMPOSITION, NULL);
  gst_object_set_name (GST_OBJECT (new), name);

  return new;
}

void
gnl_composition_add_operation (GnlComposition *composition, 
		               GnlOperation *operation, guint64 start)
{
  gnl_layer_add_source (GNL_LAYER (composition), GNL_SOURCE (operation), start); 
}


void
gnl_composition_append_layer (GnlComposition *composition, GnlLayer *layer)
{
  g_return_if_fail (composition != NULL);
  g_return_if_fail (GNL_IS_COMPOSITION (composition));
  g_return_if_fail (layer != NULL);
  g_return_if_fail (GNL_IS_LAYER (layer));

  g_object_ref (G_OBJECT (layer));

  composition->layers = g_list_append (composition->layers, layer);
}

static guint64
gnl_composition_next_change (GnlLayer *layer, guint64 time)
{
  GnlComposition *composition = GNL_COMPOSITION (layer);
  GList *layers = composition->layers;
  guint64 res = G_MAXINT64;

  res = parent_class->next_change (layer, time);

  while (layers) {
    GnlLayer *clayer = GNL_LAYER (layers->data);
    gint64 lnext;

    lnext = gnl_layer_next_change (clayer, time);

    if (lnext != G_MAXINT64 && lnext < res) {
      res = lnext;
    }
    layers = g_list_next (layers);
  }

  return res;
}

static GnlLayer*
find_layer_for_time (GnlComposition *composition, guint64 time, guint index)
{
  GList *layers = composition->layers;

  while (layers) {
    GnlLayer *layer = GNL_LAYER (layers->data);

    if (gnl_layer_occupies_time (layer, time)) {
      if (!index--) {
        return layer;
      }
    }
    layers = g_list_next (layers);
  }

  return NULL;
}

static GnlOperation*
find_operation_for_time (GnlComposition *composition, guint64 time)
{
  GnlSource *source;
  
  source = gnl_layer_get_source_for_time (GNL_LAYER (composition), time);
  if (source)
    return GNL_OPERATION (source);

  return NULL;
}

static gboolean
gnl_composition_occupies_time (GnlLayer *layer, guint64 time)
{ 
  if (find_operation_for_time (GNL_COMPOSITION (layer), time))
    return TRUE;
  if (find_layer_for_time (GNL_COMPOSITION (layer), time, 0))
    return TRUE;

  return FALSE;
} 


static gboolean
is_layer_active (GnlComposition *composition, GnlLayer *layer)
{
  if (g_list_index (composition->active, layer) != -1)
    return TRUE;

  return FALSE;
}

static gboolean
gnl_composition_prepare_cut (GnlLayer *layer, GstClockTime start, GstClockTime stop,
			     GnlLayerCutDoneCallback func, gpointer user_data)
{
  GnlOperation *operation;
  guint num_layers = 0;
  guint lindex;
  guint64 next_change = stop;
  GList *to_schedule = NULL, *walk;
  GstPad *ghostpad = NULL, *oldghostpad = NULL;
  GnlComposition *composition = GNL_COMPOSITION (layer);
  gboolean res = TRUE;
	          
  operation = find_operation_for_time (composition, start);

  /* first find out if we have an operation */
  if (operation) {
    g_print ("composition %s operation for %lld %p\n",
	     GST_ELEMENT_NAME (composition), start, operation);

    /* we have to combine N layers */
    num_layers = gnl_operation_get_num_sinks (operation);
    if (operation != composition->ocurrent) {
      /* add it to the bin */
      g_print ("composition %s adding new operation %s for %lld %p\n",
	     GST_ELEMENT_NAME (composition), GST_ELEMENT_NAME (operation), start, operation);
      gst_bin_add (GST_BIN (composition), GST_ELEMENT (operation));
      ghostpad = gst_element_get_pad (GST_ELEMENT (operation), "internal_src");
      g_print ("target ghost pad %p\n", ghostpad);

      composition->ocurrent = operation;
    }
  }
  else {
    num_layers = 1;
    if (composition->ocurrent) {
      GstPad *srcpad, *peerpad;

      g_print ("composition %s removing old operation %s for %lld %p\n",
	     GST_ELEMENT_NAME (composition), GST_ELEMENT_NAME (composition->ocurrent), start, operation);

      srcpad = gst_element_get_pad (GST_ELEMENT (composition->ocurrent), "internal_src");
      peerpad = GST_PAD_PEER (srcpad);
      if (peerpad)
        gst_pad_disconnect (srcpad, peerpad);

      gst_bin_remove (GST_BIN (composition), GST_ELEMENT (composition->ocurrent));
      composition->ocurrent = NULL;
    }
  }

  g_print ("composition %s combining %d layers\n", GST_ELEMENT_NAME (composition), num_layers);

  /* collect the layers we need to show */
  for (lindex = 0; lindex < num_layers; lindex++) {
    GnlLayer *layer;

    layer = find_layer_for_time (composition, start, lindex);
    if (!layer) {
      if (composition->ocurrent) {
        g_warning ("not enough layers for operation");
      }
      g_print ("composition %s done\n", GST_ELEMENT_NAME (composition));
      gst_element_set_state (GST_ELEMENT (composition), GST_STATE_PAUSED);
      res = FALSE;
      goto done;
    }
    g_print ("composition %s need to schedule %lld %p (%s) %d\n", 
	     GST_ELEMENT_NAME (composition), start, layer, GST_ELEMENT_NAME (layer),
	     lindex);

    to_schedule = g_list_prepend (to_schedule, layer);
    
    next_change = MIN (next_change, gnl_layer_next_change (GNL_LAYER (composition), start+1));
  }

  /* scheduler the layers */
  walk = to_schedule = g_list_reverse (to_schedule);
  lindex = 0;

  while (walk) {
    GnlLayer *layer = GNL_LAYER (walk->data);
    walk = g_list_next (walk);

    g_print ("composition %s scheduling %lld %p (%s) %lld %lld\n", 
	     GST_ELEMENT_NAME (composition), start, layer, GST_ELEMENT_NAME (layer),
	     next_change,
	     gnl_layer_next_change (GNL_LAYER (layer), start+1));

    if (is_layer_active (composition, layer)) {
      GstPad *srcpad, *peerpad;

      g_print ("composition %s reusing layer %s\n", GST_ELEMENT_NAME (composition), GST_ELEMENT_NAME (layer));

      composition->active = g_list_remove (composition->active, layer);

      srcpad = gst_element_get_pad (GST_ELEMENT (layer), "internal_src");
      peerpad = GST_PAD_PEER (srcpad);
      if (peerpad)
        gst_pad_disconnect (srcpad, peerpad);

    }
    else {
      g_print ("composition %s new layer %s\n", GST_ELEMENT_NAME (composition), GST_ELEMENT_NAME (layer));

      gst_bin_add (GST_BIN (composition), GST_ELEMENT (layer));
    }
    gst_element_set_state (GST_ELEMENT (layer), GST_STATE_PAUSED);
    gnl_layer_prepare_cut (layer, start, next_change, func, user_data);
    
    if (composition->ocurrent) {
      gst_element_connect (GST_ELEMENT (layer), "internal_src", GST_ELEMENT (composition->ocurrent), g_strdup_printf ("sink%d", lindex));
    }
    else {
      ghostpad = gst_element_get_pad (GST_ELEMENT (layer), "internal_src");
    }

    lindex++;
  }

  if (!lindex) {
    res = FALSE;
    g_print ("composition %s somthing wrong %d\n", GST_ELEMENT_NAME (composition), lindex);
    goto done;
  }

  oldghostpad = gst_element_get_pad (GST_ELEMENT (composition), "internal_src");
  if (oldghostpad) {
    gst_element_remove_ghost_pad (GST_ELEMENT (composition), oldghostpad);
  }
  gst_element_add_ghost_pad (GST_ELEMENT (composition), ghostpad, "internal_src");

done:
  walk = composition->active;
  while (walk) {
    GnlLayer *layer = GNL_LAYER (walk->data);

    g_print ("%s removing old layer %s\n", GST_ELEMENT_NAME (composition), GST_ELEMENT_NAME (layer));
    gst_bin_remove (GST_BIN (composition), GST_ELEMENT (layer));

    walk = g_list_next (walk);
  }
  g_list_free (composition->active);
  composition->active = to_schedule;

  return res;
} 

static GstElementStateReturn
gnl_composition_change_state (GstElement *element)
{
  GnlComposition *composition = GNL_COMPOSITION (element);
 
  switch (GST_STATE_TRANSITION (composition)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
    case GST_STATE_READY_TO_NULL:
    default:
      break;
  }

  GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}


