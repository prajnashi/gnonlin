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

#include "gnl/gnllayer.h"

static void 		gnl_layer_class_init 		(GnlLayerClass *klass);
static void 		gnl_layer_init 			(GnlLayer *layer);

static gboolean		gnl_layer_prepare_cut_func 	(GnlLayer *layer, guint64 start, guint64 stop,
							 GnlLayerCutDoneCallback func, gpointer user_data);

static guint64 		gnl_layer_next_change_func	(GnlLayer *layer, guint64 time);
static gboolean 	gnl_layer_occupies_time_func 	(GnlLayer *layer, guint64 time);
static void 		gnl_layer_child_done 		(GnlSource *child, GstClockTime time, gpointer user_data);

static GstElementStateReturn
			gnl_layer_change_state 		(GstElement *element);
	
static GstBinClass *parent_class = NULL;

void			gnl_layer_show	 		(GnlLayer *layer);

#define CLASS(layer)  GNL_LAYER_CLASS (G_OBJECT_GET_CLASS (layer))


typedef struct
{
  GnlSource *source;
  guint64 start;
} GnlLayerEntry;

GType
gnl_layer_get_type (void)
{
  static GType layer_type = 0;

  if (!layer_type) {
    static const GTypeInfo layer_info = {
      sizeof (GnlLayerClass),
      NULL,
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
gnl_layer_class_init (GnlLayerClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;
  gstbin_class = (GstBinClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_BIN);

  gstelement_class->change_state = gnl_layer_change_state;

  klass->next_change	= gnl_layer_next_change_func;
  klass->occupies_time	= gnl_layer_occupies_time_func;
  klass->prepare_cut	= gnl_layer_prepare_cut_func;
}


static void
gnl_layer_init (GnlLayer *layer)
{
  layer->sources = NULL;
}

static guint64
gnl_layer_next_change_func (GnlLayer *layer, guint64 time)
{
  GList *sources = layer->sources;

  while (sources) {
    GnlLayerEntry *entry = (GnlLayerEntry *) sources->data;

    if (entry->start >= time)
      return entry->start;

    if (entry->start + (entry->source->stop - entry->source->start) >= time)
      return entry->start + entry->source->stop - entry->source->start;

    sources = g_list_next (sources);
  }

  return G_MAXINT64;
}

static GnlLayerEntry*
find_entry_for_time (GnlLayer *layer, guint64 time)
{
  GList *sources = layer->sources;

  while (sources) {
    GnlLayerEntry *entry = (GnlLayerEntry *) (sources->data);

    if (entry->start <= time &&
        entry->start + (entry->source->stop - entry->source->start) > time)
      return entry;

    sources = g_list_next (sources);
  }

  return NULL;
}

GnlSource*
gnl_layer_get_source_for_time (GnlLayer *layer, guint64 time)
{
  GnlLayerEntry* entry;

  entry = find_entry_for_time (layer, time);
  if (entry) {
    return entry->source;
  }
  return NULL;
}

static gboolean
gnl_layer_occupies_time_func (GnlLayer *layer, guint64 time)
{
  return (find_entry_for_time (layer, time) != NULL);
}

guint64
gnl_layer_next_change (GnlLayer *layer, guint64 time)
{
  if (CLASS (layer)->next_change)
    return CLASS (layer)->next_change (layer, time);

  return 0;
}

gboolean
gnl_layer_occupies_time (GnlLayer *layer, guint64 time)
{
  if (CLASS (layer)->occupies_time)
    return CLASS (layer)->occupies_time (layer, time);

  return FALSE;
}

gboolean
gnl_layer_prepare_cut (GnlLayer *layer, guint64 start, guint64 stop,
 		       GnlLayerCutDoneCallback func, gpointer user_data)
{
  if (CLASS (layer)->prepare_cut)
    return CLASS (layer)->prepare_cut (layer, start, stop, func, user_data);

  return FALSE;
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
gnl_layer_add_source (GnlLayer *layer, GnlSource *source, guint64 start)
{
  GnlLayerEntry *entry;

  g_return_if_fail (layer != NULL);
  g_return_if_fail (GNL_IS_LAYER (layer));
  g_return_if_fail (source != NULL);
  g_return_if_fail (GNL_IS_SOURCE (source));

  entry = g_malloc (sizeof (GnlLayerEntry));
  entry->start = start;
  entry->source = g_object_ref (G_OBJECT (source));
  
  layer->sources = g_list_insert_sorted (layer->sources, entry, _entry_compare_func);
}

static gboolean
gnl_layer_prepare_cut_func (GnlLayer *layer, guint64 start, guint64 stop,
 		       	    GnlLayerCutDoneCallback func, gpointer user_data)
{
  GnlLayerEntry *entry;

  layer->cut_done_func = func;
  layer->user_data = user_data;

  entry = find_entry_for_time (layer, start);
  if (entry) {
    GnlSource *source = entry->source;
    GstPad *pad;
    guint64 duration, real_start;

    if (layer->current != source) {
      pad = gst_element_get_pad (GST_ELEMENT (layer), "internal_src");
      if (pad) {
        gst_element_remove_ghost_pad (GST_ELEMENT (layer), pad);
      }
      if (layer->current) {
        gst_bin_remove (GST_BIN (layer), GST_ELEMENT (layer->current));
      }
      gst_bin_add (GST_BIN (layer), GST_ELEMENT (source));
      gst_element_set_state (GST_ELEMENT (source), GST_STATE_PAUSED);
      layer->current = source;

      gst_element_add_ghost_pad (GST_ELEMENT (layer), 
		    	       gst_element_get_pad (GST_ELEMENT (source), "internal_src"), 
			       "internal_src");
    }

    g_print ("%s scheduling source %lld %p\n", GST_ELEMENT_NAME (layer), start, source);  

    duration = MIN (stop - start, source->stop - source->start) - 1; 
    real_start = start - entry->start + source->start; 

    gnl_source_prepare_cut (source, real_start, real_start + duration, start, gnl_layer_child_done, layer);

    return TRUE;
  }

  return FALSE;
}

static void
gnl_layer_child_done (GnlSource *child, GstClockTime time, gpointer user_data)
{
  GnlLayer *layer = GNL_LAYER (user_data);

  g_print ("layer: %s child %s done at %lld\n", GST_ELEMENT_NAME (layer), GST_ELEMENT_NAME (child), time);

  layer->cut_done_func (layer, time, layer->user_data);

  return;
}

static GstElementStateReturn
gnl_layer_change_state (GstElement *element)
{
  GnlLayer *layer = GNL_LAYER (element);
  gint transition = GST_STATE_TRANSITION (layer);
  
  GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  return GST_STATE_SUCCESS;
}

void
gnl_layer_show (GnlLayer *layer)
{
  GList *sources = layer->sources;

  while (sources) {
    GnlLayerEntry *entry = (GnlLayerEntry *) sources->data;
    const GList *walk = gst_element_get_pad_list (GST_ELEMENT (entry->source));

    while (walk) {
      GstPad *pad = GST_PAD (walk->data);

      g_print ("%p %s %p %s\n", entry->source, gst_element_get_name (GST_ELEMENT (entry->source)), 
		      pad, GST_PAD_NAME (pad));

      walk = g_list_next (walk);
    }
    sources = g_list_next (sources);
  }
}

