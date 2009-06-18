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

#include "mafw-gst-renderer-state-playing.h"
#include "mafw-gst-renderer-utils.h"
#include <libmafw/mafw.h>

#undef  G_LOG_DOMAIN
#define G_LOG_DOMAIN "mafw-gst-renderer-state-playing"

/*----------------------------------------------------------------------------
  Playback
  ----------------------------------------------------------------------------*/

static void _do_play(MafwGstRendererState *self, GError **error);
static void _do_play_object(MafwGstRendererState *self, const gchar *object_id,
			    GError **error);
static void _do_stop(MafwGstRendererState *self, GError **error);
static void _do_pause(MafwGstRendererState *self, GError **error);
static void _do_set_position(MafwGstRendererState *self,
			     MafwRendererSeekMode mode, gint seconds,
			     GError **error);
static void _do_get_position(MafwGstRendererState *self,
			     gint *seconds,
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
static void _notify_pause(MafwGstRendererState *self, GError **error);
static void _notify_seek(MafwGstRendererState *self, GError **error);
static void _notify_buffer_status(MafwGstRendererState *self, gdouble percent,
				  GError **error);
static void _notify_eos(MafwGstRendererState *self, GError **error);

/*----------------------------------------------------------------------------
  Playlist editing signals
  ----------------------------------------------------------------------------*/

static void _playlist_contents_changed(MafwGstRendererState *self,
				       gboolean clip_changed,
				       GError **error);

/*----------------------------------------------------------------------------
  Property methods
  ----------------------------------------------------------------------------*/

static GValue* _get_property_value(MafwGstRendererState *self,
				   const gchar *name);

/*----------------------------------------------------------------------------
  Memory card event handlers
  ----------------------------------------------------------------------------*/

static void _handle_pre_unmount(MafwGstRendererState *self,
				const gchar *mount_point);

/*----------------------------------------------------------------------------
  GObject initialization
  ----------------------------------------------------------------------------*/

G_DEFINE_TYPE(MafwGstRendererStatePlaying, mafw_gst_renderer_state_playing,
	      MAFW_TYPE_GST_RENDERER_STATE);

static void mafw_gst_renderer_state_playing_init(MafwGstRendererStatePlaying *self)
{
}

static void mafw_gst_renderer_state_playing_class_init(
	MafwGstRendererStatePlayingClass *klass)
{
        MafwGstRendererStateClass *state_class;

	state_class = MAFW_GST_RENDERER_STATE_CLASS(klass);
        g_return_if_fail(state_class != NULL);

	state_class->name = g_strdup("Playing");

	/* Playback */

	state_class->play         = _do_play;
	state_class->play_object  = _do_play_object;
	state_class->stop         = _do_stop;
	state_class->pause        = _do_pause;
        /* state_class->resume is not allowed */
	state_class->set_position = _do_set_position;
	state_class->get_position = _do_get_position;

	/* Playlist */

	state_class->next       = _do_next;
	state_class->previous   = _do_previous;
	state_class->goto_index = _do_goto_index;

        /* Notification metadata */

        state_class->notify_metadata = _notify_metadata;

        /* Notification worker */

        state_class->notify_play          = _notify_play;
        state_class->notify_pause         = _notify_pause;
        state_class->notify_seek          = _notify_seek;
        state_class->notify_buffer_status = _notify_buffer_status;
        state_class->notify_eos           = _notify_eos;

	/* Playlist editing signals */

	state_class->playlist_contents_changed =
		_playlist_contents_changed;

	/* Property methods */

	state_class->get_property_value = _get_property_value;

	/* Memory card event handlers */

	state_class->handle_pre_unmount = _handle_pre_unmount;
}

GObject *mafw_gst_renderer_state_playing_new(MafwGstRenderer *renderer)
{
	MafwGstRendererState *state;

	state = MAFW_GST_RENDERER_STATE(
		g_object_new(MAFW_TYPE_GST_RENDERER_STATE_PLAYING, NULL));
	state->renderer = renderer;

	return G_OBJECT(state);
}

/*----------------------------------------------------------------------------
  Playback
  ----------------------------------------------------------------------------*/

static void _do_play(MafwGstRendererState *self, GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PLAYING(self));
	mafw_gst_renderer_state_do_play(self, error);
}

static void _do_play_object(MafwGstRendererState *self, const gchar *object_id,
			    GError **error)
{
	MafwGstRendererPlaybackMode cur_mode, prev_mode;

        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PLAYING(self));

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
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PLAYING(self));

	/* Stop playback */
        mafw_gst_renderer_state_do_stop(self, error);
}

static void _do_pause(MafwGstRendererState *self, GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PLAYING(self));

        MafwGstRenderer *renderer = MAFW_GST_RENDERER_STATE(self)->renderer;
        mafw_gst_renderer_worker_pause(renderer->worker);

        /* Transition will be done when receiving pause
         * notification */
}

static void _do_set_position(MafwGstRendererState *self,
			     MafwRendererSeekMode mode, gint seconds,
			     GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PLAYING(self));
	mafw_gst_renderer_state_do_set_position(self, mode, seconds, error);
}

static void _do_get_position(MafwGstRendererState *self,
			     gint *seconds,
			     GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PLAYING(self));
	mafw_gst_renderer_state_do_get_position(self, seconds, error);
}


/*----------------------------------------------------------------------------
  Playlist
  ----------------------------------------------------------------------------*/

static void _do_next(MafwGstRendererState *self, GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PLAYING(self));
	mafw_gst_renderer_state_do_next(self, error);
}

