/* Gnonlin
 * Copyright (C) <2001> Wim Taymans <wim.taymans@chello.be>
 *               <2004> Edward Hervey <edward@fluendo.com>
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

static GstStaticPadTemplate gnl_source_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gnlsource);
#define GST_CAT_DEFAULT gnlsource

GST_BOILERPLATE (GnlSource, gnl_source, GnlObject, GNL_TYPE_OBJECT);

static GstElementDetails gnl_source_details = GST_ELEMENT_DETAILS
    ("GNonLin Source",
    "Filter/Editor",
    "Manages source elements",
    "Wim Taymans <wim.taymans@chello.be>, Edward Hervey <edward@fluendo.com>");

struct _GnlSourcePrivate
{
  gboolean dispose_has_run;
  GstPad *ghostpad;
};

static gboolean gnl_source_add_element (GstBin * bin, GstElement * element);

static gboolean gnl_source_remove_element (GstBin * bin, GstElement * element);

static void gnl_source_dispose (GObject * object);
static void gnl_source_finalize (GObject * object);

static void
gnl_source_base_init (gpointer g_class)
{
  GstElementClass *gstclass = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstclass, &gnl_source_details);
}

static void
gnl_source_class_init (GnlSourceClass * klass)
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

  GST_DEBUG_CATEGORY_INIT (gnlsource, "gnlsource",
      GST_DEBUG_FG_BLUE | GST_DEBUG_BOLD, "GNonLin Source Element");

  gstbin_class->add_element = GST_DEBUG_FUNCPTR (gnl_source_add_element);
  gstbin_class->remove_element = GST_DEBUG_FUNCPTR (gnl_source_remove_element);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gnl_source_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gnl_source_finalize);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gnl_source_src_template));

/*   gstelement_class->change_state 	= gnl_source_change_state; */

}


static void
gnl_source_init (GnlSource * source, GnlSourceClass * klass)
{
  GST_OBJECT_FLAG_SET (source, GNL_OBJECT_SOURCE);
  source->element = NULL;
  source->priv = g_new0 (GnlSourcePrivate, 1);
}

static void
gnl_source_dispose (GObject * object)
{
  GnlSource *source = GNL_SOURCE (object);

  if (source->priv->dispose_has_run)
    return;

  GST_INFO_OBJECT (object, "dispose");
  source->priv->dispose_has_run = TRUE;

  G_OBJECT_CLASS (parent_class)->dispose (object);
  GST_INFO_OBJECT (object, "dispose END");
}

static void
gnl_source_finalize (GObject * object)
{
  GnlSource *source = GNL_SOURCE (object);

  GST_INFO_OBJECT (object, "finalize");
  g_free (source->priv);

  G_OBJECT_CLASS (parent_class)->finalize (object);
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
  given element. Fills in pad if so. The returned pad has an incremented refcount
*/

static gboolean
get_valid_src_pad (GnlSource * source, GstElement * element, GstPad ** pad)
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

static void
no_more_pads_in_child (GstElement * element, GnlSource * source)
{
  GstPad *pad;

  /* check if we can get a valid src pad to ghost */
  GST_DEBUG_OBJECT (element, "let's find a suitable pad");

  if (get_valid_src_pad (source, element, &pad)) {
    source->priv->ghostpad = gnl_object_ghost_pad (GNL_OBJECT (source),
        GST_PAD_NAME (pad), pad);
    gst_object_unref (pad);
  };

  GST_DEBUG ("pad %s:%s ghost-ed", GST_DEBUG_PAD_NAME (pad));

  if (!(source->priv->ghostpad))
    GST_WARNING_OBJECT (source, "Couldn't get a valid source pad");

}

static gboolean
gnl_source_add_element (GstBin * bin, GstElement * element)
{
  GnlSource *source = GNL_SOURCE (bin);
  gboolean pret;

  if (source->element) {
    GST_WARNING_OBJECT (bin, "GnlSource can only handle one element at a time");
    return FALSE;
  }

  /* call parent add_element */
  pret = GST_BIN_CLASS (parent_class)->add_element (bin, element);

  if (pret) {
    GstPad *pad;

    source->element = element;
    gst_object_ref (element);

    /* need to get the src pad */
    if (get_valid_src_pad (source, element, &pad)) {
      GST_DEBUG_OBJECT (bin, "We have a valid src pad: %s:%s",
          GST_DEBUG_PAD_NAME (pad));
      source->priv->ghostpad = gnl_object_ghost_pad (GNL_OBJECT (source),
          GST_PAD_NAME (pad), pad);
      gst_object_unref (pad);
      if (!(source->priv->ghostpad))
        return FALSE;
    } else {
      GST_DEBUG_OBJECT (bin, "no src pads available yet, connecting callback");
      /* we'll get the pad later */
      g_signal_connect (G_OBJECT (element), "no-more-pads",
          G_CALLBACK (no_more_pads_in_child), source);
    }
  }

  return pret;
}

static gboolean
gnl_source_remove_element (GstBin * bin, GstElement * element)
{
  GnlSource *source = GNL_SOURCE (bin);
  gboolean pret;

  if ((!source->element) || (source->element != element)) {
    return FALSE;
  }


  /* try to remove it */
  pret = GST_BIN_CLASS (parent_class)->remove_element (bin, element);

  if (pret) {
    /* remove ghostpad */
    if (source->priv->ghostpad) {
      gnl_object_remove_ghost_pad (GNL_OBJECT (bin), source->priv->ghostpad);
      source->priv->ghostpad = NULL;
    }
    gst_object_unref (element);
    source->element = NULL;
  }
  return pret;
}
