/* Gnonlin
 * Copyright (C) <2001> Wim Taymans <wim.taymans@chello.be>
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

#include "gnlsource.h"
#include "gnlmarshal.h"
#include "string.h"
#include "gnllayer.h"
  
static GstElementDetails gnl_source_details = GST_ELEMENT_DETAILS (
  "GNL Source",
  "Source",
  "Manages source elements",
  "Wim Taymans <wim.taymans@chello.be>"
  );

enum {
  ARG_0,
  ARG_ELEMENT,
  ARG_START,
  ARG_STOP,
  ARG_MEDIA_START,
  ARG_MEDIA_STOP,
  ARG_RATE,
  ARG_RATE_CONTROL,
  ARG_TIME,
};

enum
{
  GET_PAD_FOR_STREAM_ACTION,
  LAST_SIGNAL
};

#define GNL_TYPE_SOURCE_RATE_CONTROL (gnl_source_rate_control_get_type())
static GType
gnl_source_rate_control_get_type (void)
{
  static GType source_rate_control_type = 0;
  static GEnumValue source_rate_control[] = {
    { GNL_SOURCE_INVALID_RATE_CONTROL, "0", "Invalid"},
    { GNL_SOURCE_FIX_MEDIA_STOP,       "1", "Fix media stop time to match object start/stop times"},
    { GNL_SOURCE_USE_MEDIA_STOP,       "2", "Use media stop time to adjust rate"},
    { 0, NULL, NULL},
  };
  if (!source_rate_control_type) {
    source_rate_control_type = g_enum_register_static ("GnlSourceRateControlType", source_rate_control);
  }
  return source_rate_control_type;
}

static void 		gnl_source_base_init 		(gpointer g_class);
static void 		gnl_source_class_init 		(GnlSourceClass *klass, gpointer class_data);
static void 		gnl_source_init 		(GnlSource *source);

static void		gnl_source_set_property 	(GObject *object, guint prop_id,
							 const GValue *value, GParamSpec *pspec);
static void		gnl_source_get_property 	(GObject *object, guint prop_id, GValue *value,
		                                         GParamSpec *pspec);

static gboolean 	gnl_source_send_event 		(GstElement *element, GstEvent *event);



static GstElementStateReturn
			gnl_source_change_state 	(GstElement *element);


static GstData* 	source_getfunction 		(GstPad *pad);
static void 		source_chainfunction 		(GstPad *pad, GstData *buffer);

typedef struct 
{
  GnlSource *source;
  const gchar *padname;
  GstPad *target;
} ConnectData;

static void		source_element_new_pad	 	(GstElement *element, 
							 GstPad *pad, 
							 ConnectData *data);

static GstElementClass *parent_class = NULL;
static guint gnl_source_signals[LAST_SIGNAL] = { 0 };

typedef struct {
  GSList *queue;
  GstPad *srcpad,
         *sinkpad;
} SourcePadPrivate;

#define CLASS(source)  GNL_SOURCE_CLASS (G_OBJECT_GET_CLASS (source))

GType
gnl_source_get_type (void)
{
  static GType source_type = 0;

  if (!source_type) {
    static const GTypeInfo source_info = {
      sizeof (GnlSourceClass),
      (GBaseInitFunc) gnl_source_base_init,
      NULL,
      (GClassInitFunc) gnl_source_class_init,
      NULL,
      NULL,
      sizeof (GnlSource),
      32,
      (GInstanceInitFunc) gnl_source_init,
    };
    source_type = g_type_register_static (GST_TYPE_ELEMENT, "GnlSource", &source_info, 0);
  }
  return source_type;
}

static void
gnl_source_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gnl_source_details);
}

static void
gnl_source_class_init (GnlSourceClass *klass, gpointer class_data)
{
  GObjectClass 		*gobject_class;
  GstElementClass 	*gstelement_class;
  GnlSourceClass 	*gnlsource_class;

  gobject_class = 	(GObjectClass*)klass;
  gstelement_class = 	(GstElementClass*)klass;
  gnlsource_class = 	(GnlSourceClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gnl_source_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gnl_source_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ELEMENT,
    g_param_spec_object ("element", "Element", "The element to manage",
                         GST_TYPE_ELEMENT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_START,
    g_param_spec_uint64 ("start", "Start", "The start position relative to the parent",
                         0, G_MAXUINT64, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_STOP,
    g_param_spec_uint64 ("stop", "Stop", "The stop position relative to the parent",
                         0, G_MAXUINT64, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MEDIA_START,
    g_param_spec_uint64 ("media_start", "Media start", "The media start position",
                         0, G_MAXUINT64, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MEDIA_STOP,
    g_param_spec_uint64 ("media_stop", "Media stop", "The media stop position",
                         0, G_MAXUINT64, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_RATE,
    g_param_spec_float ("rate", "Rate", "The current rate of the source",
                        G_MINFLOAT, G_MAXFLOAT, 1.0, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_RATE_CONTROL,
    g_param_spec_enum ("rate_control", "Rate control", "Specify the rate control method",
                       GNL_TYPE_SOURCE_RATE_CONTROL, 1, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TIME,
    g_param_spec_uint64 ("current_time", "Current time", "The current time",
                         0, G_MAXUINT64, G_MAXUINT64, G_PARAM_READABLE));

  gnl_source_signals[GET_PAD_FOR_STREAM_ACTION] =
    g_signal_new("get_pad_for_stream",
                 G_TYPE_FROM_CLASS(klass),
		 G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                 G_STRUCT_OFFSET (GnlSourceClass, get_pad_for_stream),
                 NULL, NULL,
                 gnl_marshal_OBJECT__STRING,
                 GST_TYPE_PAD, 1, G_TYPE_STRING);

  gstelement_class->change_state 	= gnl_source_change_state;
  gstelement_class->send_event 		= gnl_source_send_event;

  klass->get_pad_for_stream		= gnl_source_get_pad_for_stream;
}


static void
gnl_source_init (GnlSource *source)
{
  source->element_added = FALSE;
  GST_FLAG_SET (source, GST_ELEMENT_DECOUPLED);
  GST_FLAG_SET (source, GST_ELEMENT_EVENT_AWARE);

  source->bin = gst_pipeline_new ("pipeline");
  source->element = 0;
  source->connected_pads = 0;
  source->total_pads = 0;
  source->connections = NULL;
  source->seek_type = GST_FORMAT_TIME | 
		      GST_SEEK_METHOD_SET | 
		      GST_SEEK_FLAG_FLUSH |
		      GST_SEEK_FLAG_ACCURATE;

  source->start = 0;
  source->stop = 0;
  source->media_start = 0;
  source->media_stop = 0;
  source->current_time = 0;
  source->rate_control = GNL_SOURCE_FIX_MEDIA_STOP;
}

/** 
 * gnl_source_new:
 * @name: The name of the new #GnlSource
 * @element: The element managed by this source
 *
 * Creates a new source object with the given name. The
 * source will manage the given GstElement
 *
 * Returns: a new #GnlSource object or NULL in case of
 * an error.
 */
