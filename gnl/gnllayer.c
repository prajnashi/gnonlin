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

#include <gnl/gnllayer.h>

static void 		gnl_layer_class_init 		(GnlLayerClass *klass);
static void 		gnl_layer_init 			(GnlLayer *layer);

static void 		gnl_layer_set_clock 		(GstElement *element, GstClock *clock);

static GstElementStateReturn
			gnl_layer_change_state 		(GstElement *element);
	
static GstBinClass *parent_class = NULL;

static gboolean 	update_connection 		(GnlLayer *layer);

void			 gnl_layer_show 		(GnlLayer *layer);

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

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_BIN);

  gstelement_class->change_state = gnl_layer_change_state;
}


static void
gnl_layer_init (GnlLayer *layer)
{
  layer->sources = NULL;
  layer->timer = NULL;

  GST_ELEMENT (layer)->setclockfunc = gnl_layer_set_clock;
}

static void
gnl_layer_set_clock (GstElement *element, GstClock *clock)
{
  GnlLayer *layer = GNL_LAYER (element);

  //g_print ("layer got clock\n");

  layer->clock = clock;
}

static gint 
_entry_compare_func (gconstpointer a, gconstpointer b)
{
  return (gint)(((GnlLayerEntry *)a)->start - ((GnlLayerEntry *)b)->start);
}


GnlLayer*
gnl_layer_new (const gchar *name)
{
  GnlLayer *new;

  g_return_val_if_fail (name != NULL, NULL);

  new = g_object_new (GNL_TYPE_LAYER, NULL);
  gst_object_set_name (GST_OBJECT (new), name);

  new->timer = gnl_timer_new ();
  gst_object_set_name (GST_OBJECT (new->timer), "foo");

  gst_bin_add (GST_BIN (new), GST_ELEMENT (new->timer));
		  
  gst_element_add_ghost_pad (GST_ELEMENT (new), 
		  	     gst_element_get_pad (GST_ELEMENT (new->timer), "src"), 
			     "src");

  return new;
}

static void
source_ended (GnlTimer *timer, gpointer user_data) 
{
  GnlLayer *layer;

  layer = GNL_LAYER (user_data);

  g_print ("source ended\n");

  gst_element_set_state (GST_ELEMENT (layer), GST_STATE_PAUSED);

  gst_element_disconnect (GST_ELEMENT (layer->current), "src", GST_ELEMENT (layer->timer), "sink");
  gst_bin_remove (GST_BIN (layer), GST_ELEMENT (layer->current));
  gst_element_set_state (GST_ELEMENT (layer->current), GST_STATE_READY);

  if (!update_connection (layer)) {
    g_print ("EOS\n");
    layer->timer->eos = TRUE;
    //gst_element_set_eos (GST_ELEMENT (layer));
    return;
  }

  gst_element_set_state (GST_ELEMENT (layer), GST_STATE_PLAYING);
}

static gboolean
update_connection (GnlLayer *layer) 
{
  GList *walk = layer->sources;

  while (walk) {
    GnlLayerEntry *entry = (GnlLayerEntry *) walk->data;

    if (entry->start >= gnl_timer_get_time (layer->timer) + layer->base_time) {
      GnlSource *source = GNL_SOURCE (entry->source);

      layer->current = source;
      layer->base_time += entry->start;

      gst_bin_add (GST_BIN (layer), GST_ELEMENT (source));
      gst_element_set_state (GST_ELEMENT (source), GST_STATE_READY);

      gst_element_connect (GST_ELEMENT (source), "src", GST_ELEMENT (layer->timer), "sink");

      gnl_timer_notify_async (layer->timer, 
	 	      	      source->start, 
	 	      	      source->stop, 
			      source_ended, layer);

      return TRUE;
    }
    walk = g_list_next (walk);
  }

  return FALSE;
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

static GstElementStateReturn
gnl_layer_change_state (GstElement *element)
{
  GnlLayer *layer = GNL_LAYER (element);

  //g_print ("change layer to %d\n", GST_STATE_TRANSITION (element));

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      layer->base_time = 0LL;
      update_connection (layer);
      break;
    default:
      break;
  }

  GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

void
gnl_layer_show (GnlLayer *layer)
{
  GList *sources = layer->sources;

  while (sources) {
    GnlLayerEntry *entry = (GnlLayerEntry *) sources->data;
    GList *walk = gst_element_get_pad_list (GST_ELEMENT (entry->source));

    while (walk) {
      GstPad *pad = GST_PAD (walk->data);

      g_print ("%p %s %p %s\n", entry->source, gst_element_get_name (GST_ELEMENT (entry->source)), 
		      pad, GST_PAD_NAME (pad));

      walk = g_list_next (walk);
    }
    sources = g_list_next (sources);
  }
}

