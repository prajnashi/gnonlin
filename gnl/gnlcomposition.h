/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@chello.be>
 *
 * gnlcomposition.h: Header for base GnlComposition
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


#ifndef __GNL_COMPOSITION_H__
#define __GNL_COMPOSITION_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gnl/gnllayer.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GNL_TYPE_COMPOSITION \
  (gnl_composition_get_type())
#define GNL_COMPOSITION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GNL_TYPE_COMPOSITION,GnlComposition))
#define GNL_COMPOSITION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GNL_TYPE_COMPOSITION,GnlCompositionClass))
#define GNL_IS_COMPOSITION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GNL_TYPE_COMPOSITION))
#define GNL_IS_COMPOSITION_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GNL_TYPE_COMPOSITION))

typedef struct _GnlComposition GnlComposition;
typedef struct _GnlCompositionClass GnlCompositionClass;

struct _GnlComposition {
  GnlLayer layer;

  GList    *layers;

  GnlLayer *current;
  gulong    handler;
};

struct _GnlCompositionClass {
  GnlLayerClass	parent_class;
};

/* normal GComposition stuff */
GType			gnl_composition_get_type	(void);
GnlComposition*		gnl_composition_new		(const gchar *name);

void			gnl_composition_append_layer	(GnlComposition *composition,
							 GnlLayer *layer);
void			gnl_composition_insert_layer	(GnlComposition *composition,
							 GnlLayer *layer, gint before);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GNL_COMPOSITION_H__ */

