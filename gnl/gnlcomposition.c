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
static gboolean 	gnl_composition_reschedule 		(GnlComposition *composition, GstClockTime time, gboolean change_state);

static GstElementStateReturn
			gnl_composition_change_state 		(GstElement *element);

static void 		child_state_change 			(GstElement *child, GstElementState oldstate, 
								 GstElementState newstate, gpointer user_data);

static GnlCompositionClass *parent_class = NULL;

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
  GnlLayerClass *gnllayer_class;

  gobject_class =       (GObjectClass*)klass;
  gstelement_class =    (GstElementClass*)klass;
  gnllayer_class =    (GnlLayerClass*)klass;

  parent_class = g_type_class_ref (GNL_TYPE_LAYER);

  gstelement_class->change_state 	= gnl_composition_change_state;
  gnllayer_class->next_change 		= gnl_composition_next_change;
}

static void
gnl_composition_init (GnlComposition *composition)
{
  composition->layers = NULL;
  composition->current = NULL;
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
find_layer_for_time (GnlComposition *composition, guint64 time)
{
  GList *layers = composition->layers;

  while (layers) {
    GnlLayer *layer = GNL_LAYER (layers->data);

    if (gnl_layer_occupies_time (layer, time)) {
      return layer;
    }
    layers = g_list_next (layers);
  }

  return NULL;
}

static gboolean
gnl_composition_reschedule (GnlComposition *composition, GstClockTime time, gboolean change_state)
{
  GnlLayer *layer;
  GnlTimer *timer = GNL_LAYER (composition)->timer;
	          
next:
  layer = find_layer_for_time (composition, time);
  if (layer) { 
    guint64 next_change = G_MAXINT64;
    GnlLayer *next_layer;

    next_change = gnl_layer_next_change (GNL_LAYER (composition), time+1),

    g_print ("%s schedule %lld %p (%s) %lld %lld\n", 
		    GST_ELEMENT_NAME (composition), time, layer, GST_ELEMENT_NAME (layer),
		    next_change,
		    gnl_layer_next_change (GNL_LAYER (layer), time+1));

    if (next_change != G_MAXINT64) {
      next_layer = find_layer_for_time (composition, next_change);
      if (next_layer) {
        g_print ("%s layer at next_change: %s\n", 
		    GST_ELEMENT_NAME (composition), 
                    GST_ELEMENT_NAME (next_layer));

	if (next_layer == layer) {
          next_change = G_MAXINT64;
	}
      }
    }

    if (change_state)
      gst_element_set_state (GST_ELEMENT (composition), GST_STATE_PAUSED);
	
    if (composition->current) {
      g_print ("%s we currently have %s\n", GST_ELEMENT_NAME (composition), GST_ELEMENT_NAME (composition->current));
      gst_element_disconnect (GST_ELEMENT (composition->current), "src", 
			GST_ELEMENT (timer), "sink");
      if (composition->current != layer) {
        g_print ("%s removing old layer %s\n", GST_ELEMENT_NAME (composition), GST_ELEMENT_NAME (composition->current));
        gst_bin_remove (GST_BIN (composition), GST_ELEMENT (composition->current));
        g_signal_handler_disconnect (G_OBJECT (composition->current), composition->handler);
      }
    }
    if (composition->current != layer) {
      g_print ("%s adding new layer %s\n", GST_ELEMENT_NAME (composition), GST_ELEMENT_NAME (layer));
      gst_bin_add (GST_BIN (composition), GST_ELEMENT (layer));
      composition->handler = g_signal_connect (G_OBJECT (layer), "state_change", G_CALLBACK (child_state_change), composition);
      composition->current = layer;
      gnl_layer_set_timer (layer, timer);
    }
    else {
      g_print ("%s reusing layer %s\n", GST_ELEMENT_NAME (composition), GST_ELEMENT_NAME (layer));
    }
    gst_element_set_state (GST_ELEMENT (layer), GST_STATE_PAUSED);
    gnl_layer_prepare_for (layer, time, next_change);

    gst_element_connect (GST_ELEMENT (layer), "src", GST_ELEMENT (timer), "sink");

    if (change_state)
      gst_element_set_state (GST_ELEMENT (composition), GST_STATE_PLAYING);
  }
  else {
    guint64 next_time = gnl_composition_next_change (GNL_LAYER (composition), time);
    if (next_time == G_MAXINT64) {
      g_print ("%s nothing more to do %lld %p %d\n", GST_ELEMENT_NAME (composition), time, layer, change_state);
    }
    else {
      g_warning ("%s gap detected, skipping..!! %lld %lld", GST_ELEMENT_NAME (composition), time, next_time);

      time = next_time;
      gnl_timer_set_time (timer, time);
      goto next;
    }
    return FALSE;
  }
  return TRUE;
} 

static void
child_state_change (GstElement *child, GstElementState oldstate, GstElementState newstate, gpointer user_data)
{
  GnlComposition *composition = GNL_COMPOSITION (user_data);
  guint64 time;
  GnlTimer *timer = GNL_LAYER (composition)->timer;

  switch (newstate) {
    case GST_STATE_PAUSED:
      if (oldstate != GST_STATE_PLAYING)
	return;
      gnl_timer_set_time (timer, gnl_timer_get_time (timer) + 1);
      time = gnl_timer_get_time (timer);
      g_print ("%s child state change %p %lld\n", GST_ELEMENT_NAME (composition), timer, time);

      if (!gnl_composition_reschedule (composition, time, TRUE)) {
	g_print ("setting EOS on %s\n", GST_ELEMENT_NAME (timer));

        timer->eos = TRUE;	
      }
      break;
    default:
      break;
  }
}

static GstElementStateReturn
gnl_composition_change_state (GstElement *element)
{
  GnlComposition *composition = GNL_COMPOSITION (element);
  guint64 time;
 
  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      time = gnl_timer_get_time (GNL_LAYER (composition)->timer);

      g_print ("%s to paused at %lld\n", GST_ELEMENT_NAME (composition), time);
      gnl_composition_reschedule (composition, time, FALSE);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      time = gnl_timer_get_time (GNL_LAYER (composition)->timer);
      g_print ("%s ended %lld\n", GST_ELEMENT_NAME (composition), time);
      break;
    case GST_STATE_PAUSED_TO_READY:
    case GST_STATE_READY_TO_NULL:
    default:
      break;
  }

  GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}


