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



#include <gnl/gnlgroup.h>

#include <gnl/gnllayer.h>

static void 		gnl_group_class_init 		(GnlGroupClass *klass);
static void 		gnl_group_init 			(GnlGroup *group);

static gboolean 	gnl_group_prepare_cut 		(GnlLayer *layer, guint64 start, guint64 stop,
							 GnlLayerCutDoneCallback func, gpointer user_data);

static GstElementStateReturn
			gnl_group_change_state 		(GstElement *element);

static GnlCompositionClass *parent_class = NULL;

#define CLASS(group)  GNL_GROUP_CLASS (G_OBJECT_GET_CLASS (group))

GType
gnl_group_get_type (void)
{
  static GType group_type = 0;

  if (!group_type) {
    static const GTypeInfo group_info = {
      sizeof (GnlGroupClass),
      NULL,
      NULL,
      (GClassInitFunc) gnl_group_class_init,
      NULL,
      NULL,
      sizeof (GnlGroup),
      32,
      (GInstanceInitFunc) gnl_group_init,
    };
    group_type = g_type_register_static (GNL_TYPE_COMPOSITION, "GnlGroup", &group_info, 0);
  }
  return group_type;
}

static void
gnl_group_class_init (GnlGroupClass *klass)
{
  GObjectClass 		*gobject_class;
  GstBinClass 		*gstbin_class;
  GstElementClass 	*gstelement_class;
  GnlLayerClass 	*gnllayer_class;

  gobject_class = 	(GObjectClass*)klass;
  gstbin_class = 	(GstBinClass*)klass;
  gstelement_class = 	(GstElementClass*)klass;
  gnllayer_class = 	(GnlLayerClass*)klass;

  parent_class = g_type_class_ref (GNL_TYPE_COMPOSITION);

  gstelement_class->change_state = 	gnl_group_change_state;
  gnllayer_class->prepare_cut = 	gnl_group_prepare_cut;
}


static void
gnl_group_init (GnlGroup *group)
{
  group->timer = gnl_timer_new ();

  gst_object_set_name (GST_OBJECT (group->timer), "group_timer");
  gst_bin_add (GST_BIN (group), GST_ELEMENT (group->timer));

  gst_element_add_ghost_pad (GST_ELEMENT (group), 
		  	     gst_element_get_pad (GST_ELEMENT (group->timer), "src"), 
			     "src");

  group->eos_element = gst_elementfactory_make ("fakesrc", "internal_fakesrc");
  g_object_set (G_OBJECT (group->eos_element), "num_buffers", 0, NULL);

  group->has_eos = FALSE;
}


GnlGroup*
gnl_group_new (const gchar *name)
{
  GnlGroup *new;

  g_return_val_if_fail (name != NULL, NULL);

  new = g_object_new (GNL_TYPE_GROUP, NULL);
  gst_object_set_name (GST_OBJECT (new), name);

  gst_object_set_name (GST_OBJECT (new->timer), g_strdup_printf ("group_timer_%s", name));

  return new;
}

static void
gnl_group_cut_done (GnlLayer *layer, GstClockTime time, gpointer user_data)
{
  GnlGroup *group = GNL_GROUP (user_data);
  GstPad *pad, *peer;

  g_print ("group %s cut done for layer %s\n", GST_ELEMENT_NAME (group), GST_ELEMENT_NAME (layer));

  gst_element_set_state (GST_ELEMENT (group), GST_STATE_PAUSED);

  pad = gst_element_get_pad (GST_ELEMENT (group->timer), "sink");
  peer = GST_PAD_PEER (pad);
  if (peer) {
    gst_pad_disconnect (pad, peer);
  }

  if (gnl_group_prepare_cut (GNL_LAYER (group), time + 1, group->stop, gnl_group_cut_done, group)) {
    gst_element_set_state (GST_ELEMENT (group), GST_STATE_PLAYING);
  }
  else {
    gst_element_connect (group->eos_element, "src", GST_ELEMENT (group->timer), "sink");
    gst_bin_add (GST_BIN (group), group->eos_element);

    gst_element_set_state (group->eos_element, GST_STATE_PLAYING);
    gst_element_set_state (GST_ELEMENT (group->timer), GST_STATE_PLAYING);
    group->has_eos = TRUE;
  }
}
		
static gboolean
gnl_group_prepare_cut (GnlLayer *layer, guint64 start, guint64 stop,
		       GnlLayerCutDoneCallback func, gpointer user_data)
{
  GnlGroup *group = GNL_GROUP (layer);
  GstClockTime next_change;

  group->stop = stop;

  if (group->has_eos) {
    gst_element_disconnect (group->eos_element, "src", GST_ELEMENT (group->timer), "sink");
    gst_bin_remove (GST_BIN (group), group->eos_element);
    group->has_eos = FALSE;
  }
  
  next_change = MIN (gnl_layer_next_change (layer, start + 1), stop);

  g_print ("group: %s prepare for %lld->%lld %lld\n", GST_ELEMENT_NAME (group), start, stop, next_change);

  if (!GNL_LAYER_CLASS (parent_class)->prepare_cut (layer, start, next_change, gnl_group_cut_done, group)) {
    g_print ("group %s nothing to schedule %lld\n", GST_ELEMENT_NAME (group), start);
    return FALSE;
  }
  gst_element_connect (GST_ELEMENT (layer), "internal_src", GST_ELEMENT (group->timer), "sink");

  return TRUE;
} 

static GstElementStateReturn
gnl_group_change_state (GstElement *element)
{
  GnlGroup *group = GNL_GROUP (element);
  
  switch (GST_STATE_TRANSITION (group)) {
    case GST_STATE_PAUSED_TO_PLAYING:
    default:
      break;
  }

  GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
  
}
