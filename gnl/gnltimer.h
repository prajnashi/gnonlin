/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@chello.be>
 *
 * gnltimer.h: Header for base GnlTimer
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


#ifndef __GNL_TIMER_H__
#define __GNL_TIMER_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gstelement.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GNL_TYPE_TIMER \
  (gnl_timer_get_type())
#define GNL_TIMER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GNL_TYPE_TIMER,GnlTimer))
#define GNL_TIMER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GNL_TYPE_TIMER,GnlTimerClass))
#define GNL_IS_TIMER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GNL_TYPE_TIMER))
#define GNL_IS_TIMER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GNL_TYPE_TIMER))

typedef struct _GnlTimer GnlTimer;
typedef struct _GnlTimerClass GnlTimerClass;

typedef void (*GnlTimerNotify) (GnlTimer *timer, gpointer user_data);
		
struct _GnlTimer {
  GstElement 	 parent;

  GstPad 	*srcpad;
  GstPad	*sinkpad;

  guint64	 time;

  guint64	 start_time;
  guint64	 notify_time;
  GnlTimerNotify notify_func;
  gpointer	 user_data;

  gboolean 	 eos;
  gboolean 	 need_seek;
};

struct _GnlTimerClass {
  GstElementClass	parent_class;
};

void		_gnl_timer_init			(void);

/* normal GTimer stuff */
GType		gnl_timer_get_type		(void);
GnlTimer*	gnl_timer_new			(void);

guint64		gnl_timer_get_time		(GnlTimer *timer);
void		gnl_timer_notify_async		(GnlTimer *timer, guint64 start_time, guint64 end_time, 
						 GnlTimerNotify notify_func, 
						 gpointer user_data);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GNL_TIMER_H__ */

