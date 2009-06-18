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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libmafw/mafw-renderer.h>
#include <libmafw/mafw-errors.h>

#include "mafw-gst-renderer.h"
#include "mafw-gst-renderer-state.h"

#undef  G_LOG_DOMAIN
#define G_LOG_DOMAIN "mafw-gst-renderer-state"

/*----------------------------------------------------------------------------
  Default playback implementations
  ----------------------------------------------------------------------------*/

static void _default_play(MafwGstRendererState *self, GError **error)
{
	g_set_error(error, MAFW_RENDERER_ERROR, MAFW_RENDERER_ERROR_CANNOT_PLAY,
		    "Play: operation not allowed in %s state",
		    MAFW_GST_RENDERER_STATE_GET_CLASS(self)->name);
}


static void _default_play_object(MafwGstRendererState *self,
				 const gchar *objectid,
				 GError **error)
{
	g_set_error(error, MAFW_RENDERER_ERROR, MAFW_RENDERER_ERROR_CANNOT_PLAY,
		    "Play object: operation not allowed in %s state",
		    MAFW_GST_RENDERER_STATE_GET_CLASS(self)->name);
}

static void _default_stop(MafwGstRendererState *self, GError **error)
{
 	g_set_error(error, MAFW_RENDERER_ERROR, MAFW_RENDERER_ERROR_CANNOT_STOP,
		    "Stop: operation not allowed in %s state",
		    MAFW_GST_RENDERER_STATE_GET_CLASS(self)->name);
}

static void _default_pause(MafwGstRendererState *self, GError **error)
{
	g_set_error(error, MAFW_RENDERER_ERROR, MAFW_RENDERER_ERROR_CANNOT_PAUSE,
		    "Pause: operation not allowed in %s state",
		    MAFW_GST_RENDERER_STATE_GET_CLASS(self)->name);
}

static void _default_resume(MafwGstRendererState *self, GError **error)
{
	g_set_error(error, MAFW_RENDERER_ERROR, MAFW_RENDERER_ERROR_CANNOT_PLAY,
		    "Resume: operation not allowed in %s state",
		    MAFW_GST_RENDERER_STATE_GET_CLASS(self)->name);
}

static void _default_set_position (MafwGstRendererState *self,
				   MafwRendererSeekMode mode, gint seconds,
				   GError **error)
{
	g_set_error(error, MAFW_RENDERER_ERROR, MAFW_RENDERER_ERROR_CANNOT_PLAY,
		    "Set position: operation not allowed in %s state",
		    MAFW_GST_RENDERER_STATE_GET_CLASS(self)->name);
}

static void _default_get_position (MafwGstRendererState *self,
				   gint *seconds,
				   GError **error)
{
	g_set_error(error, MAFW_RENDERER_ERROR, MAFW_RENDERER_ERROR_CANNOT_GET_POSITION,
		    "Get position: operation not allowed in %s state",
		    MAFW_GST_RENDERER_STATE_GET_CLASS(self)->name);
}

/*----------------------------------------------------------------------------
  Default playlist implementations
  ----------------------------------------------------------------------------*/

static void _default_next(MafwGstRendererState *self, GError **error)
{
	g_set_error(error, MAFW_EXTENSION_ERROR, MAFW_EXTENSION_ERROR_FAILED,
		    "Next: operation not allowed in %s state",
		    MAFW_GST_RENDERER_STATE_GET_CLASS(self)->name);
}

static void _default_previous(MafwGstRendererState *self, GError **error)
{
	g_set_error(error, MAFW_EXTENSION_ERROR, MAFW_EXTENSION_ERROR_FAILED,
		    "Previous: Operation not allowed in %s state",
		    MAFW_GST_RENDERER_STATE_GET_CLASS(self)->name);
}

static void _default_goto_index(MafwGstRendererState *self, guint index,
				GError **error)
{
	g_set_error(error, MAFW_EXTENSION_ERROR, MAFW_EXTENSION_ERROR_FAILED,
		    "Goto index: operation not allowed in %s state",
		    MAFW_GST_RENDERER_STATE_GET_CLASS(self)->name);
}

