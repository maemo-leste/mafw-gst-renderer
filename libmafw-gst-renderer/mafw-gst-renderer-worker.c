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

#include <string.h>
#include <glib.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xvlib.h>

#include <gst/pbutils/missing-plugins.h>
#include <gst/base/gstbasesink.h>
#include <gst/gl/gstgldisplay.h>
#include <gst/gl/gstglcontext.h>
#include <gst/gl/gstglfuncs.h>

#include <libmafw/mafw.h>

#ifdef HAVE_GDKPIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include "gstscreenshot.h"
#endif

#include <totem-pl-parser.h>
#include "mafw-gst-renderer.h"
#include "mafw-gst-renderer-worker.h"
#include "mafw-gst-renderer-utils.h"
#include "blanking.h"
#include "keypad.h"

#undef  G_LOG_DOMAIN
#define G_LOG_DOMAIN "mafw-gst-renderer-worker"

#define MAFW_GST_RENDERER_WORKER_SECONDS_READY 60
#define MAFW_GST_RENDERER_WORKER_SECONDS_DURATION_AND_SEEKABILITY 4

#define MAFW_GST_MISSING_TYPE_DECODER "decoder"
#define MAFW_GST_MISSING_TYPE_ENCODER "encoder"

#define MAFW_GST_BUFFER_TIME  600000L
#define MAFW_GST_LATENCY_TIME (MAFW_GST_BUFFER_TIME / 2)

#define NSECONDS_TO_SECONDS(ns) ((ns)%1000000000 < 500000000?\
                                 GST_TIME_AS_SECONDS((ns)):\
                                 GST_TIME_AS_SECONDS((ns))+1)

#define _current_metadata_add(worker, key, type, value)	\
		do { \
			if (!worker->current_metadata) \
				worker->current_metadata = mafw_metadata_new(); \
			/* At first remove old value */ \
			g_hash_table_remove(worker->current_metadata, key); \
			mafw_metadata_add_something(worker->current_metadata, \
					key, type, 1, value); \
		} while (0)

/* Private variables. */
/* Global reference to worker instance, needed for Xerror handler */
static MafwGstRendererWorker *Global_worker = NULL;

/* Forward declarations. */
static void _do_play(MafwGstRendererWorker *worker);
static void _do_seek(MafwGstRendererWorker *worker, GstSeekType seek_type,
		     gboolean relative, gint position, GError **error);
static void _play_pl_next(MafwGstRendererWorker *worker);

static void _emit_metadatas(MafwGstRendererWorker *worker);

/* Playlist parsing */
static void _on_pl_entry_parsed(TotemPlParser *parser, gchar *uri,
				gpointer metadata, GSList **plitems)
{
	if (uri != NULL) {
		*plitems = g_slist_append(*plitems, g_strdup(uri));
	}
}
static GSList *_parse_playlist(const gchar *uri)
{
	static TotemPlParser *pl_parser = NULL;
	GSList *plitems = NULL;
	gulong handler_id;

	/* Initialize the playlist parser */
	if (!pl_parser)
	{
		pl_parser = totem_pl_parser_new ();
		g_object_set(pl_parser, "recurse", TRUE, "disable-unsafe",
		     TRUE, NULL);
	}
	handler_id = g_signal_connect(G_OBJECT(pl_parser), "entry-parsed",
			 G_CALLBACK(_on_pl_entry_parsed), &plitems);
	/* Parsing */
	if (totem_pl_parser_parse(pl_parser, uri, FALSE) !=
	    TOTEM_PL_PARSER_RESULT_SUCCESS) {
		/* An error happens while parsing */
		
	}
	g_signal_handler_disconnect(pl_parser, handler_id);
	return plitems;
}
		
/*
 * Sends @error to MafwGstRenderer.  Only call this from the glib main thread, or
 * face the consequences.  @err is free'd.
 */
static void _send_error(MafwGstRendererWorker *worker, GError *err)
{
	worker->is_error = TRUE;
        if (worker->notify_error_handler)
                worker->notify_error_handler(worker, worker->owner, err);
	g_error_free(err);
}

/*
 * Posts an @error on the gst bus.  _async_bus_handler will then pick it up and
 * forward to MafwGstRenderer.  @err is free'd.
 */
static void _post_error(MafwGstRendererWorker *worker, GError *err)
{
	gst_bus_post(worker->bus,
		     gst_message_new_error(GST_OBJECT(worker->pipeline),
					   err, NULL));
	g_error_free(err);
}

#ifdef HAVE_GDKPIXBUF
typedef struct {
	MafwGstRendererWorker *worker;
	gchar *metadata_key;
	GdkPixbuf *pixbuf;
} SaveGraphicData;

static gchar *_init_tmp_file(void)
{
	gint fd;
	gchar *path = NULL;

	fd = g_file_open_tmp("mafw-gst-renderer-XXXXXX.jpeg", &path, NULL);
	close(fd);

	return path;
}

static void _init_tmp_files_pool(MafwGstRendererWorker *worker)
{
	guint8 i;

	worker->tmp_files_pool_index = 0;

	for (i = 0; i < MAFW_GST_RENDERER_MAX_TMP_FILES; i++) {
		worker->tmp_files_pool[i] = NULL;
	}
}

static void _destroy_tmp_files_pool(MafwGstRendererWorker *worker)
{
	guint8 i;

	for (i = 0; (i < MAFW_GST_RENDERER_MAX_TMP_FILES) &&
		     (worker->tmp_files_pool[i] != NULL); i++) {
		g_unlink(worker->tmp_files_pool[i]);
		g_free(worker->tmp_files_pool[i]);
	}
}

static const gchar *_get_tmp_file_from_pool(
                        MafwGstRendererWorker *worker)
{
	gchar *path = worker->tmp_files_pool[worker->tmp_files_pool_index];

	if (path == NULL) {
		path = _init_tmp_file();
		worker->tmp_files_pool[worker->tmp_files_pool_index] = path;
	}

	if (++(worker->tmp_files_pool_index) >=
	    MAFW_GST_RENDERER_MAX_TMP_FILES) {
		worker->tmp_files_pool_index = 0;
	}

	return path;
}

typedef struct {
	GstBuffer *buffer;
	GstMapInfo info;
} DestroyPixbufData;

static void _destroy_pixbuf (guchar *pixbuf, gpointer data)
{
	DestroyPixbufData *dpd = data;
	gst_buffer_unmap(dpd->buffer, &dpd->info);
	gst_buffer_unref(dpd->buffer);
	g_free(dpd);
}

static void _emit_gst_buffer_as_graphic_file_cb(GstSample *sample,
						gpointer user_data)
{
	SaveGraphicData *sgd = user_data;
	GdkPixbuf *pixbuf = NULL;

	if (sample != NULL) {
		DestroyPixbufData *dpd = g_new(DestroyPixbufData, 1);
		dpd->buffer = gst_sample_get_buffer(sample);

		if (gst_buffer_map(dpd->buffer, &dpd->info, GST_MAP_READ)) {
			GstStructure *structure;
			gint width, height;

			gst_buffer_ref(dpd->buffer);
			structure = gst_caps_get_structure(
					    gst_sample_get_caps(sample), 0);
			gst_structure_get_int(structure, "width", &width);
			gst_structure_get_int(structure, "height", &height);
			pixbuf = gdk_pixbuf_new_from_data(
					 dpd->info.data, GDK_COLORSPACE_RGB,
					 FALSE, 8, width, height,
					 GST_ROUND_UP_4(3 * width),
					 _destroy_pixbuf, dpd);
		} else {
			g_free(dpd);
		}

		gst_sample_unref(sample);

		if (sgd->pixbuf != NULL) {
			g_object_unref(sgd->pixbuf);
			sgd->pixbuf = NULL;
		}
	} else {
		pixbuf = sgd->pixbuf;
	}

	if (pixbuf != NULL) {
		gboolean save_ok;
		GError *error = NULL;
		const gchar *filename;

		filename = _get_tmp_file_from_pool(sgd->worker);

		save_ok = gdk_pixbuf_save (pixbuf, filename, "jpeg", &error,
					   NULL);

		g_object_unref (pixbuf);

		if (save_ok) {
			/* Add the info to the current metadata. */
			_current_metadata_add(sgd->worker, sgd->metadata_key,
					      G_TYPE_STRING,
					      (gchar*)filename);

			/* Emit the metadata. */
			mafw_renderer_emit_metadata_string(sgd->worker->owner,
							   sgd->metadata_key,
							   (gchar *) filename);
		} else {
			if (error != NULL) {
				g_warning ("%s\n", error->message);
				g_error_free (error);
			} else {
				g_critical("Unknown error when saving pixbuf "
					   "with GStreamer data");
			}
		}
	} else {
		g_warning("Could not create pixbuf from GstBuffer");
	}

	g_free(sgd->metadata_key);
	g_free(sgd);
}

static void _pixbuf_size_prepared_cb (GdkPixbufLoader *loader, 
				      gint width, gint height,
				      gpointer user_data)
{
	/* Be sure the image size is reasonable */
	if (width > 512 || height > 512) {
		g_debug ("pixbuf: image is too big: %dx%d", width, height);
		gdouble ar;
		ar = (gdouble) width / height;
		if (width > height) {
			width = 512;
			height = width / ar;
		} else {
			height = 512;
			width = height * ar;
		}
		g_debug ("pixbuf: scaled image to %dx%d", width, height);
		gdk_pixbuf_loader_set_size (loader, width, height);
	}
}

