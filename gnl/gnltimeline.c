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



#include "gnltimeline.h"

static void 		gnl_timeline_class_init 	(GnlTimelineClass *klass);
static void 		gnl_timeline_init 		(GnlTimeline *timeline);

static GnlLayerClass *parent_class = NULL;

GType
gnl_timeline_get_type (void)
{
  static GType timeline_type = 0;

  if (!timeline_type) {
    static const GTypeInfo timeline_info = {
      sizeof (GnlTimelineClass),
      NULL,
      NULL,
      (GClassInitFunc) gnl_timeline_class_init,
      NULL,
      NULL,
      sizeof (GnlTimeline),
      4,
      (GInstanceInitFunc) gnl_timeline_init,
    };
    timeline_type = g_type_register_static (G_TYPE_OBJECT, "GnlTimeline", &timeline_info, 0);
  }
  return timeline_type;
}

static void
gnl_timeline_class_init (GnlTimelineClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class =       (GObjectClass*)klass;

  parent_class = g_type_class_ref (GNL_TYPE_LAYER);
}


static void
gnl_timeline_init (GnlTimeline *timeline)
{
}


GnlTimeline*
gnl_timeline_new (const gchar *name)
{
  return NULL;
}


