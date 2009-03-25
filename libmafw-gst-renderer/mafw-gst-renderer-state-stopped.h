/*
 * This file is a part of MAFW
 *
 * Copyright (C) 2007, 2008, 2009 Nokia Corporation, all rights reserved.
 *
 * Contact: Visa Smolander <visa.smolander@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef MAFW_GST_RENDERER_STATE_STOPPED_H
#define MAFW_GST_RENDERER_STATE_STOPPED_H


#include "mafw-gst-renderer.h"
#include "mafw-gst-renderer-state.h"

G_BEGIN_DECLS

/*----------------------------------------------------------------------------
  GObject type conversion macros
  ----------------------------------------------------------------------------*/

#define MAFW_TYPE_GST_RENDERER_STATE_STOPPED            \
        (mafw_gst_renderer_state_stopped_get_type())
#define MAFW_GST_RENDERER_STATE_STOPPED(obj)                            \
        (G_TYPE_CHECK_INSTANCE_CAST((obj), MAFW_TYPE_GST_RENDERER_STATE_STOPPED, \
				    MafwGstRendererStateStopped))
#define MAFW_IS_GST_RENDERER_STATE_STOPPED(obj)                         \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj), MAFW_TYPE_GST_RENDERER_STATE_STOPPED))
#define MAFW_GST_RENDERER_STATE_STOPPED_CLASS(klass)                    \
	(G_TYPE_CHECK_CLASS_CAST((klass), MAFW_TYPE_GST_RENDERER_STATE_STOPPED, \
				 MafwGstRendererStateStopped))
#define MAFW_GST_RENDERER_STATE_STOPPED_GET_CLASS(obj)                  \
	(G_TYPE_INSTANCE_GET_CLASS((obj), MAFW_TYPE_GST_RENDERER_STATE_STOPPED, \
				   MafwGstRendererStateStoppedClass))
#define MAFW_IS_GST_RENDERER_STATE_STOPPED_CLASS(klass)                 \
	(G_TYPE_CHECK_CLASS_TYPE((klass), MAFW_TYPE_GST_RENDERER_STATE_STOPPED))

/*----------------------------------------------------------------------------
  Type definitions
  ----------------------------------------------------------------------------*/


typedef struct _MafwGstRendererStateStopped MafwGstRendererStateStopped;
typedef struct _MafwGstRendererStateStoppedClass MafwGstRendererStateStoppedClass;

struct _MafwGstRendererStateStoppedClass {
	MafwGstRendererStateClass parent_class;
};

struct _MafwGstRendererStateStopped {
        MafwGstRendererState parent;
};

GType mafw_gst_renderer_state_stopped_get_type(void);

GObject *mafw_gst_renderer_state_stopped_new(MafwGstRenderer *renderer);

G_END_DECLS

#endif