static void _emit_gst_buffer_as_graphic_file(MafwGstRendererWorker *worker,
					     GstSample *sample,
					     const gchar *metadata_key)
{
	GdkPixbufLoader *loader;
	const GstStructure *structure;
	const gchar *mime = NULL;
	GError *error = NULL;

	g_return_if_fail((sample != NULL) && GST_IS_SAMPLE(sample));

	structure = gst_caps_get_structure(gst_sample_get_caps(sample), 0);
	mime = gst_structure_get_name (structure);

	if (g_str_has_prefix(mime, "video/x-raw")) {
		gint framerate_d, framerate_n;
		GstCaps *to_caps;
		SaveGraphicData *sgd;

		gst_structure_get_fraction (structure, "framerate",
					    &framerate_n, &framerate_d);

		to_caps = gst_caps_new_simple ("video/x-raw",
					       "format", G_TYPE_STRING, "RGB",
					       "bpp", G_TYPE_INT, 24,
					       "depth", G_TYPE_INT, 24,
					       "framerate", GST_TYPE_FRACTION,
					       framerate_n, framerate_d,
					       "pixel-aspect-ratio",
					       GST_TYPE_FRACTION, 1, 1,
					       "endianness",
					       G_TYPE_INT, G_BIG_ENDIAN,
					       "red_mask", G_TYPE_INT,
					       0xff0000,
					       "green_mask",
					       G_TYPE_INT, 0x00ff00,
					       "blue_mask",
					       G_TYPE_INT, 0x0000ff,
					       NULL);

		sgd = g_new0(SaveGraphicData, 1);
		sgd->worker = worker;
		sgd->metadata_key = g_strdup(metadata_key);

		g_debug("pixbuf: using bvw to convert image format");
		bvw_frame_conv_convert (sample, to_caps, worker->use_xv,
					_emit_gst_buffer_as_graphic_file_cb,
					sgd);
	} else {
		GdkPixbuf *pixbuf = NULL;
		loader = gdk_pixbuf_loader_new_with_mime_type (mime, &error);
		g_signal_connect (G_OBJECT (loader), "size-prepared", 
				 (GCallback)_pixbuf_size_prepared_cb, NULL);

		if (loader == NULL) {
			g_warning ("%s\n", error->message);
			g_error_free (error);
		} else {
			GstMapInfo info;
			GstBuffer *buffer = gst_sample_get_buffer (sample);

			gst_buffer_map(buffer, &info, GST_MAP_READ);

			if (!gdk_pixbuf_loader_write (loader, info.data,
						      info.size, &error)) {
				g_warning ("%s\n", error->message);
				g_error_free (error);

				gdk_pixbuf_loader_close (loader, NULL);
			} else {
				pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);

				if (!gdk_pixbuf_loader_close (loader, &error)) {
					g_warning ("%s\n", error->message);
					g_error_free (error);

				} else {
					SaveGraphicData *sgd;

					sgd = g_new0(SaveGraphicData, 1);

					sgd->worker = worker;
					sgd->metadata_key =
						g_strdup(metadata_key);
					sgd->pixbuf = g_object_ref(pixbuf);

					_emit_gst_buffer_as_graphic_file_cb(
						NULL, sgd);
				}
			}

			gst_buffer_unmap(buffer, &info);
			g_object_unref(loader);
		}
	}
}
#endif

static gboolean _go_to_gst_ready(gpointer user_data)
{
	MafwGstRendererWorker *worker = user_data;

	g_return_val_if_fail(worker->state == GST_STATE_PAUSED ||
			     worker->prerolling, FALSE);

	worker->seek_position =
		mafw_gst_renderer_worker_get_position(worker);

	g_debug("going to GST_STATE_READY");
	gst_element_set_state(worker->pipeline, GST_STATE_READY);
	worker->in_ready = TRUE;
        worker->ready_timeout = 0;

	return FALSE;
}

static void _add_ready_timeout(MafwGstRendererWorker *worker)
{
	if (worker->media.seekable) {
		if (!worker->ready_timeout)
		{
			g_debug("Adding timeout to go to GST_STATE_READY");
			worker->ready_timeout =
				g_timeout_add_seconds(
					MAFW_GST_RENDERER_WORKER_SECONDS_READY,
					_go_to_gst_ready,
					worker);
		}
	} else {
		g_debug("Not adding timeout to go to GST_STATE_READY as media "
			"is not seekable");
		worker->ready_timeout = 0;
	}
}

static void _remove_ready_timeout(MafwGstRendererWorker *worker)
{
	if (worker->ready_timeout != 0) {
		g_debug("removing timeout for READY");
		g_source_remove(worker->ready_timeout);
		worker->ready_timeout = 0;
	}
	worker->in_ready = FALSE;
}

static gboolean _emit_video_info(MafwGstRendererWorker *worker)
{
	mafw_renderer_emit_metadata_int(worker->owner,
				    MAFW_METADATA_KEY_RES_X,
				    worker->media.video_width);
	mafw_renderer_emit_metadata_int(worker->owner,
				    MAFW_METADATA_KEY_RES_Y,
				    worker->media.video_height);
	mafw_renderer_emit_metadata_double(worker->owner,
				       MAFW_METADATA_KEY_VIDEO_FRAMERATE,
				       worker->media.fps);
	return FALSE;
}

/*
 * Checks if the video details are supported.  It also extracts other useful
 * information (such as PAR and framerate) from the caps, if available.  NOTE:
 * this will be called from a different thread than glib's mainloop (when
 * invoked via _stream_info_cb);  don't call MafwGstRenderer directly.
 *
 * Returns: TRUE if video details are acceptable.
 */
static gboolean _handle_video_info(MafwGstRendererWorker *worker,
				   const GstStructure *structure)
{
	gint width, height;
	gdouble fps;

	width = height = 0;
	gst_structure_get_int(structure, "width", &width);
	gst_structure_get_int(structure, "height", &height);
	g_debug("video size: %d x %d", width, height);
	if (gst_structure_has_field(structure, "pixel-aspect-ratio"))
	{
		gst_structure_get_fraction(structure, "pixel-aspect-ratio",
					   &worker->media.par_n,
					   &worker->media.par_d);
		g_debug("video PAR: %d:%d", worker->media.par_n,
			worker->media.par_d);
		width = width * worker->media.par_n / worker->media.par_d;
	}

	fps = 1.0;
	if (gst_structure_has_field(structure, "framerate"))
	{
		gint fps_n, fps_d;

		gst_structure_get_fraction(structure, "framerate",
					   &fps_n, &fps_d);
		if (fps_d > 0)
			fps = (gdouble)fps_n / (gdouble)fps_d;
		g_debug("video fps: %f", fps);
	}

	worker->media.video_width = width;
	worker->media.video_height = height;
	worker->media.fps = fps;

	/* Add the info to the current metadata. */
	gint p_width, p_height, p_fps;

	p_width = width;
	p_height = height;
	p_fps = fps;

	_current_metadata_add(worker, MAFW_METADATA_KEY_RES_X, G_TYPE_INT,
			      p_width);
	_current_metadata_add(worker, MAFW_METADATA_KEY_RES_Y, G_TYPE_INT,
			      p_height);
	_current_metadata_add(worker, MAFW_METADATA_KEY_VIDEO_FRAMERATE,
			      G_TYPE_DOUBLE,
			      p_fps);

	/* Emit the metadata.*/
	g_idle_add((GSourceFunc)_emit_video_info, worker);

	return TRUE;
}

static void mafw_gst_renderer_worker_apply_xid(MafwGstRendererWorker *worker)
{
	/* Set sink to render on the provided XID if we have do have
	   a XID a valid video sink and we are rendeing video content */
	if (worker->xid && 
	    worker->vsink && 
	    worker->media.has_visual_content)
	{
		g_debug ("Setting overlay, window id: %x", 
			 (gint) worker->xid);
		gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(worker->vsink),
					     worker->xid);
		/* Ask the gst to redraw the frame if we are paused */
		/* TODO: in MTG this works only in non-fs -> fs way. */
		if (worker->state == GST_STATE_PAUSED)
		{
			gst_video_overlay_expose(GST_VIDEO_OVERLAY(worker->vsink));
		}
	} else {
		g_debug("Not setting overlay for window id: %x", 
			(gint) worker->xid);
	}
}

/*
 * GstBus synchronous message handler.  NOTE that this handler is NOT invoked
 * from the glib thread, so be careful what you do here.
 */
static GstBusSyncReply _sync_bus_handler(GstBus *bus, GstMessage *msg,
					 MafwGstRendererWorker *worker)
{
	if (!gst_is_video_overlay_prepare_window_handle_message(msg))
		/* do not unref message when returning PASS */
		return GST_BUS_PASS;

	if (worker->xid) {
		g_debug("got prepare-window-handle");
		worker->media.has_visual_content = TRUE;
		g_debug ("Video window to use is: %x", (gint) worker->xid);

		/* Instruct vsink to use the client-provided window */
		mafw_gst_renderer_worker_apply_xid(worker);
	} else if (worker->state != GST_STATE_NULL) {
		/* The user has to preset the XID, we don't create windows by
		 * ourselves. */
		/* We must post an error message to the bus that will
		 * be picked up by _async_bus_handler.  Calling the
		 * notification function directly from here (different
		 * thread) is not healthy. */
		g_warning("No video window set!");
		_post_error(worker,
			    g_error_new_literal(
				    MAFW_RENDERER_ERROR,
				    MAFW_RENDERER_ERROR_PLAYBACK,
				    "No video window XID set"));
	}

	gst_message_unref (msg);

	return GST_BUS_DROP;
}

static void _free_taglist_item(GstMessage *msg, gpointer data)
{
	gst_message_unref(msg);
}

static void _free_taglist(MafwGstRendererWorker *worker)
{
	if (worker->tag_list != NULL)
	{
		g_ptr_array_foreach(worker->tag_list, (GFunc)_free_taglist_item,
				    NULL);
		g_ptr_array_free(worker->tag_list, TRUE);
		worker->tag_list = NULL;
	}
}

