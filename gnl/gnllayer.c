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

#include "config.h"
#include "gnllayer.h"

static GstElementDetails gnl_layer_details = GST_ELEMENT_DETAILS (
  "GNL Layer",
  "Layer",
  "Combines GNL sources",
  "Wim Taymans <wim.taymans@chello.be>"
  );

static void 		gnl_layer_base_init 		(gpointer g_class);
static void 		gnl_layer_class_init 		(GnlLayerClass *klass, gpointer class_data);
static void 		gnl_layer_init 			(GnlLayer *layer);

static gboolean 	gnl_layer_send_event 		(GstElement *element, GstEvent *event);

static GstElementStateReturn
			gnl_layer_change_state 		(GstElement *element);
	
static GstBinClass *parent_class = NULL;

void			gnl_layer_show	 		(GnlLayer *layer);

#define CLASS(layer)  GNL_LAYER_CLASS (G_OBJECT_GET_CLASS (layer))

static GstClockTime 	gnl_layer_get_time_func 	(GnlLayer *layer);
static gboolean 	gnl_layer_covers_func		(GnlLayer *layer, GstClockTime start, GstClockTime stop,
							 GnlCoverType type);
static void 		gnl_layer_scheduling_paused_func (GnlLayer *layer);
static gboolean 	gnl_layer_prepare_func 		(GnlLayer *layer, GstClockTime start, GstClockTime stop);
static GstClockTime 	gnl_layer_nearest_cover_func 	(GnlLayer *layer, GstClockTime start, GnlDirection direction);

static void 		gnl_layer_self_state_change 	(GnlLayer *layer, 
		              				 GstElementState old, GstElementState new, 
			      				 GnlLayer *user_data);
/* XXX Get rid of me or move somewhere else */
gint64
gnl_time_to_seek_val (GstClockTime val)
{
  if (val >= G_MAXINT64) {
    return G_MAXINT64;
  } else {
    return val;
  }
}

struct _GnlLayerEntry
{
  GnlSource *source;
  const gchar *padname;
  GstClockTime start;
};

GType
gnl_layer_get_type (void)
{
  static GType layer_type = 0;

  if (!layer_type) {
    static const GTypeInfo layer_info = {
      sizeof (GnlLayerClass),
      (GBaseInitFunc) gnl_layer_base_init,
      NULL,
      (GClassInitFunc) gnl_layer_class_init,
      NULL,
      NULL,
      sizeof (GnlLayer),
      32,
      (GInstanceInitFunc) gnl_layer_init,
    };
    layer_type = g_type_register_static (GST_TYPE_BIN, "GnlLayer", &layer_info, 0);
  }
  return layer_type;
}

static void
gnl_layer_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gnl_layer_details);
}

static void
gnl_layer_class_init (GnlLayerClass *klass, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;
  gstbin_class = (GstBinClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_BIN);

  gstelement_class->change_state = gnl_layer_change_state;
  gstelement_class->send_event = gnl_layer_send_event;

  klass->get_time	 	= gnl_layer_get_time_func;
  klass->nearest_cover	 	= gnl_layer_nearest_cover_func;
  klass->covers		 	= gnl_layer_covers_func;
  klass->scheduling_paused 	= gnl_layer_scheduling_paused_func;
  klass->prepare 		= gnl_layer_prepare_func;
}


static void
gnl_layer_init (GnlLayer *layer)
{
  layer->sources = NULL;
  layer->start_pos = 0;
  layer->stop_pos = G_MAXUINT64;
  g_signal_connect (G_OBJECT (layer), "state_change", 
		    G_CALLBACK (gnl_layer_self_state_change), layer);

}

static GnlLayerEntry*
gnl_layer_find_entry (GnlLayer *layer, GstClockTime time, GnlFindMethod method)
{
  GList *sources = layer->sources;

  while (sources) {
    GnlLayerEntry *entry = (GnlLayerEntry *) (sources->data);

    switch (method) {
      case GNL_FIND_AT:
        if (entry->start <= time &&
            entry->start + (entry->source->stop - entry->source->start) > time)
	{
          return entry;
	}
	break;
      case GNL_FIND_AFTER:
        if (entry->start >= time)
          return entry;
	break;
      case GNL_FIND_START:
        if (entry->start == time)
          return entry;
	break;
      default:
	g_warning ("%s: unkown find method", gst_element_get_name (GST_ELEMENT (layer)));
	break;
    }
    sources = g_list_next (sources);
  }

  return NULL;
}

GnlSource*
gnl_layer_find_source (GnlLayer *layer, GstClockTime time, GnlFindMethod method)
{
  GnlLayerEntry *entry;

  entry = gnl_layer_find_entry (layer, time, method);
  if (entry) {
    return entry->source;
  }

  return NULL;
}

static gint 
_entry_compare_func (gconstpointer a, gconstpointer b)
{
  return (gint)(((GnlLayerEntry *)a)->start - ((GnlLayerEntry *)b)->start);
}