static void _do_previous(MafwGstRendererState *self, GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PLAYING(self));
	mafw_gst_renderer_state_do_prev(self, error);
}

static void _do_goto_index(MafwGstRendererState *self, guint index,
			   GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PLAYING(self));
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
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PLAYING(self));
        /* Kindly ignore this notification: it's received when seeking
         * in a stream */
}

static void _notify_pause(MafwGstRendererState *self, GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PLAYING(self));

        MafwGstRenderer *renderer = MAFW_GST_RENDERER_STATE(self)->renderer;

        /* Change status to pause */
        mafw_gst_renderer_set_state(renderer, Paused);
}

static void _notify_seek(MafwGstRendererState *self, GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PLAYING(self));
        mafw_gst_renderer_state_do_notify_seek(self, error);
}

static void _notify_buffer_status(MafwGstRendererState *self, gdouble percent,
				  GError **error)
{
	mafw_gst_renderer_state_do_notify_buffer_status (self, percent, error);
}

static void _notify_eos(MafwGstRendererState *self, GError **error)
{
        MafwGstRenderer *renderer;
	MafwGstRendererMovementResult move_type;
	MafwGstRendererPlaybackMode mode;

	renderer = MAFW_GST_RENDERER_STATE(self)->renderer;

        /* Update playcount */
	if (renderer->update_playcount_id > 0) {
		g_source_remove(renderer->update_playcount_id);
                mafw_gst_renderer_update_stats(renderer);
        }

	/* Notice: playback has already stopped, so calling
	 * mafw_gst_renderer_stop or mafw_gst_renderer_state_stop
	 * here is an error.
	 * To set the renderer state to Stopped use this instead:
	 * mafw_gst_renderer_set_state(self->renderer, Stopped);
	 */

	/* If we are not in playlist mode, switch to it,
	   otherwise move to the next in the playlist */
	mode = mafw_gst_renderer_get_playback_mode(renderer);
	if (mode == MAFW_GST_RENDERER_MODE_STANDALONE) {
		mafw_gst_renderer_worker_stop(self->renderer->worker);
		mafw_gst_renderer_set_state(self->renderer, Stopped);
		mafw_gst_renderer_set_playback_mode(
			renderer, MAFW_GST_RENDERER_MODE_PLAYLIST);
		mafw_gst_renderer_set_media_playlist(renderer);

		/* Do we have to resume playlist playback? */
		if (renderer->resume_playlist) {
			mafw_gst_renderer_state_play(self, error);
		}
	} else {
		/* Move to next in playlist */
		move_type =
			mafw_gst_renderer_move(renderer,
					      MAFW_GST_RENDERER_MOVE_TYPE_NEXT,
					      0, error);

		switch (move_type) {
		case MAFW_GST_RENDERER_MOVE_RESULT_OK:
			mafw_gst_renderer_state_play(self, error);
			break;
		case MAFW_GST_RENDERER_MOVE_RESULT_PLAYLIST_LIMIT:
		case MAFW_GST_RENDERER_MOVE_RESULT_NO_PLAYLIST:
			mafw_gst_renderer_worker_stop(self->renderer->worker);
			mafw_gst_renderer_set_state(self->renderer, Stopped);
			break;
		case MAFW_GST_RENDERER_MOVE_RESULT_ERROR:
			break;
		default:
			g_critical("Movement not controlled");
		}
	}
}

/*----------------------------------------------------------------------------
  Playlist editing signals
  ----------------------------------------------------------------------------*/

static void _playlist_contents_changed(MafwGstRendererState *self,
				       gboolean clip_changed,
				       GError **error)
{
	MafwGstRendererPlaybackMode mode;

        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_PLAYING(self));

	/* Play the new index only if we are not in standalone mode.
	   Otherwise, when play_object finishes the new item will be
	   played if that's been suggested with renderer->resume_playlist */
	mode = mafw_gst_renderer_get_playback_mode(self->renderer);
	if (clip_changed && mode == MAFW_GST_RENDERER_MODE_PLAYLIST) {
		mafw_gst_renderer_state_do_play(self, error);
	}
}

/*----------------------------------------------------------------------------
  Property methods
  ----------------------------------------------------------------------------*/

GValue* _get_property_value(MafwGstRendererState *self, const gchar *name)
{
	GValue *value = NULL;

	g_return_val_if_fail(MAFW_IS_GST_RENDERER_STATE_PLAYING(self), value);

	if (!g_strcmp0(name, MAFW_PROPERTY_RENDERER_TRANSPORT_ACTIONS)) {
		gboolean is_seekable =
			mafw_gst_renderer_worker_get_seekable(
				self->renderer->worker);

		value = g_new0(GValue, 1);
		g_value_init(value, G_TYPE_STRING);
		if (is_seekable) {
			g_value_set_string(value, "seek");
		} else {
			g_value_set_string(value, "");
		}
	}

	return value;
}

/*----------------------------------------------------------------------------
  Memory card event handlers
  ----------------------------------------------------------------------------*/

static void _handle_pre_unmount(MafwGstRendererState *self,
				const gchar *mount_point)
{
	/* If not playing anything, bail out */
	if (!self->renderer->media->uri) {
		return;
	}

	gchar *mount_uri = g_filename_to_uri(mount_point, NULL, NULL);

	if (g_str_has_prefix(self->renderer->media->uri, mount_uri)) {
		mafw_gst_renderer_stop(MAFW_RENDERER(self->renderer),
				       NULL,
				       NULL);
	}
}
