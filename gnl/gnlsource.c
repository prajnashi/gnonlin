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



#include <gnl/gnlsource.h>

#include <gnl/gnllayer.h>

static void 		gnl_source_class_init 		(GnlSourceClass *klass);
static void 		gnl_source_init 		(GnlSource *source);

static void 		gnl_source_set_element_func 	(GnlSource *source, GstElement *element);
static void 		gnl_source_prepare_cut_func 	(GnlSource *source, guint64 start, guint64 stop, guint64 out,
			    				 GnlCutDoneCallback func, gpointer user_data);

static GstElementStateReturn
			gnl_source_change_state 	(GstElement *element);

static GstBinClass *parent_class = NULL;

#define CLASS(source)  GNL_SOURCE_CLASS (G_OBJECT_GET_CLASS (source))

GType
gnl_source_get_type (void)
{
  static GType source_type = 0;

  if (!source_type) {
    static const GTypeInfo source_info = {
      sizeof (GnlSourceClass),
      NULL,
      NULL,
      (GClassInitFunc) gnl_source_class_init,
      NULL,
      NULL,
      sizeof (GnlSource),
      32,
      (GInstanceInitFunc) gnl_source_init,
    };
    source_type = g_type_register_static (GST_TYPE_BIN, "GnlSource", &source_info, 0);
  }
  return source_type;
}

static void
gnl_source_class_init (GnlSourceClass *klass)
{
  GObjectClass 		*gobject_class;
  GstBinClass 		*gstbin_class;
  GstElementClass 	*gstelement_class;
  GnlSourceClass 	*gnlsource_class;

  gobject_class = 	(GObjectClass*)klass;
  gstbin_class = 	(GstBinClass*)klass;
  gstelement_class = 	(GstElementClass*)klass;
  gnlsource_class = 	(GnlSourceClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_BIN);

  gstelement_class->change_state = 	gnl_source_change_state;
  gnlsource_class->set_element = 	gnl_source_set_element_func;
  gnlsource_class->prepare_cut = 	gnl_source_prepare_cut_func;
}


static void
gnl_source_init (GnlSource *source)
{
  source->timer = gnl_timer_new ();

  gst_object_set_name (GST_OBJECT (source->timer), "timer");
  gst_bin_add (GST_BIN (source), GST_ELEMENT (source->timer));

  gst_element_add_ghost_pad (GST_ELEMENT (source), 
		  	     gst_element_get_pad (GST_ELEMENT (source->timer), "src"), 
			     "internal_src");
}


GnlSource*
gnl_source_new (const gchar *name)
{
  GnlSource *new;

  g_return_val_if_fail (name != NULL, NULL);

  new = g_object_new (GNL_TYPE_SOURCE, NULL);
  gst_object_set_name (GST_OBJECT (new), name);

  gst_object_set_name (GST_OBJECT (new->timer), g_strdup_printf ("timer_%s", name));

  return new;
}

static void
gnl_source_set_element_func (GnlSource *source, GstElement *element)
{
  source->element = element;

  gst_bin_add (GST_BIN (source), element);
  gst_element_connect_pads (element, "src", GST_ELEMENT (source->timer), "sink");
}

void
gnl_source_set_element (GnlSource *source, GstElement *element)
{
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (source != NULL);
  g_return_if_fail (GNL_IS_SOURCE (source));

  if (CLASS (source)->set_element)
    CLASS (source)->set_element (source, element);
}

static void
source_ended (GnlTimer *timer, gpointer user_data) 
{
  GnlSource *source;

  source = GNL_SOURCE (user_data);

  if (source->cut_done_func)
    source->cut_done_func (source, gnl_source_get_time (source), source->user_data);
}

static void
gnl_source_prepare_cut_func (GnlSource *source, guint64 start, guint64 stop, guint64 out,
			     GnlCutDoneCallback func, gpointer user_data)
{
  gnl_timer_notify_async (source->timer,  
                          start,
                          stop, 
                          out,
                          source_ended, source);
}

void
gnl_source_prepare_cut (GnlSource *source, guint64 start, guint64 stop, guint64 out,
			GnlCutDoneCallback func, gpointer user_data)
{
  g_return_if_fail (source != NULL);
  g_return_if_fail (GNL_IS_SOURCE (source));

  source->cut_done_func = func;
  source->user_data = user_data;

  if (CLASS (source)->prepare_cut)
    CLASS (source)->prepare_cut (source, start, stop, out, func, user_data);
}


void
gnl_source_set_start_stop (GnlSource *source, guint64 start, guint64 stop)
{
  g_return_if_fail (source != NULL);
  g_return_if_fail (GNL_IS_SOURCE (source));
  g_return_if_fail (start < stop);

  source->start = start;
  source->stop = stop;
}

GstClockTime
gnl_source_get_time (GnlSource *source)
{
  return gnl_timer_get_time (source->timer);
}

static GstElementStateReturn
gnl_source_change_state (GstElement *element)
{
  GnlSource *source = GNL_SOURCE (element);
  
  switch (GST_STATE_TRANSITION (source)) {
    case GST_STATE_PAUSED_TO_PLAYING:
    default:
      break;
  }

  GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
  
}
