/* Gnonlin
 * Copyright (C) <2001> Wim Taymans <wim.taymans@chello.be>
 *               <2004> Edward Hervey <bilboed@bilboed.com>
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

#include <string.h>
#include "config.h"
#include "gnl.h"
#include "gnlmarshal.h"

GST_DEBUG_CATEGORY_STATIC (gnlobject);
#define GST_CAT_DEFAULT gnlobject

enum {
  ARG_0,
  ARG_START,
  ARG_STOP,
  ARG_MEDIA_START,
  ARG_MEDIA_STOP,
  ARG_PRIORITY,
  ARG_ACTIVE,
};

enum
{
  LAST_SIGNAL
};

static void
gnl_object_class_init 		(GnlObjectClass *klass);

static void
gnl_object_init 		(GnlObject *object);

static void
gnl_object_set_property 	(GObject *object, guint prop_id,
				 const GValue *value, GParamSpec *pspec);

static void
gnl_object_get_property 	(GObject *object, guint prop_id, GValue *value,
				 GParamSpec *pspec);
static gboolean
gnl_object_do_seek 		(GnlObject *object, GstSeekType type, 
				 GstClockTime start, GstClockTime stop);

static gboolean
gnl_object_send_event 		(GstElement *element, GstEvent *event);

static gboolean
gnl_object_query 		(GstElement *element, GstQueryType type,
				 GstFormat *format, gint64 *value);

static gboolean
gnl_object_covers_func 		(GnlObject *object, GstClockTime start,
				 GstClockTime stop, GnlCoverType type);

static GstElementStateReturn
gnl_object_change_state 	(GstElement *element);


static GstBinClass *parent_class = NULL;

#define CLASS(object)  GNL_OBJECT_CLASS (G_OBJECT_GET_CLASS (object))

GType
gnl_object_get_type (void)
{
  static GType object_type = 0;

  if (!object_type) {
    static const GTypeInfo object_info = {
      sizeof (GnlObjectClass),
      NULL,
      NULL,
      (GClassInitFunc) gnl_object_class_init,
      NULL,
      NULL,
      sizeof (GnlObject),
      32,
      (GInstanceInitFunc) gnl_object_init,
    };
    object_type = g_type_register_static (GST_TYPE_BIN, "GnlObject", &object_info, G_TYPE_FLAG_ABSTRACT);
  }
  return object_type;
}


static void
gnl_object_class_init (GnlObjectClass *klass)
{
  GObjectClass 		*gobject_class;
  GstElementClass 	*gstelement_class;
  GnlObjectClass 	*gnlobject_class;

  gobject_class = 	(GObjectClass*)klass;
  gstelement_class = 	(GstElementClass*)klass;
  gnlobject_class = 	(GnlObjectClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_BIN);

  GST_DEBUG_CATEGORY_INIT (gnlobject, "gnlobject", 0, "GNonLin Base class");

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gnl_object_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gnl_object_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_START,
    g_param_spec_uint64 ("start", "Start", "The start position relative to the parent",
                         0, G_MAXINT64, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_STOP,
    g_param_spec_uint64 ("stop", "Stop", "The stop position relative to the parent",
                         0, G_MAXINT64, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MEDIA_START,
    g_param_spec_uint64 ("media_start", "Media start", "The media start position",
                         0, G_MAXINT64, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MEDIA_STOP,
    g_param_spec_uint64 ("media_stop", "Media stop", "The media stop position",
                         0, G_MAXINT64, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PRIORITY,
    g_param_spec_int ("priority", "Priority", "The priority of the object",
                       0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ACTIVE,
    g_param_spec_boolean ("active", "Active", "The state of the object",
                          TRUE, G_PARAM_READWRITE));

  gstelement_class->change_state 	= gnl_object_change_state;
  gstelement_class->send_event 		= gnl_object_send_event;
  gstelement_class->query 		= gnl_object_query;

  klass->covers				= gnl_object_covers_func;
}

static void
gnl_object_init (GnlObject *object)
{
  object->start = 0;
  object->stop = 0;
  object->media_start = GST_CLOCK_TIME_NONE;
  object->media_stop = GST_CLOCK_TIME_NONE;
  object->current_time = 0;
  object->priority = 0;
  object->active = TRUE;
}

/**
 * gnl_object_to_media_time:
 * @object:
 * @objecttime: The #GstClockTime we want to convert
 * @mediatime: A pointer on a #GstClockTime to fill
 *
 * Converts a #GstClockTime from the object (container) context to the media context
 *
 * Returns: TRUE if @objecttime was within the limits of the @object start/stop time,
 * FALSE otherwise
 */

