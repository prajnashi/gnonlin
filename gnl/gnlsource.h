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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

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

typedef struct _GnlSource GnlSource;
typedef struct _GnlSourceClass GnlSourceClass;

struct _GnlSource {
  GstBin bin;

  guint64 start;
  guint64 stop;

  GstElement *source;

};

struct _GnlSourceClass {
  GstBinClass	parent_class;
};

/* normal GSource stuff */
GType		gnl_source_get_type		(void);
GnlSource*	gnl_source_new			(const gchar *name);

void		gnl_source_set_element		(GnlSource *source, GstElement *element);
void		gnl_source_set_start_stop	(GnlSource *source, guint64 start, guint64 stop);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GNL_SOURCE_H__ */

