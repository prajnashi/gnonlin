/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@chello.be>
 *
 * gnlsource.h: Header for base GnlSource
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


#ifndef __GNL_SOURCE_H__
#define __GNL_SOURCE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GNL_TYPE_SOURCE \
  (gnl_source_get_type())
#define GNL_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GNL_TYPE_SOURCE,GnlSource))
#define GNL_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GNL_TYPE_SOURCE,GnlSourceClass))
#define GNL_IS_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GNL_TYPE_SOURCE))
#define GNL_IS_SOURCE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GNL_TYPE_SOURCE))

typedef enum {
  GNL_SOURCE_INVALID_RATE_CONTROL = 0,
  GNL_SOURCE_FIX_MEDIA_STOP = 1,
  GNL_SOURCE_USE_MEDIA_STOP = 2,
} GnlSourceRateControl;
		
typedef struct _GnlSource GnlSource;
typedef struct _GnlSourceClass GnlSourceClass;

struct _GnlSource {
  GstElement 		 parent;

  GstClockTime  	 start;
  GstClockTime 		 stop;
  GstClockTime  	 media_start;
  GstClockTime 		 media_stop;

  GnlSourceRateControl	 rate_control;

  GstElement 		*element;
  GstElement		*bin;

  gboolean		 element_added;
  gint 			 connected_pads;
  gint 			 total_pads;
  GSList		*connections;

  GstClockTime 		 real_start;
  GstClockTime 		 real_stop;
  GstSeekType		 seek_type;

  GstClockTime  	 current_time;
};

struct _GnlSourceClass {
  GstElementClass	parent_class;

  GstPad*	(*get_pad_for_stream)	(GnlSource *source, const gchar *padname);
};

/* normal GSource stuff */
GType			gnl_source_get_type		(void);
GnlSource*		gnl_source_new			(const gchar *name, GstElement *element);

void			gnl_source_set_element		(GnlSource *source, GstElement *element);
GstElement*		gnl_source_get_element		(GnlSource *source);

void			gnl_source_set_media_start_stop	(GnlSource *source, GstClockTime start, GstClockTime stop);
void			gnl_source_get_media_start_stop	(GnlSource *source, GstClockTime *start, GstClockTime *stop);
void			gnl_source_set_start_stop	(GnlSource *source, GstClockTime start, GstClockTime stop);
void			gnl_source_get_start_stop	(GnlSource *source, GstClockTime *start, GstClockTime *stop);

GstPad*			gnl_source_get_pad_for_stream	(GnlSource *source, const gchar *padname);

GnlSourceRateControl	gnl_source_get_rate_control	(GnlSource *source);
void			gnl_source_set_rate_control	(GnlSource *source, GnlSourceRateControl control);

GstClockTime		gnl_source_get_time		(GnlSource *source);
gfloat			gnl_source_get_rate		(GnlSource *source);

G_END_DECLS

#endif /* __GNL_SOURCE_H__ */