gboolean
gnl_object_to_media_time (GnlObject *object, GstClockTime otime, GstClockTime *mtime)
{
  GST_DEBUG ("ObjectTime : %" GST_TIME_FORMAT,
	     GST_TIME_ARGS(otime));
  if ((otime < object->start) || (otime >= object->stop))
    return FALSE;
  if (object->media_start != GST_CLOCK_TIME_NONE)
    *mtime = otime + object->media_start - object->start;
  else
    *mtime = otime;
  GST_DEBUG ("Returning MediaTime : %" GST_TIME_FORMAT,
	     GST_TIME_ARGS(*mtime));
  return TRUE;
}

/**
 * gnl_media_to_object_time:
 * @object:
 * @mediatime: The #GstClockTime we want to convert
 * @objecttime: A pointer on a #GstClockTime to fill
 *
 * Converts a #GstClockTime from the media context to the object (container) context
 *
 * Returns: TRUE if @objecttime was within the limits of the @object media start/stop time,
 * FALSE otherwise
 */

gboolean
gnl_media_to_object_time (GnlObject *object, GstClockTime mtime, GstClockTime *otime)
{
  GST_DEBUG ("MediaTime : %" GST_TIME_FORMAT,
	     GST_TIME_ARGS(mtime));
  if ((object->media_start != GST_CLOCK_TIME_NONE) 
      && (object->media_stop != GST_CLOCK_TIME_NONE) 
      && ((mtime < object->media_start) || (mtime >= object->media_stop)))
    return FALSE;
  if (GST_CLOCK_TIME_NONE != object->media_start)
    *otime = mtime + object->start - object->media_start;
  else
    *otime = mtime;
  GST_DEBUG ("Returning ObjectTime : %" GST_TIME_FORMAT,
	     GST_TIME_ARGS(*otime));
  return TRUE;
}

/** 
 * gnl_object_set_start_stop:
 * @object: The object element to modify
 * @start: The start time of this object relative to the parent
 * @stop: The stop time of this object relative to the parent
 *
 * Sets the specified start and stop times on the object.
 */
void
gnl_object_set_start_stop (GnlObject *object, GstClockTime start, GstClockTime stop)
{
  gboolean	startm = TRUE;
  gboolean	stopm = TRUE;

  g_return_if_fail (GNL_IS_OBJECT (object));
  g_return_if_fail (start < stop);
  
  GST_INFO("Object:%s , START[%lld]/STOP[%lld]",
	   gst_element_get_name(GST_ELEMENT(object)),
	   start, stop);
  
  if (object->start == start)
    startm = FALSE;
  else
    object->start = start;
  if (object->stop == stop)
    stopm = FALSE;
  else
    object->stop = stop;

  g_object_freeze_notify (G_OBJECT (object));
  if (startm)
    g_object_notify (G_OBJECT (object), "start");
  if (stopm)
    g_object_notify (G_OBJECT (object), "stop");
  g_object_thaw_notify (G_OBJECT (object));
}

/** 
 * gnl_object_get_start_stop:
 * @object: The object element to query
 * @start: A pointer to a GstClockTime to hold the result start time
 * @stop: A pointer to a GstClockTime to hold the result stop time
 *
 * Get the currently configured start and stop times on this object.
 * You can optionally pass a NULL pointer to stop or start when you are not
 * interested in its value.
 */
void
gnl_object_get_start_stop (GnlObject *object, GstClockTime *start, GstClockTime *stop)
{
  g_return_if_fail (GNL_IS_OBJECT (object));
  g_return_if_fail (start != NULL || stop != NULL);

  if (start) *start = object->start;
  if (stop)  *stop = object->stop;
}

