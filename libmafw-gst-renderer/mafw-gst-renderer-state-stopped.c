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

#include <libmafw/mafw-errors.h>
#include "mafw-gst-renderer-state-stopped.h"

#undef  G_LOG_DOMAIN
#define G_LOG_DOMAIN "mafw-gst-renderer-state-stopped"

/*----------------------------------------------------------------------------
  Playback
  ----------------------------------------------------------------------------*/

static void _do_play(MafwGstRendererState *self, GError **error);
static void _do_play_object(MafwGstRendererState *self, const gchar *object_id,
			    GError **error);
static void _do_stop(MafwGstRendererState *self, GError **error);

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
  GObject initialization
  ----------------------------------------------------------------------------*/

G_DEFINE_TYPE(MafwGstRendererStateStopped, mafw_gst_renderer_state_stopped,
	      MAFW_TYPE_GST_RENDERER_STATE);

static void mafw_gst_renderer_state_stopped_init(MafwGstRendererStateStopped *self)
{
}

static void mafw_gst_renderer_state_stopped_class_init(
	MafwGstRendererStateStoppedClass *klass)
{
	MafwGstRendererStateClass *state_klass;

	state_klass = MAFW_GST_RENDERER_STATE_CLASS(klass);
	g_return_if_fail(state_klass != NULL);

	state_klass->name = g_strdup("Stopped");

	/* Playback */

	state_klass->play        = _do_play;
	state_klass->play_object = _do_play_object;
	state_klass->stop        = _do_stop;

	/* Playlist */

	state_klass->next       = _do_next;
	state_klass->previous   = _do_previous;
	state_klass->goto_index = _do_goto_index;

	/* Metadata */

	state_klass->notify_metadata = _notify_metadata;

	/* Playlist editing signals */

	state_klass->playlist_contents_changed =
		_playlist_contents_changed;

	/* Property methods */

	state_klass->get_property_value = _get_property_value;
}

GObject *mafw_gst_renderer_state_stopped_new(MafwGstRenderer *renderer)
{
	MafwGstRendererState *state;

        state = MAFW_GST_RENDERER_STATE(
		g_object_new(MAFW_TYPE_GST_RENDERER_STATE_STOPPED, NULL));
	state->renderer = renderer;

	return G_OBJECT(state);
}

/*----------------------------------------------------------------------------
  Playback
  ----------------------------------------------------------------------------*/

static void _do_play(MafwGstRendererState *self, GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_STOPPED(self));
	mafw_gst_renderer_state_do_play(self, error);
}

static void _do_play_object(MafwGstRendererState *self, const gchar *object_id,
			    GError **error)
{
	MafwGstRendererPlaybackMode cur_mode, prev_mode;

        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_STOPPED(self));

	prev_mode = mafw_gst_renderer_get_playback_mode(self->renderer);
	mafw_gst_renderer_state_do_play_object(self, object_id, error);
	cur_mode = mafw_gst_renderer_get_playback_mode(self->renderer);

	/* If this happens it means that we interrupted playlist mode
	   but we did so in Stopped state, so when play_object finishes
	   we want to stay Stopped */
	if (cur_mode != prev_mode) {
		self->renderer->resume_playlist = FALSE;
	}
}

static void _do_stop(MafwGstRendererState *self, GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_STOPPED(self));
	/* We are already in Stopped state, so do nothing */
}

/*----------------------------------------------------------------------------
  Playlist
  ----------------------------------------------------------------------------*/

static void _do_next(MafwGstRendererState *self, GError **error)
{
        MafwGstRenderer *renderer = NULL;
	MafwGstRendererMovementResult value = MAFW_GST_RENDERER_MOVE_RESULT_OK;

        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_STOPPED(self));

	renderer = MAFW_GST_RENDERER_STATE(self)->renderer;

	value = mafw_gst_renderer_move(renderer,
				      MAFW_GST_RENDERER_MOVE_TYPE_NEXT,
				      0, error);

	switch (value) {
	case MAFW_GST_RENDERER_MOVE_RESULT_ERROR:
	case MAFW_GST_RENDERER_MOVE_RESULT_OK:
		break;
	case MAFW_GST_RENDERER_MOVE_RESULT_NO_PLAYLIST:
		g_set_error (error,
			     MAFW_RENDERER_ERROR,
			     MAFW_RENDERER_ERROR_NO_MEDIA,
			     "There is no playlist or media to play");
		break;
	case MAFW_GST_RENDERER_MOVE_RESULT_PLAYLIST_LIMIT:
		mafw_playlist_iterator_reset(renderer->iterator, NULL);
		mafw_gst_renderer_set_media_playlist(renderer);
		break;
	default:
		g_critical("Movement not controlled");
	}
}

