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

static GstStaticPadTemplate gnl_filesource_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gnlfilesource);
#define GST_CAT_DEFAULT gnlfilesource


GST_BOILERPLATE (GnlFileSource, gnl_filesource, GnlObject, GNL_TYPE_OBJECT);

static GstElementDetails gnl_filesource_details = GST_ELEMENT_DETAILS
    ("GNonLin File Source",
    "Filter/Editor",
    "High-level File Source element",
    "Edward Hervey <edward@fluendo.com>");

enum
{
  ARG_0,
  ARG_LOCATION,
};

struct _GnlFileSourcePrivate
{
  gboolean dispose_has_run;
  GstElement *filesource;
  GstElement *decodebin;
  GstPad *ghostpad;
  GstEvent *seek;
};

static void gnl_filesource_dispose (GObject * object);

static void gnl_filesource_finalize (GObject * object);

static void
gnl_filesource_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void
gnl_filesource_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean
gnl_filesource_send_event (GstElement * element, GstEvent * event);

static gboolean gnl_filesource_prepare (GnlObject * object);

static void pad_blocked_cb (GstPad * pad, gboolean blocked, GnlFileSource * fs);

/* static GstStateChangeReturn */
/* gnl_filesource_change_state	(GstElement *element, GstStateChange transition); */

static void
gnl_filesource_base_init (gpointer g_class)
{
  GstElementClass *gstclass = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstclass, &gnl_filesource_details);
}

static void
gnl_filesource_class_init (GnlFileSourceClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;
  GnlObjectClass *gnlobject_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbin_class = (GstBinClass *) klass;
  gnlobject_class = (GnlObjectClass *) klass;

  parent_class = g_type_class_ref (GNL_TYPE_OBJECT);

  GST_DEBUG_CATEGORY_INIT (gnlfilesource, "gnlfilesource",
      GST_DEBUG_FG_BLUE | GST_DEBUG_BOLD, "GNonLin File Source Element");

  gnlobject_class->prepare = GST_DEBUG_FUNCPTR (gnl_filesource_prepare);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gnl_filesource_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gnl_filesource_finalize);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gnl_filesource_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gnl_filesource_get_property);

  gstelement_class->send_event = GST_DEBUG_FUNCPTR (gnl_filesource_send_event);

/*   gstelement_class->change_state = */
/*     GST_DEBUG_FUNCPTR (gnl_filesource_change_state); */

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gnl_filesource_src_template));

  gst_element_class_install_std_props (GST_ELEMENT_CLASS (klass),
      "location", ARG_LOCATION, G_PARAM_READWRITE, NULL);


}

static gint
compare_src_pad (GstPad * pad, GstCaps * caps)
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
get_valid_src_pad (GnlFileSource * source, GstElement * element, GstPad ** pad)
{
  GstIterator *srcpads;

  g_return_val_if_fail (pad, FALSE);

  srcpads = gst_element_iterate_src_pads (element);
  *pad = (GstPad *) gst_iterator_find_custom (srcpads,
      (GCompareFunc) compare_src_pad, GNL_OBJECT (source)->caps);
  gst_iterator_free (srcpads);

  if (*pad)
    return TRUE;
  return FALSE;
}

static gboolean
ghost_seek_pad (GnlFileSource * fs)
{
  GstPad *pad;
  GstPad *target;

  if (fs->private->ghostpad)
    return FALSE;

  if (!(get_valid_src_pad (fs, fs->private->decodebin, &pad)))
    return FALSE;

  GST_DEBUG_OBJECT (fs, "ghosting %s:%s", GST_DEBUG_PAD_NAME (pad));

  fs->private->ghostpad = gnl_object_ghost_pad_full
      (GNL_OBJECT (fs), GST_PAD_NAME (pad), pad, TRUE);

  GST_DEBUG_OBJECT (fs, "emitting no more pads");
  gst_element_no_more_pads (GST_ELEMENT (fs));

  if (fs->private->seek) {
    GST_DEBUG_OBJECT (fs, "sending queued seek event");
    gst_pad_send_event (fs->private->ghostpad, fs->private->seek);
    GST_DEBUG_OBJECT (fs, "queued seek sent");
    fs->private->seek = NULL;
  }

  target = gst_ghost_pad_get_target (GST_GHOST_PAD (pad));
  GST_DEBUG_OBJECT (fs, "about to unblock %s:%s", GST_DEBUG_PAD_NAME (target));

  gst_pad_set_blocked_async (target, FALSE,
      (GstPadBlockCallback) pad_blocked_cb, fs);

  gst_object_unref (pad);
  gst_object_unref (target);

  return FALSE;
}

static void
pad_blocked_cb (GstPad * pad, gboolean blocked, GnlFileSource * fs)
{
  GST_DEBUG_OBJECT (fs, "blocked:%d pad:%s:%s",
      blocked, GST_DEBUG_PAD_NAME (pad));

  if (blocked)
    g_idle_add ((GSourceFunc) ghost_seek_pad, fs);
/*     ghost_seek_pad(fs); */
}


