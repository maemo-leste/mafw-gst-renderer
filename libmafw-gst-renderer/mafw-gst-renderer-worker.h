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
#ifndef MAFW_GST_RENDERER_WORKER_H
#define MAFW_GST_RENDERER_WORKER_H

#include <X11/Xdefs.h>
#include <glib-object.h>
#include <gst/gst.h>
#include "mafw-gst-renderer-worker-volume.h"

#define MAFW_GST_RENDERER_MAX_TMP_FILES 5

typedef struct _MafwGstRendererWorker MafwGstRendererWorker;

typedef void (*MafwGstRendererWorkerNotifySeekCb)(MafwGstRendererWorker *worker, gpointer owner);
typedef void (*MafwGstRendererWorkerNotifyPauseCb)(MafwGstRendererWorker *worker, gpointer owner);
typedef void (*MafwGstRendererWorkerNotifyPlayCb)(MafwGstRendererWorker *worker, gpointer owner);
typedef void (*MafwGstRendererWorkerNotifyBufferStatusCb)(MafwGstRendererWorker *worker, gpointer owner, gdouble percent);
typedef void (*MafwGstRendererWorkerNotifyEOSCb)(MafwGstRendererWorker *worker, gpointer owner);
typedef void (*MafwGstRendererWorkerNotifyErrorCb)(MafwGstRendererWorker *worker,
                                                   gpointer owner,
                                                   const GError *error);

typedef enum {
	WORKER_MODE_SINGLE_PLAY,
        WORKER_MODE_PLAYLIST,
        WORKER_MODE_REDUNDANT,
} PlaybackMode;

typedef enum {
	SEEKABILITY_UNKNOWN = -1,
	SEEKABILITY_NO_SEEKABLE,
	SEEKABILITY_SEEKABLE,
} SeekabilityType;

/*
 * media:        Information about currently selected media.
 *   location:           Current media location
 *   length_nanos:       Length of the media, in nanoseconds
 *   has_visual_content: the clip contains some visual content (video)
 *   video_width:        If media contains video, this tells the video width
 *   video_height:       If media contains video, this tells the video height
 *   seekable:           Tells whether the media can be seeked
 *   par_n:              Video pixel aspect ratio numerator
 *   par_d:              Video pixel aspect ratio denominator
 * owner:        Owner of the worker; usually a MafwGstRenderer (FIXME USUALLY?)
 * pipeline:     Playback pipeline
 * bus:          Message bus
 * state:        Current playback pipeline state
 * is_stream:    Is currently playing media a stream
 * muted:        Is the audio muted
 * eos:          Has playback reached EOS already
 * is_error:     Has there been an error situation
 * buffering:    Indicates the buffering state
 * prerolling:   Indicates the prerolling state (NULL -> PAUSED)
 * report_statechanges: Report state change bus messages
 * current_volume:      Current audio volume [0.0 .. 1.0], see playbin:volume
 * async_bus_id:        ID handle for GstBus
 * buffer_probe_id:     ID of the video renderer buffer probe
 * seek_position:       Indicates the pos where to seek, in seconds
 * vsink:               Video sink element of the pipeline
 * asink:               Audio sink element of the pipeline
 * xid:                 XID for video playback
 * current_frame_on_pause: whether to emit current frame when pausing
 */
