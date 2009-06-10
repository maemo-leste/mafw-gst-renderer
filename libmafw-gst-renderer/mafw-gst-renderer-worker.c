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
#include <gst/interfaces/xoverlay.h>
#include <gst/pbutils/missing-plugins.h>
#include <gst/base/gstbasesink.h>
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

#undef  G_LOG_DOMAIN
#define G_LOG_DOMAIN "mafw-gst-renderer-worker"

#define MAFW_GST_RENDERER_WORKER_SECONDS_READY 60
#define MAFW_GST_RENDERER_WORKER_SECONDS_DURATION_AND_SEEKABILITY 4

#define MAFW_GST_MISSING_TYPE_DECODER "decoder"
#define MAFW_GST_MISSING_TYPE_ENCODER "encoder"

#define MAFW_GST_BUFFER_TIME  600000L
#define MAFW_GST_LATENCY_TIME (MAFW_GST_BUFFER_TIME / 2)

/* Private variables. */
/* Global reference to worker instance, needed for Xerror handler */
static MafwGstRendererWorker *Global_worker = NULL;

/* Forward declarations. */
static void _do_play(MafwGstRendererWorker *worker);
static void _do_seek(MafwGstRendererWorker *worker, GstSeekType seek_type,
		     gint position, GError **error);
static void _play_pl_next(MafwGstRendererWorker *worker);

static void _emit_metadatas(MafwGstRendererWorker *worker);

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

static void _destroy_pixbuf (guchar *pixbuf, gpointer data)
{
	gst_buffer_unref(GST_BUFFER(data));
}

