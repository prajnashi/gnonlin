/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@chello.be>
 *               2004 Edward Hervey <bilboed@bilboed.com>
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

GST_BOILERPLATE (GnlOperation, gnl_operation, GnlObject, GNL_TYPE_OBJECT);

static GstElementDetails gnl_operation_details =
GST_ELEMENT_DETAILS ("GNonLin Operation",
    "Filter/Editor",
    "Encapsulates filters/effects for use with GNL Objects",
    "Wim Taymans <wim.taymans@chello.be>, Edward Hervey <bilboed@bilboed.com>");

GST_DEBUG_CATEGORY_STATIC (gnloperation);
#define GST_CAT_DEFAULT gnloperation

enum
{
  ARG_0,
  ARG_SINKS,
};

static void gnl_operation_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gnl_operation_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gnl_operation_prepare (GnlObject * object);

static gboolean gnl_operation_add_element (GstBin * bin, GstElement * element);
static gboolean gnl_operation_remove_element (GstBin * bin, GstElement * element);

static void
gnl_operation_base_init (gpointer g_class)
{
  GstElementClass *gstclass = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstclass, &gnl_operation_details);
}

static void
gnl_operation_class_init (GnlOperationClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBinClass *gstbin_class = (GstBinClass *) klass;
/*   GstElementClass *gstelement_class = (GstElementClass *) klass; */
  GnlObjectClass *gnlobject_class = (GnlObjectClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gnloperation, "gnloperation",
      GST_DEBUG_FG_BLUE | GST_DEBUG_BOLD, "GNonLin Operation element");

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gnl_operation_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gnl_operation_get_property);

  gnlobject_class->prepare = GST_DEBUG_FUNCPTR (gnl_operation_prepare);

  gstbin_class->add_element = GST_DEBUG_FUNCPTR (gnl_operation_add_element);
  gstbin_class->remove_element = GST_DEBUG_FUNCPTR (gnl_operation_remove_element);

  g_object_class_install_property (gobject_class, ARG_SINKS,
      g_param_spec_uint ("sinks", "Sinks", "Number of input sinks",
          1, G_MAXUINT, 1, G_PARAM_READWRITE));

}

static void
gnl_operation_init (GnlOperation * operation, GnlOperationClass * klass)
{
  operation->num_sinks = 1;
  operation->realsinks = 0;
  operation->element = NULL;
}

static gboolean
element_is_valid_filter (GstElement * element)
{
  GstElementFactory * factory;
  const GList * templates;
  gboolean havesink = FALSE;
  gboolean havesrc = FALSE;
  gboolean done = FALSE;
  GstIterator *pads;
  gpointer res;
  GstPad * pad;

  pads = gst_element_iterate_pads (element);

  while (!done) {
    switch (gst_iterator_next (pads, &res)) {
    case GST_ITERATOR_OK:
      pad = (GstPad *) res;
      if (gst_pad_get_direction (pad) == GST_PAD_SRC)
	havesrc = TRUE;
      else if (gst_pad_get_direction (pad) == GST_PAD_SINK)
	havesink = TRUE;
      break;
    case GST_ITERATOR_RESYNC:
      havesrc = FALSE;
      havesink = FALSE;
      break;
    case GST_ITERATOR_DONE:
    case GST_ITERATOR_ERROR:
      done = TRUE;
    }
  }

  if (havesrc && havesink)
    return TRUE;
  
  factory = gst_element_get_factory (element);
  
  for (templates = gst_element_factory_get_static_pad_templates (factory);
       templates; templates = g_list_next (templates)) {
    GstStaticPadTemplate * template = (GstStaticPadTemplate*) templates->data;
    
    if (template->direction == GST_PAD_SRC)
      havesrc = TRUE;
    else if (template->direction == GST_PAD_SINK)
      havesink = TRUE;
  }
  
  return (havesink && havesrc);
}

static gboolean
gnl_operation_add_element (GstBin * bin, GstElement * element)
{
  GnlOperation * operation = GNL_OPERATION (bin);
  gboolean res = FALSE;

  if (operation->element) {
    GST_WARNING_OBJECT (operation, "We already control an element : %s",
			GST_OBJECT_NAME (operation->element));
  } else {
    if (!element_is_valid_filter (element)) {
      GST_WARNING_OBJECT (operation, "Element %s is not a valid filter element");
    } else {
      res = GST_BIN_CLASS (parent_class)->add_element (bin, element);
    }
  }

  return res;
}

static gboolean
gnl_operation_remove_element (GstBin * bin, GstElement * element)
{
  GnlOperation * operation = GNL_OPERATION (bin);
  gboolean res = FALSE;

  if (operation->element) {
    if ((res = GST_BIN_CLASS (parent_class)->remove_element (bin, element)))
      operation->element = NULL;
    
  }
  return res;
}

static void
gnl_operation_set_sinks (GnlOperation * operation, guint sinks)
{
  /* FIXME : Check if sinkpad of element is on-demand .... */

  operation->num_sinks = sinks;
}

static void
gnl_operation_set_property (GObject *object, guint prop_id,
			    const GValue *value, GParamSpec *pspec)
{
  GnlOperation *operation;

  g_return_if_fail (GNL_IS_OPERATION (object));

  operation = GNL_OPERATION (object);

  switch (prop_id) {
    case ARG_SINKS:
      gnl_operation_set_sinks (operation, g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gnl_operation_get_property (GObject *object, guint prop_id,
			    GValue *value, GParamSpec *pspec)
{
  GnlOperation *operation;

  g_return_if_fail (GNL_IS_OPERATION (object));

  operation = GNL_OPERATION (object);

  switch (prop_id) {
    case ARG_SINKS:
      g_value_set_uint (value, operation->num_sinks);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
add_sink_pad (GnlOperation * operation)
{
  return TRUE;
}

static gboolean
remove_sink_pad (GnlOperation * operation)
{
  return TRUE;
}

static void
synchronize_sinks (GnlOperation * operation)
{
  if (operation->num_sinks == operation->realsinks)
    return;

  if (operation->num_sinks > operation->realsinks) {
    /* Add pad */
    add_sink_pad (operation);
  } else {
    /* Remove pad */
    remove_sink_pad (operation);
  }
}

static gboolean
gnl_operation_prepare (GnlObject * object)
{
  if (!(GNL_OPERATION (object)->element))
    return FALSE;

  /* Prepare the pads */
  synchronize_sinks (GNL_OPERATION (object));

  return TRUE;
}