/** 
 * gnl_object_set_media_start_stop:
 * @object: The object element to modify
 * @start: The media start time to configure
 * @stop: The media stop time to configure
 *
 * Set the specified media start and stop times on the object.
 */
void
gnl_object_set_media_start_stop (GnlObject *object, GstClockTime start, GstClockTime stop)
{
  gboolean	startm = TRUE;
  gboolean	stopm = TRUE;

  g_return_if_fail (GNL_IS_OBJECT (object));
  g_return_if_fail (start < stop);

  GST_INFO("Object:%s , START[%lld]/STOP[%lld]",
	   gst_element_get_name(GST_ELEMENT(object)),
	   start, stop);

  if (object->media_start == start)
    startm = FALSE;
  else
    object->media_start = start;
  if (object->media_stop == stop)
    stopm = FALSE;
  else
    object->media_stop = stop;

  if (startm || stopm) {
/*     if (startm && stopm) */
/*       gnl_object_do_seek (object, */
/* 			  GST_FORMAT_TIME | */
/* 			  GST_SEEK_METHOD_SET | */
/* 			  GST_SEEK_FLAG_FLUSH | */
/* 			  GST_SEEK_FLAG_ACCURATE,  */
/* 			  object->start, object->start + (stop - start)); */
    
    g_object_freeze_notify (G_OBJECT (object));
    if (startm)
      g_object_notify (G_OBJECT (object), "media_start");
    if (stopm)
      g_object_notify (G_OBJECT (object), "media_stop");
    g_object_thaw_notify (G_OBJECT (object));
  }
}

/** 
 * gnl_object_get_media_start_stop:
 * @object: The object element to query
 * @start: A pointer to a GstClockTime to hold the result media start time
 * @stop: A pointer to a GstClockTime to hold the result media stop time
 *
 * Get the currently configured media start and stop times on this object.
 * You can optionally pass a NULL pointer to stop or start when you are not
 * interested in its value.
 */
void
gnl_object_get_media_start_stop (GnlObject *object, GstClockTime *start, GstClockTime *stop)
{
  g_return_if_fail (GNL_IS_OBJECT (object));
  g_return_if_fail (start != NULL || stop != NULL);

  if (start) *start = object->media_start;
  if (stop)  *stop = object->media_stop;
}

/** 
 * gnl_object_set_priority:
 * @object: The object element to modify
 * @priority: The new priority of the object
 *
 * Set the priority on the given object
 */
void
gnl_object_set_priority (GnlObject *object, gint priority)
{
  g_return_if_fail (object != NULL);
  g_return_if_fail (GNL_IS_OBJECT (object));
  g_return_if_fail (priority > 0);
  
  if (object->priority != priority) {
    object->priority = priority;
    g_object_notify (G_OBJECT (object), "priority");
  }
}

/** 
 * gnl_object_get_priority:
 * @object: The object element to query
 *
 * Get the priority of the object
 *
 * Returns: The priority of the object
 */
gint
gnl_object_get_priority (GnlObject *object)
{
  g_return_val_if_fail (GNL_IS_OBJECT (object), -1);

  return object->priority;
}

/** 
 * gnl_object_is_active:
 * @object: The object element to query
 *
 * Check if the object is active.
 *
 * Returns: The state of the object
 */
gboolean
gnl_object_is_active (GnlObject *object)
{
  g_return_val_if_fail (GNL_IS_OBJECT (object), FALSE);

  return object->active;
}

/** 
 * gnl_object_set_active:
 * @object: The object element to activate
 * @active: the new state of the object
 *
 * Activate or dectivate the given object based on the active
 * argument.
 */
void
gnl_object_set_active (GnlObject *object, gboolean active)
{
  g_return_if_fail (object != NULL);
  g_return_if_fail (GNL_IS_OBJECT (object));
  
  GST_INFO("Active[%d] %s", active, gst_element_get_name(GST_ELEMENT(object)));

  if (object->active != active) {
    object->active = active;
    g_object_notify (G_OBJECT (object), "active");
  }
}