/*----------------------------------------------------------------------------
  Default notify  metadata implementation
  ----------------------------------------------------------------------------*/

static void _default_notify_metadata(MafwGstRendererState *self,
				     const gchar *object_id,
				     GHashTable *metadata,
				     GError **error)
{

	g_critical("Notify metadata: got unexpected metadata in %s state",
		    MAFW_GST_RENDERER_STATE_GET_CLASS(self)->name);
}

/*----------------------------------------------------------------------------
  Default notify worker implementations
  ----------------------------------------------------------------------------*/

static void _default_notify_play(MafwGstRendererState *self, GError **error)
{
	g_critical("Notify play: unexpected Play notification received in %s "
		   "state", MAFW_GST_RENDERER_STATE_GET_CLASS(self)->name);
}

static void _default_notify_pause(MafwGstRendererState *self, GError **error)
{

	g_critical("Notify pause: unexpected Pause notification received %s "
		   "state", MAFW_GST_RENDERER_STATE_GET_CLASS(self)->name);
}

static void _default_notify_seek(MafwGstRendererState *self, GError **error)
{
	g_critical("Notify seek: incorrect operation in %s state",
		   MAFW_GST_RENDERER_STATE_GET_CLASS(self)->name);
}

static void _default_notify_buffer_status(MafwGstRendererState *self,
					  gdouble percent,
					  GError **error)
{
	g_critical("Notify buffer status: incorrect operation in %s state",
		   MAFW_GST_RENDERER_STATE_GET_CLASS(self)->name);
}


static void _default_notify_eos(MafwGstRendererState *self, GError **error)
{
	g_critical("Notify eos: incorrect operation in %s state",
		   MAFW_GST_RENDERER_STATE_GET_CLASS(self)->name);
}

/*----------------------------------------------------------------------------
  Default playlist editing signal handlers implementation
  ----------------------------------------------------------------------------*/

static void _default_playlist_contents_changed(MafwGstRendererState *self,
					       gboolean clip_changed,
					       GError **error)
{
	g_warning("playlist::contents-changed not implemented in %s state",
		  MAFW_GST_RENDERER_STATE_GET_CLASS(self)->name);
}

/*----------------------------------------------------------------------------
  Default property methods implementation
  ----------------------------------------------------------------------------*/

static GValue* _default_get_property_value(MafwGstRendererState *self,
					const gchar *name)
{
	g_warning("get_property_value function not implemented in %s state",
		  MAFW_GST_RENDERER_STATE_GET_CLASS(self)->name);
	return NULL;
}

/*----------------------------------------------------------------------------
  Default memory card event handlers implementation
  ----------------------------------------------------------------------------*/

static void _default_handle_pre_unmount(MafwGstRendererState *self,
					const gchar *mount_point)
{
	g_debug("pre-unmount signal received: %s in state %s", mount_point,
		MAFW_GST_RENDERER_STATE_GET_CLASS(self)->name);
}

/*----------------------------------------------------------------------------
  GObject initialization
  ----------------------------------------------------------------------------*/

G_DEFINE_ABSTRACT_TYPE(MafwGstRendererState, mafw_gst_renderer_state,
		       G_TYPE_OBJECT);

static void mafw_gst_renderer_state_init(MafwGstRendererState *self)
{
}

static void mafw_gst_renderer_state_class_init(MafwGstRendererStateClass *klass)
{
	/* Playback */

	klass->play         = _default_play;
	klass->play_object  = _default_play_object;
	klass->stop         = _default_stop;
	klass->pause        = _default_pause;
	klass->resume       = _default_resume;
	klass->set_position = _default_set_position;
	klass->get_position = _default_get_position;

	/* Playlist */

	klass->next              = _default_next;
	klass->previous          = _default_previous;
	klass->goto_index        = _default_goto_index;

	/* Notification metadata */

	klass->notify_metadata = _default_notify_metadata;

	/* Notification worker */

	klass->notify_play          = _default_notify_play;
	klass->notify_pause         = _default_notify_pause;
	klass->notify_seek          = _default_notify_seek;
	klass->notify_buffer_status = _default_notify_buffer_status;
	klass->notify_eos           = _default_notify_eos;

	klass->notify_eos           = _default_notify_eos;

	/* Playlist editing signals */

	klass->playlist_contents_changed =
		_default_playlist_contents_changed;

	/* Property methods */

	klass->get_property_value = _default_get_property_value;

	/* Memory card event handlers */

	klass->handle_pre_unmount = _default_handle_pre_unmount;
}