GnlSource*
gnl_source_new (const gchar *name, GstElement *element)
{
  GnlSource *source;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (element != NULL, NULL);

  source = g_object_new (GNL_TYPE_SOURCE, 
		         "name", name, 
			 "element", element, 
			 NULL);

  return GNL_SOURCE (source);
}

/** 
 * gnl_source_get_element:
 * @source: The source element to get the element of
 *
 * Get the element managed by this source.
 *
 * Returns: The element managed by this source.
 */
GstElement*
gnl_source_get_element (GnlSource *source)
{
  g_return_val_if_fail (GNL_IS_SOURCE (source), NULL);

  return source->element;
}

/** 
 * gnl_source_set_element:
 * @source: The source element to set the element on
 * @element: The element that should be managed by the source
 *
 * Set the given element on the given source. If the source
 * was managing another element, it will be removed first.
 */
void
gnl_source_set_element (GnlSource *source, GstElement *element)
{
  g_return_if_fail (GNL_IS_SOURCE (source));
  g_return_if_fail (GST_IS_ELEMENT (element));

  if (source->element) {
    gst_bin_remove (GST_BIN (source->bin), source->element);
    gst_object_unref (GST_OBJECT (source->element));
  }

  gst_object_ref (GST_OBJECT (element));

  source->element = element;
  source->connected_pads = 0;
  source->total_pads = 0;
  source->connections = NULL;
  source->seek_type = GST_FORMAT_TIME | 
		      GST_SEEK_METHOD_SET | 
		      GST_SEEK_FLAG_FLUSH |
		      GST_SEEK_FLAG_ACCURATE;

  gst_bin_add (GST_BIN (source->bin), source->element);
}

