/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstelements.c:
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

#include "config.h"
#include "gnlsource.h"
#include "gnllayer.h"

GST_DEBUG_CATEGORY (gnl_debug);

static gboolean
plugin_init (GstPlugin *plugin)
{
  GST_DEBUG_CATEGORY_INIT (gnl_debug, "gnl", 0, "gnonlinear");

  /* XXX is GST_RANK_NONE correct? */
  if (!gst_element_register (plugin, "gnlsource", GST_RANK_NONE, GNL_TYPE_SOURCE))
    return FALSE;
  if (!gst_element_register (plugin, "gnllayer", GST_RANK_NONE, GNL_TYPE_LAYER))
    return FALSE;
  
  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gnlelements",
  "Standard elements for nonlinear video editing",
  plugin_init,
  VERSION,
  GNL_LICENSE,
  GNL_PACKAGE,
  GNL_ORIGIN)

