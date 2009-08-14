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

#include <string.h>
#include "mafw-gst-renderer-state-transitioning.h"

#undef  G_LOG_DOMAIN
#define G_LOG_DOMAIN "mafw-gst-renderer-state-transitioning"

#define UPDATE_DELAY 10

/*----------------------------------------------------------------------------
  Playback
  ----------------------------------------------------------------------------*/

static void _do_play(MafwGstRendererState *self, GError **error);
static void _do_play_object(MafwGstRendererState *self, const gchar *object_id,
			    GError **error);
static void _do_pause(MafwGstRendererState *self, GError **error);
static void _do_stop(MafwGstRendererState *self, GError **error);
static void _do_resume(MafwGstRendererState *self, GError **error);
static void _do_get_position(MafwGstRendererState *self, gint *seconds, 
			     GError **error);

/*----------------------------------------------------------------------------
  Playlist
  ----------------------------------------------------------------------------*/

static void _do_next(MafwGstRendererState *self, GError **error);
static void _do_previous(MafwGstRendererState *self,GError **error);
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
static void _notify_pause(MafwGstRendererState *self,GError **error);

static void _notify_buffer_status(MafwGstRendererState *self,
				  gdouble percent,
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

G_DEFINE_TYPE(MafwGstRendererStateTransitioning,
	      mafw_gst_renderer_state_transitioning,
	      MAFW_TYPE_GST_RENDERER_STATE);

static void mafw_gst_renderer_state_transitioning_init(
	MafwGstRendererStateTransitioning *self)
{
}

static void mafw_gst_renderer_state_transitioning_class_init(
	MafwGstRendererStateTransitioningClass *klass)
{
	MafwGstRendererStateClass *state_klass ;

	state_klass = MAFW_GST_RENDERER_STATE_CLASS(klass);
	g_return_if_fail(state_klass != NULL);

	state_klass->name = g_strdup("Transitioning");

	/* Playback */

	state_klass->play         = _do_play;
	state_klass->play_object  = _do_play_object;
	state_klass->stop         = _do_stop;
	state_klass->pause        = _do_pause;
	state_klass->resume       = _do_resume;
	state_klass->get_position = _do_get_position;

	/* Playlist */

	state_klass->next       = _do_next;
	state_klass->previous   = _do_previous;
	state_klass->goto_index = _do_goto_index;

	/* Metadata */

	state_klass->notify_metadata = _notify_metadata;

	/* Notification worker */

  	state_klass->notify_play          = _notify_play;
	state_klass->notify_pause         = _notify_pause;
	state_klass->notify_buffer_status = _notify_buffer_status;

	/* Playlist editing signals */

	state_klass->playlist_contents_changed =
		_playlist_contents_changed;

	/* Property methods */

	state_klass->get_property_value = _get_property_value;
}

GObject *mafw_gst_renderer_state_transitioning_new(MafwGstRenderer *renderer)
{
	MafwGstRendererState *state;

        state = MAFW_GST_RENDERER_STATE(
		g_object_new(MAFW_TYPE_GST_RENDERER_STATE_TRANSITIONING, NULL));
	state->renderer = renderer;

	return G_OBJECT(state);
}

/*----------------------------------------------------------------------------
  Playback
  ----------------------------------------------------------------------------*/

static void _do_play(MafwGstRendererState *self, GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_TRANSITIONING(self));
      	mafw_gst_renderer_state_do_play(self, error);
}

static void _do_play_object(MafwGstRendererState *self, const gchar *object_id,
			    GError **error)
{
	MafwGstRendererPlaybackMode cur_mode, prev_mode;

        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_TRANSITIONING(self));

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
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_TRANSITIONING(self));

	/* Stop playback */
	mafw_gst_renderer_state_do_stop(self, error);
}

static void _do_pause(MafwGstRendererState *self, GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_TRANSITIONING(self));
	g_debug("Got pause while transitioning");
	self->renderer->worker->stay_paused = TRUE;
}

static void _do_resume(MafwGstRendererState *self, GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_TRANSITIONING(self));
	if (self->renderer->worker->stay_paused) {
		g_debug("Got resume while transitioning/paused");
		self->renderer->worker->stay_paused = FALSE;
	} else {
		g_set_error(error, MAFW_RENDERER_ERROR,
			    MAFW_RENDERER_ERROR_CANNOT_PLAY,
			    "cannot resume in transitioning state without "
			    "having paused before");
	}
}

static void _do_get_position(MafwGstRendererState *self, gint *seconds, 
			     GError **error)
{
	*seconds = 0;
}

/*----------------------------------------------------------------------------
  Playlist
  ----------------------------------------------------------------------------*/

static void _do_next(MafwGstRendererState *self, GError **error)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_TRANSITIONING(self));
	mafw_gst_renderer_state_do_next(self, error);
}

