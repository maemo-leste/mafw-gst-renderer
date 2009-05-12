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
#ifndef MAFW_GST_RENDERER_H
#define MAFW_GST_RENDERER_H

#include <glib-object.h>
#include <libmafw/mafw-renderer.h>
#include <libmafw/mafw-registry.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <libhal.h>
#include <gio/gio.h>

#include "mafw-gst-renderer-utils.h"
#include "mafw-gst-renderer-worker.h"
#include "mafw-playlist-iterator.h"
/* Solving the cyclic dependencies */
typedef struct _MafwGstRenderer MafwGstRenderer;
typedef struct _MafwGstRendererClass MafwGstRendererClass;
#include "mafw-gst-renderer-state.h"

#ifdef HAVE_CONIC
#include <conicconnection.h>
#endif

typedef enum {
	MAFW_GST_RENDERER_ERROR_PLUGIN_NOT_FOUND,
	MAFW_GST_RENDERER_ERROR_VIDEO_CODEC_NOT_SUPPORTED,
 	MAFW_GST_RENDERER_ERROR_AUDIO_CODEC_NOT_SUPPORTED,
} MafwGstRendererError;

typedef enum {
	MAFW_GST_RENDERER_MODE_PLAYLIST,
	MAFW_GST_RENDERER_MODE_STANDALONE,
} MafwGstRendererPlaybackMode;

typedef enum {
	MAFW_GST_RENDERER_MOVE_RESULT_OK,
	MAFW_GST_RENDERER_MOVE_RESULT_NO_PLAYLIST,
	MAFW_GST_RENDERER_MOVE_RESULT_PLAYLIST_LIMIT,
	MAFW_GST_RENDERER_MOVE_RESULT_ERROR,
} MafwGstRendererMovementResult;

typedef enum {
	MAFW_GST_RENDERER_MOVE_TYPE_INDEX,
	MAFW_GST_RENDERER_MOVE_TYPE_PREV,
	MAFW_GST_RENDERER_MOVE_TYPE_NEXT,
} MafwGstRendererMovementType;

#ifdef HAVE_GDKPIXBUF
#define MAFW_PROPERTY_GST_RENDERER_CURRENT_FRAME_ON_PAUSE       \
	"current-frame-on-pause"
#endif

/*----------------------------------------------------------------------------
  GObject type conversion macros
  ----------------------------------------------------------------------------*/

#define MAFW_TYPE_GST_RENDERER                  \
        (mafw_gst_renderer_get_type())
#define MAFW_GST_RENDERER(obj)                                          \
        (G_TYPE_CHECK_INSTANCE_CAST((obj), MAFW_TYPE_GST_RENDERER, MafwGstRenderer))
#define MAFW_IS_GST_RENDERER(obj)                                       \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj), MAFW_TYPE_GST_RENDERER))
#define MAFW_GST_RENDERER_CLASS(klass)                                  \
	(G_TYPE_CHECK_CLASS_CAST((klass), MAFW_TYPE_GST_RENDERER, MafwGstRenderer))
#define MAFW_GST_RENDERER_GET_CLASS(obj)                                \
	(G_TYPE_INSTANCE_GET_CLASS((obj), MAFW_TYPE_GST_RENDERER,       \
				   MafwGstRendererClass))
#define MAFW_IS_GST_RENDERER_CLASS(klass)                               \
	(G_TYPE_CHECK_CLASS_TYPE((klass), MAFW_TYPE_GST_RENDERER))

#define MAFW_GST_RENDERER_ERROR (mafw_gst_renderer_error_quark ())

/* Gst renderer plugin name for the plugin descriptor */
#define MAFW_GST_RENDERER_PLUGIN_NAME "Mafw-Gst-Renderer-Plugin"
/* Gst renderer name */
#define MAFW_GST_RENDERER_NAME "Mafw-Gst-Renderer"
/* Gst renderer UUID */
#define MAFW_GST_RENDERER_UUID "gstrenderer"

/*----------------------------------------------------------------------------
  Type definitions
  ----------------------------------------------------------------------------*/

typedef struct {
	gchar *object_id;
	gchar *uri;
	gchar *title;
	gchar *artist;
	gchar *album;

	guint duration;
	gint position;

	/* Seekability coming from source */
	SeekabilityType seekability;
} MafwGstRendererMedia;

struct _MafwGstRendererClass {
	MafwRendererClass parent;
};

/*
 * media:             Current media details
 * worker:            Worker
 * registry:          The registry that owns this renderer
 * media_timer:      Stream timer data
 * current_state:     The renderer's current state
 * requested_state:   When transitioning, stores the next state to set
 * playlist:          The renderer's playlist
 * play_index:        A playlist index that is currently playing
 * buffering:         Buffering indicator
 * seek_pending:      Seek is pending or ongoing
 * seek_type_pending: Type of the pending seek
 * seeking_to:        The position of pending seek (milliseconds)
 * is_stream:         is the URI a stream?
 * play_failed_count: The number of unably played items from the playlist.
 * playback_mode:     Playback mode
 * resume_playlist:   Do we want to resume playlist playback when play_object
 *                    is finished
 * states:            State array
 * error_policy:      error policy
 */