static GstElementStateReturn
gnl_object_change_state (GstElement *element)
{
  GnlObject *object = GNL_OBJECT (element);
  
  switch (GST_STATE_TRANSITION (object)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element);
}

static gboolean
gnl_object_covers_func (GnlObject *object, GstClockTime start,
		        GstClockTime stop, GnlCoverType type)
{
  
  GST_INFO("Object[%s] Start[%lld]/Stop[%lld] type[%d]",
	   gst_element_get_name(GST_ELEMENT(object)),
	   start, stop, type);
  
  switch (type) {
  case GNL_COVER_ALL:
  case GNL_COVER_SOME:
    if (start >= object->start && stop < object->stop) {
      GST_INFO("TRUE");
      return TRUE;
    }
    break;
  case GNL_COVER_START:
    if (start >= object->start && start < object->stop) {
      GST_INFO("TRUE");
      return TRUE;
    }
    break;
  case GNL_COVER_STOP:
    if (stop >= object->start && stop < object->stop) {
      GST_INFO("TRUE");
      return TRUE;
    }
    break;
  default:
    break;
  }
  
  GST_INFO("FALSE");
  return FALSE;
}

/**
 * gnl_object_covers:
 * @object : The #GnlObject
 * @start : The start #GstClockTime for the query
 * @stop : The stop #GstClockTime for the query
 * @type : The type of #GnlCoverType query used
 *
 * Returns: TRUE if the #GnlObject is covered by the given start/stop time
 * and the type of #GnlCoverType
 */

gboolean
gnl_object_covers (GnlObject *object, GstClockTime start,
		   GstClockTime stop, GnlCoverType type)
{
  g_return_val_if_fail (GNL_IS_OBJECT (object), FALSE);

  GST_INFO("Object:%s , START[%lld]/STOP[%lld], TYPE:%d",
	   gst_element_get_name(GST_ELEMENT(object)),
	   start, stop, type);

  if (CLASS (object)->covers)
    return CLASS (object)->covers (object, start, stop, type);

  return FALSE;
}

static gboolean
gnl_object_do_seek (GnlObject *object, GstSeekType type, GstClockTime start, GstClockTime stop)
{
  GstClockTime seek_start, seek_stop;
  gdouble ratio;
  gboolean res = FALSE;
  GstEvent *event;

  if (!CLASS (object)->prepare)
    return res;

  GST_INFO("%s media_[%" GST_TIME_FORMAT "]->[%" GST_TIME_FORMAT "] time[%" GST_TIME_FORMAT "]->[%" GST_TIME_FORMAT "] seek[%" GST_TIME_FORMAT "]->[%" GST_TIME_FORMAT"]",
	   gst_element_get_name (GST_ELEMENT (object)),
	   GST_TIME_ARGS(object->media_start), GST_TIME_ARGS(object->media_stop),
	   GST_TIME_ARGS(object->start), GST_TIME_ARGS(object->stop),
	   GST_TIME_ARGS(start), GST_TIME_ARGS(stop) );

  /* Verify that the seek can apply to the object */
  if ((start >= object->stop) || (stop < object->start)) {
    GST_WARNING ("Seek is outside object limits, returning TRUE anyways");
    return TRUE;
  }
  /* Limit the seeks to the object's limit (stop/start) */
  if (start < object->start)
    start = object->start;
  if (stop >= object->stop)
    stop = object->stop;

  GST_INFO ("%s: adjusted seek to %" GST_TIME_FORMAT " -> %" GST_TIME_FORMAT,
	    gst_element_get_name (GST_ELEMENT (object)),
	    GST_TIME_ARGS(start),
	    GST_TIME_ARGS(stop));
  
  if ((object->media_start == GST_CLOCK_TIME_NONE) || (object->media_stop == GST_CLOCK_TIME_NONE)) {
    /* If object hasn't set media start/stop, forward the adjusted seek */
    seek_start = start;
    seek_stop = stop;
  } else {
    /* Correct the seek start/stop depending on the media start/stop value */

    ratio = (gdouble) (object->media_stop - object->media_start) / (object->stop - object->start);
    seek_start = object->media_start + (start - object->start) * ratio;
    seek_stop = object->media_start + (stop - object->start) * ratio;
  }

  GST_INFO("Changed to [%" GST_TIME_FORMAT "] -> [%" GST_TIME_FORMAT "]", 
	   GST_TIME_ARGS (seek_start), GST_TIME_ARGS (seek_stop));

  event = gst_event_new_segment_seek (type, seek_start, seek_stop);
  res = CLASS (object)->prepare (object, event);

  return res;
}