static gboolean _seconds_duration_equal(gint64 duration1, gint64 duration2)
{
	gint64 duration1_seconds, duration2_seconds;

	duration1_seconds = NSECONDS_TO_SECONDS(duration1);
	duration2_seconds = NSECONDS_TO_SECONDS(duration2);

	return duration1_seconds == duration2_seconds;
}

static void _check_duration(MafwGstRendererWorker *worker, gint64 value)
{
	MafwGstRenderer *renderer = worker->owner;
	gboolean right_query = TRUE;

	if (value == -1) {
		right_query = gst_element_query_duration(
				      worker->pipeline, GST_FORMAT_TIME,
				      &value);
	}

	if (right_query && value > 0) {
		gint duration_seconds = NSECONDS_TO_SECONDS(value);

		if (!_seconds_duration_equal(worker->media.length_nanos,
					     value)) {			
			/* Add the duration to the current metadata. */
			_current_metadata_add(worker, MAFW_METADATA_KEY_DURATION,
						G_TYPE_INT64,
						(gint64)duration_seconds);
			/* Emit the duration. */
			mafw_renderer_emit_metadata_int64(
				worker->owner, MAFW_METADATA_KEY_DURATION,
				(gint64)duration_seconds);
		}

		/* We compare this duration we just got with the
		 * source one and update it in the source if needed */
		if (duration_seconds > 0 &&
			duration_seconds != renderer->media->duration) {
			mafw_gst_renderer_update_source_duration(
				renderer,
				duration_seconds);
		}
	}

	worker->media.length_nanos = value;
	g_debug("media duration: %" G_GUINT64_FORMAT,
		worker->media.length_nanos);
}

static void _check_seekability(MafwGstRendererWorker *worker)
{
	MafwGstRenderer *renderer = worker->owner;
	SeekabilityType seekable = SEEKABILITY_NO_SEEKABLE;

	if (worker->media.length_nanos != -1)
	{
		g_debug("source seekability %d", renderer->media->seekability);

		if (renderer->media->seekability != SEEKABILITY_NO_SEEKABLE) {
			g_debug("Quering GStreamer for seekability");
			GstQuery *seek_query;
			GstFormat format = GST_FORMAT_TIME;
			/* Query the seekability of the stream */
			seek_query = gst_query_new_seeking(format);
			if (gst_element_query(worker->pipeline, seek_query)) {
				gboolean renderer_seekable = FALSE;
				gst_query_parse_seeking(seek_query, NULL,
							&renderer_seekable,
							NULL, NULL);
				g_debug("GStreamer seekability %d",
					renderer_seekable);
				seekable = renderer_seekable ?
					SEEKABILITY_SEEKABLE :
					SEEKABILITY_NO_SEEKABLE;
			}
			gst_query_unref(seek_query);
		}
	}

	if (worker->media.seekable != seekable) {
		gboolean is_seekable = (seekable == SEEKABILITY_SEEKABLE);

		/* Add the seekability to the current metadata. */
		_current_metadata_add(worker, MAFW_METADATA_KEY_IS_SEEKABLE,
			G_TYPE_BOOLEAN, is_seekable);

		/* Emit. */
		mafw_renderer_emit_metadata_boolean(
			worker->owner, MAFW_METADATA_KEY_IS_SEEKABLE,
			is_seekable);
	}

	g_debug("media seekable: %d", seekable);
	worker->media.seekable = seekable;
}

static gboolean _query_duration_and_seekability_timeout(gpointer data)
{
	MafwGstRendererWorker *worker = data;

	_check_duration(worker, -1);
	_check_seekability(worker);

	worker->duration_seek_timeout = 0;

	return FALSE;
}

/*
 * Called when the pipeline transitions into PAUSED state.  It extracts more
 * information from Gst.
 */
static void _finalize_startup(MafwGstRendererWorker *worker)
{
	/* Check video caps */
	if (worker->media.has_visual_content) {
		GstElement *vsink = GST_ELEMENT(worker->vsink);
		GstPad *pad = GST_PAD(vsink->sinkpads->data);
		GstCaps *caps = gst_pad_get_current_caps(pad);
		if (caps && gst_caps_is_fixed(caps)) {
			GstStructure *structure;
			structure = gst_caps_get_structure(caps, 0);
			if (!_handle_video_info(worker, structure)) {
				gst_caps_unref(caps);
				return;
			}
			gst_caps_unref(caps);
		}
	}

	/* Something might have gone wrong at this point already. */
	if (worker->is_error) {
		g_debug("Error occured during preroll");
		return;
	}

	/* Check duration and seekability */
	if (worker->duration_seek_timeout != 0) {
		g_source_remove(worker->duration_seek_timeout);
		worker->duration_seek_timeout = 0;
	}
	_check_duration(worker, -1);
	_check_seekability(worker);
}

static void _add_duration_seek_query_timeout(MafwGstRendererWorker *worker)
{
	if (worker->duration_seek_timeout != 0) {
		g_source_remove(worker->duration_seek_timeout);
	}
	worker->duration_seek_timeout = g_timeout_add_seconds(
		MAFW_GST_RENDERER_WORKER_SECONDS_DURATION_AND_SEEKABILITY,
		_query_duration_and_seekability_timeout,
		worker);
}

static void _do_pause_postprocessing(MafwGstRendererWorker *worker)
{
	if (worker->notify_pause_handler) {
		worker->notify_pause_handler(worker, worker->owner);
	}

#ifdef HAVE_GDKPIXBUF
	if (worker->media.has_visual_content &&
	    worker->current_frame_on_pause) {
		GstSample *sample = NULL;

		g_object_get(worker->pipeline, "sample", &sample, NULL);

		if (sample != NULL) {
			_emit_gst_buffer_as_graphic_file(
				worker, sample,
				MAFW_METADATA_KEY_PAUSED_THUMBNAIL_URI);
		}
	}
#endif

	_add_ready_timeout(worker);
}

static void _report_playing_state(MafwGstRendererWorker * worker)
{
	if (worker->report_statechanges) {
		switch (worker->mode) {
		case WORKER_MODE_SINGLE_PLAY:
			/* Notify play if we are playing in
			 * single mode */
			if (worker->notify_play_handler)
				worker->notify_play_handler(
					worker,
					worker->owner);
			break;
		case WORKER_MODE_PLAYLIST:
		case WORKER_MODE_REDUNDANT:
			/* Only notify play when the "playlist"
			   playback starts, don't notify play for each
			   individual element of the playlist. */
			if (worker->pl.notify_play_pending) {
				if (worker->notify_play_handler)
					worker->notify_play_handler(
						worker,
						worker->owner);
				worker->pl.notify_play_pending = FALSE;
			}
			break;
		default: break;
		}
	}
}

static void _handle_state_changed(GstMessage *msg, MafwGstRendererWorker *worker)
{
	GstState newstate, oldstate;
	GstStateChange statetrans;
	MafwGstRenderer *renderer = (MafwGstRenderer*)worker->owner;

	gst_message_parse_state_changed(msg, &oldstate, &newstate, NULL);
	statetrans = GST_STATE_TRANSITION(oldstate, newstate);
	g_debug ("State changed: %d: %d -> %d", worker->state, oldstate, newstate);

	/* If the state is the same we do nothing, otherwise, we keep
	 * it */
	if (worker->state == newstate) {
		return;
	} else {
		worker->state = newstate;
	}

        if (statetrans == GST_STATE_CHANGE_READY_TO_PAUSED &&
            worker->in_ready) {
                /* Woken up from READY, resume stream position and playback */
                g_debug("State changed to pause after ready");
                if (worker->seek_position > 0) {
                        _check_seekability(worker);
                        if (worker->media.seekable) {
                                g_debug("performing a seek");
				_do_seek(worker, GST_SEEK_TYPE_SET, FALSE,
                                         worker->seek_position, NULL);
                        } else {
                                g_critical("media is not seekable (and should)");
                        }
                }

                /* If playing a stream wait for buffering to finish before
                   starting to play */
                if (!worker->is_stream || worker->is_live) {
                        _do_play(worker);
                }
                return;
        }

	/* While buffering, we have to wait in PAUSED 
	   until we reach 100% before doing anything */
	if (worker->buffering) {
	        if (statetrans == GST_STATE_CHANGE_PAUSED_TO_PLAYING) {
			/* Mmm... probably the client issued a seek on the
			 * stream and then a play/resume command right away,
			 * so the stream got into PLAYING state while
			 * buffering. When the next buffering signal arrives,
			 * the stream will be PAUSED silently and resumed when
			 * buffering is done (silently too), so let's signal
			 * the state change to PLAYING here. */
			_report_playing_state(worker);			
		}
		return;
	}

	switch (statetrans) {
	case GST_STATE_CHANGE_READY_TO_PAUSED:
		if (worker->prerolling && worker->report_statechanges) {
			/* PAUSED after pipeline has been
			 * constructed. We check caps, seek and
			 * duration and if staying in pause is needed,
			 * we perform operations for pausing, such as
			 * current frame on pause and signalling state
			 * change and adding the timeout to go to ready */
			g_debug ("Prerolling done, finalizaing startup");
			_finalize_startup(worker);
			_do_play(worker);
			renderer->play_failed_count = 0;

			if (worker->stay_paused) {
				_do_pause_postprocessing(worker);
			}
			worker->prerolling = FALSE;
		}
		break;
	case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
		/* When pausing we do the stuff, like signalling
		 * state, current frame on pause and timeout to go to
		 * ready */
		if (worker->report_statechanges) {
			_do_pause_postprocessing(worker);
		}
		break;
	case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
		/* if seek was called, at this point it is really ended */
		worker->seek_position = -1;
                worker->eos = FALSE;

		/* Signal state change if needed */
		_report_playing_state(worker);

		/* Prevent blanking if we are playing video */
                if (worker->media.has_visual_content) {
                        blanking_prohibit();
                }
		keypadlocking_prohibit();
		/* Remove the ready timeout if we are playing [again] */
		_remove_ready_timeout(worker);
                /* If mode is redundant we are trying to play one of several
                 * candidates, so when we get a successful playback, we notify
                 * the real URI that we are playing */
                if (worker->mode == WORKER_MODE_REDUNDANT) {
                        mafw_renderer_emit_metadata_string(
                                worker->owner,
                                MAFW_METADATA_KEY_URI,
                                worker->media.location);
                }

		/* Emit metadata. We wait until we reach the playing
		   state because this speeds up playback start time */
		_emit_metadatas(worker);
		/* Query duration and seekability. Useful for vbr
		 * clips or streams. */
		_add_duration_seek_query_timeout(worker);
		break;
	case GST_STATE_CHANGE_PAUSED_TO_READY:
		/* If we went to READY, we free the taglist and
		 * deassign the timout it */
		if (worker->in_ready) {
			g_debug("changed to GST_STATE_READY");
			_free_taglist(worker);
		}
		break;
	default:
		break;
	}
}

