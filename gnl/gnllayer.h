/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@chello.be>
 *
 * gnllayer.h: Header for base GnlLayer
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


#ifndef __GNL_LAYER_H__
#define __GNL_LAYER_H__

#include <gnl/gnlsource.h>

G_BEGIN_DECLS

#define GNL_TYPE_LAYER \
  (gnl_layer_get_type())
#define GNL_LAYER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GNL_TYPE_LAYER,GnlLayer))
#define GNL_LAYER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GNL_TYPE_LAYER,GnlLayerClass))
#define GNL_IS_LAYER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GNL_TYPE_LAYER))
#define GNL_IS_LAYER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GNL_TYPE_LAYER))

extern GstElementDetails gnl_layer_details;

typedef struct _GnlLayer GnlLayer;
typedef struct _GnlLayerClass GnlLayerClass;

typedef struct _GnlLayerEntry GnlLayerEntry;

typedef enum
{
  GNL_FIND_AT,
  GNL_FIND_AFTER,
  GNL_FIND_START,
} GnlFindMethod;

typedef enum
{
  GNL_COVER_ALL,
  GNL_COVER_SOME,
  GNL_COVER_START,
  GNL_COVER_STOP,
} GnlCoverType;

typedef enum
{
  GNL_DIRECTION_FORWARD,
  GNL_DIRECTION_BACKWARD,
} GnlDirection;

struct _GnlLayer {
  GstBin bin;

  GList		*sources;

  GnlLayerEntry	*current;

  GstClockTime	 start_pos;
  GstClockTime	 stop_pos;
};

struct _GnlLayerClass {
  GstBinClass	parent_class;

  GstClockTime	(*get_time)			(GnlLayer *layer);
  GstClockTime	(*nearest_cover)		(GnlLayer *layer, GstClockTime start, GnlDirection direction);
  gboolean	(*covers)			(GnlLayer *layer, GstClockTime start, GstClockTime stop, GnlCoverType);
  void 		(*scheduling_paused)		(GnlLayer *layer);
  gboolean 	(*prepare)			(GnlLayer *layer, GstClockTime start, GstClockTime stop);
};

GType		gnl_layer_get_type		(void);
GnlLayer*	gnl_layer_new			(const gchar *name);

void		gnl_layer_add_source 		(GnlLayer *layer, 
						 GnlSource *source, 
						 const gchar *padname); 

GstClockTime	gnl_layer_get_time		(GnlLayer *layer);
GstClockTime	gnl_layer_nearest_cover		(GnlLayer *layer, GstClockTime start, GnlDirection direction);
gboolean	gnl_layer_covers		(GnlLayer *layer, GstClockTime start, GstClockTime stop, GnlCoverType);

GnlSource*	gnl_layer_find_source		(GnlLayer *layer, GstClockTime time, GnlFindMethod method);

G_END_DECLS

#endif /* __GNL_LAYER_H__ */

