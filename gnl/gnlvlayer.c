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

#include "gnlvlayer.h"

static void 		gnl_vlayer_class_init 		(GnlVLayerClass *klass);
static void 		gnl_vlayer_init 		(GnlVLayer *layer);

static void		gnl_vlayer_scheduling_paused 	(GnlLayer *layer);
static gboolean 	gnl_vlayer_prepare 		(GnlLayer *vlayer, GstClockTime start, GstClockTime stop);
static gboolean 	gnl_vlayer_covers 		(GnlLayer *layer, GstClockTime start,
	          					 GstClockTime stop, GnlCoverType type);
static GstClockTime 	gnl_vlayer_nearest_cover 	(GnlLayer *layer, GstClockTime start, GnlDirection direction);
static GstClockTime 	gnl_vlayer_get_time 		(GnlLayer *layer);

static GnlLayerClass *parent_class = NULL;

#define CLASS(vlayer)  GNL_VLAYER_CLASS (G_OBJECT_GET_CLASS (vlayer))

GType
gnl_vlayer_get_type (void)
{
  static GType vlayer_type = 0;

  if (!vlayer_type) {
    static const GTypeInfo vlayer_info = {
      sizeof (GnlVLayerClass),
      NULL,
      NULL,
      (GClassInitFunc) gnl_vlayer_class_init,
      NULL,
      NULL,
      sizeof (GnlVLayer),
      32,
      (GInstanceInitFunc) gnl_vlayer_init,
    };
    vlayer_type = g_type_register_static (GNL_TYPE_LAYER, "GnlVLayer", &vlayer_info, 0);
  }
  return vlayer_type;
}

static void
gnl_vlayer_class_init (GnlVLayerClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GnlLayerClass *gnllayer_class;

  gobject_class    = (GObjectClass*)    klass;
  gstelement_class = (GstElementClass*) klass;
  gnllayer_class   = (GnlLayerClass*)   klass;

  parent_class = g_type_class_ref (GNL_TYPE_LAYER);

  gnllayer_class->covers	 	= gnl_vlayer_covers;
  gnllayer_class->nearest_cover	 	= gnl_vlayer_nearest_cover;
  gnllayer_class->scheduling_paused 	= gnl_vlayer_scheduling_paused;
  gnllayer_class->prepare 		= gnl_vlayer_prepare;
  gnllayer_class->get_time 		= gnl_vlayer_get_time;
}


static void
gnl_vlayer_init (GnlVLayer *vlayer)
{
  vlayer->layers = NULL;
}

GnlVLayer*
gnl_vlayer_new (const gchar *name)
{
  GnlVLayer *vlayer;

  g_return_val_if_fail (name != NULL, NULL);

  vlayer = g_object_new (GNL_TYPE_VLAYER, NULL);
  gst_object_set_name (GST_OBJECT (vlayer), name);

  return vlayer;
}

void
gnl_vlayer_append_layer (GnlVLayer *vlayer, GnlLayer *layer)
{
  g_return_if_fail (GNL_IS_VLAYER (vlayer));
  g_return_if_fail (GNL_IS_LAYER (layer));

  gst_object_ref (GST_OBJECT (layer));

  vlayer->layers = g_list_append (vlayer->layers, layer);
}

static GstClockTime
gnl_vlayer_get_time (GnlLayer *layer)
{
  GnlVLayer *vlayer = GNL_VLAYER (layer);
  GnlLayer *current;

  current = vlayer->current;
  if (current) {
    return gnl_layer_get_time (current);
  }
  return layer->start_pos; 
}