static void _handle_duration(MafwGstRendererWorker *worker, GstMessage *msg)
{
	GstFormat fmt;
	gint64 duration;

	gst_message_parse_duration(msg, &fmt, &duration);

	if (worker->duration_seek_timeout != 0) {
		g_source_remove(worker->duration_seek_timeout);
		worker->duration_seek_timeout = 0;
	}

	_check_duration(worker,
			duration != GST_CLOCK_TIME_NONE ? duration : -1);
	_check_seekability(worker);
}

#ifdef HAVE_GDKPIXBUF
static void _emit_renderer_art(MafwGstRendererWorker *worker,
			       const GstTagList *list)
{
	GstSample *sample = NULL;
	const GValue *value = NULL;

	g_return_if_fail(gst_tag_list_get_tag_size(list, GST_TAG_IMAGE) > 0);

	value = gst_tag_list_get_value_index(list, GST_TAG_IMAGE, 0);

	g_return_if_fail((value != NULL) && G_VALUE_HOLDS(value, GST_TYPE_SAMPLE));

	sample = g_value_peek_pointer(value);

	g_return_if_fail((sample != NULL) && GST_IS_SAMPLE(sample));

	_emit_gst_buffer_as_graphic_file(worker, sample,
					 MAFW_METADATA_KEY_RENDERER_ART_URI);
}
#endif

static GHashTable* _build_tagmap(void)
{
	GHashTable *hash_table = NULL;

	hash_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
					   g_free);

	g_hash_table_insert(hash_table, g_strdup(GST_TAG_TITLE),
			    g_strdup(MAFW_METADATA_KEY_TITLE));
	g_hash_table_insert(hash_table, g_strdup(GST_TAG_ARTIST),
			    g_strdup(MAFW_METADATA_KEY_ARTIST));
	g_hash_table_insert(hash_table, g_strdup(GST_TAG_AUDIO_CODEC),
			    g_strdup(MAFW_METADATA_KEY_AUDIO_CODEC));
	g_hash_table_insert(hash_table, g_strdup(GST_TAG_VIDEO_CODEC),
			    g_strdup(MAFW_METADATA_KEY_VIDEO_CODEC));
	g_hash_table_insert(hash_table, g_strdup(GST_TAG_BITRATE),
			    g_strdup(MAFW_METADATA_KEY_BITRATE));
	g_hash_table_insert(hash_table, g_strdup(GST_TAG_LANGUAGE_CODE),
			    g_strdup(MAFW_METADATA_KEY_ENCODING));
	g_hash_table_insert(hash_table, g_strdup(GST_TAG_ALBUM),
			    g_strdup(MAFW_METADATA_KEY_ALBUM));
	g_hash_table_insert(hash_table, g_strdup(GST_TAG_GENRE),
			    g_strdup(MAFW_METADATA_KEY_GENRE));
	g_hash_table_insert(hash_table, g_strdup(GST_TAG_TRACK_NUMBER),
			    g_strdup(MAFW_METADATA_KEY_TRACK));
	g_hash_table_insert(hash_table, g_strdup(GST_TAG_ORGANIZATION),
			    g_strdup(MAFW_METADATA_KEY_ORGANIZATION));
#ifdef HAVE_GDKPIXBUF
	g_hash_table_insert(hash_table, g_strdup(GST_TAG_IMAGE),
			    g_strdup(MAFW_METADATA_KEY_RENDERER_ART_URI));
#endif

	return hash_table;
}

/*
 * Emits metadata-changed signals for gst tags.
 */
static void _emit_tag(const GstTagList *list, const gchar *tag,
		      MafwGstRendererWorker *worker)
{
	/* Mapping between Gst <-> MAFW metadata tags
	 * NOTE: This assumes that GTypes matches between GST and MAFW. */
	static GHashTable *tagmap = NULL;
	gint i, count;
	const gchar *mafwtag;
	GType type;
	GValueArray *values;

	if (tagmap == NULL) {
		tagmap = _build_tagmap();
	}

	g_debug("tag: '%s' (type: %s)", tag,
		g_type_name(gst_tag_get_type(tag)));
	/* Is there a mapping for this tag? */
	mafwtag = g_hash_table_lookup(tagmap, tag);
	if (!mafwtag)
		return;

#ifdef HAVE_GDKPIXBUF
	if (strcmp (mafwtag, MAFW_METADATA_KEY_RENDERER_ART_URI) == 0) {
		_emit_renderer_art(worker, list);
		return;
	}
#endif

	/* Build a value array of this tag.  We need to make sure that strings
	 * are UTF-8.  GstTagList API says that the value is always UTF8, but it
	 * looks like the ID3 demuxer still might sometimes produce non-UTF-8
	 * strings. */
	count = gst_tag_list_get_tag_size(list, tag);
	type = gst_tag_get_type(tag);
	values = g_value_array_new(count);
	for (i = 0; i < count; ++i) {
		GValue *v = (GValue *)
			gst_tag_list_get_value_index(list, tag, i);
		if (type == G_TYPE_STRING) {
			gchar *orig, *utf8;

			gst_tag_list_get_string_index(list, tag, i, &orig);
			if (convert_utf8(orig, &utf8)) {
				GValue utf8gval = {0};

				g_value_init(&utf8gval, G_TYPE_STRING);
				g_value_take_string(&utf8gval, utf8);
				_current_metadata_add(worker, mafwtag, G_TYPE_STRING,
							utf8);
				g_value_array_append(values, &utf8gval);
				g_value_unset(&utf8gval);
			}
			g_free(orig);
		} else if (type == G_TYPE_UINT) {
			GValue intgval = {0};
			gint intval;

			g_value_init(&intgval, G_TYPE_INT);
			g_value_transform(v, &intgval);
			intval = g_value_get_int(&intgval);
			_current_metadata_add(worker, mafwtag, G_TYPE_INT,
						intval);
			g_value_array_append(values, &intgval);
			g_value_unset(&intgval);
		} else {
			_current_metadata_add(worker, mafwtag, G_TYPE_VALUE,
						v);
			g_value_array_append(values, v);
		}
	}

	/* Emit the metadata. */
	g_signal_emit_by_name(worker->owner, "metadata-changed", mafwtag,
			      values);

	g_value_array_free(values);
}

/**
 * Collect tag-messages, parse it later, when playing is ongoing
 */
static void _handle_tag(MafwGstRendererWorker *worker, GstMessage *msg)
{
	/* Do not emit metadata until we get to PLAYING state to speed up
	   playback start */
	if (worker->tag_list == NULL)
		worker->tag_list = g_ptr_array_new();
	g_ptr_array_add(worker->tag_list, gst_message_ref(msg));

	/* Some tags come in playing state, so in this case we have
	   to emit them right away (example: radio stations) */
	if (worker->state == GST_STATE_PLAYING) {
		_emit_metadatas(worker);
	}
}

/**
 * Parses the list of tag-messages
 */
static void _parse_tagmsg(GstMessage *msg, MafwGstRendererWorker *worker)
{
	GstTagList *new_tags;

	gst_message_parse_tag(msg, &new_tags);
	gst_tag_list_foreach(new_tags, (gpointer)_emit_tag, worker);
	gst_tag_list_free(new_tags);
	gst_message_unref(msg);
}

/**
 * Parses the collected tag messages, and emits the metadatas
 */
static void _emit_metadatas(MafwGstRendererWorker *worker)
{
	if (worker->tag_list != NULL)
	{
		g_ptr_array_foreach(worker->tag_list, (GFunc)_parse_tagmsg,
				    worker);
		g_ptr_array_free(worker->tag_list, TRUE);
		worker->tag_list = NULL;
	}
}

static void _reset_volume_and_mute_to_pipeline(MafwGstRendererWorker *worker)
{
#ifdef MAFW_GST_RENDERER_DISABLE_PULSE_VOLUME
	g_debug("resetting volume and mute to pipeline");

	if (worker->pipeline != NULL) {
		g_object_set(
			G_OBJECT(worker->pipeline), "volume",
			mafw_gst_renderer_worker_volume_get(worker->wvolume),
			"mute",
			mafw_gst_renderer_worker_volume_is_muted(worker->wvolume),
			NULL);
	}
#endif
}

