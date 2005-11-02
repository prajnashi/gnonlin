/* Gnonlin
 * Copyright (C) <2005> Edward Hervey <edward@fluendo.com>
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gnl.h"
#include "gnlmarshal.h"

GST_DEBUG_CATEGORY_STATIC (gnlfilesource);
#define GST_CAT_DEFAULT gnlfilesource


GST_BOILERPLATE (GnlFileSource, gnl_filesource, GnlObject, GNL_TYPE_OBJECT);

static GstElementDetails gnl_filesource_details = GST_ELEMENT_DETAILS
(
  "GNonLin File Source",
  "Filter/Editor",
  "High-level File Source element",
  "Edward Hervey <edward@fluendo.com>"
);

enum {
  ARG_0,
  ARG_LOCATION,
};

struct _GnlFileSourcePrivate {
  gboolean	dispose_has_run;
  GstElement	*filesource;
  GstElement	*decodebin;
  GstPad	*ghostpad;
  GstEvent	*seek;

  gulong	buswatchid;
};

static void
gnl_filesource_dispose	(GObject *object);

static void
gnl_filesource_finalize (GObject *object);

static void
gnl_filesource_set_property 	(GObject *object, guint prop_id,
				 const GValue *value,
				 GParamSpec *pspec);

static void
gnl_filesource_get_property 	(GObject *object, guint prop_id,
				 GValue *value,
				 GParamSpec *pspec);

static gboolean
gnl_filesource_send_event	(GstElement *element, GstEvent *event);

static void
pad_blocked_cb	(GstPad *pad, gboolean blocked, GnlFileSource *fs);

/* static GstStateChangeReturn */
/* gnl_filesource_change_state	(GstElement *element, GstStateChange transition); */

static void
gnl_filesource_base_init (gpointer g_class)
{
  GstElementClass *gstclass = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstclass, &gnl_filesource_details);
}

static void
gnl_filesource_class_init (GnlFileSourceClass *klass)
{
  GObjectClass 		*gobject_class;
  GstElementClass 	*gstelement_class;
  GstBinClass		*gstbin_class;
  GnlObjectClass 	*gnlobject_class;

  gobject_class = 	(GObjectClass*)klass;
  gstelement_class = 	(GstElementClass*)klass;
  gstbin_class =	(GstBinClass*)klass;
  gnlobject_class = 	(GnlObjectClass*)klass;

  parent_class = g_type_class_ref (GNL_TYPE_OBJECT);

  GST_DEBUG_CATEGORY_INIT (gnlfilesource, "gnlfilesource", 0, "GNonLin File Source Element");

/*   gnlobject_class->prepare = GST_DEBUG_FUNCPTR (gnl_filesource_prepare); */

  gobject_class->dispose      = GST_DEBUG_FUNCPTR (gnl_filesource_dispose);
  gobject_class->finalize     = GST_DEBUG_FUNCPTR (gnl_filesource_finalize);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gnl_filesource_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gnl_filesource_get_property);

  gstelement_class->send_event =
    GST_DEBUG_FUNCPTR (gnl_filesource_send_event);

/*   gstelement_class->change_state = */
/*     GST_DEBUG_FUNCPTR (gnl_filesource_change_state); */

  gst_element_class_install_std_props (GST_ELEMENT_CLASS (klass),
      "location", ARG_LOCATION, G_PARAM_READWRITE, NULL);
							

}

static gint
compare_src_pad (GstPad *pad, GstCaps *caps)
{
  gint ret;

  if (gst_pad_accept_caps (pad, caps))
    ret = 0;
  else {
    gst_object_unref (pad);
    ret = 1;
  }
  return ret;
}

/*
  get_valid_src_pad

  Returns True if there's a src pad compatible with the GnlObject caps in the
  given element. Fills in pad if so.
*/

static gboolean
get_valid_src_pad (GnlFileSource *source, GstElement *element, GstPad **pad)
{
  GstIterator	*srcpads;

  g_return_val_if_fail (pad, FALSE);

  srcpads = gst_element_iterate_src_pads (element);
  *pad = (GstPad*) gst_iterator_find_custom (srcpads,
     (GCompareFunc) compare_src_pad, GNL_OBJECT(source)->caps);
  gst_iterator_free (srcpads);

  if (*pad)
    return TRUE;
  return FALSE;
}

