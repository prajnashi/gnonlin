/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@chello.be>
 *
 * gnllayer.h: Header for base GnlVLayer
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


#ifndef __GNL_VLAYER_H__
#define __GNL_VLAYER_H__

#include <gnl/gnllayer.h>

G_BEGIN_DECLS

#define GNL_TYPE_VLAYER \
  (gnl_vlayer_get_type())
#define GNL_VLAYER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GNL_TYPE_VLAYER,GnlVLayer))
#define GNL_VLAYER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GNL_TYPE_VLAYER,GnlVLayerClass))
#define GNL_IS_VLAYER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GNL_TYPE_VLAYER))
#define GNL_IS_VLAYER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GNL_TYPE_VLAYER))

typedef struct _GnlVLayer GnlVLayer;
typedef struct _GnlVLayerClass GnlVLayerClass;

struct _GnlVLayer {
  GnlLayer 	 parent;

  GList		*layers;
  GnlLayer	*current;
  GstEvent	*event;
};

struct _GnlVLayerClass {
  GnlLayerClass	parent_class;
};

GType		gnl_vlayer_get_type		(void);
GnlVLayer*	gnl_vlayer_new			(const gchar *name);

void		gnl_vlayer_append_layer 	(GnlVLayer *vlayer, 
						 GnlLayer *layer); 

G_END_DECLS

#endif /* __GNL_VLAYER_H__ */