static void _handle_buffering(MafwGstRendererWorker *worker, GstMessage *msg)
{
	gint percent;
	MafwGstRenderer *renderer = (MafwGstRenderer*)worker->owner;

	gst_message_parse_buffering(msg, &percent);
	g_debug("buffering: %d", percent);

        /* No state management needed for live pipelines */
        if (!worker->is_live) {
		worker->buffering = TRUE;
		if (percent < 100 && worker->state == GST_STATE_PLAYING) {
			g_debug("setting pipeline to PAUSED not to wolf the "
				"buffer down");
			worker->report_statechanges = FALSE;
			/* We can't call _pause() here, since it sets
			 * the "report_statechanges" to TRUE.  We don't
			 * want that, application doesn't need to know
			 * that internally the state changed to
			 * PAUSED. */
			if (gst_element_set_state(worker->pipeline,
					      GST_STATE_PAUSED) ==
		    			GST_STATE_CHANGE_ASYNC)
			{
				/* XXX this blocks at most 2 seconds. */
				gst_element_get_state(worker->pipeline, NULL,
					      NULL,
					      2 * GST_SECOND);
			}
		}

                if (percent >= 100) {
                        /* On buffering we go to PAUSED, so here we move back to
                           PLAYING */
                        worker->buffering = FALSE;
                        if (worker->state == GST_STATE_PAUSED) {
                                /* If buffering more than once, do this only the
                                   first time we are done with buffering */
                                if (worker->prerolling) {
					g_debug("buffering concluded during "
						"prerolling");
					_finalize_startup(worker);
					_do_play(worker);
					renderer->play_failed_count = 0;
					/* Send the paused notification */
					if (worker->stay_paused &&
					    worker->notify_pause_handler) {
						worker->notify_pause_handler(
							worker,
							worker->owner);
					}
					worker->prerolling = FALSE;
                                } else if (worker->in_ready) {
					/* If we had been woken up from READY
					   and we have finish our buffering,
					   check if we have to play or stay
					   paused and if we have to play,
					   signal the state change. */
                                        g_debug("buffering concluded, "
                                                "continuing playing");
                                        _do_play(worker);
                                } else if (!worker->stay_paused) {
					/* This means, that we were playing but
					   ran out of buffer, so we silently
					   paused waited for buffering to
					   finish and now we continue silently
					   (silently meaning we do not expose
					   state changes) */
					g_debug("buffering concluded, setting "
						"pipeline to PLAYING again");
					_reset_volume_and_mute_to_pipeline(
						worker);
					if (gst_element_set_state(
						worker->pipeline,
						GST_STATE_PLAYING) ==
		    					GST_STATE_CHANGE_ASYNC)
					{
						/* XXX this blocks at most 2 seconds. */
						gst_element_get_state(
							worker->pipeline, NULL, NULL,
							2 * GST_SECOND);
					}
				}
                        } else if (worker->state == GST_STATE_PLAYING) {
				g_debug("buffering concluded, signalling "
					"state change");
				/* In this case we got a PLAY command while 
				   buffering, likely because it was issued
				   before we got the first buffering signal.
				   The UI should not do this, but if it does,
				   we have to signal that we have executed
				   the state change, since in 
				   _handle_state_changed we do not do anything 
				   if we are buffering  */

				/* Set the pipeline to playing. This is an async
				   handler, it could be, that the reported state
				   is not the real-current state */
				if (gst_element_set_state(
						worker->pipeline,
						GST_STATE_PLAYING) ==
		    					GST_STATE_CHANGE_ASYNC)
				{
					/* XXX this blocks at most 2 seconds. */
					gst_element_get_state(
						worker->pipeline, NULL, NULL,
						2 * GST_SECOND);
				}
				if (worker->report_statechanges &&
                		    worker->notify_play_handler) {
					worker->notify_play_handler(
                                               		worker,
	                                                worker->owner);
				}
                                _add_duration_seek_query_timeout(worker);
                        }
                }
        }

	/* Send buffer percentage */
        if (worker->notify_buffer_status_handler)
                worker->notify_buffer_status_handler(worker, worker->owner,
						     percent);
}

static void _handle_element_msg(MafwGstRendererWorker *worker, GstMessage *msg)
{
	/* Only HelixBin sends "resolution" messages. */
	if (gst_structure_has_name(gst_message_get_structure(msg),
				   "resolution") &&
	    _handle_video_info(worker, gst_message_get_structure(msg)))
	{
		worker->media.has_visual_content = TRUE;
	}
}

static void _reset_pl_info(MafwGstRendererWorker *worker)
{
	if (worker->pl.items) {
		g_slist_foreach(worker->pl.items, (GFunc) g_free, NULL);
		g_slist_free(worker->pl.items);
		worker->pl.items = NULL;
	}

	worker->pl.current = 0;
	worker->pl.notify_play_pending = TRUE;
}

static GError * _get_specific_missing_plugin_error(GstMessage *msg)
{
	const GstStructure *gst_struct;
	const gchar *type;

	GError *error;
	gchar *desc;

	desc = gst_missing_plugin_message_get_description(msg);

	gst_struct = gst_message_get_structure(msg);
	type = gst_structure_get_string(gst_struct, "type");

	if ((type) && ((strcmp(type, MAFW_GST_MISSING_TYPE_DECODER) == 0) ||
		       (strcmp(type, MAFW_GST_MISSING_TYPE_ENCODER) == 0))) {

		/* Missing codec error. */
		const GValue *val;
		const GstCaps *caps;
		GstStructure *caps_struct;
		const gchar *mime;

		val = gst_structure_get_value(gst_struct, "detail");
		caps = gst_value_get_caps(val);
		caps_struct = gst_caps_get_structure(caps, 0);
		mime = gst_structure_get_name(caps_struct);

		if (g_strrstr(mime, "video")) {
			error = g_error_new_literal(
				MAFW_RENDERER_ERROR,
				MAFW_RENDERER_ERROR_VIDEO_CODEC_NOT_FOUND,
				desc);
		} else if (g_strrstr(mime, "audio")) {
			error = g_error_new_literal(
				MAFW_RENDERER_ERROR,
				MAFW_RENDERER_ERROR_AUDIO_CODEC_NOT_FOUND,
				desc);
		} else {
			error = g_error_new_literal(
				MAFW_RENDERER_ERROR,
				MAFW_RENDERER_ERROR_CODEC_NOT_FOUND,
				desc);
		}
	} else {
		/* Unsupported type error. */
		error = g_error_new(
			MAFW_RENDERER_ERROR,
			MAFW_RENDERER_ERROR_UNSUPPORTED_TYPE,
			"missing plugin: %s", desc);
	}

	g_free(desc);

	return error;
}

/*
 * Asynchronous message handler.  It gets removed from if it returns FALSE.
 */
static gboolean _async_bus_handler(GstBus *bus, GstMessage *msg,
				   MafwGstRendererWorker *worker)
{
	/* No need to handle message if error has already occured. */
	if (worker->is_error)
		return TRUE;

	/* Handle missing-plugin (element) messages separately, relaying more
	 * details. */
	if (gst_is_missing_plugin_message(msg)) {
		GError *err = _get_specific_missing_plugin_error(msg);
		/* FIXME?: for some reason, calling the error handler directly
		 * (_send_error) causes problems.  On the other hand, turning
		 * the error into a new GstMessage and letting the next
		 * iteration handle it seems to work. */
		_post_error(worker, err);
		return TRUE;
	}

	switch (GST_MESSAGE_TYPE(msg)) {
	case GST_MESSAGE_ERROR:
		if (!worker->is_error) {
			gchar *debug;
			GError *err;

			debug = NULL;
			gst_message_parse_error(msg, &err, &debug);
			g_debug("gst error: domain = %d, code = %d, "
				"message = '%s', debug = '%s'",
				err->domain, err->code, err->message, debug);
			if (debug)
				g_free(debug);

			/* If we are in playlist/radio mode, we silently
			   ignore the error and continue with the next
			   item until we end the playlist. If no
			   playable elements we raise the error and
			   after finishing we go to normal mode */

			if (worker->mode == WORKER_MODE_PLAYLIST ||
                            worker->mode == WORKER_MODE_REDUNDANT) {
				if (worker->pl.current <
				    (g_slist_length(worker->pl.items) - 1)) {
					/* If the error is "no space left"
					   notify, otherwise try to play the
					   next item */
					if (err->code ==
					    GST_RESOURCE_ERROR_NO_SPACE_LEFT) {
						_send_error(worker, err);

					} else {
						_play_pl_next(worker);
					}
				} else {
                                        /* Playlist EOS. We cannot try another
                                         * URI, so we have to go back to normal
                                         * mode and signal the error (done
                                         * below) */
					worker->mode = WORKER_MODE_SINGLE_PLAY;
					_reset_pl_info(worker);
				}
			}

			if (worker->mode == WORKER_MODE_SINGLE_PLAY) {
				if (err->domain == GST_STREAM_ERROR &&
					err->code == GST_STREAM_ERROR_WRONG_TYPE)
				{/* Maybe it is a playlist? */
					GSList *plitems = _parse_playlist(worker->media.location);
					
					if (plitems)
					{/* Yes, it is a plitem */
						g_error_free(err);
						mafw_gst_renderer_worker_play(worker, NULL, plitems);
						break;
					}
					
					
				}
				_send_error(worker, err);
			}
		}
		break;
	case GST_MESSAGE_EOS:
		if (!worker->is_error) {
			worker->eos = TRUE;

			if (worker->mode == WORKER_MODE_PLAYLIST) {
				if (worker->pl.current <
				    (g_slist_length(worker->pl.items) - 1)) {
					/* If the playlist EOS is not reached
					   continue playing */
					_play_pl_next(worker);
				} else {
					/* Playlist EOS, go back to normal
					   mode */
					worker->mode = WORKER_MODE_SINGLE_PLAY;
					_reset_pl_info(worker);
				}
			}

			if (worker->mode == WORKER_MODE_SINGLE_PLAY ||
                            worker->mode == WORKER_MODE_REDUNDANT) {
				if (worker->notify_eos_handler)
					worker->notify_eos_handler(
						worker,
						worker->owner);

				/* We can remove the message handlers now, we
				   are not interested in bus messages
				   anymore. */
				if (worker->bus) {
					gst_bus_set_sync_handler(worker->bus,
                                                                 NULL,
								 NULL,
								 NULL);
				}
				if (worker->async_bus_id) {
					g_source_remove(worker->async_bus_id);
					worker->async_bus_id = 0;
				}

                                if (worker->mode == WORKER_MODE_REDUNDANT) {
                                        /* Go to normal mode */
                                        worker->mode = WORKER_MODE_SINGLE_PLAY;
                                        _reset_pl_info(worker);
                                }
			}
		}
		break;
	case GST_MESSAGE_TAG:
		_handle_tag(worker, msg);
		break;
	case GST_MESSAGE_BUFFERING:
		_handle_buffering(worker, msg);
		break;
	case GST_MESSAGE_DURATION:
		_handle_duration(worker, msg);
		break;
	case GST_MESSAGE_ELEMENT:
		_handle_element_msg(worker, msg);
		break;
	case GST_MESSAGE_STATE_CHANGED:
		if ((GstElement *)GST_MESSAGE_SRC(msg) == worker->pipeline)
			_handle_state_changed(msg, worker);
		break;
	default:
		break;
	}
	return TRUE;
}

