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

#ifndef MAFW_GST_RENDERER_STATE_H
#define MAFW_GST_RENDERER_STATE_H


#include <glib-object.h>
#include "mafw-gst-renderer-worker.h"

/* Solving the cyclic dependencies */
typedef struct _MafwGstRendererState MafwGstRendererState;
typedef struct _MafwGstRendererStateClass MafwGstRendererStateClass;
#include "mafw-gst-renderer.h"

G_BEGIN_DECLS

/*----------------------------------------------------------------------------
  GObject type conversion macros
  ----------------------------------------------------------------------------*/

#define MAFW_TYPE_GST_RENDERER_STATE            \
        (mafw_gst_renderer_state_get_type())
#define MAFW_GST_RENDERER_STATE(obj)                                    \
        (G_TYPE_CHECK_INSTANCE_CAST((obj), MAFW_TYPE_GST_RENDERER_STATE, \
				    MafwGstRendererState))
#define MAFW_IS_GST_RENDERER_STATE(obj)                                 \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj), MAFW_TYPE_GST_RENDERER_STATE))
#define MAFW_GST_RENDERER_STATE_CLASS(klass)                            \
	(G_TYPE_CHECK_CLASS_CAST((klass), MAFW_TYPE_GST_RENDERER_STATE, \
				 MafwGstRendererStateClass))
#define MAFW_GST_RENDERER_STATE_GET_CLASS(obj)                          \
	(G_TYPE_INSTANCE_GET_CLASS((obj), MAFW_TYPE_GST_RENDERER_STATE, \
				   MafwGstRendererStateClass))
#define MAFW_IS_GST_RENDERER_STATE_CLASS(klass)                         \
	(G_TYPE_CHECK_CLASS_TYPE((klass), MAFW_TYPE_GST_RENDERER_STATE))

/*----------------------------------------------------------------------------
  Type definitions
  ----------------------------------------------------------------------------*/


struct _MafwGstRendererStateClass {
	GObjectClass parent_class;
	const gchar* name;

	/* Playback */

	void (*play)(MafwGstRendererState *self, GError **error);
	void (*play_object)(MafwGstRendererState *self, const gchar *object_id,
			    GError **error);
	void (*stop)(MafwGstRendererState *self, GError **error);
	void (*pause)(MafwGstRendererState *self, GError **error);
	void (*resume)(MafwGstRendererState *self, GError **error);
	void (*set_position) (MafwGstRendererState *self,
			      MafwRendererSeekMode mode, gint seconds,
			      GError **error);
	void (*get_position) (MafwGstRendererState *self,
			      gint *seconds,
			      GError **error);

	/* Playlist */

	void (*next)(MafwGstRendererState *self, GError **error);
	void (*previous)(MafwGstRendererState *self, GError **error);
	void (*goto_index)(MafwGstRendererState *self, guint index,
			   GError **error);

	/* Notification metadata */

	void (*notify_metadata)(MafwGstRendererState *self,
			        const gchar *object_id,
			        GHashTable *metadata,
			        GError **error);


	/* Notifications */

	void (*notify_play)(MafwGstRendererState *self, GError **error);
	void (*notify_pause)(MafwGstRendererState *self, GError **error);
	void (*notify_seek)(MafwGstRendererState *self, GError **error);
	void (*notify_buffer_status)(MafwGstRendererState *self, gdouble percent,
				     GError **error);
	void (*notify_eos) (MafwGstRendererState *self, GError **error);

	/* Playlist editing signals */

	void (*playlist_contents_changed)(MafwGstRendererState *self,
					  gboolean clip_changed,
					  GError **error);
	/* Property methods */

	GValue* (*get_property_value)(MafwGstRendererState *self,
				      const gchar *name);

	/* Memory card event handlers */

	void (*handle_pre_unmount)(MafwGstRendererState *self,
				   const gchar *mount_point);
};

struct _MafwGstRendererState {
	GObject parent;