static GstPadLinkReturn
source_connect (GstPad *pad, const GstCaps *caps)
{
  GstPad *otherpad;
  SourcePadPrivate *private;

  private = gst_pad_get_element_private (pad);
  
  otherpad = (GST_PAD_IS_SRC (pad)? private->sinkpad : private->srcpad);

/*  if (GST_CAPS_IS_FIXED (caps))*/
    return gst_pad_try_set_caps (otherpad, caps);
/*  else
    return GST_PAD_CONNECT_DELAYED;*/
}

/** 
 * gnl_source_get_pad_for_stream:
 * @source: The source element to query
 * @padname: The padname of the element managed by this source
 *
 * Get a handle to a pad that provides the data from the given pad
 * of the managed element.
 *
 * Returns: A pad 
 */
GstPad*
gnl_source_get_pad_for_stream (GnlSource *source, const gchar *padname)
{
  GstPad *srcpad, *sinkpad, *pad;
  SourcePadPrivate *private;
  gchar *ourpadname;

  g_return_val_if_fail (GNL_IS_SOURCE (source), NULL);
  g_return_val_if_fail (padname != NULL, NULL);

  private = g_new0 (SourcePadPrivate, 1);

  srcpad = gst_pad_new (padname, GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (source), srcpad);
  gst_pad_set_element_private (srcpad, private);
  gst_pad_set_get_function (srcpad, source_getfunction);
  gst_pad_set_link_function (srcpad, source_connect);

  ourpadname = g_strdup_printf ("internal_sink_%s", padname);
  sinkpad = gst_pad_new (ourpadname, GST_PAD_SINK);
  g_free (ourpadname);
  gst_element_add_pad (GST_ELEMENT (source), sinkpad);
  gst_pad_set_element_private (sinkpad, private);
  gst_pad_set_chain_function (sinkpad, source_chainfunction);
  gst_pad_set_link_function (sinkpad, source_connect);

  private->srcpad  = srcpad;
  private->sinkpad = sinkpad;

  source->connections = g_slist_prepend (source->connections, private);

  pad = gst_element_get_pad (source->element, padname);

  source->total_pads++;

  if (pad) {
    gst_pad_connect (pad, sinkpad);
    source->connected_pads++;
  }
  else {
    ConnectData *data = g_new0 (ConnectData, 1);

    data->source = source;
    data->padname = padname;
    data->target = sinkpad;
    
    g_signal_connect (G_OBJECT (source->element), 
		      "new_pad", 
		      G_CALLBACK (source_element_new_pad), 
		      data);
  }

  return srcpad;
}

static void
clear_queues (GnlSource *source)
{
  GSList *walk = source->connections;

  while (walk) {
    SourcePadPrivate *private = (SourcePadPrivate *) walk->data;

    g_slist_free (private->queue);
    private->queue = NULL;
    
    walk = g_slist_next (walk);
  }
}

static gboolean
source_is_media_queued (GnlSource *source)
{
  const GList *pads = gst_element_get_pad_list (GST_ELEMENT (source));

  while (pads) {
    GstPad *pad = GST_PAD (pads->data);
    SourcePadPrivate *private = gst_pad_get_element_private (pad);

    if (!private->queue)
      return FALSE;
    
    pads = g_list_next (pads);
  }
  
  return TRUE;
}

static gboolean
source_send_seek (GnlSource *source, GstClockTime start, GstClockTime stop)
{
  const GList *pads;

  /* ghost all pads */
  pads = gst_element_get_pad_list (source->element);
  while (pads) {  
    GstPad *pad = GST_PAD (pads->data);
    GstEvent *event;

    g_print ("%s: seeking to %lld %lld\n", 
	      gst_element_get_name (GST_ELEMENT (source)), start, stop);
    
    event = gst_event_new_segment_seek (source->seek_type,
					gnl_time_to_seek_val (start),
					gnl_time_to_seek_val (stop));

    if (!gst_pad_send_event (pad, event)) {
      g_warning ("%s: could not seek", 
		 gst_element_get_name (GST_ELEMENT (source)));
    }

    pads = g_list_next (pads);
  }
  clear_queues (source);
  return TRUE;
}