static void _volume_cb(MafwGstRendererWorkerVolume *wvolume, gdouble volume,
		       gpointer data)
{
	MafwGstRendererWorker *worker = data;
	GValue value = {0, };

	_reset_volume_and_mute_to_pipeline(worker);

	g_value_init(&value, G_TYPE_UINT);
	g_value_set_uint(&value, (guint) (volume * 100.0));
	mafw_extension_emit_property_changed(MAFW_EXTENSION(worker->owner),
					     MAFW_PROPERTY_RENDERER_VOLUME,
					     &value);
}

#ifdef MAFW_GST_RENDERER_ENABLE_MUTE

static void _mute_cb(MafwGstRendererWorkerVolume *wvolume, gboolean mute,
		     gpointer data)
{
	MafwGstRendererWorker *worker = data;
	GValue value = {0, };

	_reset_volume_and_mute_to_pipeline(worker);

	g_value_init(&value, G_TYPE_BOOLEAN);
	g_value_set_boolean(&value, mute);
	mafw_extension_emit_property_changed(MAFW_EXTENSION(worker->owner),
					     MAFW_PROPERTY_RENDERER_MUTE,
					     &value);
}

#endif

/* TODO: I think it's not enought to act on error, we need to handle
 * DestroyNotify on the given window ourselves, because for example helixbin
 * does it and silently stops the decoder thread.  But it doesn't notify
 * us... */
static int xerror(Display *dpy, XErrorEvent *xev)
{
	MafwGstRendererWorker *worker;

	if (Global_worker == NULL) {
		return -1;
	} else {
		worker = Global_worker;
	}

	/* Swallow BadWindow and stop pipeline when the error is about the
	 * currently set xid. */
	if (worker->xid &&
	    xev->resourceid == worker->xid &&
	    xev->error_code == BadWindow)
	{
		g_warning("BadWindow received for current xid (%x).",
			(gint)xev->resourceid);
		worker->xid = 0;
		/* We must post a message to the bus, because this function is
		 * invoked from a different thread (xvimagerenderer's queue). */
		_post_error(worker, g_error_new_literal(
				    MAFW_RENDERER_ERROR,
				    MAFW_RENDERER_ERROR_PLAYBACK,
				    "Video window gone"));
	}
	return 0;
}

/*
 * Resets the media information.
 */
static void _reset_media_info(MafwGstRendererWorker *worker)
{
	if (worker->media.location) {
		g_free(worker->media.location);
		worker->media.location = NULL;
	}
	worker->media.length_nanos = -1;
	worker->media.has_visual_content = FALSE;
	worker->media.seekable = SEEKABILITY_UNKNOWN;
	worker->media.video_width = 0;
	worker->media.video_height = 0;
	worker->media.fps = 0.0;
}

static void _set_volume_and_mute(MafwGstRendererWorker *worker, gdouble vol,
				 gboolean mute)
{
	g_return_if_fail(worker->wvolume != NULL);

	mafw_gst_renderer_worker_volume_set(worker->wvolume, vol, mute);
}

static void _set_volume(MafwGstRendererWorker *worker, gdouble new_vol)
{
	g_return_if_fail(worker->wvolume != NULL);

	_set_volume_and_mute(
		worker, new_vol,
		mafw_gst_renderer_worker_volume_is_muted(worker->wvolume));
}

static void _set_mute(MafwGstRendererWorker *worker, gboolean mute)
{
	g_return_if_fail(worker->wvolume != NULL);

	_set_volume_and_mute(
		worker, mafw_gst_renderer_worker_volume_get(worker->wvolume),
		mute);
}

/*
 * Start to play the media
 */
static void _start_play(MafwGstRendererWorker *worker)
{
	MafwGstRenderer *renderer = (MafwGstRenderer*) worker->owner;
	GstStateChangeReturn state_change_info;

	g_assert(worker->pipeline);
	g_object_set(G_OBJECT(worker->pipeline),
		     "uri", worker->media.location, NULL);

	g_debug("URI: %s", worker->media.location);
	g_debug("setting pipeline to PAUSED");

	worker->report_statechanges = TRUE;
	state_change_info = gst_element_set_state(worker->pipeline, 
						  GST_STATE_PAUSED);
	if (state_change_info == GST_STATE_CHANGE_NO_PREROLL) {
		/* FIXME:  for live sources we may have to handle
		   buffering and prerolling differently */
		g_debug ("Source is live!");
		worker->is_live = TRUE;
	}
        worker->prerolling = TRUE;

	worker->is_stream = uri_is_stream(worker->media.location);

        if (renderer->update_playcount_id > 0) {
                g_source_remove(renderer->update_playcount_id);
                renderer->update_playcount_id = 0;
        }

}

#ifndef GL_RENDERER
#define GL_RENDERER 0x1F01
#endif

static void gst_gl_ctx_thread_fn (GstGLContext * context, gpointer data)
{
	const GstGLFuncs *gl = context->gl_vtable;

	if (gl->GetString) {
		gboolean *use_gles2 = data;
		const gchar *renderer;

		renderer = (const gchar *)gl->GetString(GL_RENDERER);

		if (renderer && !strstr(renderer, "llvmpipe"))
			*use_gles2 = TRUE;
	}
}

/*
 * Try to force gstreamer GL to use EGL/GLES2 to check if it is HW accelerated.
 * If renderer is not llvmpipe, use EGL/GLES2 GL, otherwise use whatever
 * gstreamer decides to use.
 */
static void _check_gl_renderer(void)
{
	GstGLDisplay *gl_dpy;
	gboolean use_gles2 = FALSE;

	if (getenv("GST_GL_PLATFORM") || getenv("GST_GL_API"))
		return;

	setenv("GST_GL_PLATFORM", "egl", 1);
	setenv("GST_GL_API", "gles2", 1);

	gl_dpy = gst_gl_display_new();

	if (gl_dpy) {
		GstGLContext *gl_ctx = NULL;
		GST_OBJECT_LOCK(gl_dpy);

		if (gst_gl_display_create_context(
			    gl_dpy, NULL, &gl_ctx, NULL)) {
			GST_OBJECT_UNLOCK(gl_dpy);
			/* Despite its misleading name,
			 * gst_gl_context_thread_add() will block until thread
			 * function returns.
			 */
			gst_gl_context_thread_add(gl_ctx, gst_gl_ctx_thread_fn,
						  &use_gles2);
			g_debug("GLES2 renderer is%s llvmpipe",
				use_gles2 ? " not" : "");
			gst_object_unref(gl_ctx);
		} else {
			GST_OBJECT_UNLOCK(gl_dpy);
		}

		gst_object_unref(gl_dpy);
	} else {
		g_debug("Cannot create gst EGL/GLES2 GL context");
	}

	if (!use_gles2) {
		g_debug("Using default gst GL context");
		unsetenv("GST_GL_PLATFORM");
		unsetenv("GST_GL_API");
	} else {
		g_debug("Using EGL/GLES2 gst GL context");
	}
}

static gboolean
_check_xv_supported(void)
{
	Display *dpy = XOpenDisplay(NULL);
	gboolean rv = FALSE;
	XvAdaptorInfo *adaptors;
	guint nadaptors;
	gint i;

	if (!dpy) {
		g_warning("Failed to open $DISPLAY");
		goto no_dpy;
	}

	if (!XQueryExtension (dpy, "XVideo", &i, &i, &i))
		goto no_xv;

	if (Success != XvQueryAdaptors(dpy, DefaultRootWindow(dpy),
				       &nadaptors, &adaptors)) {
		goto no_xv;
	}

	for (i = 0; i < nadaptors; i++) {
		if (adaptors[i].type & XvImageMask) {
			/* There is at least one adaptor that has XvImage port.
			 * Lets assume it is useful for us and move on
			 */
			rv = TRUE;
			break;
		}

	}

	XvFreeAdaptorInfo (adaptors);

no_xv:
	XCloseDisplay(dpy);

no_dpy:
	return rv;
}

/*
 * Constructs gst pipeline
 *
 * FIXME: Could the same pipeline be used for playing all media instead of
 *  constantly deleting and reconstructing it again?
 */
