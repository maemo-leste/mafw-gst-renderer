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

#include "mafw-gst-renderer-state-paused.h"
#include "mafw-gst-renderer-utils.h"
#include <libmafw/mafw.h>

#undef  G_LOG_DOMAIN
#define G_LOG_DOMAIN "mafw-gst-renderer-state-paused"

/*----------------------------------------------------------------------------
  Playback
  ----------------------------------------------------------------------------*/

static void _do_play(MafwGstRendererState *self, GError **error);
static void _do_play_object(MafwGstRendererState *self, const gchar *object_id,
			    GError **error);
static void _do_stop(MafwGstRendererState *self, GError **error);
static void _do_resume(MafwGstRendererState *self, GError **error);
static void _do_set_position(MafwGstRendererState *self,
			     MafwRendererSeekMode mode, gint seconds,
			     GError **error);

/*----------------------------------------------------------------------------
  Playlist
  ----------------------------------------------------------------------------*/

static void _do_next(MafwGstRendererState *self, GError **error);
static void _do_previous(MafwGstRendererState *self, GError **error);
static void _do_goto_index(MafwGstRendererState *self, guint index,
			   GError **error);
/*----------------------------------------------------------------------------
  Notification metatada
  ----------------------------------------------------------------------------*/

static void _notify_metadata(MafwGstRendererState *self,
			     const gchar *object_id,
			     GHashTable *metadata,
			     GError **error);

/*----------------------------------------------------------------------------
  Notification worker
  ----------------------------------------------------------------------------*/

static void _notify_play(MafwGstRendererState *self, GError **error);
static void _notify_seek(MafwGstRendererState *self, GError **error);
static void _notify_buffer_status(MafwGstRendererState *self, gdouble percent,
				  GError **error);

/*----------------------------------------------------------------------------
  Playlist editing signals
  ----------------------------------------------------------------------------*/

static void _playlist_contents_changed(MafwGstRendererState *self,
				       gboolean clip_changed,
				       GError **error);

/*----------------------------------------------------------------------------
  GObject initialization
  ----------------------------------------------------------------------------*/

G_DEFINE_TYPE(MafwGstRendererStatePaused, mafw_gst_renderer_state_paused,
	      MAFW_TYPE_GST_RENDERER_STATE);

static void mafw_gst_renderer_state_paused_init(MafwGstRendererStatePaused *self)
{
}

static void mafw_gst_renderer_state_paused_class_init(
	MafwGstRendererStatePausedClass *klass)
{
        MafwGstRendererStateClass *state_class;

	state_class = MAFW_GST_RENDERER_STATE_CLASS(klass);
        g_return_if_fail(state_class != NULL);

	state_class->name = g_strdup("Paused");

	/* Playback */

	state_class->play         = _do_play;
	state_class->play_object  = _do_play_object;
	state_class->stop         = _do_stop;
        state_class->resume       = _do_resume;
	state_class->set_position = _do_set_position;

	/* Playlist */

	state_class->next       = _do_next;
	state_class->previous   = _do_previous;
	state_class->goto_index = _do_goto_index;

        /* Notification metadata */

        state_class->notify_metadata = _notify_metadata;

        /* Notification worker */

        state_class->notify_play = _notify_play;
        /* state_class->notify_pause is not allowed */
        state_class->notify_seek = _notify_seek;
        state_class->notify_buffer_status = _notify_buffer_status;

	/* Playlist editing signals */

	state_class->playlist_contents_changed =
		_playlist_contents_changed;
}

GObject *mafw_gst_renderer_state_paused_new(MafwGstRenderer *renderer)
{
	MafwGstRendererState *state;

        state = MAFW_GST_RENDERER_STATE(
		g_object_new(MAFW_TYPE_GST_RENDERER_STATE_PAUSED, NULL));
	state->renderer = renderer;

	return G_OBJECT(state);
}

/*----------------------------------------------------------------------------
  Playback
  ----------------------------------------------------------------------------*/

