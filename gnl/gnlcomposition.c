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

static gboolean 	gnl_composition_send_event 		(GstElement *element, GstEvent *event);

static GstElementStateReturn
			gnl_composition_change_state 		(GstElement *element);

static GnlVLayerClass *parent_class = NULL;

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
    composition_type = g_type_register_static (GNL_TYPE_VLAYER, "GnlComposition", &composition_info, 0);
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

  parent_class = g_type_class_ref (GNL_TYPE_VLAYER);

  gstelement_class->change_state 	= gnl_composition_change_state;
  gstelement_class->send_event	 	= gnl_composition_send_event;

}

static void
gnl_composition_init (GnlComposition *composition)
{
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
  gnl_layer_add_source (GNL_LAYER (composition), GNL_SOURCE (operation), "internal_src"); 
}


static gboolean
gnl_composition_send_event (GstElement *element, GstEvent *event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK_SEGMENT:
      break;
    default:
      return FALSE;
  }

  return TRUE;
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