static gboolean
source_queue_media (GnlSource *source)
{
  gboolean filled;

  gst_element_set_state (source->bin, GST_STATE_PLAYING);

  filled = FALSE;

  while (!filled) {
    if (!gst_bin_iterate (GST_BIN (source->bin))) {
      break;
    }
    filled = source_is_media_queued (source);
  }
  source_send_seek (source, source->real_start, source->real_stop);
  
  gst_element_set_state (source->bin, GST_STATE_PAUSED);

  return filled;
}

static void
source_chainfunction (GstPad *pad, GstData *buffer)
{
  SourcePadPrivate *private;
  GnlSource *source;

  private = gst_pad_get_element_private (pad);
  source = GNL_SOURCE (gst_pad_get_parent (pad));

  private->queue = g_slist_append (private->queue, buffer);
}

static GstData*
source_getfunction (GstPad *pad)
{
  GstData *buffer;
  SourcePadPrivate *private;
  GnlSource *source;
  gboolean found = FALSE;

  private = gst_pad_get_element_private (pad);
  source = GNL_SOURCE (gst_pad_get_parent (pad));

  while (!found) {
    while (!private->queue) {
      if (!gst_bin_iterate (GST_BIN (source->bin))) {
        buffer = GST_DATA (gst_event_new (GST_EVENT_EOS));
        break;
      }
    }

    if (private->queue) {
      buffer = GST_DATA (private->queue->data);

      if (GST_IS_EVENT (buffer)) {
        if (GST_EVENT_TYPE (buffer) == GST_EVENT_EOS) {
          g_print ("%s: EOS at %lld %lld %lld %lld %lld %lld\n", 
	       gst_element_get_name (GST_ELEMENT (source)), 
	       source->media_start,
	       source->media_stop,
	       source->real_start,
	       source->real_stop,
	       source->start,
	       source->stop);

          source->current_time = source->real_stop - source->media_start + source->start;
	  gst_element_set_eos (GST_ELEMENT (source));
	  found = TRUE;
        }
      }
      else {
        GstClockTimeDiff outtime, intime;

        intime = GST_BUFFER_TIMESTAMP (buffer);

        outtime = intime - source->media_start + source->start;
	      
        source->current_time = outtime;

	if (outtime >= 0) {
          g_print ("%s: get %lld corrected to %lld\n", 
	       gst_element_get_name (GST_ELEMENT (source)), 
	       intime, 
	       outtime);

          GST_BUFFER_TIMESTAMP (buffer) = outtime;

          found = TRUE;
	}
	else {
          gst_buffer_unref (buffer);
	}
      }
 
      private->queue = g_slist_remove (private->queue, buffer);
    }
  }
  
  return buffer;
}



static void
source_element_new_pad (GstElement *element, GstPad *pad, ConnectData *data)
{
  g_print ("souce %s new pad %s\n", GST_OBJECT_NAME (data->source), GST_PAD_NAME (pad));
  g_print ("connect %s new pad %s %d\n", data->padname, gst_pad_get_name (pad),
     					GST_PAD_IS_CONNECTED (data->target));

  if (!g_ascii_strcasecmp (gst_pad_get_name (pad), data->padname) && 
     !GST_PAD_IS_CONNECTED (data->target)) 
  {
     gst_pad_connect (pad, data->target);
  }
}

/** 
 * gnl_source_set_start_stop:
 * @source: The source element to modify
 * @start: The start time of this source relative to the parent
 * @stop: The stop time of this source relative to the parent
 *
 * Sets the specified start and stop times on the source.
 */
void
gnl_source_set_start_stop (GnlSource *source, GstClockTime start, GstClockTime stop)
{
  g_return_if_fail (GNL_IS_SOURCE (source));
  g_return_if_fail (start < stop);

  source->start = start;
  source->stop = stop;
  source->real_stop = source->real_start + stop-start;

  g_object_freeze_notify (G_OBJECT (source));
  g_object_notify (G_OBJECT (source), "start");
  g_object_notify (G_OBJECT (source), "stop");
  g_object_thaw_notify (G_OBJECT (source));
}

