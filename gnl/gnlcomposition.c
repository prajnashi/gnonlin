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

  gobject_class =       (GObjectClass*)klass;

  parent_class = g_type_class_ref (GNL_TYPE_LAYER);
}


static void
gnl_composition_init (GnlComposition *composition)
{
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

}