static void _do_previous(MafwGstRendererState *self, GError **error)
{
        MafwGstRenderer *renderer = NULL;
	MafwGstRendererMovementResult value = MAFW_GST_RENDERER_MOVE_RESULT_OK;


        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_STOPPED(self));

	renderer = MAFW_GST_RENDERER_STATE(self)->renderer;

	value = mafw_gst_renderer_move(renderer,
				      MAFW_GST_RENDERER_MOVE_TYPE_PREV,
				      0, error);

	switch (value) {
	case MAFW_GST_RENDERER_MOVE_RESULT_ERROR:
	case MAFW_GST_RENDERER_MOVE_RESULT_OK:
		break;
	case MAFW_GST_RENDERER_MOVE_RESULT_NO_PLAYLIST:
		g_set_error(error,
			    MAFW_RENDERER_ERROR,
			    MAFW_RENDERER_ERROR_NO_MEDIA,
			    "There is no playlist or media to play");
		break;
	case MAFW_GST_RENDERER_MOVE_RESULT_PLAYLIST_LIMIT:

		mafw_playlist_iterator_move_to_last(renderer->iterator, NULL);
		mafw_gst_renderer_set_media_playlist(renderer);
		break;
	default:
		g_critical("Movement not controlled");
	}
}

static void _do_goto_index(MafwGstRendererState *self, guint index,
			   GError **error)
{
        MafwGstRenderer *renderer = NULL;
	MafwGstRendererMovementResult value = MAFW_GST_RENDERER_MOVE_RESULT_OK;

        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_STOPPED(self));

	renderer = MAFW_GST_RENDERER_STATE(self)->renderer;

	value = mafw_gst_renderer_move(renderer,
				      MAFW_GST_RENDERER_MOVE_TYPE_INDEX,
				      index, error);

	switch (value) {
	case MAFW_GST_RENDERER_MOVE_RESULT_ERROR:
	case MAFW_GST_RENDERER_MOVE_RESULT_OK:
		break;
	case MAFW_GST_RENDERER_MOVE_RESULT_NO_PLAYLIST:
		g_set_error(error,
			    MAFW_RENDERER_ERROR,
			    MAFW_RENDERER_ERROR_NO_MEDIA,
			    "There is no playlist or media to play");
		break;
	case MAFW_GST_RENDERER_MOVE_RESULT_PLAYLIST_LIMIT:
		g_set_error(error,
			    MAFW_RENDERER_ERROR,
			    MAFW_RENDERER_ERROR_INDEX_OUT_OF_BOUNDS,
			    "Index is out of bounds");
		break;
	default:
		g_critical("Movement not controlled");
	}
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
	/* This happens because we issued a play() command, this moved us to
	   Transitioning state, waiting for the URL of the objectid to play,
	   but before we got the URL and moved to Playing state, a stop()
	   command was issued. Now we got the results of the stopped play()
	   command, so we just ignore the result and stay in Stopped state. */

}

/*----------------------------------------------------------------------------
  Playlist editing signals
  ----------------------------------------------------------------------------*/

static void _playlist_contents_changed(MafwGstRendererState *self,
				       gboolean clip_changed,
				       GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_STOPPED(self));

	/* Do nothing, we just stay in Stopped state in any case */
}

/*----------------------------------------------------------------------------
  Property methods
  ----------------------------------------------------------------------------*/

GValue* _get_property_value(MafwGstRendererState *self, const gchar *name)
{
	GValue *value = NULL;

	g_return_val_if_fail(MAFW_IS_GST_RENDERER_STATE_STOPPED(self), value);

	if (!g_strcmp0(name, MAFW_PROPERTY_RENDERER_TRANSPORT_ACTIONS)) {
		value = g_new0(GValue, 1);
		g_value_init(value, G_TYPE_STRING);
		g_value_set_string(value, "");
	}

	return value;
}