/*----------------------------------------------------------------------------
  Playback
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_state_play(MafwGstRendererState *self, GError **error)

{
	MAFW_GST_RENDERER_STATE_GET_CLASS(self)->play(self, error);
}

void mafw_gst_renderer_state_play_object(MafwGstRendererState *self,
				       const gchar *object_id,
				       GError **error)
{
	MAFW_GST_RENDERER_STATE_GET_CLASS(self)->play_object(self, object_id,
							   error);
}

void mafw_gst_renderer_state_stop(MafwGstRendererState *self, GError **error)
{
	MAFW_GST_RENDERER_STATE_GET_CLASS(self)->stop(self, error);
}

void mafw_gst_renderer_state_pause(MafwGstRendererState *self, GError **error)
{
 	MAFW_GST_RENDERER_STATE_GET_CLASS(self)->pause(self, error);
}

void mafw_gst_renderer_state_resume(MafwGstRendererState *self, GError **error)
{
	MAFW_GST_RENDERER_STATE_GET_CLASS(self)->resume(self, error);
}

void mafw_gst_renderer_state_set_position(MafwGstRendererState *self,
					 MafwRendererSeekMode mode, gint seconds,
					 GError **error)
{
	MAFW_GST_RENDERER_STATE_GET_CLASS(self)->set_position(self, mode, seconds,
							     error);
}

void mafw_gst_renderer_state_get_position(MafwGstRendererState *self,
					  gint *seconds,
					  GError **error)
{
	MAFW_GST_RENDERER_STATE_GET_CLASS(self)->get_position(self, seconds, 
							      error);
}

/*----------------------------------------------------------------------------
  Playlist
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_state_next(MafwGstRendererState *self, GError **error)
{
	MAFW_GST_RENDERER_STATE_GET_CLASS(self)->next(self, error);
}

void mafw_gst_renderer_state_previous(MafwGstRendererState *self, GError **error)
{
	MAFW_GST_RENDERER_STATE_GET_CLASS(self)->previous(self, error);
}

void mafw_gst_renderer_state_goto_index(MafwGstRendererState *self, guint index,
				      GError **error)
{
	MAFW_GST_RENDERER_STATE_GET_CLASS(self)->goto_index(self, index, error);

}

/*----------------------------------------------------------------------------
  Notification metatada
  ----------------------------------------------------------------------------*/

void  mafw_gst_renderer_state_notify_metadata(MafwGstRendererState *self,
					    const gchar *object_id,
					    GHashTable *metadata,
					    GError **error)
{
	MAFW_GST_RENDERER_STATE_GET_CLASS(self)->notify_metadata(self, object_id,
							       metadata,
							       error);
}

/*----------------------------------------------------------------------------
  Notification worker
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_state_notify_play(MafwGstRendererState *self,
					GError **error)
{
	MAFW_GST_RENDERER_STATE_GET_CLASS(self)->notify_play(self, error);
}

void mafw_gst_renderer_state_notify_pause(MafwGstRendererState *self,
					GError **error)
{
	MAFW_GST_RENDERER_STATE_GET_CLASS(self)->notify_pause(self, error);
}

void mafw_gst_renderer_state_notify_seek(MafwGstRendererState *self,
					GError **error)
{
	MAFW_GST_RENDERER_STATE_GET_CLASS(self)->notify_seek(self, error);
}

void mafw_gst_renderer_state_notify_buffer_status(MafwGstRendererState *self,
						gdouble percent,
						GError **error)
{
	MAFW_GST_RENDERER_STATE_GET_CLASS(self)->notify_buffer_status(self,
								    percent,
								    error);
}

void mafw_gst_renderer_state_notify_eos(MafwGstRendererState *self,
				       GError **error)
{
	MAFW_GST_RENDERER_STATE_GET_CLASS(self)->notify_eos(self, error);
}

/*----------------------------------------------------------------------------
  Playlist editing handlers
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_state_playlist_contents_changed_handler(
	MafwGstRendererState *self,
	gboolean clip_changed,
	GError **error)
{
	MAFW_GST_RENDERER_STATE_GET_CLASS(self)->playlist_contents_changed(
		self,
		clip_changed,
		error);
}

/*----------------------------------------------------------------------------
  Property methods
  ----------------------------------------------------------------------------*/