static gboolean
gnl_vlayer_prepare (GnlLayer *layer, GstClockTime start, GstClockTime stop)
{
  GnlVLayer *vlayer = GNL_VLAYER (layer);
  GList *walk = vlayer->layers;
  GstClockTime next = GST_CLOCK_TIME_NONE;
  GnlLayer *sub_layer = NULL;
  gboolean res = FALSE;
  GstElementState state;

  state = gst_element_get_state (GST_ELEMENT (layer));

  g_assert (state >= GST_STATE_READY);

  g_print ("%s: vlayer prepare %lld %lld\n", 
		    gst_element_get_name (GST_ELEMENT (vlayer)),
		    start, stop);

  while (walk) {
    sub_layer = GNL_LAYER (walk->data);

    if (gnl_layer_covers (sub_layer, start, stop, GNL_COVER_START)) {
      GstPad *pad;
      
      g_print ("%s: layer %s covers %lld %lld\n", 
		    gst_element_get_name (GST_ELEMENT (vlayer)),
		    gst_element_get_name (GST_ELEMENT (sub_layer)),
		    start, stop);

      pad = gst_element_get_pad (GST_ELEMENT (vlayer), "src");
      if (pad) {
        gst_element_remove_ghost_pad (GST_ELEMENT (vlayer), pad);
      }
      if (vlayer->current != sub_layer) {
        g_print ("%s: adding child %s\n", 
		    gst_element_get_name (GST_ELEMENT (vlayer)),
		    gst_element_get_name (GST_ELEMENT (sub_layer)));

        gst_bin_add (GST_BIN (vlayer), GST_ELEMENT (sub_layer));
        //gst_element_set_state (GST_ELEMENT (sub_layer), GST_STATE_READY);
      }
      break;
    }
    else {
      next = MIN (gnl_layer_nearest_cover (sub_layer, start, GNL_DIRECTION_FORWARD), next);
    }
    
    walk = g_list_next (walk);
  }
  if (!walk) {
    sub_layer = NULL;
  }

  if (sub_layer) {
    g_print ("%s: scheduling child %s for %lld->%lld\n", 
		    gst_element_get_name (GST_ELEMENT (vlayer)),
		    gst_element_get_name (GST_ELEMENT (sub_layer)),
		    start, next);

    vlayer->event = gst_event_new_segment_seek (
                              GST_FORMAT_TIME |
                              GST_SEEK_METHOD_SET |
                              GST_SEEK_FLAG_FLUSH |
                              GST_SEEK_FLAG_ACCURATE,
                              gnl_time_to_seek_val (start),
                              gnl_time_to_seek_val (next));

    gst_element_send_event (GST_ELEMENT (sub_layer), vlayer->event);

    res = TRUE;

    g_print ("%s: setting child %s to %d\n", 
		    gst_element_get_name (GST_ELEMENT (vlayer)),
		    gst_element_get_name (GST_ELEMENT (sub_layer)),
		    state);

    //gst_element_set_state (GST_ELEMENT (sub_layer), state);
    gst_element_add_ghost_pad (GST_ELEMENT (vlayer), 
	      gst_element_get_pad (GST_ELEMENT (sub_layer), "src"), "src");
  }

  if (vlayer->current && sub_layer && vlayer->current != sub_layer) {
    gst_bin_remove (GST_BIN (vlayer), GST_ELEMENT (vlayer->current));
    //gst_element_set_state (GST_ELEMENT (vlayer->current), GST_STATE_READY);
  }
  vlayer->current = sub_layer;
  
  return res;
}

static gboolean
gnl_vlayer_covers (GnlLayer *layer, GstClockTime start,
	           GstClockTime stop, GnlCoverType type)
{
  GnlVLayer *vlayer = GNL_VLAYER (layer);
  GList *layers = vlayer->layers;

  switch (type) {
    case GNL_COVER_ALL:
      g_warning ("vlayer covers all, implement me");
      break;
    case GNL_COVER_SOME:
      g_warning ("vlayer covers some, implement me");
      break;
    case GNL_COVER_START:
    {
      gboolean sub_covers = FALSE;

      while (layers && !sub_covers) {
        GnlLayer *sub_layer = GNL_LAYER (layers->data);

        sub_covers |= gnl_layer_covers (sub_layer, start, stop, type);

        layers = g_list_next (layers);
      }
      return sub_covers;
    }
    case GNL_COVER_STOP:
      g_warning ("vlayer covers stop, implement me");
      break;
    default:
      break;
  }

  return FALSE;
}

static GstClockTime
gnl_vlayer_nearest_cover (GnlLayer *layer, GstClockTime start, GnlDirection direction)
{
  GnlVLayer *vlayer = GNL_VLAYER (layer);
  GList *layers = vlayer->layers;
  GstClockTime nearest = GST_CLOCK_TIME_NONE;

  while (layers) {
    GnlLayer *sub_layer = GNL_LAYER (layers->data);
    GstClockTime sub_nearest;

    sub_nearest = gnl_layer_nearest_cover (sub_layer, start, direction);

    nearest = MIN (nearest, sub_nearest);
    
    layers = g_list_next (layers);
  }
  
  return nearest;
}

static void
gnl_vlayer_scheduling_paused (GnlLayer *layer)
{
  GnlVLayer *vlayer = GNL_VLAYER (layer);
  GnlLayer *current;

  current = vlayer->current;

  if (current) {
    GstClockTime time = gnl_layer_get_time (current);

    g_print ("%s: child %s caused new paused state at time %lld\n", 
		    gst_element_get_name (GST_ELEMENT (vlayer)),
		    gst_element_get_name (GST_ELEMENT (current)),
		    time);

    layer->start_pos = time;
    /*
    if (layer->stop_pos > layer->start_pos)
      gnl_vlayer_prepare (layer, layer->start_pos, layer->stop_pos);
      */
  }
}