GnlLayer*
gnl_layer_new (const gchar *name)
{
  GnlLayer *layer;

  g_return_val_if_fail (name != NULL, NULL);

  layer = g_object_new (GNL_TYPE_LAYER, NULL);
  gst_object_set_name (GST_OBJECT (layer), name);

  return layer;
}

void
gnl_layer_add_source (GnlLayer *layer, GnlSource *source, const gchar *padname)
{
  GnlLayerEntry *entry;

  g_return_if_fail (GNL_IS_LAYER (layer));
  g_return_if_fail (GNL_IS_SOURCE (source));

  entry = g_malloc (sizeof (GnlLayerEntry));
  gnl_source_get_start_stop (source, &entry->start, NULL);
  entry->padname = padname;
  entry->source = g_object_ref (G_OBJECT (source));

  if (gst_element_get_pad (GST_ELEMENT (source), padname) == NULL) {
    gnl_source_get_pad_for_stream (source, padname);
  }
  
  layer->sources = g_list_insert_sorted (layer->sources, entry, _entry_compare_func);
}

static void
gnl_layer_ghost_child_state_change (GnlSource *child, 
		          GstElementState old, GstElementState new, 
		          GnlLayer *layer)
{
  if (old == GST_STATE_PLAYING && new == GST_STATE_PAUSED)
    gst_element_set_state (GST_ELEMENT (layer), GST_STATE_PAUSED);
}

static void
gnl_layer_schedule_entry (GnlLayer *layer, GnlLayerEntry *entry)
{
  GnlSource *source = entry->source;
  GstElement *source_element = GST_ELEMENT (source);
  GstClockTime source_start, source_stop, source_length;
  GstElementState state;
  GstPad *pad;

  state = gst_element_get_state (GST_ELEMENT (layer));

  g_assert (state <= GST_STATE_PAUSED);

  if (entry != layer->current) {
    if (gst_element_get_parent (source_element) == NULL) {
      gst_bin_add (GST_BIN (layer), source_element);
    }
    else {
      g_signal_connect (G_OBJECT (source_element), "state_change", 
		      G_CALLBACK (gnl_layer_ghost_child_state_change), layer);
    }

    if (layer->current && entry->source != layer->current->source) {
      GstPad *pad;

      pad = gst_element_get_pad (GST_ELEMENT (layer), "src");
      gst_element_remove_ghost_pad (GST_ELEMENT (layer), pad);

      g_print ("%s: removing old %s\n",
		    gst_element_get_name (GST_ELEMENT (layer)),
		    gst_element_get_name (GST_ELEMENT (layer->current->source)));

      if (gst_element_get_parent (source_element) == GST_OBJECT (layer)) {
        gst_bin_remove (GST_BIN (layer), GST_ELEMENT (layer->current->source));
      }
      else {
	g_signal_handlers_disconnect_by_func (G_OBJECT (layer->current->source),
		      G_CALLBACK (gnl_layer_ghost_child_state_change), layer);
      }
	      
    }
    layer->current = entry;
  }

  gnl_source_get_start_stop (source, &source_start, &source_stop);
  source_length = source_stop - source_start;
  
  if (layer->start_pos > entry->start || 
      layer->start_pos + source_length > layer->stop_pos) 
  {
    g_print ("%s: need to schedule partial src %lld %lld %lld %lld\n",
		    gst_element_get_name (GST_ELEMENT (layer)),
		    layer->start_pos, entry->start, layer->stop_pos, source_length);

    gst_element_send_event (GST_ELEMENT (source),
                              gst_event_new_segment_seek (
                                GST_FORMAT_TIME |
                                GST_SEEK_METHOD_SET |
			        GST_SEEK_FLAG_FLUSH |
			        GST_SEEK_FLAG_ACCURATE,
			        gnl_time_to_seek_val (layer->start_pos),
			        gnl_time_to_seek_val (layer->stop_pos)));

  }

  g_print ("%s: getting pad \"%s\" from %s\n", 
		    gst_element_get_name (GST_ELEMENT (layer)),
		    entry->padname, gst_element_get_name (source_element));

  pad = gst_element_get_pad (GST_ELEMENT (layer), "src");
  if (!pad) {
    gst_element_add_ghost_pad (GST_ELEMENT (layer), 
		      gst_element_get_pad (source_element, entry->padname), "src");
  }
}

static void 
gnl_layer_scheduling_paused_func (GnlLayer *layer)
{
  GnlLayerEntry *entry;

  entry = layer->current;
  if (entry) {
    GstClockTime time = gnl_source_get_time (entry->source);

    g_print ("%s: source %s changed state at time %lld\n", 
		  gst_element_get_name (GST_ELEMENT (layer)),
		  gst_element_get_name (GST_ELEMENT (entry->source)),
		  time);

    layer->start_pos = time;
  }
  else {
    g_warning ("%s: going to paused but nothing was being scheduled",
		    gst_element_get_name (GST_ELEMENT (layer)));
  }
}

static void
gnl_layer_self_state_change (GnlLayer *layer, 
		              GstElementState old, GstElementState new, 
			      GnlLayer *user_data)
{
  g_print ("%s: received state change %d %d\n",
		  gst_element_get_name (GST_ELEMENT (layer)),
		  old, new); 
  
  if (old == GST_STATE_PLAYING && new == GST_STATE_PAUSED) {
    CLASS (layer)->scheduling_paused (layer);
  }
}