/** 
 * gnl_source_get_start_stop:
 * @source: The source element to query
 * @start: A pointer to a GstClockTime to hold the result start time
 * @stop: A pointer to a GstClockTime to hold the result stop time
 *
 * Get the currently configured start and stop times on this source.
 * You can optionally pass a NULL pointer to stop or start when you are not
 * interested in its value.
 */
void
gnl_source_get_start_stop (GnlSource *source, GstClockTime *start, GstClockTime *stop)
{
  g_return_if_fail (GNL_IS_SOURCE (source));
  g_return_if_fail (start != NULL || stop != NULL);

  if (start) *start = source->start;
  if (stop)  *stop = source->stop;
}

/** 
 * gnl_source_set_media_start_stop:
 * @source: The source element to modify
 * @start: The media start time to configure
 * @stop: The media stop time to configure
 *
 * Set the specified media start and stop times on the source.
 */
void
gnl_source_set_media_start_stop (GnlSource *source, GstClockTime start, GstClockTime stop)
{
  g_return_if_fail (GNL_IS_SOURCE (source));
  g_return_if_fail (start < stop);

  source->media_start = start;
  source->media_stop = stop;
  source->real_start = start;
  source->real_stop = stop;

  if (gst_element_get_state (GST_ELEMENT (source)) == GST_STATE_PAUSED) {
    source_send_seek (source, start, stop);
  }

  g_object_freeze_notify (G_OBJECT (source));
  g_object_notify (G_OBJECT (source), "media_start");
  g_object_notify (G_OBJECT (source), "media_stop");
  g_object_thaw_notify (G_OBJECT (source));
}

/** 
 * gnl_source_get_media_start_stop:
 * @source: The source element to query
 * @start: A pointer to a GstClockTime to hold the result media start time
 * @stop: A pointer to a GstClockTime to hold the result media stop time
 *
 * Get the currently configured media start and stop times on this source.
 * You can optionally pass a NULL pointer to stop or start when you are not
 * interested in its value.
 */
void
gnl_source_get_media_start_stop (GnlSource *source, GstClockTime *start, GstClockTime *stop)
{
  g_return_if_fail (GNL_IS_SOURCE (source));
  g_return_if_fail (start != NULL || stop != NULL);

  if (start) *start = source->media_start;
  if (stop)  *stop = source->media_stop;
}

/** 
 * gnl_source_get_time:
 * @source: The source element to query
 *
 * Get the current time of the source element.
 *
 * Returns: The time of the source.
 */
GstClockTime
gnl_source_get_time (GnlSource *source)
{
  g_return_val_if_fail (GNL_IS_SOURCE (source), GST_CLOCK_TIME_NONE);

  return source->current_time;
}

/** 
 * gnl_source_get_rate:
 * @source: The source element to query
 *
 * Get the rate of the managed element based on start/stop position
 * and media stop/start
 *
 * Returns: The rate of the element, 1.0 is normal playback rate.
 */
gfloat
gnl_source_get_rate (GnlSource *source)
{
  gfloat rate;

  g_return_val_if_fail (GNL_IS_SOURCE (source), 0.0);

  if (source->media_stop == source->media_start ||
      source->stop == source->start) 
  {
    return 0.0;
  }
	
  rate = ((gfloat) (source->media_stop - source->media_start)) / (source->stop - source->start);

  return rate;
}

/** 
 * gnl_source_get_rate_control:
 * @source: The source element to query
 *
 * Get the currently configured method for handling the relation
 * between the media times and the start/stop position.
 *
 * Returns: The RateControl method used.
 */
GnlSourceRateControl
gnl_source_get_rate_control (GnlSource *source)
{
  g_return_val_if_fail (GNL_IS_SOURCE (source), GNL_SOURCE_INVALID_RATE_CONTROL);

  return source->rate_control;
}

/** 
 * gnl_source_set_rate_control:
 * @source: The source element to modify
 * @control: The method to use for rate control
 *
 * Set the method for handling differences in media and normal
 * start/stop times.
 */
