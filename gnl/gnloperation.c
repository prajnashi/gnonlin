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



#include "gnloperation.h"

static void 		gnl_operation_class_init 	(GnlOperationClass *klass);
static void 		gnl_operation_init 		(GnlOperation *operation);

static GnlSourceClass *parent_class = NULL;

GType
gnl_operation_get_type (void)
{
  static GType operation_type = 0;

  if (!operation_type) {
    static const GTypeInfo operation_info = {
      sizeof (GnlOperationClass),
      NULL,
      NULL,
      (GClassInitFunc) gnl_operation_class_init,
      NULL,
      NULL,
      sizeof (GnlOperation),
      32,
      (GInstanceInitFunc) gnl_operation_init,
    };
    operation_type = g_type_register_static (GST_TYPE_BIN, "GnlOperation", &operation_info, 0);
  }
  return operation_type;
}

static void
gnl_operation_class_init (GnlOperationClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class =       (GObjectClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_BIN);
}


static void
gnl_operation_init (GnlOperation *operation)
{
}


GnlOperation*
gnl_operation_new (const gchar *name)
{
  return NULL;
}