static void
decodebin_new_pad_cb (GstElement * element, GstPad * pad, GnlFileSource * fs)
{
  GstPad *target;

  GST_DEBUG_OBJECT (fs, "pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  if (fs->private->ghostpad) {
    GST_WARNING_OBJECT (fs, "Already have a ghostpad: %s:%s",
        GST_PAD_NAME (fs->private->ghostpad));
    return;
  }

  /* check if it's the pad we want */
  if (!(gst_pad_accept_caps (pad, GNL_OBJECT (fs)->caps))) {
    GST_DEBUG_OBJECT (pad, "wasn't a valid caps");
    return;
  }

  target = gst_ghost_pad_get_target (GST_GHOST_PAD (pad));
  if (!(gst_pad_set_blocked_async (target, TRUE,
              (GstPadBlockCallback) pad_blocked_cb, fs)))
    GST_WARNING_OBJECT (fs, "returned FALSE !");
  else
    GST_DEBUG_OBJECT (fs, "cb is set on blocking pad");
  gst_object_unref (target);
}

static void
decodebin_pad_removed_cb (GstElement * element, GstPad * pad,
    GnlFileSource * fs)
{
  GstPad *target;

  GST_DEBUG_OBJECT (fs, "pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  if (fs->private->ghostpad) {
    GST_DEBUG_OBJECT (fs, "We still have a ghostpad");

    target = gst_ghost_pad_get_target (GST_GHOST_PAD (fs->private->ghostpad));
    gst_pad_set_blocked (target, FALSE);

    GST_DEBUG_OBJECT (fs,
        "Comparing received pad to our ghostpad's target: %s:%s",
        GST_DEBUG_PAD_NAME (target));

    if (target == pad) {
      gnl_object_remove_ghost_pad (GNL_OBJECT (fs), fs->private->ghostpad);
      fs->private->ghostpad = NULL;
    } else {
      GST_DEBUG_OBJECT (fs, "The pad wasn't our ghostpad's target");
    }
  }
}

static void
gnl_filesource_init (GnlFileSource * filesource, GnlFileSourceClass * klass)
{
  GST_OBJECT_FLAG_SET (filesource, GNL_OBJECT_SOURCE);
  filesource->private = g_new0 (GnlFileSourcePrivate, 1);

  if (!(filesource->private->filesource =
          gst_element_factory_make ("gnomevfssrc", "internal-filesource")))
    if (!(filesource->private->filesource =
            gst_element_factory_make ("filesource", "internal-filesource")))
      g_warning
          ("Could not create a gnomevfssrc or filesource element, are you sure you have any of them installed ?");
  if (!(filesource->private->decodebin =
          gst_element_factory_make ("decodebin", "internal-decodebin")))
    g_warning
        ("Could not create a decodebin element, are you sure you have decodebin installed ?");

  gst_bin_add_many (GST_BIN (filesource),
      filesource->private->filesource, filesource->private->decodebin, NULL);

  if (!(gst_element_link (filesource->private->filesource,
              filesource->private->decodebin)))
    g_warning ("Could not link the file source element to decodebin");

  GST_DEBUG_OBJECT (filesource, "About to add signal watch");

  g_signal_connect (G_OBJECT (filesource->private->decodebin),
      "pad-added", G_CALLBACK (decodebin_new_pad_cb), (gpointer) filesource);

  g_signal_connect (G_OBJECT (filesource->private->decodebin),
      "pad-removed", G_CALLBACK (decodebin_pad_removed_cb),
      (gpointer) filesource);

  GST_DEBUG_OBJECT (filesource, "done");
}

static void
gnl_filesource_dispose (GObject * object)
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
gnl_filesource_finalize (GObject * object)
{
  GnlFileSource *filesource = GNL_FILESOURCE (object);

  GST_INFO_OBJECT (object, "finalize");
  g_free (filesource->private);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gnl_filesource_prepare (GnlObject * object)
{
  /* send initial seek event on the pad */
  GST_DEBUG_OBJECT (object,
      "About to send seek event %" GST_TIME_FORMAT " -- %" GST_TIME_FORMAT,
      GST_TIME_ARGS (object->start), GST_TIME_ARGS (object->stop));
  return gnl_filesource_send_event (GST_ELEMENT (object),
      gst_event_new_seek (1.0, GST_FORMAT_TIME,
          GST_SEEK_FLAG_FLUSH,
          GST_SEEK_TYPE_SET, object->start, GST_SEEK_TYPE_SET, object->stop));
}

static gboolean
gnl_filesource_send_event (GstElement * element, GstEvent * event)
{
  GnlFileSource *fs = GNL_FILESOURCE (element);
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      GST_DEBUG_OBJECT (fs, "got seek event");
      
      if (fs->private->seek)
	gst_event_unref (fs->private->seek);
      if (fs->private->ghostpad) {
	fs->private->seek = NULL;
        res = gst_pad_send_event (fs->private->ghostpad, event);
      } else {
        fs->private->seek = event;
      }
      break;
    default:
      break;
  }

  return res;
}

static void
gnl_filesource_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GnlFileSource *fs = GNL_FILESOURCE (object);

  switch (prop_id) {
    case ARG_LOCATION:
      /* proxy to gnomevfssrc */
      g_object_set_property (G_OBJECT (fs->private->filesource), "location",
          value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gnl_filesource_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GnlFileSource *fs = GNL_FILESOURCE (object);

  switch (prop_id) {
    case ARG_LOCATION:
      /* proxy from gnomevfssrc */
      g_object_get_property (G_OBJECT (fs->private->filesource), "location",
          value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}
