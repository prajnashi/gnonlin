/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@chello.be>
 *               2004 Edward Hervey <bilboed@bilboed.com>
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


#include "gnl.h"

static GstElementDetails gnl_group_details = GST_ELEMENT_DETAILS (
  "GNL Group",
  "Filter/Editor",
  "GNonlin Single media type composition",
  "Wim Taymans <wim.taymans@chello.be>, Edward Hervey <bilboed@bilboed.com>"
);

static void		gnl_group_base_init		(gpointer g_class);
static void 		gnl_group_class_init 		(GnlGroupClass *klass);
static void 		gnl_group_init 			(GnlGroup *group);

static GnlCompositionClass *parent_class = NULL;

#define CLASS(group)  GNL_GROUP_CLASS (G_OBJECT_GET_CLASS (group))

GType
gnl_group_get_type (void)
{
  static GType group_type = 0;

  if (!group_type) {
    static const GTypeInfo group_info = {
      sizeof (GnlGroupClass),
      (GBaseInitFunc) gnl_group_base_init,
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
gnl_group_base_init (gpointer g_class)
{
  GstElementClass *gstclass = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstclass, &gnl_group_details);
}

static void
gnl_group_class_init (GnlGroupClass *klass)
{
  GObjectClass 		*gobject_class;
  GstBinClass 		*gstbin_class;
  GstElementClass 	*gstelement_class;

  gobject_class = 	(GObjectClass*)klass;
  gstbin_class = 	(GstBinClass*)klass;
  gstelement_class = 	(GstElementClass*)klass;

  parent_class = g_type_class_ref (GNL_TYPE_COMPOSITION);
}


static void
gnl_group_init (GnlGroup *group)
{
}

GnlGroup*
gnl_group_new (const gchar *name)
{
  GnlGroup *new;

  g_return_val_if_fail (name != NULL, NULL);

  new = g_object_new (GNL_TYPE_GROUP, NULL);
  gst_object_set_name (GST_OBJECT (new), name);

  return new;
}

void
gnl_group_append_composition (GnlGroup *group, GnlComposition *comp)
{
  gnl_composition_add_object (GNL_COMPOSITION (group), GNL_OBJECT (comp));
}