static void _construct_pipeline(MafwGstRendererWorker *worker)
{
	g_debug("constructing pipeline");
	g_assert(worker != NULL);

	/* Return if we have already one */
	if (worker->pipeline)
		return;

	_free_taglist(worker);

	g_debug("Creating a new instance of playbin");
	worker->pipeline = gst_element_factory_make("playbin",
						    "playbin");

	if (!worker->pipeline) {
		g_critical("failed to create playback pipeline");
		g_signal_emit_by_name(MAFW_EXTENSION (worker->owner), 
				      "error",
				      MAFW_RENDERER_ERROR,
				      MAFW_RENDERER_ERROR_UNABLE_TO_PERFORM,
				      "Could not create pipeline");
		g_assert_not_reached();
	}


	worker->bus = gst_pipeline_get_bus(GST_PIPELINE(worker->pipeline));
	gst_bus_set_sync_handler(worker->bus,
				 (GstBusSyncHandler)_sync_bus_handler, worker,
				 NULL);
	worker->async_bus_id = gst_bus_add_watch_full(worker->bus,G_PRIORITY_HIGH,
						 (GstBusFunc)_async_bus_handler,
						 worker, NULL);

#ifndef MAFW_GST_RENDERER_DISABLE_PULSE_VOLUME
	

	/* Set audio and video sinks ourselves. We create and configure
	   them only once. */
	if (!worker->asink) {
		worker->asink = gst_element_factory_make("pulsesink", NULL);
		if (!worker->asink) {
			g_critical("Failed to create pipeline audio sink");
			g_signal_emit_by_name(MAFW_EXTENSION (worker->owner), 
					      "error",
					      MAFW_RENDERER_ERROR,
					      MAFW_RENDERER_ERROR_UNABLE_TO_PERFORM,
					      "Could not create audio sink");
			g_assert_not_reached();
		}
		gst_object_ref(worker->asink);
		g_object_set(worker->asink,
				"buffer-time", (gint64) MAFW_GST_BUFFER_TIME,
				"latency-time", (gint64) MAFW_GST_LATENCY_TIME,
				NULL);
	}
	g_object_set(worker->pipeline, "audio-sink", worker->asink, NULL);
#endif

	if (!worker->vsink) {
		if (_check_xv_supported()) {
			g_debug("Using XV accelerated output");
			worker->use_xv = TRUE;
			worker->vsink = gst_element_factory_make(
						"xvimagesink", NULL);
		} else {
			worker->use_xv = FALSE;
			g_debug("Using GL accelerated output");
			_check_gl_renderer();
			worker->vsink = gst_element_factory_make(
						"glimagesink", NULL);
		}

		if (!worker->vsink) {
			g_critical("Failed to create pipeline video sink");
			g_signal_emit_by_name(MAFW_EXTENSION (worker->owner), 
					      "error",
					      MAFW_RENDERER_ERROR,
					      MAFW_RENDERER_ERROR_UNABLE_TO_PERFORM,
					      "Could not create video sink");
			g_assert_not_reached();
		}

		gst_object_ref(worker->vsink);
		g_object_set(G_OBJECT(worker->vsink),
			     "handle-events", FALSE,
			     "force-aspect-ratio", TRUE,
			     NULL);
	}

	gst_video_overlay_set_window_handle(
				GST_VIDEO_OVERLAY(worker->vsink), 0);
	g_object_set(worker->pipeline,
			"video-sink", worker->vsink,
			"flags", 0x43,
			NULL);
}

/*
 * @seek_type: GstSeekType
 * @position: Time in seconds where to seek
 */