static gboolean
gnl_object_send_event (GstElement *element, GstEvent *event)
{
  GnlObject *object = GNL_OBJECT (element);
  gboolean res = FALSE;

  GST_DEBUG ("Got Event %d for element %s",
	     GST_EVENT_TYPE (event),
	     gst_element_get_name (element));
  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_SEEK_SEGMENT:
    res = gnl_object_do_seek (object, 
			      GST_EVENT_SEEK_TYPE (event),
			      GST_EVENT_SEEK_OFFSET (event),
			      GST_EVENT_SEEK_ENDOFFSET (event));
    break;
  case GST_EVENT_SEEK:
    res = gnl_object_do_seek (object,
			      GST_EVENT_SEEK_TYPE (event),
			      GST_EVENT_SEEK_OFFSET (event),
			      G_MAXINT64);
    break;
  default:
    break;
  }
  gst_event_unref (event);
	      
  return res;
} 

static gboolean
gnl_object_query (GstElement *element, GstQueryType type,
		  GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  GnlObject *object = GNL_OBJECT (element);

  GST_INFO("Object:%s , Type[%d], Format[%d]",
	   gst_element_get_name(element),
	   type, *format);
  GST_INFO("Start:%lld, Stop:%lld, priority:%d",
	   object->start, object->stop, object->priority);

  if (*format != GST_FORMAT_TIME)
    return FALSE;
  
  switch (type) {
    case GST_QUERY_TOTAL:
      *value = object->stop - object->start;
      break;
    case GST_QUERY_POSITION:
      *value = object->current_time;
      break;
    case GST_QUERY_START:
      *value = object->start;
      break;
    case GST_QUERY_SEGMENT_END:
      break;
    default:
      res = FALSE;
      break;
  }
  return res;
}

static void
gnl_object_set_property (GObject *object, guint prop_id,
			 const GValue *value, GParamSpec *pspec)
{
  GnlObject *gnlobject;

  g_return_if_fail (GNL_IS_OBJECT (object));

  gnlobject = GNL_OBJECT (object);

  switch (prop_id) {
    case ARG_START:
      gnlobject->start = g_value_get_uint64 (value);
      break;
    case ARG_STOP:
      gnlobject->stop = g_value_get_uint64 (value);
      break;
    case ARG_MEDIA_START:
      gnlobject->media_start = g_value_get_uint64 (value);
      break;
    case ARG_MEDIA_STOP:
      gnlobject->media_stop = g_value_get_uint64 (value);
      break;
    case ARG_PRIORITY:
      gnl_object_set_priority (gnlobject, g_value_get_int (value));
      break;
    case ARG_ACTIVE:
      gnl_object_set_active (gnlobject, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gnl_object_get_property (GObject *object, guint prop_id, 
			 GValue *value, GParamSpec *pspec)
{
  GnlObject *gnlobject;
  
  g_return_if_fail (GNL_IS_OBJECT (object));

  gnlobject = GNL_OBJECT (object);

  switch (prop_id) {
    case ARG_START:
      g_value_set_uint64 (value, gnlobject->start);
      break;
    case ARG_STOP:
      g_value_set_uint64 (value, gnlobject->stop);
      break;
    case ARG_MEDIA_START:
      g_value_set_uint64 (value, gnlobject->media_start);
      break;
    case ARG_MEDIA_STOP:
      g_value_set_uint64 (value, gnlobject->media_stop);
      break;
    case ARG_PRIORITY:
      g_value_set_int (value, gnl_object_get_priority (gnlobject));
      break;
    case ARG_ACTIVE:
      g_value_set_boolean (value, gnl_object_is_active (gnlobject));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
