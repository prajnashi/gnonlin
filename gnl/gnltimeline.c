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
#include "gnltimer.h"

static void 		gnl_timeline_class_init 	(GnlTimelineClass *klass);
static void 		gnl_timeline_init 		(GnlTimeline *timeline);

static GstElementStateReturn
			gnl_timeline_change_state 	(GstElement *element);


static GstBinClass *parent_class = NULL;

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
    timeline_type = g_type_register_static (GST_TYPE_BIN, "GnlTimeline", &timeline_info, 0);
  }
  return timeline_type;
}

static void
gnl_timeline_class_init (GnlTimelineClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class =       (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_BIN);

  gstelement_class->change_state = gnl_timeline_change_state;
}


static void
gnl_timeline_init (GnlTimeline *timeline)
{
  timeline->groups = NULL;
}

GnlTimeline*
gnl_timeline_new (const gchar *name)
{
  GnlTimeline *new;

  g_return_val_if_fail (name != NULL, NULL);

  new = g_object_new (GNL_TYPE_TIMELINE, NULL);
  gst_object_set_name (GST_OBJECT (new), name);

  return new;
}

void
gnl_timeline_add_group (GnlTimeline *timeline, GnlGroup *group)
{
  timeline->groups = g_list_prepend (timeline->groups, group);

  gst_bin_add (GST_BIN (timeline), GST_ELEMENT (group));
}

static GstElementStateReturn
gnl_timeline_change_state (GstElement *element)
{
  GnlTimeline *timeline = GNL_TIMELINE (element);

  switch (GST_STATE_TRANSITION (timeline)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
    {
      GList *walk = timeline->groups;
      gboolean res = TRUE;

      while (walk && res) {
	GnlLayer *layer = GNL_LAYER (walk->data);

        res &= gnl_layer_prepare_cut (layer, 0, G_MAXINT64, NULL, NULL);

	walk = g_list_next (walk);
      }
      if (!res)
	return GST_STATE_FAILURE;
      break;
    }
    default:
      break;
  }
	  
  return GST_ELEMENT_CLASS (parent_class)->change_state (element);
}