static void _do_seek(MafwGstRendererWorker *worker, GstSeekType seek_type,
		     gboolean relative, gint position, GError **error)
{
	gboolean ret;
	gint64 spos;

	g_assert(worker != NULL);

	if (worker->eos || !worker->media.seekable)
		goto err;

	/* According to the docs, relative seeking is not so easy:
	GST_SEEK_TYPE_CUR - change relative to currently configured segment.
	This can't be used to seek relative to the current playback position -
	do a position query, calculate the desired position and then do an
	absolute position seek instead if that's what you want to do. */
	if (relative)
	{
		gint curpos = mafw_gst_renderer_worker_get_position(worker);
		position = curpos + position;
	}

	if (position < 0) {
		position = 0;
	}

	worker->seek_position = position;
	worker->report_statechanges = FALSE;
	spos = (gint64)position * GST_SECOND;
	g_debug("seek: type = %d, offset = %" G_GUINT64_FORMAT, seek_type, spos);

        /* If the pipeline has been set to READY by us, then wake it up by
	   setting it to PAUSED (when we get the READY->PAUSED transition
	   we will execute the seek). This way when we seek we disable the
	   READY state (logical, since the player is not idle anymore)
	   allowing the sink to render the destination frame in case of
	   video playback */
        if (worker->in_ready && worker->state == GST_STATE_READY) {
                gst_element_set_state(worker->pipeline, GST_STATE_PAUSED);
        } else {
                ret = gst_element_seek(worker->pipeline, 1.0, GST_FORMAT_TIME,
                                       GST_SEEK_FLAG_FLUSH|GST_SEEK_FLAG_KEY_UNIT,
                                       seek_type, spos,
                                       GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
                if (!ret) {
                        /* Seeking is async, so seek_position should not be
                           invalidated here */
                        goto err;
                }
	}
        return;

err:    g_set_error(error,
		    MAFW_RENDERER_ERROR,
		    MAFW_RENDERER_ERROR_CANNOT_SET_POSITION,
		    "Seeking to %d failed", position);
}

/* @vol should be between [0 .. 100], higher values (up to 1000) are allowed,
 * but probably cause distortion. */
void mafw_gst_renderer_worker_set_volume(
	MafwGstRendererWorker *worker, guint volume)
{
        _set_volume(worker, CLAMP((gdouble)volume / 100.0, 0.0, 1.0));
}

guint mafw_gst_renderer_worker_get_volume(
	MafwGstRendererWorker *worker)
{
        return (guint)
		(mafw_gst_renderer_worker_volume_get(worker->wvolume) * 100);
}

void mafw_gst_renderer_worker_set_mute(MafwGstRendererWorker *worker,
                                     gboolean mute)
{
        _set_mute(worker, mute);
}

gboolean mafw_gst_renderer_worker_get_mute(MafwGstRendererWorker *worker)
{
	return mafw_gst_renderer_worker_volume_is_muted(worker->wvolume);
}

#ifdef HAVE_GDKPIXBUF
void mafw_gst_renderer_worker_set_current_frame_on_pause(MafwGstRendererWorker *worker,
								gboolean current_frame_on_pause)
{
        worker->current_frame_on_pause = current_frame_on_pause;
}

gboolean mafw_gst_renderer_worker_get_current_frame_on_pause(MafwGstRendererWorker *worker)
{
	return worker->current_frame_on_pause;
}
#endif

void mafw_gst_renderer_worker_set_position(MafwGstRendererWorker *worker,
					  GstSeekType seek_type,
					  gboolean relative,
					  gint position, GError **error)
{
        /* If player is paused and we have a timeout for going to ready
	 * restart it. This is logical, since the user is seeking and
	 * thus, the player is not idle anymore. Also this prevents that
	 * when seeking streams we enter buffering and in the middle of
	 * the buffering process we set the pipeline to ready (which stops
	 * the buffering before it reaches 100%, making the client think
	 * buffering is still going on).
	 */
        if (worker->ready_timeout) {
                _remove_ready_timeout(worker);
                _add_ready_timeout(worker);
        }

	_do_seek(worker, seek_type, relative, position, error);
        if (worker->notify_seek_handler)
                worker->notify_seek_handler(worker, worker->owner);
}

/*
 * Gets current position, rounded down into precision of one second.  If a seek
 * is pending, returns the position we are going to seek.  Returns -1 on
 * failure.
 */
gint mafw_gst_renderer_worker_get_position(MafwGstRendererWorker *worker)
{
	gint64 time = 0;
	g_assert(worker != NULL);

	/* If seek is ongoing, return the position where we are seeking. */
	if (worker->seek_position != -1)
	{
		return worker->seek_position;
	}
	/* Otherwise query position from pipeline. */
	if (worker->pipeline &&
	    gst_element_query_position(worker->pipeline, GST_FORMAT_TIME,
				       &time))
	{
		return (gint)(NSECONDS_TO_SECONDS(time));
	}
	return -1;
}

GHashTable *mafw_gst_renderer_worker_get_current_metadata(
	MafwGstRendererWorker *worker)
{
	return worker->current_metadata;
}

void mafw_gst_renderer_worker_set_xid(MafwGstRendererWorker *worker, XID xid)
{
	/* Check for errors on the target window */
	XSetErrorHandler(xerror);

	/* Store the target window id */
	g_debug("Setting xid: %x", (guint)xid);
	worker->xid = xid;

	/* Check if we should use it right away */
	mafw_gst_renderer_worker_apply_xid(worker);
}

XID mafw_gst_renderer_worker_get_xid(MafwGstRendererWorker *worker)
{
	return worker->xid;
}

gboolean mafw_gst_renderer_worker_get_seekable(MafwGstRendererWorker *worker)
{
	return worker->media.seekable;
}

static void _play_pl_next(MafwGstRendererWorker *worker) {
	gchar *next;

	g_assert(worker != NULL);
	g_return_if_fail(worker->pl.items != NULL);

	next = (gchar *) g_slist_nth_data(worker->pl.items,
					  ++worker->pl.current);
	mafw_gst_renderer_worker_stop(worker);
	_reset_media_info(worker);

	worker->media.location = g_strdup(next);
	_construct_pipeline(worker);
	_start_play(worker);
}

static void _do_play(MafwGstRendererWorker *worker)
{
	g_assert(worker != NULL);

	if (worker->pipeline == NULL) {
		g_debug("play without a pipeline!");
		return;
	}
	worker->report_statechanges = TRUE;

	/* If we have to stay paused, we do and add the ready
	 * timeout. Otherwise, we move the pipeline */
	if (!worker->stay_paused) {
		/* If pipeline is READY, we move it to PAUSED,
		 * otherwise, to PLAYING */
		if (worker->state == GST_STATE_READY) {
			gst_element_set_state(worker->pipeline,
					      GST_STATE_PAUSED);
			g_debug("setting pipeline to PAUSED");
		} else {
			_reset_volume_and_mute_to_pipeline(worker);
			gst_element_set_state(worker->pipeline,
					      GST_STATE_PLAYING);
			g_debug("setting pipeline to PLAYING");
		}
	}
	else {
		g_debug("staying in PAUSED state");
		_add_ready_timeout(worker);
	}
}

void mafw_gst_renderer_worker_play(MafwGstRendererWorker *worker,
				  const gchar *uri, GSList *plitems)
{
	g_assert(uri || plitems);

	mafw_gst_renderer_worker_stop(worker);
	_reset_media_info(worker);
	_reset_pl_info(worker);
	/* Check if the item to play is a single item or a playlist. */
	if (plitems || uri_is_playlist(uri)){
		gchar *item;
		/* In case of a playlist we parse it and start playing the first
		   item of the playlist. */
		if (plitems)
		{
			worker->pl.items = plitems;
		}
		else
		{
			worker->pl.items = _parse_playlist(uri);
		}
		if (!worker->pl.items)
		{
			_send_error(worker,
			    g_error_new(MAFW_RENDERER_ERROR,
					MAFW_RENDERER_ERROR_PLAYLIST_PARSING,
					"Playlist parsing failed: %s",
					uri));
			return;
		}

		/* Set the playback mode */
		worker->mode = WORKER_MODE_PLAYLIST;
		worker->pl.notify_play_pending = TRUE;

		/* Set the item to be played */
		worker->pl.current = 0;
		item = (gchar *) g_slist_nth_data(worker->pl.items, 0);
		worker->media.location = g_strdup(item);
	} else {
		/* Single item. Set the playback mode according to that */
		worker->mode = WORKER_MODE_SINGLE_PLAY;

		/* Set the item to be played */
		worker->media.location = g_strdup(uri);
	}
	_construct_pipeline(worker);
	_start_play(worker);
}

void mafw_gst_renderer_worker_play_alternatives(MafwGstRendererWorker *worker,
                                                gchar **uris)
{
        gint i;
        gchar *item;

        g_assert(uris && uris[0]);

        mafw_gst_renderer_worker_stop(worker);
        _reset_media_info(worker);
        _reset_pl_info(worker);

        /* Add the uris to playlist */
        i = 0;
        while (uris[i]) {
                worker->pl.items =
                        g_slist_append(worker->pl.items, g_strdup(uris[i]));
                i++;
        }

        /* Set the playback mode */
        worker->mode = WORKER_MODE_REDUNDANT;
        worker->pl.notify_play_pending = TRUE;

        /* Set the item to be played */
        worker->pl.current = 0;
        item = (gchar *) g_slist_nth_data(worker->pl.items, 0);
        worker->media.location = g_strdup(item);

        /* Start playing */
        _construct_pipeline(worker);
        _start_play(worker);
}

/*
 * Currently, stop destroys the Gst pipeline and resets the worker into
 * default startup configuration.
 */
void mafw_gst_renderer_worker_stop(MafwGstRendererWorker *worker)
{
	g_debug("worker stop");
	g_assert(worker != NULL);

	/* If location is NULL, this is a pre-created pipeline */
	if (worker->async_bus_id && worker->pipeline && !worker->media.location)
		return;

	if (worker->pipeline) {
		g_debug("destroying pipeline");
		if (worker->async_bus_id) {
			g_source_remove(worker->async_bus_id);
			worker->async_bus_id = 0;
		}
		gst_bus_set_sync_handler(worker->bus, NULL, NULL, NULL);
		gst_element_set_state(worker->pipeline, GST_STATE_NULL);
		if (worker->bus) {
			gst_object_unref(GST_OBJECT_CAST(worker->bus));
			worker->bus = NULL;
		}
		gst_object_unref(GST_OBJECT(worker->pipeline));
		worker->pipeline = NULL;
	}

	/* Reset worker */
	worker->report_statechanges = TRUE;
	worker->state = GST_STATE_NULL;
	worker->prerolling = FALSE;
	worker->is_live = FALSE;
	worker->buffering = FALSE;
	worker->is_stream = FALSE;
	worker->is_error = FALSE;
	worker->eos = FALSE;
	worker->seek_position = -1;
	worker->stay_paused = FALSE;
	_remove_ready_timeout(worker);
	_free_taglist(worker);
	if (worker->current_metadata) {
		g_hash_table_destroy(worker->current_metadata);
		worker->current_metadata = NULL;
	}

	if (worker->duration_seek_timeout != 0) {
		g_source_remove(worker->duration_seek_timeout);
		worker->duration_seek_timeout = 0;
	}

	/* Reset media iformation */
	_reset_media_info(worker);

	/* We are not playing, so we can let the screen blank */
	blanking_allow();
	keypadlocking_allow();

	/* And now get a fresh pipeline ready */
	_construct_pipeline(worker);
}

void mafw_gst_renderer_worker_pause(MafwGstRendererWorker *worker)
{
	g_assert(worker != NULL);

	if (worker->buffering && worker->state == GST_STATE_PAUSED &&
	    !worker->prerolling) {
		/* If we are buffering and get a pause, we have to
		 * signal state change and stay_paused */
		g_debug("Pausing while buffering, signalling state change");
		worker->stay_paused = TRUE;
		if (worker->notify_pause_handler) {
			worker->notify_pause_handler(
				worker,
				worker->owner);
		}
	} else {
		worker->report_statechanges = TRUE;

		if (gst_element_set_state(worker->pipeline, GST_STATE_PAUSED) ==
		    GST_STATE_CHANGE_ASYNC)
		{
			/* XXX this blocks at most 2 seconds. */
			gst_element_get_state(worker->pipeline, NULL, NULL,
				      2 * GST_SECOND);
		}
		blanking_allow();
		keypadlocking_allow();
	}
}

void mafw_gst_renderer_worker_resume(MafwGstRendererWorker *worker)
{
	if (worker->mode == WORKER_MODE_PLAYLIST ||
            worker->mode == WORKER_MODE_REDUNDANT) {
		/* We must notify play if the "playlist" playback
		   is resumed */
		worker->pl.notify_play_pending = TRUE;
	}
	if (worker->buffering && worker->state == GST_STATE_PAUSED &&
	    !worker->prerolling) {
		/* If we are buffering we cannot resume, but we know
		 * that the pipeline will be moved to PLAYING as
		 * stay_paused is FALSE, so we just activate the state
		 * change report, this way as soon as buffering is finished
		 * the pipeline will be set to PLAYING and the state
		 * change will be reported */
		worker->report_statechanges = TRUE;
		g_debug("Resumed while buffering, activating pipeline state "
			"changes");
		/* Notice though that we can receive the Resume before
		   we get any buffering information. In that case
		   we go with the "else" branch and set the pipeline to
		   to PLAYING. However, it is possible that in this case
		   we get the fist buffering signal before the
		   PAUSED -> PLAYING state change. In that case, since we
		   ignore state changes while buffering we never signal
		   the state change to PLAYING. We can only fix this by
		   checking, when we receive a PAUSED -> PLAYING transition
		   if we are buffering, and in that case signal the state
		   change (if we get that transition while buffering
		   is on, it can only mean that the client resumed playback
		   while buffering, and we must notify the state change) */
	} else {
		_do_play(worker);
	}
}

static void _volume_init_cb(MafwGstRendererWorkerVolume *wvolume,
			    gpointer data)
{
	MafwGstRendererWorker *worker = data;
	gdouble volume;
#ifdef MAFW_GST_RENDERER_ENABLE_MUTE
	gboolean mute;
#endif
	worker->wvolume = wvolume;

	g_debug("volume manager initialized");

	volume = mafw_gst_renderer_worker_volume_get(wvolume);
#ifdef MAFW_GST_RENDERER_ENABLE_MUTE
	mute = mafw_gst_renderer_worker_volume_is_muted(wvolume);
#endif
	_volume_cb(wvolume, volume, worker);
#ifdef MAFW_GST_RENDERER_ENABLE_MUTE
	_mute_cb(wvolume, mute, worker);
#endif
}

MafwGstRendererWorker *mafw_gst_renderer_worker_new(gpointer owner)
{
        MafwGstRendererWorker *worker;
	GMainContext *main_context;

	worker = g_new0(MafwGstRendererWorker, 1);
	worker->mode = WORKER_MODE_SINGLE_PLAY;
	worker->pl.items = NULL;
	worker->pl.current = 0;
	worker->pl.notify_play_pending = TRUE;
	worker->owner = owner;
	worker->report_statechanges = TRUE;
	worker->state = GST_STATE_NULL;
	worker->seek_position = -1;
	worker->ready_timeout = 0;
	worker->in_ready = FALSE;
	worker->xid = 0;
	worker->vsink = NULL;
	worker->asink = NULL;
	worker->tag_list = NULL;
	worker->current_metadata = NULL;

#ifdef HAVE_GDKPIXBUF
	worker->current_frame_on_pause = FALSE;
	_init_tmp_files_pool(worker);
#endif
	worker->notify_seek_handler = NULL;
	worker->notify_pause_handler = NULL;
	worker->notify_play_handler = NULL;
	worker->notify_buffer_status_handler = NULL;
	worker->notify_eos_handler = NULL;
	worker->notify_error_handler = NULL;
	Global_worker = worker;
	main_context = g_main_context_default();
	worker->wvolume = NULL;
	mafw_gst_renderer_worker_volume_init(main_context,
					     _volume_init_cb, worker,
					     _volume_cb, worker,
#ifdef MAFW_GST_RENDERER_ENABLE_MUTE
					     _mute_cb,
#else
					     NULL,
#endif
					     worker);
	blanking_init();
	_construct_pipeline(worker);

	return worker;
}

void mafw_gst_renderer_worker_exit(MafwGstRendererWorker *worker)
{
	blanking_deinit();
#ifdef HAVE_GDKPIXBUF
	_destroy_tmp_files_pool(worker);
#endif
	mafw_gst_renderer_worker_volume_destroy(worker->wvolume);
        mafw_gst_renderer_worker_stop(worker);
}
/* vi: set noexpandtab ts=8 sw=8 cino=t0,(0: */