GValue* mafw_gst_renderer_state_get_property_value(MafwGstRendererState *self,
						   const gchar *name)
{
	return MAFW_GST_RENDERER_STATE_GET_CLASS(self)->get_property_value(
		self,
		name);
}

/*----------------------------------------------------------------------------
  Memory card event handlers
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_state_handle_pre_unmount(MafwGstRendererState *self,
						const gchar *mount_point)
{
	MAFW_GST_RENDERER_STATE_GET_CLASS(self)->
		handle_pre_unmount(self, mount_point);
}

/*----------------------------------------------------------------------------
  Helpers
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_state_do_play(MafwGstRendererState *self, GError **error)
{
        MafwGstRenderer *renderer;
        GError *gm_error = NULL;
	MafwGstRendererPlaybackMode mode;

	renderer = MAFW_GST_RENDERER_STATE(self)->renderer;

        /* Stop any on going playback */
        mafw_gst_renderer_worker_stop(renderer->worker);

	/* Play command only affects playlists, so switch to playlist
	   mode first if necessary */
	mode = mafw_gst_renderer_get_playback_mode(renderer);
	if (mode == MAFW_GST_RENDERER_MODE_STANDALONE) {
		mafw_gst_renderer_set_playback_mode(
			renderer, MAFW_GST_RENDERER_MODE_PLAYLIST);
		mafw_gst_renderer_set_media_playlist(renderer);
	}

	/* Do we have any objectid to play? Otherwise we cannot do it */
        if (renderer->media->object_id) {
		/* If so, resolve URI for this objectid */
                mafw_gst_renderer_get_metadata(renderer,
					     renderer->media->object_id,
					     &gm_error);
                if (gm_error) {
			MafwGstRendererErrorClosure *error_closure;
			if (error) {
                                g_set_error(error,
                                            MAFW_RENDERER_ERROR,
                                            MAFW_RENDERER_ERROR_NO_MEDIA,
                                            "Unable to find media");
			}

			/* This is a playback error: execute error policy */
			error_closure = g_new0(MafwGstRendererErrorClosure, 1);
			error_closure->renderer = renderer;
			error_closure->error = g_error_copy(gm_error);
			g_idle_add(mafw_gst_renderer_manage_error_idle,
				   error_closure);

			g_error_free(gm_error);
                } else {
                        mafw_gst_renderer_set_state(renderer, Transitioning);
		}
        } else if (error) {
                g_set_error(error,
                            MAFW_RENDERER_ERROR,
                            MAFW_RENDERER_ERROR_NO_MEDIA,
                            "There is no media to play");
                mafw_gst_renderer_set_state(renderer, Stopped);
        }
}