void
gnl_source_set_rate_control (GnlSource *source, GnlSourceRateControl control)
{
  g_return_if_fail (source != NULL);
  g_return_if_fail (GNL_IS_SOURCE (source));
  g_return_if_fail (control >= GNL_SOURCE_FIX_MEDIA_STOP &&
                    control <= GNL_SOURCE_USE_MEDIA_STOP);

  source->rate_control = control;
}

static gboolean
gnl_source_send_event (GstElement *element, GstEvent *event)
{
  GnlSource *source = GNL_SOURCE (element);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK_SEGMENT:
    {
      GstClockTime seek_start, seek_stop;

      g_print ("%s: received seek to %lld %lld\n", 
		      gst_element_get_name (element),
		      GST_EVENT_SEEK_OFFSET (event),
		      GST_EVENT_SEEK_ENDOFFSET (event));

      seek_start = GST_EVENT_SEEK_OFFSET (event) - source->start;
      seek_stop = GST_EVENT_SEEK_ENDOFFSET (event) - source->start;

      g_print ("%s: corrected seek to %lld %lld\n", 
		      gst_element_get_name (element),
		      seek_start,
		      seek_stop);

      source->real_start = MAX (source->media_start + seek_start, source->media_start);
      source->real_stop  = MIN (source->media_start + seek_stop, source->media_stop);
      if (source->real_stop == source->media_stop) {
	source->seek_type = (GST_EVENT_SEEK_TYPE (event) & !GST_SEEK_FLAG_SEGMENT_LOOP);
      }
      else {
	source->seek_type = GST_EVENT_SEEK_TYPE (event);
      }
      if (gst_element_get_state (element) >= GST_STATE_READY) {
        source_send_seek (source, source->real_start, source->real_stop);
      }
      break;
    }
    default:
      return FALSE;
  }

  return TRUE;
}


static GstElementStateReturn
gnl_source_change_state (GstElement *element)
{
  GnlSource *source = GNL_SOURCE (element);
  
  switch (GST_STATE_TRANSITION (source)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      source_queue_media (source);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      gst_element_set_state (source->bin, GST_STATE_PLAYING);
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      gst_element_set_state (source->bin, GST_STATE_PAUSED);
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
  
}

static void
gnl_source_set_property (GObject *object, guint prop_id,
			 const GValue *value, GParamSpec *pspec)
{
  GnlSource *source;

  g_return_if_fail (GNL_IS_SOURCE (object));

  source = GNL_SOURCE (object);

  switch (prop_id) {
    case ARG_ELEMENT:
      gnl_source_set_element (source, GST_ELEMENT (g_value_get_object (value)));
      break;
    case ARG_START:
      source->start = g_value_get_uint64 (value);
      break;
    case ARG_STOP:
      source->stop = g_value_get_uint64 (value);
      break;
    case ARG_MEDIA_START:
      source->media_start = g_value_get_uint64 (value);
      break;
    case ARG_MEDIA_STOP:
      source->media_stop = g_value_get_uint64 (value);
      break;
    case ARG_RATE_CONTROL:
      gnl_source_set_rate_control (source, g_value_get_enum (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gnl_source_get_property (GObject *object, guint prop_id, 
			 GValue *value, GParamSpec *pspec)
{
  GnlSource *source;
  
  g_return_if_fail (GNL_IS_SOURCE (object));

  source = GNL_SOURCE (object);

  switch (prop_id) {
    case ARG_ELEMENT:
      g_value_set_object (value, gnl_source_get_element (source));
      break;
    case ARG_START:
      g_value_set_uint64 (value, source->start);
      break;
    case ARG_STOP:
      g_value_set_uint64 (value, source->stop);
      break;
    case ARG_MEDIA_START:
      g_value_set_uint64 (value, source->media_start);
      break;
    case ARG_MEDIA_STOP:
      g_value_set_uint64 (value, source->media_stop);
      break;
    case ARG_RATE:
      g_value_set_float (value, gnl_source_get_rate (source));
      break;
    case ARG_RATE_CONTROL:
      g_value_set_enum (value, gnl_source_get_rate_control (source));
      break;
    case ARG_TIME:
      g_value_set_uint64 (value, gnl_source_get_time (source));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
