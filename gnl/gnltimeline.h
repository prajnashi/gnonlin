/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@chello.be>
 *
 * gnltimeline.h: Header for base GnlTimeline
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


#ifndef __GNL_TIMELINE_H__
#define __GNL_TIMELINE_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gnl/gnlcomposition.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GNL_TYPE_TIMELINE \
  (gnl_timeline_get_type())
#define GNL_TIMELINE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GNL_TYPE_TIMELINE,GnlTimeline))
#define GNL_TIMELINE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GNL_TYPE_TIMELINE,GnlTimelineClass))
#define GNL_IS_TIMELINE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GNL_TYPE_TIMELINE))
#define GNL_IS_TIMELINE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GNL_TYPE_TIMELINE))

typedef struct _GnlTimeline GnlTimeline;
typedef struct _GnlTimelineClass GnlTimelineClass;

struct _GnlTimeline {
  GnlComposition	composition;

};

struct _GnlTimelineClass {
  GnlCompositionClass	parent_class;
};

GType		gnl_timeline_get_type		(void);
GnlTimeline*	gnl_timeline_new		(const gchar *name);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GNL_TIMELINE_H__ */