static gboolean
ghost_seek_pad	(GnlFileSource *fs)
{
  GstPad	*pad;
  GstPad	*target;

  if (!(get_valid_src_pad (fs, fs->private->decodebin, &pad)))
    return FALSE;

  GST_DEBUG_OBJECT (fs, "ghosting %s:%s", GST_DEBUG_PAD_NAME (pad));
 
  fs->private->ghostpad = gnl_object_ghost_pad
    (GNL_OBJECT (fs), GST_PAD_NAME (pad), pad);

  GST_DEBUG_OBJECT (fs, "emitting no more pads");
  gst_element_no_more_pads (GST_ELEMENT (fs));

  if (fs->private->seek) {
    GST_DEBUG_OBJECT (fs, "sending queued seek event");
    gst_pad_send_event (pad, fs->private->seek);
    GST_DEBUG_OBJECT (fs, "queued seek sent");
    fs->private->seek = NULL;
  }

  target = gst_ghost_pad_get_target (GST_GHOST_PAD (pad));
  GST_DEBUG_OBJECT (fs, "about to unblock %s:%s",
		    GST_DEBUG_PAD_NAME (target));

  gst_pad_set_blocked_async (target, FALSE,
			     pad_blocked_cb, fs);

  gst_object_unref (pad);
  gst_object_unref (target);
  
  return FALSE;
}

/* static gboolean */
/* pad_ready_cb	(GstBus *bus, GstMessage *message, GnlFileSource *fs) */
/* { */
/*   GstPad	*pad; */

/*   GST_DEBUG_OBJECT (fs, "bus callback "); */

/*   if ( (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ELEMENT) && (GST_MESSAGE_SRC(message) == GST_OBJECT (fs))) { */
/*     /\* our pad is ready *\/ */
/*     GST_DEBUG_OBJECT (fs, "yep"); */
/*     if (get_valid_src_pad (fs, fs->private->decodebin, &pad)) { */
/*       fs->private->ghostpad = gnl_object_ghost_pad (GNL_OBJECT (fs), */
/* 						    GST_PAD_NAME (pad), */
/* 						    pad); */
/*       gst_element_no_more_pads (GST_ELEMENT (fs)); */

/*       if (fs->private->seek) { */
/* 	GST_DEBUG_OBJECT (fs, "sending queued seek event"); */
/* 	gst_pad_send_event (pad, fs->private->seek); */
/* 	fs->private->seek = NULL; */
/*       } */
      
/*       gst_pad_set_blocked (pad, FALSE); */
/*       gst_object_unref (pad); */
/*     } */
/*   } */
/*   return TRUE; */
/* } */

static void
pad_blocked_cb	(GstPad *pad, gboolean blocked, GnlFileSource *fs)
{
  GST_DEBUG_OBJECT (fs, "blocked:%d pad:%s:%s",
		    blocked, GST_DEBUG_PAD_NAME (pad));

  if (blocked)
    g_idle_add ((GSourceFunc) ghost_seek_pad, fs);
/*   gst_element_post_message (GST_ELEMENT (fs->private->decodebin), gst_message_new_element  */
/* 			    (GST_OBJECT (fs), */
/* 			     gst_structure_from_string("pad-ready", NULL))); */
/*   GST_DEBUG_OBJECT (fs, "posted message"); */
}

static void
decodebin_no_more_pads_cb	(GstElement *element, GnlFileSource *fs)
{
  GstPad	*pad;
  GstPad	*target;

  /* check if we can get a valid src pad to ghost */
  GST_DEBUG_OBJECT (element, "let's find a suitable pad");

  if (get_valid_src_pad (fs, element, &pad)) {
    target = gst_ghost_pad_get_target (GST_GHOST_PAD (pad));
    if (!(gst_pad_set_blocked_async (target, TRUE,
				     (GstPadBlockCallback) pad_blocked_cb,
				     fs)))
      GST_WARNING_OBJECT (fs, "returned FALSE !");
    else
      GST_DEBUG_OBJECT (fs, "cb is set on blocking pad");
    gst_object_unref (pad);
    gst_object_unref (target);
  } else {
    GST_WARNING_OBJECT (fs, "Couldn't get a valid source pad");
  };
}

