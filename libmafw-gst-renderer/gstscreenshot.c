/* Small helper element for format conversion
 * (c) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * Portion Copyright © 2009 Nokia Corporation and/or its
 * subsidiary(-ies).* All rights reserved. *
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>

#include "gstscreenshot.h"

typedef struct {
	GstSample *result;
	GstElement *src;
	GstElement *sink;
	GstElement *pipeline;
	BvwFrameConvCb cb;
	gpointer cb_data;
} GstScreenshotData;

/* GST_DEBUG_CATEGORY_EXTERN (_totem_gst_debug_cat); */
/* #define GST_CAT_DEFAULT _totem_gst_debug_cat */

static void feed_fakesrc(GstElement *src, GstBuffer *buf, GstPad *pad,
			 gpointer data)
{
	GstSample *sample = GST_SAMPLE(data);
	GstBuffer *in_buf = gst_sample_get_buffer(sample);

	g_assert(gst_buffer_is_writable(buf));

	gst_buffer_remove_all_memory(buf);

	g_assert(gst_buffer_copy_into(buf, in_buf,
				      GST_BUFFER_COPY_MEMORY, 0, -1));

	GST_DEBUG("feeding buffer %p, size %" G_GSIZE_FORMAT ", caps %" GST_PTR_FORMAT,
		  buf, gst_buffer_get_size(buf), gst_sample_get_caps(sample));

	gst_sample_unref(sample);
}

static void save_result(GstElement *sink, GstBuffer *buf, GstPad *pad,
			gpointer data)
{
	GstScreenshotData *gsd = data;

	gsd->result = gst_sample_new(
			      buf, gst_pad_get_current_caps(pad), NULL, NULL);

	GST_DEBUG("received converted buffer %p with caps %" GST_PTR_FORMAT,
		  gsd->result, gst_sample_get_caps(gsd->result));
}

static gboolean create_element(const gchar *factory_name, GstElement **element,
			       GError **err)
{
	*element = gst_element_factory_make(factory_name, NULL);
	if (*element)
		return TRUE;

	if (err && *err == NULL) {
		*err = g_error_new(
			GST_CORE_ERROR, GST_CORE_ERROR_MISSING_PLUGIN,
			"cannot create element '%s' - please check your "
			"GStreamer installation", factory_name);
	}

	return FALSE;
}

static gboolean finalize_process(GstScreenshotData *gsd)
{
	g_signal_handlers_disconnect_matched(gsd->sink, (GSignalMatchType)
					     G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
					     save_result, NULL);
	g_signal_handlers_disconnect_matched(gsd->src, (GSignalMatchType)
					     G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
					     feed_fakesrc, NULL);
	gst_element_set_state(gsd->pipeline, GST_STATE_NULL);

	g_free(gsd);

	return FALSE;
}

static gboolean async_bus_handler(GstBus *bus, GstMessage *msg,
				  gpointer data)
{
	GstScreenshotData *gsd = data;
	gboolean keep_watch = TRUE;

	switch (GST_MESSAGE_TYPE(msg)) {
	case GST_MESSAGE_EOS: {
		if (gsd->result != NULL) {
			GST_DEBUG("conversion successful: result = %p",
				  gsd->result);
		} else {
			GST_WARNING("EOS but no result frame?!");
		}
		gsd->cb(gsd->result, gsd->cb_data);
		keep_watch = finalize_process(gsd);
		break;
	}
	case GST_MESSAGE_ERROR: {
		gchar *dbg = NULL;
		GError *error = NULL;

		gst_message_parse_error(msg, &error, &dbg);
		if (error != NULL) {
			g_warning("Could not take screenshot: %s",
				  error->message);
			GST_DEBUG("%s [debug: %s]", error->message,
				  GST_STR_NULL(dbg));
			g_error_free(error);
		} else {
			g_warning("Could not take screenshot(and "
				  "NULL error!)");
		}
		g_free(dbg);
		gsd->result = NULL;
		gsd->cb(gsd->result, gsd->cb_data);
		keep_watch = finalize_process(gsd);
		break;
	}
	default:
		break;
	}

	return keep_watch;
}

