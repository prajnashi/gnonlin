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

#include <gnl/gnltimer.h>

static GstElementDetails gnl_timer_details =
{
  "Monitors the time on buffers",
  "Filter/Control/Timer",
  "Part of GNonLin",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2002",
};

static void 		gnl_timer_class_init 	(GnlTimerClass *klass);
static void 		gnl_timer_init 		(GnlTimer *timer);

static void 		gnl_timer_chain 	(GstPad *pad, GstBuffer *buf);

static GstElementStateReturn
			gnl_timer_change_state 	(GstElement *element);

static GstElementClass *parent_class = NULL;

void
_gnl_timer_init (void)
{
  GstElementFactory *factory;

  /* create an elementfactory for the gst_lame element */
  factory = gst_elementfactory_new ("gnltimer", GNL_TYPE_TIMER,
		                    &gnl_timer_details);
  g_return_if_fail (factory != NULL);
}

GType
gnl_timer_get_type (void)
{
  static GType timer_type = 0;

  if (!timer_type) {
    static const GTypeInfo timer_info = {
      sizeof (GnlTimerClass),
      NULL,
      NULL,
      (GClassInitFunc) gnl_timer_class_init,
      NULL,
      NULL,
      sizeof (GnlTimer),
      32,
      (GInstanceInitFunc) gnl_timer_init,
    };
    timer_type = g_type_register_static (GST_TYPE_ELEMENT, "GnlTimer", &timer_info, 0);
  }
  return timer_type;
}

static void
gnl_timer_class_init (GnlTimerClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gnl_timer_change_state;
}

static void
gnl_timer_init (GnlTimer *timer)
{
  timer->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_pad_set_chain_function (timer->sinkpad, gnl_timer_chain);
  gst_element_add_pad (GST_ELEMENT (timer), timer->sinkpad);
  
  timer->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (timer), timer->srcpad);

  timer->time = 0LL;
  timer->eos = FALSE;

  GST_FLAG_SET (timer, GST_ELEMENT_EVENT_AWARE);
}

GnlTimer*
gnl_timer_new (void)
{
  GnlTimer *timer;

  timer = g_object_new (GNL_TYPE_TIMER, NULL);

  return timer;
}

void
gnl_timer_notify_async (GnlTimer *timer, 
		        guint64 start_time, 
		        guint64 end_time, 
		        GnlTimerNotify notify_func, 
		        gpointer user_data)
{
  timer->start_time  = start_time;
  timer->notify_time = end_time;
  timer->notify_func = notify_func;
  timer->user_data   = user_data;

  timer->need_seek   = TRUE;
}

static void
gnl_timer_chain (GstPad *pad, GstBuffer *buf)
{
  GnlTimer *timer;
  guint64 timestamp;
  
  timer = GNL_TIMER (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (buf)) {
    gst_pad_event_default (pad, GST_EVENT (buf));
    return;
  }

  timestamp = GST_BUFFER_TIMESTAMP (buf);
  
  g_print ("timer got buffer %lld %lld\n", timestamp, timer->notify_time);

  gst_pad_push (timer->srcpad, buf);

  timer->time = timestamp;

  if (timestamp >= timer->notify_time) {
    timer->notify_time = -1;
    timer->notify_func (timer, timer->user_data);

    if (timer->eos) {
      gst_pad_push (timer->srcpad, GST_BUFFER (gst_event_new (GST_EVENT_EOS)));
      timer->eos = FALSE;
    }
  }
}

guint64
gnl_timer_get_time (GnlTimer *timer)
{
  g_return_val_if_fail (timer != NULL, 0LL);
  g_return_val_if_fail (GNL_IS_TIMER (timer), 0LL);

  return timer->time;
}

static GstElementStateReturn
gnl_timer_change_state (GstElement *element)
{
  GnlTimer *timer = GNL_TIMER (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_PLAYING:
    if (timer->need_seek) {
      GstRealPad *peer = GST_RPAD_PEER (timer->sinkpad);

      gst_pad_send_event (GST_PAD (peer), gst_event_new_seek (GST_SEEK_TIMEOFFSET_SET, timer->start_time, TRUE));
      timer->need_seek = FALSE;
      break;
    }
    default:
      break;
  }

  GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
  
}