static void
gnl_filesource_init (GnlFileSource *filesource, GnlFileSourceClass *klass)
{
/*   GstBus	*bus; */

  GST_OBJECT_FLAG_SET (filesource, GNL_OBJECT_SOURCE);
  filesource->private = g_new0(GnlFileSourcePrivate, 1);

  if (!(filesource->private->filesource = gst_element_factory_make ("gnomevfssrc", "internal-filesource")))
    if (!(filesource->private->filesource = gst_element_factory_make ("filesource", "internal-filesource")))
      g_warning ("Could not create a gnomevfssrc or filesource element, are you sure you have any of them installed ?");
  if (!(filesource->private->decodebin = gst_element_factory_make ("decodebin", "internal-decodebin")))
    g_warning ("Could not create a decodebin element, are you sure you have decodebin installed ?");

  gst_bin_add_many (GST_BIN (filesource), 
		    filesource->private->filesource,
		    filesource->private->decodebin,
		    NULL);

  if (!(gst_element_link (filesource->private->filesource,
			  filesource->private->decodebin)))
    g_warning ("Could not link the file source element to decodebin");
  
  GST_DEBUG_OBJECT (filesource, "About to add signal watch");

/*   bus = GST_BIN(filesource)->child_bus; */
/*   filesource->private->buswatchid = gst_bus_add_watch */
/*     (bus, (GstBusFunc) pad_ready_cb, filesource); */

/*   GST_DEBUG_OBJECT (bus, "got bus"); */
/*   gst_bus_add_signal_watch (bus); */
/*   filesource->private->buswatchid = g_signal_connect (G_OBJECT (bus), */
/* 						      "message", */
/* 						      G_CALLBACK (pad_ready_cb),  */
/* 						      filesource); */
  GST_DEBUG_OBJECT (filesource, "done");
  g_signal_connect (G_OBJECT (filesource->private->decodebin),
		    "no-more-pads", G_CALLBACK (decodebin_no_more_pads_cb),
		    (gpointer) filesource);
}

static void
gnl_filesource_dispose (GObject *object)
{
  GnlFileSource *filesource = GNL_FILESOURCE (object);

  if (filesource->private->dispose_has_run)
    return;

  GST_INFO_OBJECT (object, "dispose");
  filesource->private->dispose_has_run = TRUE;
  
  G_OBJECT_CLASS (parent_class)->dispose (object);
  GST_INFO_OBJECT (object, "dispose END");
}

static void
gnl_filesource_finalize (GObject *object)
{
  GnlFileSource *filesource = GNL_FILESOURCE (object);

  GST_INFO_OBJECT (object, "finalize");
  g_free (filesource->private);
  
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gnl_filesource_send_event	(GstElement *element, GstEvent *event)
{
  GnlFileSource	*fs = GNL_FILESOURCE (element);
  gboolean	res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
  case GST_EVENT_SEEK:
    if (fs->private->ghostpad)
      res = gst_pad_send_event (fs->private->ghostpad, event);
    else {
      if (fs->private->seek)
	gst_event_unref (fs->private->seek);
      fs->private->seek = event;
    }
    break;
  default:
    break;
  }

  return res;
}

static void
gnl_filesource_set_property 	(GObject *object, guint prop_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
  GnlFileSource	*fs = GNL_FILESOURCE (object);

  switch (prop_id) {
  case ARG_LOCATION:
    /* proxy to gnomevfssrc */
    g_object_set_property (G_OBJECT (fs->private->filesource), "location", value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
gnl_filesource_get_property 	(GObject *object, guint prop_id,
				 GValue *value,
				 GParamSpec *pspec)
{
  GnlFileSource	*fs = GNL_FILESOURCE (object);

  switch (prop_id) {
  case ARG_LOCATION:
    /* proxy from gnomevfssrc */
    g_object_get_property (G_OBJECT (fs->private->filesource), "location", value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }

}