static void _do_play(MafwGstRendererState *self, GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PAUSED(self));
	mafw_gst_renderer_state_do_play(self, error);
}

static void _do_play_object(MafwGstRendererState *self, const gchar *object_id,
			    GError **error)
{
	MafwGstRendererPlaybackMode cur_mode, prev_mode;

        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PAUSED(self));

	prev_mode = mafw_gst_renderer_get_playback_mode(self->renderer);
	mafw_gst_renderer_state_do_play_object(self, object_id, error);
	cur_mode = mafw_gst_renderer_get_playback_mode(self->renderer);

	/* If this happens it means that we interrupted playlist playback
	   so let's resume it when play_object is finished */
	if (cur_mode != prev_mode) {
		self->renderer->resume_playlist = TRUE;
	}
}

static void _do_stop(MafwGstRendererState *self, GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PAUSED(self));

	/* Stop playback */
        mafw_gst_renderer_state_do_stop(self, error);
}

static void _do_resume(MafwGstRendererState *self, GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PAUSED(self));

        MafwGstRenderer *renderer = MAFW_GST_RENDERER_STATE(self)->renderer;
        mafw_gst_renderer_worker_resume(renderer->worker);

        /* Transition will be done after receiving notify_play */
}

static void _do_set_position(MafwGstRendererState *self,
			     MafwRendererSeekMode mode, gint seconds,
			     GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PAUSED(self));
	mafw_gst_renderer_state_do_set_position(self, mode, seconds, error);
}


/*----------------------------------------------------------------------------
  Playlist
  ----------------------------------------------------------------------------*/

static void _do_next(MafwGstRendererState *self, GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PAUSED(self));
	mafw_gst_renderer_state_do_next(self, error);
}

static void _do_previous(MafwGstRendererState *self, GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PAUSED(self));
	mafw_gst_renderer_state_do_prev(self, error);
}

static void _do_goto_index(MafwGstRendererState *self, guint index,
			   GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PAUSED(self));
	mafw_gst_renderer_state_do_goto_index(self, index, error);
}


/*----------------------------------------------------------------------------
  Notification metatada
  ----------------------------------------------------------------------------*/

static void _notify_metadata(MafwGstRendererState *self,
			     const gchar *object_id,
			     GHashTable *metadata,
			     GError **error)
{
	g_debug("running _notify_metadata...");
        /* Kindly Ignore this notification:
	   probably a delayed (now useless) metadata resolution */
}


/*----------------------------------------------------------------------------
  Notification worker
  ----------------------------------------------------------------------------*/

static void _notify_play(MafwGstRendererState *self, GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PAUSED(self));

        MafwGstRenderer *renderer = MAFW_GST_RENDERER_STATE(self)->renderer;

        /* Change status to play */
        mafw_gst_renderer_set_state(renderer, Playing);
}

static void _notify_seek(MafwGstRendererState *self, GError **error)
{
        mafw_gst_renderer_state_do_notify_seek(self, error);
}

static void _notify_buffer_status(MafwGstRendererState *self, gdouble percent,
				  GError **error)
{
	mafw_gst_renderer_state_do_notify_buffer_status (self, percent, error);
}

/*----------------------------------------------------------------------------
  Playlist editing signals
  ----------------------------------------------------------------------------*/

static void _playlist_contents_changed(MafwGstRendererState *self,
				       gboolean clip_changed,
				       GError **error)
{
	MafwGstRendererPlaybackMode mode;

        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PAUSED(self));

	/* Play the new index only if we are not in standalone mode.
	   Otherwise, when play_object finishes the new item will be
	   played if that's been suggested with renderer->resume_playlist */
	mode = mafw_gst_renderer_get_playback_mode(self->renderer);
	if (clip_changed && mode == MAFW_GST_RENDERER_MODE_PLAYLIST) {
		mafw_gst_renderer_state_do_play(self, error);
	}
}