struct _MafwGstRenderer{
	MafwRenderer parent;

	MafwGstRendererMedia *media;
	MafwGstRendererWorker *worker;
	MafwRegistry *registry;
	LibHalContext *hal_ctx;
	MafwPlayState current_state;
	MafwPlaylist *playlist;
	MafwPlaylistIterator *iterator;
	gboolean buffering;
	gboolean seek_pending;
	GstSeekType seek_type_pending;
	gint seeking_to;
	gboolean is_stream;
        gint update_playcount_id;
	guint play_failed_count;

	MafwGstRendererPlaybackMode playback_mode;
	gboolean resume_playlist;
 	MafwGstRendererState **states;
	MafwRendererErrorPolicy error_policy;

#ifdef HAVE_CONIC
	gboolean connected;
	ConIcConnection *connection;
#endif
	GVolumeMonitor *volume_monitor;
};

typedef struct {
        MafwGstRenderer *renderer;
        GError *error;
} MafwGstRendererErrorClosure;

G_BEGIN_DECLS

GType mafw_gst_renderer_get_type(void);
GObject *mafw_gst_renderer_new(MafwRegistry *registry);
GQuark mafw_gst_renderer_error_quark(void);

/*----------------------------------------------------------------------------
  Playback
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_play(MafwRenderer *self, MafwRendererPlaybackCB callback,
                            gpointer user_data);
void mafw_gst_renderer_play_object(MafwRenderer *self, const gchar *object_id,
                                   MafwRendererPlaybackCB callback,
                                   gpointer user_data);
void mafw_gst_renderer_stop(MafwRenderer *self, MafwRendererPlaybackCB callback,
                            gpointer user_data);
void mafw_gst_renderer_pause(MafwRenderer *self, MafwRendererPlaybackCB callback,
                             gpointer user_data);
void mafw_gst_renderer_resume(MafwRenderer *self, MafwRendererPlaybackCB callback,
                              gpointer user_data);

/*----------------------------------------------------------------------------
  Status
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_get_status(MafwRenderer *self, MafwRendererStatusCB callback,
                                  gpointer user_data);

/*----------------------------------------------------------------------------
  Set Media
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_set_object(MafwGstRenderer *self, const gchar *object_id);
void mafw_gst_renderer_clear_media(MafwGstRenderer *self);

/*----------------------------------------------------------------------------
  Metadata
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_get_metadata(MafwGstRenderer* self, const gchar* objectid,
                                    GError **error);
gboolean mafw_gst_renderer_update_stats(gpointer data);

/*----------------------------------------------------------------------------
  Playlist
  ----------------------------------------------------------------------------*/

gboolean mafw_gst_renderer_assign_playlist(MafwRenderer *self,
                                           MafwPlaylist *playlist,
                                           GError **error);
void mafw_gst_renderer_next(MafwRenderer *self, MafwRendererPlaybackCB callback,
                            gpointer user_data);
void mafw_gst_renderer_previous(MafwRenderer *self, MafwRendererPlaybackCB callback,
                                gpointer user_data);
void mafw_gst_renderer_goto_index(MafwRenderer *self, guint index,
                                  MafwRendererPlaybackCB callback,
                                  gpointer user_data);
MafwGstRendererMovementResult mafw_gst_renderer_move(MafwGstRenderer *renderer,
                                                     MafwGstRendererMovementType type,
                                                     guint index,
                                                     GError **error);

/*----------------------------------------------------------------------------
  Set media
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_set_media_playlist(MafwGstRenderer* self);

/*----------------------------------------------------------------------------
  Position
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_set_position(MafwRenderer *self,
                                    MafwRendererSeekMode mode, gint seconds,
                                    MafwRendererPositionCB callback,
                                    gpointer user_data);
void mafw_gst_renderer_get_position(MafwRenderer *self, MafwRendererPositionCB callback,
                                    gpointer user_data);

/*----------------------------------------------------------------------------
  Local API
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_set_state(MafwGstRenderer *self, MafwPlayState state);

gboolean mafw_gst_renderer_manage_error_idle(gpointer data);

void mafw_gst_renderer_manage_error(MafwGstRenderer *self, const GError *error);

void mafw_gst_renderer_set_playback_mode(MafwGstRenderer *self,
                                         MafwGstRendererPlaybackMode mode);

MafwGstRendererPlaybackMode mafw_gst_renderer_get_playback_mode(
	MafwGstRenderer *self);

G_END_DECLS

#endif

/* vi: set noexpandtab ts=8 sw=8 cino=t0,(0: */