/* takes ownership of the input sample */
gboolean bvw_frame_conv_convert(GstSample *sample, GstCaps *to_caps,
				BvwFrameConvCb cb, gpointer cb_data)
{
	static GstElement *src = NULL, *sink = NULL, *pipeline = NULL,
		*filter1 = NULL, *filter2 = NULL, *download = NULL;
	static GstBus *bus;
	GError *error = NULL;
	GstScreenshotData *gsd;

	g_return_val_if_fail(gst_sample_get_caps(sample) != NULL, FALSE);
	g_return_val_if_fail(cb != NULL, FALSE);

	if (pipeline == NULL) {
		GstElement *csp, *vscale;

		pipeline = gst_pipeline_new("screenshot-pipeline");
		if(pipeline == NULL) {
			g_warning("Could not take screenshot: "
				  "no pipeline (unknown error)");
			return FALSE;
		}

		/* videoscale is here to correct for the
		 * pixel-aspect-ratio for us */
		GST_DEBUG("creating elements");
		if(!create_element("fakesrc", &src, &error) ||
		   !create_element("videoconvert", &csp, &error) ||
		   !create_element("videoscale", &vscale, &error) ||
		   !create_element("capsfilter", &filter1, &error) ||
		   !create_element("capsfilter", &filter2, &error) ||
		   !create_element("gldownload",  &download, &error) ||
		   !create_element("fakesink", &sink, &error)) {
			g_warning("Could not take screenshot: %s",
				  error->message);
			g_error_free(error);
			return FALSE;
		}

		GST_DEBUG("adding elements");
		gst_bin_add_many(GST_BIN(pipeline), src, filter1, csp, filter2,
				 vscale, download, sink, NULL);

		g_object_set(sink, "signal-handoffs", TRUE, NULL);

		/* set to 'fixed' sizetype */
		g_object_set(src, "sizetype", 2, "num-buffers", 1,
			     "signal-handoffs", TRUE, NULL);

		GST_DEBUG("linking src->filter1");
		if(!gst_element_link_pads(src, "src", filter1, "sink"))
			return FALSE;

		GST_DEBUG("linking filter1->download");
		if(!gst_element_link_pads(filter1, "src", download, "sink"))
			return FALSE;

		GST_DEBUG("linking download->csp");
		if(!gst_element_link_pads(download, "src", csp, "sink"))
			return FALSE;

		GST_DEBUG("linking csp->vscale");
		if(!gst_element_link_pads(csp, "src", vscale, "sink"))
			return FALSE;

		GST_DEBUG("linking vscale->capsfilter");
		if(!gst_element_link_pads(vscale, "src", filter2, "sink"))
			return FALSE;

		GST_DEBUG("linking capsfilter->sink");
		if(!gst_element_link_pads(filter2, "src", sink, "sink"))
			return FALSE;

		bus = gst_element_get_bus(pipeline);
	}

	g_object_set(filter1, "caps", gst_sample_get_caps(sample), NULL);

	g_object_set(filter2, "caps", to_caps, NULL);
	gst_caps_unref(to_caps);

	gsd = g_new0(GstScreenshotData, 1);

	gsd->src = src;
	gsd->sink = sink;
	gsd->pipeline = pipeline;
	gsd->cb = cb;
	gsd->cb_data = cb_data;

	g_signal_connect(sink, "handoff", G_CALLBACK(save_result), gsd);

	g_signal_connect(src, "handoff", G_CALLBACK(feed_fakesrc), sample);

	gst_bus_add_watch(bus, async_bus_handler, gsd);

	/* set to 'fixed' sizetype */
	g_object_set(src, "sizemax",
		     gst_buffer_get_size(gst_sample_get_buffer(sample)), NULL);

	GST_DEBUG("running conversion pipeline");
	gst_element_set_state(pipeline, GST_STATE_PLAYING);

	return TRUE;
}
