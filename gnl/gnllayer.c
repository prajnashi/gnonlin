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

static GnlLayerClass *parent_class = NULL;

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

  gobject_class = (GObjectClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_BIN);
}


static void
gnl_layer_init (GnlLayer *layer)
{
  layer->sources = NULL;
  layer->output = NULL;
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

  return new;
}

static gboolean
update_connection (GnlLayer *layer) 
{
  if (layer->sources) {
    GnlLayerEntry *entry = (GnlLayerEntry *) layer->sources->data;
    GstElement *source = GST_ELEMENT (entry->source);

    if (layer->output && source) {
      gst_element_connect (source, "src", layer->output, "sink");
    }
  }
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
  entry->source = source;
  
  layer->sources = g_list_insert_sorted (layer->sources, entry, _entry_compare_func);

  gst_bin_add (GST_BIN (layer), GST_ELEMENT (source));

  update_connection (layer);
}

void
gnl_layer_set_output (GnlLayer *layer, GstElement *output)
{
  g_return_if_fail (layer != NULL);
  g_return_if_fail (GNL_IS_LAYER (layer));
  g_return_if_fail (output != NULL);
  g_return_if_fail (GST_IS_ELEMENT (output));

  layer->output = output;
  gst_bin_add (GST_BIN (layer), output);

  update_connection (layer);
}