	MafwGstRenderer *renderer;
};

GType mafw_gst_renderer_state_get_type(void);


/*----------------------------------------------------------------------------
  Playback
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_state_play(MafwGstRendererState *self, GError **error);
void mafw_gst_renderer_state_play_object(MafwGstRendererState *self,
                                         const gchar *object_id,
                                         GError **error);
void mafw_gst_renderer_state_stop(MafwGstRendererState *self, GError **error);
void mafw_gst_renderer_state_pause(MafwGstRendererState *self, GError **error);
void mafw_gst_renderer_state_resume(MafwGstRendererState *self, GError **error);
void mafw_gst_renderer_state_set_position(MafwGstRendererState *self,
                                          MafwRendererSeekMode mode, gint seconds,
                                          GError **error);
void mafw_gst_renderer_state_get_position(MafwGstRendererState *self,
                                          gint *seconds,
                                          GError **error);

/*----------------------------------------------------------------------------
  Playlist
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_state_next(MafwGstRendererState *self, GError **error);
void mafw_gst_renderer_state_previous(MafwGstRendererState *self, GError **error);
void mafw_gst_renderer_state_goto_index(MafwGstRendererState *self, guint index,
                                        GError **error);


/*----------------------------------------------------------------------------
  Notification metatada
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_state_notify_metadata(MafwGstRendererState *self,
                                             const gchar *object_id,
                                             GHashTable *metadata,
                                             GError **error);

/*----------------------------------------------------------------------------
  Notification worker
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_state_notify_play(MafwGstRendererState *self,
                                         GError **error);
void mafw_gst_renderer_state_notify_pause(MafwGstRendererState *self,
                                          GError **error);
void mafw_gst_renderer_state_notify_seek(MafwGstRendererState *self,
                                         GError **error);
void mafw_gst_renderer_state_notify_buffer_status(MafwGstRendererState *self,
                                                  gdouble percent,
                                                  GError **error);
void mafw_gst_renderer_state_notify_eos(MafwGstRendererState *self,
                                        GError **error);

/*----------------------------------------------------------------------------
  Playlist editing handlers
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_state_playlist_contents_changed_handler(
	MafwGstRendererState *self,
	gboolean clip_changed,
	GError **error);

/*----------------------------------------------------------------------------
  Property methods
  ----------------------------------------------------------------------------*/

GValue* mafw_gst_renderer_state_get_property_value(MafwGstRendererState *self,
						   const gchar *name);

/*----------------------------------------------------------------------------
  Memory card event handlers
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_state_handle_pre_unmount(MafwGstRendererState *self,
						const gchar *mount_point);

/*----------------------------------------------------------------------------
  Helpers
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_state_do_play(MafwGstRendererState *self, GError **error);
void mafw_gst_renderer_state_do_play_object(MafwGstRendererState *self,
                                            const gchar *object_id,
                                            GError **error);
void mafw_gst_renderer_state_do_stop(MafwGstRendererState *self,
                                     GError **error);
void mafw_gst_renderer_state_do_next(MafwGstRendererState *self,
                                     GError **error);
void mafw_gst_renderer_state_do_prev(MafwGstRendererState *self,
                                     GError **error);
void mafw_gst_renderer_state_do_goto_index(MafwGstRendererState *self,
                                           guint index,
                                           GError **error);
void mafw_gst_renderer_state_do_set_position(MafwGstRendererState *self,
                                             MafwRendererSeekMode mode, gint seconds,
                                             GError **error);
void mafw_gst_renderer_state_do_get_position(MafwGstRendererState *self,
                                             gint *seconds,
                                             GError **error);
void mafw_gst_renderer_state_do_notify_seek(MafwGstRendererState *self,
                                            GError **error);
void mafw_gst_renderer_state_do_notify_buffer_status(MafwGstRendererState *self,
                                                     gdouble percent,
                                                     GError **error);

G_END_DECLS

#endif