void  mafw_gst_renderer_state_do_play_object(MafwGstRendererState *self,
					   const gchar *object_id,
					   GError **error)
{
        MafwGstRenderer *renderer;
        GError *gm_error = NULL;

	renderer = MAFW_GST_RENDERER_STATE(self)->renderer;

	/* Stop any ongoing playback */
        mafw_gst_renderer_worker_stop(renderer->worker);

        if (object_id) {
		/* Switch to standalone mode */
		mafw_gst_renderer_set_playback_mode(
			renderer, MAFW_GST_RENDERER_MODE_STANDALONE);

                mafw_gst_renderer_set_object(renderer, object_id);
                mafw_gst_renderer_get_metadata(renderer,
					     renderer->media->object_id,
					     &gm_error);
                if (gm_error) {
			MafwGstRendererErrorClosure *error_closure;
                        if (error) {
                                g_set_error(error,
                                            MAFW_RENDERER_ERROR,
                                            MAFW_RENDERER_ERROR_NO_MEDIA,
                                            "Unable to find media");
			}

			/* This is a playback error: execute error policy */
			error_closure = g_new0(MafwGstRendererErrorClosure, 1);
			error_closure->renderer = renderer;
			error_closure->error = g_error_copy(gm_error);
			g_idle_add(mafw_gst_renderer_manage_error_idle,
				   error_closure);
                        g_error_free(gm_error);
                } else {
			/* Play object has been successful */
                        mafw_gst_renderer_set_state(renderer, Transitioning);
		}
        } else if (error) {
                g_set_error(error,
                            MAFW_RENDERER_ERROR,
                            MAFW_RENDERER_ERROR_NO_MEDIA,
                            "There is no media to play");
                mafw_gst_renderer_set_state(renderer, Stopped);
        }
}

void mafw_gst_renderer_state_do_stop(MafwGstRendererState *self, GError **error)
{
        MafwGstRenderer *renderer;
	MafwGstRendererPlaybackMode mode;

	renderer = MAFW_GST_RENDERER_STATE(self)->renderer;

	/* Stop any ongoing playback */
	mafw_gst_renderer_worker_stop(renderer->worker);

	/* Cancel update */
	if (renderer->update_playcount_id > 0) {
		g_source_remove(renderer->update_playcount_id);
		renderer->update_playcount_id = 0;
	}

	/* Set new state */
	mafw_gst_renderer_set_state(renderer, Stopped);

	/* If we were playing a standalone object, then go back
	   to playlist mode and stay stopped */
	mode = mafw_gst_renderer_get_playback_mode(renderer);
	if (mode == MAFW_GST_RENDERER_MODE_STANDALONE) {
		mafw_gst_renderer_set_playback_mode(
			renderer, MAFW_GST_RENDERER_MODE_PLAYLIST);
		mafw_gst_renderer_set_media_playlist(renderer);
	}
}

void mafw_gst_renderer_state_do_next (MafwGstRendererState *self, GError **error)
{
        MafwGstRenderer *renderer;
	MafwGstRendererMovementResult move_type;
	MafwGstRendererPlaybackMode mode;

	renderer = MAFW_GST_RENDERER_STATE(self)->renderer;

	/* If we are in standalone mode, we switch back to playlist
	 * mode. Then we resume playback only if renderer->resume_playlist
	 * was set.
	 * If we are in playlist mode we just move to the next and
	 * play.
	 */
	mode = mafw_gst_renderer_get_playback_mode(renderer);
	if (mode == MAFW_GST_RENDERER_MODE_STANDALONE) {
		mafw_gst_renderer_set_playback_mode(
			renderer, MAFW_GST_RENDERER_MODE_PLAYLIST);
		mafw_gst_renderer_set_media_playlist(renderer);
	}

	move_type = mafw_gst_renderer_move(renderer,
					  MAFW_GST_RENDERER_MOVE_TYPE_NEXT,
					  0, error);
	switch (move_type) {
	case MAFW_GST_RENDERER_MOVE_RESULT_OK:
		if (mode == MAFW_GST_RENDERER_MODE_PLAYLIST ||
		    renderer->resume_playlist) {
			/* We issued the comand in playlist mode, or
			  in standalone mode but with resume_playlist
			  set, so let's play the new item */
			mafw_gst_renderer_state_play(self, error);

		} else {
			/* We issued the command in standalone mode and we
			  do not want to resume playlist, so let's
			  move to Stopped */
			mafw_gst_renderer_state_stop(self, NULL);
		}
		break;
	case MAFW_GST_RENDERER_MOVE_RESULT_NO_PLAYLIST:
		g_set_error(error,
			    MAFW_RENDERER_ERROR,
			    MAFW_RENDERER_ERROR_NO_MEDIA,
			    "There is no playlist or media to play");
		mafw_gst_renderer_state_stop(self, NULL);
		break;
	case MAFW_GST_RENDERER_MOVE_RESULT_PLAYLIST_LIMIT:
		/* Normal mode */
		mafw_playlist_iterator_reset(renderer->iterator, NULL);
		mafw_gst_renderer_set_media_playlist(renderer);
		mafw_gst_renderer_state_play(self, error);
		break;
	case MAFW_GST_RENDERER_MOVE_RESULT_ERROR:
		break;
	default:
		g_critical("Movement not controlled");
	}
}