static gboolean
gnl_layer_prepare_func (GnlLayer *layer, GstClockTime start, GstClockTime stop)
{
  GnlLayerEntry *entry;

  entry = gnl_layer_find_entry (layer, start, GNL_FIND_AT);
  if (entry) {
    GnlSource *source = entry->source;

    g_print ("%s: need to schedule source %s at time %lld, pad %s\n", 
		    gst_element_get_name (GST_ELEMENT (layer)), 
		    gst_element_get_name (GST_ELEMENT (source)), 
		    start,
		    entry->padname);

    gnl_layer_schedule_entry (layer, entry);

    g_print ("%s: configured\n", 
		    gst_element_get_name (GST_ELEMENT (layer)));

    return TRUE;
  }
  
  return FALSE;
}

static GstClockTime
gnl_layer_get_time_func (GnlLayer *layer)
{
  if (layer->current) {
    return (gnl_source_get_time (layer->current->source));
  }
  return layer->start_pos;
}

GstClockTime
gnl_layer_get_time (GnlLayer *layer)
{
  g_return_val_if_fail (GNL_IS_LAYER (layer), FALSE);

  if (CLASS (layer)->get_time)
    return CLASS (layer)->get_time (layer);

  return GST_CLOCK_TIME_NONE;
}

static gboolean
gnl_layer_covers_func (GnlLayer *layer, GstClockTime start, 
		       GstClockTime stop, GnlCoverType type)
{
  switch (type) {
    case GNL_COVER_ALL:
      g_warning ("layer covers all, implement me");
      break;
    case GNL_COVER_SOME:
      g_warning ("layer covers some, implement me");
      break;
    case GNL_COVER_START:
      if (gnl_layer_find_entry (layer, start, GNL_FIND_AT))
	return TRUE;
      break;
    case GNL_COVER_STOP:
      g_warning ("layer covers stop, implement me");
      break;
    default:
      break;
  }
  
  return FALSE;
}

gboolean
gnl_layer_covers (GnlLayer *layer, GstClockTime start, 
		  GstClockTime stop, GnlCoverType type)
{
  g_return_val_if_fail (GNL_IS_LAYER (layer), FALSE);

  if (CLASS (layer)->covers)
    return CLASS (layer)->covers (layer, start, stop, type);

  return FALSE;
}

static GstClockTime
gnl_layer_nearest_cover_func (GnlLayer *layer, GstClockTime start, GnlDirection direction)
{
  GList *sources = layer->sources;
  GstClockTime last = GST_CLOCK_TIME_NONE;

  while (sources) {
    GnlLayerEntry *entry = (GnlLayerEntry *) (sources->data);

    if (entry->start >= start) {
      if (direction == GNL_DIRECTION_FORWARD)
        return entry->start;
      else
        return last;
    }
    last = entry->start;
    
    sources = g_list_next (sources);
  }

  return GST_CLOCK_TIME_NONE;
}

GstClockTime
gnl_layer_nearest_cover (GnlLayer *layer, GstClockTime start, GnlDirection direction)
{
  g_return_val_if_fail (GNL_IS_LAYER (layer), FALSE);

  if (CLASS (layer)->nearest_cover)
    return CLASS (layer)->nearest_cover (layer, start, direction);

  return GST_CLOCK_TIME_NONE;
}

static gboolean
gnl_layer_send_event (GstElement *element, GstEvent *event)
{
  GnlLayer *layer = GNL_LAYER (element);
  GstElementState state;

  state = gst_element_get_state (element);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK_SEGMENT:
      layer->start_pos = GST_EVENT_SEEK_OFFSET (event);
      layer->stop_pos = GST_EVENT_SEEK_ENDOFFSET (event);
      g_print ("%s: received seek %lld %lld\n", 
		      gst_element_get_name (element),
		      layer->start_pos,
		      layer->stop_pos);

      CLASS (layer)->prepare (layer, layer->start_pos, layer->stop_pos);
      break;
    default:
      return FALSE;
  }
  
  return TRUE;
}

static GstElementStateReturn
gnl_layer_change_state (GstElement *element)
{
  GnlLayer *layer = GNL_LAYER (element);
  gint transition = GST_STATE_TRANSITION (layer);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      layer->start_pos = gnl_layer_nearest_cover (layer, 0, GNL_DIRECTION_FORWARD);
      layer->stop_pos = gnl_layer_nearest_cover (layer, G_MAXUINT64, GNL_DIRECTION_BACKWARD);
      break;
    case GST_STATE_READY_TO_PAUSED:
      g_print ("%s: 1 ready->paused\n", gst_element_get_name (GST_ELEMENT (layer)));
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      g_print ("%s: 1 paused->playing\n", gst_element_get_name (GST_ELEMENT (layer)));
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      g_print ("%s: 1 playing->paused\n", gst_element_get_name (GST_ELEMENT (layer)));
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element);
}

