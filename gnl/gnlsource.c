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

static GstBinClass *parent_class = NULL;

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
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_BIN);
}


static void
gnl_source_init (GnlSource *source)
{
}


GnlSource*
gnl_source_new (const gchar *name)
{
  GnlSource *new;

  g_return_val_if_fail (name != NULL, NULL);

  new = g_object_new (GNL_TYPE_SOURCE, NULL);
  gst_object_set_name (GST_OBJECT (new), name);
  
  return new;
}

void
gnl_source_set_element (GnlSource *source, GstElement *element)
{
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (source != NULL);
  g_return_if_fail (GNL_IS_SOURCE (source));

  source->source = element;

  gst_bin_add (GST_BIN (source), element);

  gst_element_add_ghost_pad (GST_ELEMENT (source), gst_element_get_pad (element, "src"), "src");
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