void mafw_gst_renderer_state_do_prev(MafwGstRendererState *self, GError **error)
{
        MafwGstRenderer *renderer;
	MafwGstRendererMovementResult move_type;
	MafwGstRendererPlaybackMode mode;

	renderer = MAFW_GST_RENDERER_STATE(self)->renderer;

	mode = mafw_gst_renderer_get_playback_mode(renderer);
	if (mode == MAFW_GST_RENDERER_MODE_STANDALONE) {
		mafw_gst_renderer_set_playback_mode(
			renderer, MAFW_GST_RENDERER_MODE_PLAYLIST);
		mafw_gst_renderer_set_media_playlist(renderer);
	}

	move_type = mafw_gst_renderer_move(renderer,
					  MAFW_GST_RENDERER_MOVE_TYPE_PREV,
					  0, error);
	switch (move_type) {
	case MAFW_GST_RENDERER_MOVE_RESULT_OK:
		if (mode == MAFW_GST_RENDERER_MODE_PLAYLIST ||
		    renderer->resume_playlist) {
			/* We issued the comand in playlist mode, or
			  in standalone mode but with resume_playlist
			  set, so let's play the new item */
			mafw_gst_renderer_state_play(self, error);

		} else {
			/* We issued the command in standalone mode and we
			  do not want to resume playlist, so let's
			  move to Stopped */
			mafw_gst_renderer_state_stop(self, NULL);
		}
		break;
	case MAFW_GST_RENDERER_MOVE_RESULT_NO_PLAYLIST:
		g_set_error(error,
			    MAFW_RENDERER_ERROR,
			    MAFW_RENDERER_ERROR_NO_MEDIA,
			    "There is no playlist or media to play");
		mafw_gst_renderer_state_stop(self, NULL);
		break;
	case MAFW_GST_RENDERER_MOVE_RESULT_PLAYLIST_LIMIT:
		/* Normal mode */
		mafw_playlist_iterator_move_to_last(renderer->iterator, NULL);
		mafw_gst_renderer_set_media_playlist(renderer);
		mafw_gst_renderer_state_play(self, error);
		break;
	case MAFW_GST_RENDERER_MOVE_RESULT_ERROR:
		break;
	default:
		g_critical("Movement not controlled");
	}
}


void mafw_gst_renderer_state_do_goto_index(MafwGstRendererState *self,
					 guint index,
					 GError **error)
{
        MafwGstRenderer *renderer;
	MafwGstRendererMovementResult move_type;
	MafwGstRendererPlaybackMode mode;

	renderer = MAFW_GST_RENDERER_STATE(self)->renderer;

	/* If we are in standalone mode, we switch back to playlist
	 * mode. Then we resume playback only if renderer->resume_playlist
	 * was set.
	 * If we are in playlist mode we just move to the next and
	 * play.
	 */
	mode = mafw_gst_renderer_get_playback_mode(renderer);
	if (mode == MAFW_GST_RENDERER_MODE_STANDALONE) {
		mafw_gst_renderer_set_playback_mode(
			renderer, MAFW_GST_RENDERER_MODE_PLAYLIST);
		mafw_gst_renderer_set_media_playlist(renderer);
	}

	move_type = mafw_gst_renderer_move(renderer, MAFW_GST_RENDERER_MOVE_TYPE_INDEX, index, error);

	switch (move_type) {
	case MAFW_GST_RENDERER_MOVE_RESULT_OK:
		if (mode == MAFW_GST_RENDERER_MODE_PLAYLIST ||
		    renderer->resume_playlist) {
			/* We issued the comand in playlist mode, or
			  in standalone mode but with resume_playlist
			  set, so let's play the new item */
			mafw_gst_renderer_state_play(self, error);

		} else {
			/* We issued the command in standalone mode and we
			  do not want to resume playlist, so let's
			  move to Stopped */
			mafw_gst_renderer_state_stop(self, NULL);
		}
		break;
	case MAFW_GST_RENDERER_MOVE_RESULT_NO_PLAYLIST:
		g_set_error(error,
			    MAFW_RENDERER_ERROR,
			    MAFW_RENDERER_ERROR_NO_MEDIA,
			    "There is no playlist or media to play");
		mafw_gst_renderer_state_stop(self, NULL);
		break;
 	case MAFW_GST_RENDERER_MOVE_RESULT_PLAYLIST_LIMIT:
		g_set_error(error,
			    MAFW_RENDERER_ERROR,
			    MAFW_RENDERER_ERROR_INDEX_OUT_OF_BOUNDS,
			    "Index is out of bounds");
		mafw_gst_renderer_state_stop(self, NULL);
		break;
	case MAFW_GST_RENDERER_MOVE_RESULT_ERROR:
		break;
	default:
		g_critical("Movement not controlled");
	}
}

