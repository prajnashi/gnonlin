/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@chello.be>
 *
 * gnlobject.h: Header for base GnlObject
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


#ifndef __GNL_OBJECT_H__
#define __GNL_OBJECT_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#ifdef USE_GLIB2
#include <glib-object.h>	// note that this gets wrapped in __GNL_OBJECT_H__ 
#include <gnl/gnlmarshal.h>
#else
#include <gnl/gobject2gtk.h>
#endif

#include <gnl/gnltrace.h>
#include <parser.h>

#include <gnl/gnltypes.h>

#ifdef HAVE_ATOMIC_H
#include <asm/atomic.h>
#endif

// FIXME
#include "gnllog.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GNL_TYPE_OBJECT \
  (gnl_object_get_type())
#define GNL_OBJECT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GNL_TYPE_OBJECT,GnlObject))
#define GNL_OBJECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GNL_TYPE_OBJECT,GnlObjectClass))
#define GNL_IS_OBJECT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GNL_TYPE_OBJECT))
#define GNL_IS_OBJECT_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GNL_TYPE_OBJECT))

//typedef struct _GnlObject GnlObject;
//typedef struct _GnlObjectClass GnlObjectClass;
//
typedef enum
{
  GNL_DESTROYED   = 0,
  GNL_FLOATING,

  GNL_OBJECT_FLAG_LAST   = 4,
} GnlObjectFlags;

struct _GnlObject {
  GstObject object;
};

struct _GnlObjectClass {
  GstObjectClass	parent_class;
};

/* normal GObject stuff */
GType		gnl_object_get_type		(void);
GnlObject*	gnl_object_new			(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GNL_OBJECT_H__ */