static void _emit_gst_buffer_as_graphic_file_cb(GstBuffer *new_buffer,
						gpointer user_data)
{
	SaveGraphicData *sgd = user_data;
	GdkPixbuf *pixbuf = NULL;

	if (new_buffer != NULL) {
		gint width, height;
		GstStructure *structure;

		structure =
			gst_caps_get_structure(GST_BUFFER_CAPS(new_buffer), 0);

		gst_structure_get_int(structure, "width", &width);
		gst_structure_get_int(structure, "height", &height);

		pixbuf = gdk_pixbuf_new_from_data(
			GST_BUFFER_DATA(new_buffer), GDK_COLORSPACE_RGB,
			FALSE, 8, width, height,
			GST_ROUND_UP_4(3 * width), _destroy_pixbuf,
			new_buffer);

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
					     GstBuffer *buffer,
					     const gchar *metadata_key)
{
	GdkPixbufLoader *loader;
	GstStructure *structure;
	const gchar *mime = NULL;
	GError *error = NULL;

	g_return_if_fail((buffer != NULL) && GST_IS_BUFFER(buffer));

	structure = gst_caps_get_structure(GST_BUFFER_CAPS(buffer), 0);
	mime = gst_structure_get_name(structure);

	if (g_str_has_prefix(mime, "video/x-raw")) {
		gint framerate_d, framerate_n;
		GstCaps *to_caps;
		SaveGraphicData *sgd;

		gst_structure_get_fraction (structure, "framerate",
					    &framerate_n, &framerate_d);

		to_caps = gst_caps_new_simple ("video/x-raw-rgb",
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
		bvw_frame_conv_convert (buffer, to_caps,
					_emit_gst_buffer_as_graphic_file_cb,
					sgd);
	} else {
		GdkPixbuf *pixbuf = NULL;
		loader = gdk_pixbuf_loader_new_with_mime_type(mime, &error);
		g_signal_connect (G_OBJECT (loader), "size-prepared", 
				 (GCallback)_pixbuf_size_prepared_cb, NULL);

		if (loader == NULL) {
			g_warning ("%s\n", error->message);
			g_error_free (error);
		} else {
			if (!gdk_pixbuf_loader_write (loader,
						      GST_BUFFER_DATA(buffer),
						      GST_BUFFER_SIZE(buffer),
						      &error)) {
				g_warning ("%s\n", error->message);
				g_error_free (error);

				gdk_pixbuf_loader_close (loader, NULL);
			} else {
				pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);

				if (!gdk_pixbuf_loader_close (loader, &error)) {
					g_warning ("%s\n", error->message);
					g_error_free (error);

					g_object_unref(pixbuf);
				} else {
					SaveGraphicData *sgd;

					sgd = g_new0(SaveGraphicData, 1);

					sgd->worker = worker;
					sgd->metadata_key =
						g_strdup(metadata_key);
					sgd->pixbuf = pixbuf;

					_emit_gst_buffer_as_graphic_file_cb(
						NULL, sgd);
				}
			}
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
	g_debug("removing timeout for READY");
	if (worker->ready_timeout != 0) {
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
	g_idle_add((GSourceFunc)_emit_video_info, worker);

	return TRUE;
}

static void _parse_stream_info_item(MafwGstRendererWorker *worker, GObject *obj)
{
	GParamSpec *pspec;
	GEnumValue *val;
	gint type;

	g_object_get(obj, "type", &type, NULL);
	pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(obj), "type");
	val = g_enum_get_value(G_PARAM_SPEC_ENUM(pspec)->enum_class, type);
	if (!val)
		return;
	if (!g_ascii_strcasecmp(val->value_nick, "video") ||
	    !g_ascii_strcasecmp(val->value_name, "video"))
	{
		GstCaps *vcaps;
		GstObject *object;

		object = NULL;
		g_object_get(obj, "object", &object, NULL);
		vcaps = NULL;
		if (object) {
			vcaps = gst_pad_get_caps(GST_PAD_CAST(object));
		} else {
			g_object_get(obj, "caps", &vcaps, NULL);
			gst_caps_ref(vcaps);
		}
		if (vcaps) {
			if (gst_caps_is_fixed(vcaps))
			{
				_handle_video_info(
					worker,
					gst_caps_get_structure(vcaps, 0));
			}
			gst_caps_unref(vcaps);
		}
	}
}

/* It always returns FALSE, because it is used as an idle callback as well. */
static gboolean _parse_stream_info(MafwGstRendererWorker *worker)
{
	GList *stream_info, *s;

	stream_info = NULL;
	if (g_object_class_find_property(G_OBJECT_GET_CLASS(worker->pipeline),
					 "stream-info"))
	{
		g_object_get(worker->pipeline,
			     "stream-info", &stream_info, NULL);
	}
	for (s = stream_info; s; s = g_list_next(s))
		_parse_stream_info_item(worker, G_OBJECT(s->data));
	return FALSE;
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
		gst_x_overlay_set_xwindow_id(GST_X_OVERLAY(worker->vsink), 
					     worker->xid);
		/* Ask the gst to redraw the frame if we are paused */
		/* TODO: in MTG this works only in non-fs -> fs way. */
		if (worker->state == GST_STATE_PAUSED)
		{
			gst_x_overlay_expose(GST_X_OVERLAY(worker->vsink));
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
	if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ELEMENT &&
	    gst_structure_has_name(msg->structure, "prepare-xwindow-id"))
	{
		g_debug("got prepare-xwindow-id");
		worker->media.has_visual_content = TRUE;
		/* The user has to preset the XID, we don't create windows by
		 * ourselves. */
		if (!worker->xid) {
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
			return GST_BUS_DROP;
		} else {
			g_debug ("Video window to use is: %x", 
				 (gint) worker->xid);
		}

		/* Instruct vsink to use the client-provided window */
		mafw_gst_renderer_worker_apply_xid(worker);

		/* Handle colorkey and autopaint */
		mafw_gst_renderer_worker_set_autopaint(
			worker,
			worker->autopaint);
		g_object_get(worker->vsink,
			     "colorkey", &worker->colorkey, NULL);
		/* Defer the signal emission to the thread running the
		 * mainloop. */
		if (worker->colorkey != -1) {
			gst_bus_post(worker->bus,
				     gst_message_new_application(
					     GST_OBJECT(worker->vsink),
					     gst_structure_empty_new("ckey")));
		}
		return GST_BUS_DROP;
	}
	return GST_BUS_PASS;
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

	duration1_seconds = duration1 / GST_SECOND;
	duration2_seconds = duration2 / GST_SECOND;

	return duration1_seconds == duration2_seconds;
}

static void _check_duration(MafwGstRendererWorker *worker, gint64 value)
{
	gboolean right_query = TRUE;

	if (value == -1) {
		GstFormat format = GST_FORMAT_TIME;
		right_query =
			gst_element_query_duration(worker->pipeline, &format,
						   &value);
	}

	if (right_query && value > 0 &&
	    !_seconds_duration_equal(worker->media.length_nanos, value)) {
		mafw_renderer_emit_metadata_int64(worker->owner,
						  MAFW_METADATA_KEY_DURATION,
						  value / GST_SECOND);
	}

	worker->media.length_nanos = value;
	g_debug("media duration: %lld", worker->media.length_nanos);
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
		mafw_renderer_emit_metadata_boolean(
			worker->owner,
			MAFW_METADATA_KEY_IS_SEEKABLE,
			seekable == SEEKABILITY_SEEKABLE ? TRUE : FALSE);
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
		GstPad *pad = GST_BASE_SINK_PAD(worker->vsink);
		GstCaps *caps = GST_PAD_CAPS(pad);
		if (caps && gst_caps_is_fixed(caps)) {
			GstStructure *structure;
			structure = gst_caps_get_structure(caps, 0);
			if (!_handle_video_info(worker, structure))
				return;
		}
	}

	/* Something might have gone wrong at this point already. */
	if (worker->is_error) {
		g_debug("Error occured during preroll");
		return;
	}

	/* Streaminfo might reveal the media to be unsupported.  Therefore we
	 * need to check the error again. */
	_parse_stream_info(worker);
	if (worker->is_error) {
		g_debug("Error occured. Leaving");
		return;
	}

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

static void _handle_state_changed(GstMessage *msg, MafwGstRendererWorker *worker)
{
	GstState newstate, oldstate;
	MafwGstRenderer *renderer = (MafwGstRenderer*)worker->owner;

	gst_message_parse_state_changed(msg, &oldstate, &newstate, NULL);
	if (oldstate == newstate) {
		return;
	}

	/* While buffering, we have to wait in PAUSED 
	   until we reach 100% before doing anything */
	if (worker->buffering) {
		worker->state = newstate;
		return;
	}

	/* Woken up from READY, resume stream position and playback */
	if (newstate == GST_STATE_PAUSED && worker->in_ready &&
	    worker->state == GST_STATE_READY) {
		g_debug("State changed to pause after ready");
		worker->state = GST_STATE_PAUSED;
		_do_seek(worker, GST_SEEK_TYPE_SET, worker->seek_position,
			 NULL);
		_do_play(worker);
	}
	else if (newstate == GST_STATE_PAUSED &&
		   worker->report_statechanges && !worker->in_ready)
	{

/* 		/\* Perform pending seek, 1st try.  Some formats can seek already */
/* 		 * in PAUSED state. *\/ */
/* 		if (worker->seek_position != -1) { */
/* 			g_debug("trying to seek in PAUSED state"); */
/* 			_do_seek(worker, GST_SEEK_TYPE_SET, */
/* 				 worker->seek_position, NULL); */
/* 		} */

		/* PAUSED after pipeline has been constructed */
		if (worker->prerolling) {
			g_debug ("Prerolling done, finalizaing startup");
			_finalize_startup(worker);
			_do_play(worker);
			renderer->play_failed_count = 0;
		} else {
			_add_ready_timeout(worker);
		}

		/* PAUSED while in PLAYING/TRANSITIONING state
		 * stay_paused is set if we were paused while transitioning
		 * prerolling is not set if we were paused while playing
		 */
		if ((worker->prerolling && worker->stay_paused) ||
		    !worker->prerolling) {
			if (worker->notify_pause_handler)
				worker->notify_pause_handler(worker, worker->owner);

#ifdef HAVE_GDKPIXBUF
			if (worker->media.has_visual_content &&
			    worker->current_frame_on_pause) {
				GstBuffer *buffer = NULL;

				g_object_get(worker->pipeline, "frame", &buffer,
					     NULL);

				if (buffer) {
					_emit_gst_buffer_as_graphic_file(
						worker,
						buffer,
						MAFW_METADATA_KEY_PAUSED_THUMBNAIL_URI);
				}
			}
#endif
		}
		worker->prerolling = FALSE;
		worker->state = GST_STATE_PAUSED;
	}
	else if (newstate == GST_STATE_PLAYING)
	{
		/* if seek was called, at this point it is really ended */
		worker->seek_position = -1;
		if (worker->report_statechanges)
		{
			worker->state = GST_STATE_PLAYING;
			worker->eos = FALSE;

			switch (worker->mode) {
			case WORKER_MODE_SINGLE_PLAY:
				/* Notify play */
				if (worker->notify_play_handler)
					worker->notify_play_handler(
						worker,
						worker->owner);
				break;
			case WORKER_MODE_PLAYLIST:
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
			if (worker->media.has_visual_content)
				blanking_prohibit();
		}
		_remove_ready_timeout(worker);
		_emit_metadatas(worker);
		/* Query duration and seekability. Useful for vbr
		 * clips or streams. */
		_add_duration_seek_query_timeout(worker);
	}
	else if (newstate == GST_STATE_READY && worker->in_ready) {
		g_debug("changed to GST_STATE_READY");
		worker->state = GST_STATE_READY;
		worker->ready_timeout = 0;
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
	GstBuffer *buffer = NULL;
	const GValue *value = NULL;

	g_return_if_fail(gst_tag_list_get_tag_size(list, GST_TAG_IMAGE) > 0);

	value = gst_tag_list_get_value_index(list, GST_TAG_IMAGE, 0);

	g_return_if_fail((value != NULL) && G_VALUE_HOLDS(value, GST_TYPE_BUFFER));

	buffer = g_value_peek_pointer(value);

	g_return_if_fail((buffer != NULL) && GST_IS_BUFFER(buffer));

	_emit_gst_buffer_as_graphic_file(worker, buffer,
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

static void _get_mafw_tag_values(const gchar *tag, const GstTagList *list,
				 const gchar **mafwtag, GValueArray **values)
{

	/* Mapping between Gst <-> MAFW metadata tags
	 * NOTE: This assumes that GTypes matches between GST and MAFW. */

	static GHashTable *tagmap = NULL;
	gint i, count;
	GType type;

	g_assert(mafwtag);
	g_assert(values);

	/* Initialize return values  */
	*mafwtag = NULL;
	*values = NULL;

	/* Don't handle MAFW_METADATA_KEY_RENDERER_ART_URI here because its
	   value is asynchronously obtained. */
	if (strcmp(tag, GST_TAG_IMAGE) == 0) {
		return;
	}

	if (!tagmap) {
		tagmap = _build_tagmap();
	}

	/* Is there a mapping for this tag? */
	if (!(*mafwtag = g_hash_table_lookup(tagmap, tag))) {
		return;
	}

	/* Build a value array of this tag.  We need to make sure that strings
	 * are UTF-8.  GstTagList API says that the value is always UTF8, but it
	 * looks like the ID3 demuxer still might sometimes produce non-UTF-8
	 * strings. */

	count = gst_tag_list_get_tag_size(list, tag);
	type = gst_tag_get_type(tag);
	*values = g_value_array_new(count);
	for (i = 0; i < count; ++i) {
		GValue *v;

		v = (GValue *) gst_tag_list_get_value_index(list, tag, i);
		if (type == G_TYPE_STRING) {
			gchar *orig, *utf8;

			gst_tag_list_get_string_index(list, tag, i, &orig);
			if (convert_utf8(orig, &utf8)) {
				GValue utf8gval = {0};

				g_value_init(&utf8gval, G_TYPE_STRING);
				g_value_take_string(&utf8gval, utf8);
				g_value_array_append(*values, &utf8gval);
				g_value_unset(&utf8gval);
			}
			g_free(orig);
		} else if (type == G_TYPE_UINT) {
			GValue intgval = {0};

			g_value_init(&intgval, G_TYPE_INT);
			g_value_transform(v, &intgval);
			g_value_array_append(*values, &intgval);
			g_value_unset(&intgval);
		} else {
			g_value_array_append(*values, v);
		}
	}
}

/*
 * Emits metadata-changed signals for gst tags.
 */
static void _emit_tag(const GstTagList *list, const gchar *tag,
		      MafwGstRendererWorker *worker)
{

	const gchar *mafwtag;
	GValueArray *values = NULL;

	g_debug("tag: '%s' (type: %s)", tag,
		g_type_name(gst_tag_get_type(tag)));

#ifdef HAVE_GDKPIXBUF
	if (strcmp(tag, GST_TAG_IMAGE) == 0) {
		_emit_renderer_art(worker, list);
		return;
	}
#endif

	_get_mafw_tag_values(tag, list, &mafwtag, &values);

	if (mafwtag && values) {
		g_signal_emit_by_name(worker->owner, "metadata-changed",
				      mafwtag, values);
		g_value_array_free(values);
	}
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
	}
}

static void _get_tag_metadata(const GstTagList *list, const gchar *tag,
			      gpointer data)
{
	GHashTable **metadata = (GHashTable **) data;
	const gchar *mafwtag;
	GValueArray *values;

	g_assert(metadata);

	_get_mafw_tag_values(tag, list, &mafwtag, &values);

	if (mafwtag && values) {
		if (!*metadata)
			*metadata = mafw_metadata_new();

		if (values->n_values == 1) {
			GValue *val = g_value_array_get_nth(values, 0);
			GValue *new_val = g_new0(GValue, 1);

			g_value_init(new_val, G_VALUE_TYPE(val));
			g_value_copy(val, new_val);
			g_hash_table_insert(*metadata, g_strdup(mafwtag),
					    new_val);

			g_value_array_free(values);
		} else {
			g_hash_table_insert(*metadata, g_strdup(mafwtag),
					    values);
		}
	}
}

static void _get_metadata(GstMessage *msg, gpointer data)
{
	GstTagList *tags;

	gst_message_parse_tag(msg, &tags);
	gst_tag_list_foreach(tags, (gpointer) _get_tag_metadata, data);
	gst_tag_list_free(tags);
}

static void _handle_buffering(MafwGstRendererWorker *worker, GstMessage *msg)
{
	gint percent;
	MafwGstRenderer *renderer = (MafwGstRenderer*)worker->owner;

	gst_message_parse_buffering(msg, &percent);
	g_debug("buffering: %d, live: %d", percent, worker->is_live);

        /* No state management needed for live pipelines */
        if (!worker->is_live) {
                if (!worker->buffering) {
                        worker->buffering = TRUE;
                        if (worker->state == GST_STATE_PLAYING) {
                                worker->report_statechanges = FALSE;
                                /* We can't call _pause() here, since it sets
                                 * the "report_statechanges" to TRUE.  We don't
                                 * want that, application doesn't need to know
                                 * that internally the state changed to
                                 * PAUSED. */
                                gst_element_set_state(worker->pipeline,
                                                      GST_STATE_PAUSED);
                                /* XXX this blocks till statechange. */
                                gst_element_get_state(worker->pipeline, NULL,
                                                      NULL,
                                                      GST_CLOCK_TIME_NONE);
                        }
                }

                if (percent >= 100) {
                        worker->buffering = FALSE;
                        /* On buffering we go to PAUSED, so here we move back to
                           PLAYING */
                        if (worker->state == GST_STATE_PAUSED) {
                                /* If buffering more than once, do this only the
                                   first time we are done with buffering */
                                if (worker->prerolling) {
					_finalize_startup(worker);
					worker->prerolling = FALSE;
                                }
                                _do_play(worker);
                                renderer->play_failed_count = 0;
                        } else if (worker->state == GST_STATE_PLAYING) {
				/* In this case we got a PLAY command while 
				   buffering, likely because it was issued
				   before we got the first buffering signal.
				   The UI should not do this, but if it does,
				   we have to signal that we have executed
				   the state change, since in 
				   _handle_state_changed we do not do anything 
				   if we are buffering  */
				if (worker->report_statechanges) {
					if (worker->state == GST_STATE_PAUSED && 
		                            worker->notify_pause_handler) {
						worker->notify_pause_handler(
                                		                worker,
                                                		worker->owner);
					} else  if (worker->state == GST_STATE_PLAYING &&
                		                    worker->notify_play_handler) {
						worker->notify_play_handler(
                                                		worker,
		                                                worker->owner);
					}
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
	if (gst_structure_has_name(msg->structure, "resolution") &&
	    _handle_video_info(worker, msg->structure))
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

			/* If we are in playlist mode, we silently
			   ignore the error and continue with the next
			   item until we end the playlist. If no
			   playable elements we raise the error and
			   after finishing we go to normal mode */

			if (worker->mode == WORKER_MODE_PLAYLIST) {
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
					/* Playlist EOS. Go to normal mode */
					worker->mode = WORKER_MODE_SINGLE_PLAY;
					_reset_pl_info(worker);
				}
			}

			if (worker->mode == WORKER_MODE_SINGLE_PLAY) {
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

			if (worker->mode == WORKER_MODE_SINGLE_PLAY) {
				if (worker->notify_eos_handler)
					worker->notify_eos_handler(
						worker,
						worker->owner);

				/* We can remove the message handlers now, we
				   are not interested in bus messages anymore. */
				if (worker->bus) {
					gst_bus_set_sync_handler(worker->bus, NULL,
								 NULL);
				}
				if (worker->async_bus_id) {
					g_source_remove(worker->async_bus_id);
					worker->async_bus_id = 0;
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
	case GST_MESSAGE_APPLICATION:
		if (gst_structure_has_name(gst_message_get_structure(msg),
					   "ckey"))
		{
			GValue v = {0};
			g_value_init(&v, G_TYPE_INT);
			g_value_set_int(&v, worker->colorkey);
			mafw_extension_emit_property_changed(
				MAFW_EXTENSION(worker->owner),
				MAFW_PROPERTY_RENDERER_COLORKEY,
				&v);
		}
	default: break;
	}
	return TRUE;
}

/* NOTE this function will possibly be called from a different thread than the
 * glib main thread. */
static void _stream_info_cb(GstObject *pipeline, GParamSpec *unused,
			    MafwGstRendererWorker *worker)
{
	g_debug("stream-info changed");
	_parse_stream_info(worker);
}

static void _volume_cb(MafwGstRendererWorkerVolume *wvolume, gdouble volume,
		       gpointer data)
{
	MafwGstRendererWorker *worker = data;
	GValue value = {0, };

	g_value_init(&value, G_TYPE_UINT);
	g_value_set_uint(&value, (guint) (volume * 100.0));
	mafw_extension_emit_property_changed(MAFW_EXTENSION(worker->owner),
					     MAFW_PROPERTY_RENDERER_VOLUME,
					     &value);
}

static void _mute_cb(MafwGstRendererWorkerVolume *wvolume, gboolean mute,
		     gpointer data)
{
	MafwGstRendererWorker *worker = data;
	GValue value = {0, };

	g_value_init(&value, G_TYPE_BOOLEAN);
	g_value_set_boolean(&value, mute);
	mafw_extension_emit_property_changed(MAFW_EXTENSION(worker->owner),
					     MAFW_PROPERTY_RENDERER_MUTE,
					     &value);
}

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

	g_debug("Creating a new instance of playbin2");
	worker->pipeline = gst_element_factory_make("playbin2",
						    "playbin");
	if (worker->pipeline == NULL)
	{
		/* Let's try with playbin */
		g_warning ("playbin2 failed, falling back to playbin");
		worker->pipeline = gst_element_factory_make("playbin",
							    "playbin");

		if (worker->pipeline) {
			/* Use nwqueue only for non-rtsp and non-mms(h)
			   streams. */
			gboolean use_nw;
			use_nw = worker->media.location && 
				!g_str_has_prefix(worker->media.location, 
						  "rtsp://") &&
				!g_str_has_prefix(worker->media.location, 
						  "mms://") &&
				!g_str_has_prefix(worker->media.location, 
						  "mmsh://");
			
			g_debug("playbin using network queue: %d", use_nw);

			/* These need a modified version of playbin. */
			g_object_set(G_OBJECT(worker->pipeline),
				     "nw-queue", use_nw, NULL);
			g_object_set(G_OBJECT(worker->pipeline),
				     "no-video-transform", TRUE, NULL);
		}
	}

	if (!worker->pipeline) {
		g_critical("failed to create playback pipeline");
		g_signal_emit_by_name(MAFW_EXTENSION (worker->owner), 
				      "error",
				      MAFW_RENDERER_ERROR,
				      MAFW_RENDERER_ERROR_UNABLE_TO_PERFORM,
				      "Could not create pipeline");
		g_assert_not_reached();
	}

	/* g_object_set(worker->pipeline,"flags",99,NULL); */

	worker->bus = gst_pipeline_get_bus(GST_PIPELINE(worker->pipeline));
	gst_bus_set_sync_handler(worker->bus,
				 (GstBusSyncHandler)_sync_bus_handler, worker);
	worker->async_bus_id = gst_bus_add_watch_full(worker->bus,G_PRIORITY_HIGH,
						 (GstBusFunc)_async_bus_handler,
						 worker, NULL);

	/* Listen for changes in stream-info object to find out whether the
	 * media contains video and throw error if application has not provided
	 * video window. */
	g_signal_connect(worker->pipeline, "notify::stream-info",
			 G_CALLBACK(_stream_info_cb), worker);

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
		g_object_set(worker->asink, "buffer-time", 
			     (gint64) MAFW_GST_BUFFER_TIME, NULL);
		g_object_set(worker->asink, "latency-time", 
			     (gint64) MAFW_GST_LATENCY_TIME, NULL);
	}

	if (!worker->vsink) {
		worker->vsink = gst_element_factory_make("xvimagesink", NULL);
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
		g_object_set(G_OBJECT(worker->vsink), "handle-events",
			     TRUE, NULL);
		g_object_set(worker->vsink, "force-aspect-ratio",
			     TRUE, NULL);
	}

	g_object_set(worker->pipeline, "audio-sink", worker->asink, NULL);		
	g_object_set(worker->pipeline, "video-sink", worker->vsink, NULL);
}

/*
 * @seek_type: GstSeekType
 * @position: Time in seconds where to seek
 */
static void _do_seek(MafwGstRendererWorker *worker, GstSeekType seek_type,
		     gint position, GError **error)
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
	if (seek_type == GST_SEEK_TYPE_CUR)
	{
		gint curpos = mafw_gst_renderer_worker_get_position(worker);
		position = curpos + position;
		seek_type = GST_SEEK_TYPE_SET;
	}

	if (position < 0) {
		position = 0;
	}

	worker->seek_position = position;
	worker->report_statechanges = FALSE;
	spos = (gint64)position * GST_SECOND;
	g_debug("seek: type = %d, offset = %lld", seek_type, spos);

	ret = gst_element_seek(worker->pipeline, 1.0, GST_FORMAT_TIME,
			       GST_SEEK_FLAG_FLUSH, seek_type, spos,
			       GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);
	if (ret) {
		/* Seeking is async, so seek_position should not be invalidated
		here */
		return;
	}
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
					  gint position, GError **error)
{
        _do_seek(worker, seek_type, position, error);
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
	GstFormat format;
	gint64 time = 0;
	g_assert(worker != NULL);

	/* If seek is ongoing, return the position where we are seeking. */
	if (worker->seek_position != -1)
	{
		return worker->seek_position;
	}
	/* Otherwise query position from pipeline. */
	format = GST_FORMAT_TIME;
	if (worker->pipeline &&
            gst_element_query_position(worker->pipeline, &format, &time))
	{
		return (gint)(time / GST_SECOND);
	}
	return -1;
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

gboolean mafw_gst_renderer_worker_get_autopaint(
	MafwGstRendererWorker *worker)
{
	return worker->autopaint;
}
void mafw_gst_renderer_worker_set_autopaint(
	MafwGstRendererWorker *worker, gboolean autopaint)
{
	worker->autopaint = autopaint;
	if (worker->vsink)
		g_object_set(worker->vsink, "autopaint-colorkey",
			     autopaint, NULL);
}

gint mafw_gst_renderer_worker_get_colorkey(
	MafwGstRendererWorker *worker)
{
	return worker->colorkey;
}

gboolean mafw_gst_renderer_worker_get_seekable(MafwGstRendererWorker *worker)
{
	return worker->media.seekable;
}

GHashTable *mafw_gst_renderer_worker_get_current_metadata(
	MafwGstRendererWorker *worker)
{
	GHashTable *metadata = NULL;

	if (worker->tag_list)
		g_ptr_array_foreach(worker->tag_list, (GFunc) _get_metadata,
				    &metadata);

	return metadata;
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

static void _on_pl_entry_parsed(TotemPlParser *parser, gchar *uri,
				gpointer metadata, gpointer user_data)
{
	MafwGstRendererWorker *worker = user_data;

	if (uri != NULL) {
		worker->pl.items =
			g_slist_append(worker->pl.items, g_strdup(uri));
	}
}

static void _do_play(MafwGstRendererWorker *worker)
{
	g_assert(worker != NULL);

	if (worker->pipeline == NULL) {
		g_debug("play without a pipeline!");
		return;
	}
	worker->report_statechanges = TRUE;

	if (!worker->stay_paused) {
		if (worker->state == GST_STATE_READY) {
			gst_element_set_state(worker->pipeline,
					      GST_STATE_PAUSED);
			g_debug("setting pipeline to PAUSED");
		} else {
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
				  const gchar *uri)
{
	g_assert(uri);

	mafw_gst_renderer_worker_stop(worker);
	_reset_media_info(worker);
	_reset_pl_info(worker);
	/* Check if the item to play is a single item or a playlist. */
	if (uri_is_playlist(uri)){
		/* In case of a playlist we parse it and start playing the first
		   item of the playlist. */
		TotemPlParser *pl_parser;
		gchar *item;

		/* Initialize the playlist parser */
		pl_parser = totem_pl_parser_new ();
		g_object_set(pl_parser, "recurse", TRUE, "disable-unsafe",
			     TRUE, NULL);
		g_signal_connect(G_OBJECT(pl_parser), "entry-parsed",
				 G_CALLBACK(_on_pl_entry_parsed), worker);

		/* Parsing */
		if (totem_pl_parser_parse(pl_parser, uri, FALSE) !=
		    TOTEM_PL_PARSER_RESULT_SUCCESS) {
			/* An error happens while parsing */
			_send_error(worker,
				    g_error_new(MAFW_RENDERER_ERROR,
						MAFW_RENDERER_ERROR_PLAYLIST_PARSING,
						"Playlist parsing failed: %s",
						uri));
			return;
		}

		if (!worker->pl.items) {
			/* The playlist is empty */
			_send_error(worker,
				    g_error_new(MAFW_RENDERER_ERROR,
						MAFW_RENDERER_ERROR_PLAYLIST_PARSING,
						"The playlist %s is empty.",
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

		/* Free the playlist parser */
		g_object_unref(pl_parser);
	} else {
		/* Single item. Set the playback mode according to that */
		worker->mode = WORKER_MODE_SINGLE_PLAY;

		/* Set the item to be played */
		worker->media.location = g_strdup(uri);
	}
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
		gst_bus_set_sync_handler(worker->bus, NULL, NULL);
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
	_remove_ready_timeout(worker);
	_free_taglist(worker);
	if (worker->duration_seek_timeout != 0) {
		g_source_remove(worker->duration_seek_timeout);
		worker->duration_seek_timeout = 0;
	}

	/* Reset media iformation */
	_reset_media_info(worker);

	/* We are not playing, so we can let the screen blank */
	blanking_allow();

	/* And now get a fresh pipeline ready */
	_construct_pipeline(worker);
}

void mafw_gst_renderer_worker_pause(MafwGstRendererWorker *worker)
{
	g_assert(worker != NULL);
	worker->report_statechanges = TRUE;

	/* FIXME: Check what happens if playbin is buffering? */
	gst_element_set_state(worker->pipeline, GST_STATE_PAUSED);
	/* XXX this blocks till statechange. */
	gst_element_get_state(worker->pipeline, NULL, NULL,
			      GST_CLOCK_TIME_NONE);
	blanking_allow();
}

void mafw_gst_renderer_worker_resume(MafwGstRendererWorker *worker)
{
	if (worker->mode == WORKER_MODE_PLAYLIST) {
		/* We must notify play if the "playlist" playback
		   is resumed */
		worker->pl.notify_play_pending = TRUE;
	}
	_do_play(worker);
}

static void _volume_init_cb(MafwGstRendererWorkerVolume *wvolume,
			    gpointer data)
{
	MafwGstRendererWorker *worker = data;
	gdouble volume;
	gboolean mute;

	worker->wvolume = wvolume;

	g_debug("volume manager initialized");

	volume = mafw_gst_renderer_worker_volume_get(wvolume);
	mute = mafw_gst_renderer_worker_volume_is_muted(wvolume);
	_volume_cb(wvolume, volume, worker);
	_mute_cb(wvolume, mute, worker);
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
	worker->autopaint = TRUE;
	worker->colorkey = -1;
	worker->vsink = NULL;
	worker->asink = NULL;
	worker->tag_list = NULL;
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
					     _mute_cb, worker);
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