static void _do_previous(MafwGstRendererState *self, GError **error)
{
	g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_TRANSITIONING(self));
	mafw_gst_renderer_state_do_prev(self, error);
}

static void _do_goto_index(MafwGstRendererState *self, guint index,
			   GError **error)
{
	g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_TRANSITIONING(self));
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
	g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_TRANSITIONING(self));

	MafwGstRenderer *renderer;
	GValue *mval;
        gpointer value;
        gint nuris, i;
        gchar **uris;
        gchar *uri;

	g_debug("running _notify_metadata...");

	renderer = MAFW_GST_RENDERER_STATE(self)->renderer;

	/* If we have received metadata for the item that we are playing
	   then play it */
	if (object_id && renderer->media->object_id &&
	    !strcmp(object_id, renderer->media->object_id)) {
                /* Check how many uris provide the object_id */
                value = g_hash_table_lookup(metadata, MAFW_METADATA_KEY_URI);
                nuris = mafw_metadata_nvalues(value);
                if (nuris == 1) {
                        mval = mafw_metadata_first(metadata,
                                                   MAFW_METADATA_KEY_URI);
                        g_assert(mval);
			g_free(renderer->media->uri);
			renderer->media->uri =
                                g_strdup(g_value_get_string(mval));
			uri = renderer->media->uri;
                } else if (nuris > 1) {
                        uris = g_new0(gchar *, nuris + 1);
                        for (i = 0; i < nuris; i++) {
                                mval = g_value_array_get_nth(value, i);
                                uris[i] = (gchar *) g_value_get_string(mval);
                        }

                        /* Try the first URI, if that fails to play back another
                         * one will be selected until we get a successful one or
                         * all failed. On success, the selected URI will be
                         * emitted as metadata */
                        g_free(renderer->media->uri);
                        renderer->media->uri = g_strdup(uris[0]);
                } else {
                        g_assert_not_reached();
                }

                /* Set seekability property; currently, if several uris are
                 * provided it uses the value of the first uri. If later another
                 * uri is actually played, then this value should be changed. */
                mval = mafw_metadata_first(metadata,
                                           MAFW_METADATA_KEY_IS_SEEKABLE);
                if (mval != NULL) {
                        renderer->media->seekability =
                                g_value_get_boolean(mval) ?
                                SEEKABILITY_SEEKABLE : SEEKABILITY_NO_SEEKABLE;
                        g_debug("_notify_metadata: source seekability %d",
                                renderer->media->seekability);
                } else {
                        renderer->media->seekability = SEEKABILITY_UNKNOWN;
                        g_debug("_notify_metadata: source seekability unknown");
                }

		/* Check for source duration to keep it updated if needed */
                mval = mafw_metadata_first(metadata,
                                           MAFW_METADATA_KEY_DURATION);

                if (mval != NULL) {
                        renderer->media->duration = g_value_get_int(mval);
                        g_debug("_notify_metadata: source duration %d",
				renderer->media->duration);
		} else {
                        renderer->media->duration = -1;
                        g_debug("_notify_metadata: source duration unknown");
                }

                /* Play the available uri(s) */
                if (nuris == 1) {
			mafw_gst_renderer_worker_play(renderer->worker, uri);
		} else {
                        mafw_gst_renderer_worker_play_alternatives(
                                renderer->worker, uris);
                        g_free(uris);
                }
        }
}

/*----------------------------------------------------------------------------
  Notification worker
  ----------------------------------------------------------------------------*/

static void _notify_play(MafwGstRendererState *self, GError **error)
{
	g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_TRANSITIONING(self));

	MafwGstRenderer *renderer = MAFW_GST_RENDERER_STATE(self)->renderer;

	if (renderer->media->object_id)
	{
                renderer->update_playcount_id = g_timeout_add_seconds(
			UPDATE_DELAY,
			mafw_gst_renderer_update_stats,
			renderer);
	}

	mafw_gst_renderer_set_state(renderer, Playing);
}

static void _notify_pause(MafwGstRendererState *self, GError **error)
{
	g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_TRANSITIONING(self));

	MafwGstRenderer *renderer = MAFW_GST_RENDERER_STATE(self)->renderer;
	self->renderer->worker->stay_paused = FALSE;
        mafw_gst_renderer_set_state(renderer, Paused);
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

        g_return_if_fail(MAFW_IS_GST_RENDERER_STATE_TRANSITIONING(self));

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

	g_return_val_if_fail(MAFW_IS_GST_RENDERER_STATE_TRANSITIONING(self),
		value);

	if (!g_strcmp0(name, MAFW_PROPERTY_RENDERER_TRANSPORT_ACTIONS)) {
		value = g_new0(GValue, 1);
		g_value_init(value, G_TYPE_STRING);
		g_value_set_string(value, "");
	}

	return value;
}