struct _MafwGstRendererWorker {
	struct {
		gchar *location;
		gint64 length_nanos;
		gboolean has_visual_content;
		gint video_width;
		gint video_height;
		gdouble fps;
		SeekabilityType seekable;
		gint par_n;
		gint par_d;
	} media;
	PlaybackMode mode;
	struct {
		GSList *items;
		gint current;
		gboolean notify_play_pending;
	} pl;
        gpointer owner;
	GstElement *pipeline;
	GstBus *bus;
	/* GStreamer state we are considering right now */
	GstState state;
	MafwGstRendererWorkerVolume *wvolume;
	gboolean is_stream;
	gboolean muted;
	/* we are handing eos or we did */
	gboolean eos;
	/* if we are handling (or handled) and error */
	gboolean is_error;
	/* pipeline is buffering */
	gboolean buffering;
	/* pipeline is prerolling */
	gboolean prerolling;
	/* stream is live and doesn't need prerolling */
	gboolean is_live;
	/* if we have to stay in paused though a do_play was
	 * requested. Usually used when pausing in transitioning */
	gboolean stay_paused;
	/* this variable should be FALSE while we are hiding state
	 * changed to the UI. This is that GStreamer can perform
	 * state_changes without us requiring it, for example, then
	 * seeking, buffering and so on and we have to hide those
	 * changes */
	gboolean report_statechanges;
	guint async_bus_id;
	gint seek_position;
	guint ready_timeout;
	guint duration_seek_timeout;
	/* After some time PAUSED, we set the pipeline to READY in order to
	 * save resources. This field states if we are in this special
	 * situation.
	 * It is set to TRUE when the state change to READY is requested
	 * and stays like that until we reach again PLAYING state (not PAUSED).
	 * The reason for this is that when resuming streams, we have to 
	 * move from READY to PAUSED, then seek to the position where the
	 * stream had been paused, then wait for buffering to finish, and then
	 * play (and notify the state change to PLAYING), and we have to
	 * differentiate this case from the one in which we have entered PAUSED
	 * silently (when we ran out of buffer while playing, because in that
	 * case, when we are done buffering we want to resume playback silently
	 * again.
	 */
	gboolean in_ready;
	GstElement *vsink;
	GstElement *asink;
	XID xid;
	gboolean autopaint;
	gint colorkey;
	GPtrArray *tag_list;
	GHashTable *current_metadata;

#ifdef HAVE_GDKPIXBUF
	gboolean current_frame_on_pause;
	gchar *tmp_files_pool[MAFW_GST_RENDERER_MAX_TMP_FILES];
	guint8 tmp_files_pool_index;
#endif

        /* Handlers for notifications */
        MafwGstRendererWorkerNotifySeekCb notify_seek_handler;
        MafwGstRendererWorkerNotifyPauseCb notify_pause_handler;
        MafwGstRendererWorkerNotifyPlayCb notify_play_handler;
        MafwGstRendererWorkerNotifyBufferStatusCb notify_buffer_status_handler;
        MafwGstRendererWorkerNotifyEOSCb notify_eos_handler;
        MafwGstRendererWorkerNotifyErrorCb notify_error_handler;
};

G_BEGIN_DECLS

MafwGstRendererWorker *mafw_gst_renderer_worker_new(gpointer owner);
void mafw_gst_renderer_worker_exit(MafwGstRendererWorker *worker);

void mafw_gst_renderer_worker_set_volume(MafwGstRendererWorker *worker,
                                         guint vol);
guint mafw_gst_renderer_worker_get_volume(MafwGstRendererWorker *worker);
void mafw_gst_renderer_worker_set_mute(MafwGstRendererWorker *worker,
                                       gboolean mute);
gboolean mafw_gst_renderer_worker_get_mute(MafwGstRendererWorker *worker);
#ifdef HAVE_GDKPIXBUF
void mafw_gst_renderer_worker_set_current_frame_on_pause(MafwGstRendererWorker *worker,
                                                         gboolean current_frame_on_pause);
gboolean mafw_gst_renderer_worker_get_current_frame_on_pause(MafwGstRendererWorker *worker);
#endif
void mafw_gst_renderer_worker_set_position(MafwGstRendererWorker *worker,
                                           GstSeekType seek_type,
                                           gint position,
                                           GError **error);
gint mafw_gst_renderer_worker_get_position(MafwGstRendererWorker *worker);
void mafw_gst_renderer_worker_set_xid(MafwGstRendererWorker *worker, XID xid);
XID mafw_gst_renderer_worker_get_xid(MafwGstRendererWorker *worker);
gboolean mafw_gst_renderer_worker_get_autopaint(MafwGstRendererWorker *worker);
void mafw_gst_renderer_worker_set_autopaint(MafwGstRendererWorker *worker, gboolean autopaint);
gint mafw_gst_renderer_worker_get_colorkey(MafwGstRendererWorker *worker);
gboolean mafw_gst_renderer_worker_get_seekable(MafwGstRendererWorker *worker);
GHashTable *mafw_gst_renderer_worker_get_current_metadata(MafwGstRendererWorker *worker);
void mafw_gst_renderer_worker_play(MafwGstRendererWorker *worker, const gchar *uri);
void mafw_gst_renderer_worker_play_alternatives(MafwGstRendererWorker *worker, gchar **uris);
void mafw_gst_renderer_worker_stop(MafwGstRendererWorker *worker);
void mafw_gst_renderer_worker_pause(MafwGstRendererWorker *worker);
void mafw_gst_renderer_worker_resume(MafwGstRendererWorker *worker);

G_END_DECLS
#endif
/* vi: set noexpandtab ts=8 sw=8 cino=t0,(0: */