void mafw_gst_renderer_state_do_get_position(MafwGstRendererState *self,
					    gint *seconds,
					    GError **error)
{
	*seconds = mafw_gst_renderer_worker_get_position(self->renderer->worker);
	if (*seconds < 0) {
		*seconds = 0;
		g_set_error(error, MAFW_EXTENSION_ERROR, 
			    MAFW_RENDERER_ERROR_CANNOT_GET_POSITION,
			    "Position query failed");
	}
}

void mafw_gst_renderer_state_do_set_position(MafwGstRendererState *self,
					    MafwRendererSeekMode mode,
					    gint seconds,
					    GError **error)
{
	MafwGstRenderer *renderer;
	GstSeekType seektype;

        renderer = MAFW_GST_RENDERER_STATE(self)->renderer;

	/* TODO Gst stuff should be moved to worker, not handled here... */
	if (mode == SeekAbsolute) {
		if (seconds < 0) {
			seektype = GST_SEEK_TYPE_END;
			seconds *= -1;
		} else {
			seektype = GST_SEEK_TYPE_SET;
		}
	} else if (mode == SeekRelative) {
		seektype = GST_SEEK_TYPE_CUR;
	} else {
		g_critical("Unknown seek mode: %d", mode);
		g_set_error(error, MAFW_EXTENSION_ERROR,
			    MAFW_EXTENSION_ERROR_INVALID_PARAMS,
			    "Unknown seek mode: %d", mode);
		return;
	}
	if (renderer->seek_pending) {
		g_debug("seek pending, storing position %d", seconds);
		renderer->seek_type_pending = seektype;
		renderer->seeking_to = seconds;
	} else {
		renderer->seek_pending = TRUE;
                mafw_gst_renderer_worker_set_position(renderer->worker,
						     seektype,
						     seconds,
						     error);
	}
}

void mafw_gst_renderer_state_do_notify_seek(MafwGstRendererState *self,
					   GError **error)
{
        MafwGstRenderer *renderer;

	renderer = MAFW_GST_RENDERER_STATE(self)->renderer;

        if (renderer->seeking_to != -1) {
                renderer->seek_pending = TRUE;
                mafw_gst_renderer_worker_set_position(renderer->worker,
                                                    renderer->seek_type_pending,
                                                    renderer->seeking_to,
                                                    NULL);
        } else {
                renderer->seek_pending = FALSE;
        }
        renderer->seeking_to = -1;
}

void mafw_gst_renderer_state_do_notify_buffer_status(MafwGstRendererState *self,
						    gdouble percent,
						    GError **error)
{
        MafwGstRenderer *renderer = NULL;

        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE(self));

        renderer = MAFW_GST_RENDERER_STATE(self)->renderer;

        if (percent >= 100.0) {
                renderer->buffering = FALSE;
        } else if (!renderer->buffering) {
                renderer->buffering = TRUE;
        }

	mafw_renderer_emit_buffering_info(MAFW_RENDERER(renderer), percent / 100.0);
}
