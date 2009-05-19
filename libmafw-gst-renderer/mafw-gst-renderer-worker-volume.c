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

#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>
#include <pulse/ext-stream-restore.h>
#include <string.h>

#include "mafw-gst-renderer-worker-volume.h"

#undef  G_LOG_DOMAIN
#define G_LOG_DOMAIN "mafw-gst-renderer-worker-volume"

#define MAFW_GST_RENDERER_WORKER_VOLUME_SERVER NULL

#define MAFW_GST_RENDERER_WORKER_VOLUME_ROLE_PREFIX "sink-input-by-media-role:"
#define MAFW_GST_RENDERER_WORKER_VOLUME_ROLE "x-maemo"


struct _MafwGstRendererWorkerVolume {
	pa_glib_mainloop *mainloop;
	pa_context *context;
	gdouble volume;
	gboolean mute;
	MafwGstRendererWorkerVolumeChangedCb cb;
	gpointer user_data;
	MafwGstRendererWorkerVolumeMuteCb mute_cb;
	gpointer mute_user_data;
};

typedef struct {
	MafwGstRendererWorkerVolume *wvolume;
	MafwGstRendererWorkerVolumeInitCb cb;
	gpointer user_data;
} InitCbClosure;

#define _pa_volume_to_per_one(volume) \
	((gdouble)(volume) / (gdouble) PA_VOLUME_NORM)
#define _pa_volume_from_per_one(volume) \
	((pa_volume_t)((gdouble)(volume) * (gdouble) PA_VOLUME_NORM))

static gchar *_get_client_name(void) {
	gchar buf[PATH_MAX];
	gchar *name = NULL;

	if (pa_get_binary_name(buf, sizeof(buf)))
		name = g_strdup_printf("mafw-gst-renderer[%s]", buf);
	else
		name = g_strdup("mafw-gst-renderer");

	return name;
}

static void _ext_stream_restore_read_cb(pa_context *c,
					const pa_ext_stream_restore2_info *i,
					int eol,
					void *userdata)
{
	MafwGstRendererWorkerVolume *wvolume = userdata;
	gdouble volume;
	gboolean mute;
	gboolean signal_volume = FALSE, signal_mute = FALSE;

	g_assert(eol >= 0);

	if (i == NULL ||
	    strcmp(i->name, MAFW_GST_RENDERER_WORKER_VOLUME_ROLE_PREFIX
		   MAFW_GST_RENDERER_WORKER_VOLUME_ROLE) != 0)
		return;

	volume = _pa_volume_to_per_one(pa_cvolume_max(&i->volume));
	mute = i->mute != 0 ? TRUE : FALSE;

	signal_volume = volume != wvolume->volume;
	signal_mute = mute != wvolume->mute;

	wvolume->volume = volume;
	wvolume->mute = mute;

	/* EMIT VOLUME */
	g_debug("ext stream volume is %lf (mute: %d) for role %s in device %s",
		wvolume->volume, i->mute, i->name, i->device);
	if (signal_volume && wvolume->cb != NULL) {
		g_debug("signalling volume");
		wvolume->cb(wvolume, volume, wvolume->user_data);
	}
	if (signal_mute && wvolume->mute_cb != NULL) {
		g_debug("signalling mute");
		wvolume->mute_cb(wvolume, mute, wvolume->mute_user_data);
	}
}

static void
_state_cb(pa_context *c, void *data)
{
	MafwGstRendererWorkerVolume *wvolume = data;
	pa_context_state_t state;

	state = pa_context_get_state(c);

	switch (state) {
	case PA_CONTEXT_TERMINATED:
	case PA_CONTEXT_FAILED:
		g_error("Unexpected problem in volume management");
		break;
	case PA_CONTEXT_READY: {
		pa_operation *o;

		o = pa_ext_stream_restore2_read(c, _ext_stream_restore_read_cb,
					       wvolume);
		g_assert(o != NULL);
		pa_operation_unref(o);

		break;
	}
	default:
		break;
	}
}

static void _ext_stream_restore_read_cb_init(pa_context *c,
					     const pa_ext_stream_restore2_info *i,
					     int eol,
					     void *userdata)
{
	InitCbClosure *closure = userdata;

	g_assert(eol >= 0);

	if (i == NULL ||
	    strcmp(i->name, MAFW_GST_RENDERER_WORKER_VOLUME_ROLE_PREFIX
		   MAFW_GST_RENDERER_WORKER_VOLUME_ROLE) != 0)
		return;

	closure->wvolume->volume =
		_pa_volume_to_per_one(pa_cvolume_max(&i->volume));
	closure->wvolume->mute = i->mute != 0 ? TRUE : FALSE;

	/* NOT EMIT VOLUME, BUT DEBUG */
	g_debug("ext stream volume is %lf (mute: %d) for role %s in device %s",
		closure->wvolume->volume, i->mute, i->name, i->device);

	closure->cb(closure->wvolume, closure->user_data);

	pa_context_set_state_callback(closure->wvolume->context, _state_cb,
				      closure->wvolume);

	g_free(closure);
}

static void _ext_stream_restore_subscribe_cb(pa_context *c, void *userdata)
{
    pa_operation *o;

    o = pa_ext_stream_restore2_read(c, _ext_stream_restore_read_cb, userdata);
    g_assert(o != NULL);
    pa_operation_unref(o);
}

static void
_state_cb_init(pa_context *c, void *data)
{
	InitCbClosure *closure = data;
	MafwGstRendererWorkerVolume *wvolume = closure->wvolume;
	pa_context_state_t state;

	state = pa_context_get_state(c);

	g_debug("state: %d", state);

	switch (state) {
	case PA_CONTEXT_TERMINATED:
	case PA_CONTEXT_FAILED:
		g_error("Unexpected problem in volume management");
		break;
	case PA_CONTEXT_READY: {
		pa_operation *o;

		g_debug("PA_CONTEXT_READY");

		o = pa_ext_stream_restore2_read(c,
					       _ext_stream_restore_read_cb_init,
					       closure);
		g_assert(o != NULL);
		pa_operation_unref(o);

		pa_ext_stream_restore_set_subscribe_cb(
			c, _ext_stream_restore_subscribe_cb, wvolume);

		o = pa_ext_stream_restore_subscribe(c, 1, NULL, NULL);
		g_assert(o != NULL);
		pa_operation_unref(o);

		break;
	}
	default:
		break;
	}
}

static gboolean _destroy_idle(gpointer data)
{
	MafwGstRendererWorkerVolume *wvolume = data;

	g_debug("destroying");

	pa_context_unref(wvolume->context);
	pa_glib_mainloop_free(wvolume->mainloop);
	g_free(wvolume);

	return FALSE;
}

static void
_state_cb_destroy(pa_context *c, void *data)
{
	pa_context_state_t state;

	state = pa_context_get_state(c);

	switch (state) {
	case PA_CONTEXT_TERMINATED:
		g_idle_add(_destroy_idle, data);
		break;
	case PA_CONTEXT_FAILED:
		g_error("Unexpected problem in volume management");
		break;
	default:
		break;
	}
}

static void _success_cb(pa_context *c, int success, void *userdata)
{
	g_assert(success != 0);
}

void mafw_gst_renderer_worker_volume_init(GMainContext *main_context,
					  MafwGstRendererWorkerVolumeInitCb cb,
					  gpointer user_data,
					  MafwGstRendererWorkerVolumeChangedCb
					  changed_cb,
					  gpointer changed_user_data,
					  MafwGstRendererWorkerVolumeMuteCb
					  mute_cb, gpointer mute_user_data)
{
	MafwGstRendererWorkerVolume *wvolume = NULL;
	gchar *name = NULL;
	pa_mainloop_api *api = NULL;
	InitCbClosure *closure;

	g_return_if_fail(cb != NULL);

	g_debug("initializing volume manager");

	wvolume = g_new0(MafwGstRendererWorkerVolume, 1);

	wvolume->volume = 1.0;
	wvolume->mute = FALSE;
	wvolume->cb = changed_cb;
	wvolume->user_data = changed_user_data;
	wvolume->mute_cb = mute_cb;
	wvolume->mute_user_data = mute_user_data;

	wvolume->mainloop = pa_glib_mainloop_new(main_context);
	g_assert(wvolume->mainloop != NULL);

	name = _get_client_name();

	/* get the mainloop api and create a context */
	api = pa_glib_mainloop_get_api(wvolume->mainloop);
	wvolume->context = pa_context_new(api, name);
	g_assert(wvolume->context != NULL);

	closure = g_new(InitCbClosure, 1);
	closure->wvolume = wvolume;
	closure->cb = cb;
	closure->user_data = user_data;

	/* register some essential callbacks */
	pa_context_set_state_callback(wvolume->context, _state_cb_init,
				      closure);

	g_debug("connecting to pulse");

	g_assert(pa_context_connect(wvolume->context,
				    MAFW_GST_RENDERER_WORKER_VOLUME_SERVER,
				    PA_CONTEXT_NOAUTOSPAWN | PA_CONTEXT_NOFAIL,
				    NULL) >= 0);
	g_free(name);
}

void mafw_gst_renderer_worker_volume_set(MafwGstRendererWorkerVolume *wvolume,
					 gdouble volume, gboolean mute)
{
	pa_ext_stream_restore2_info info;
        pa_ext_stream_restore2_info *infos[1];

	info.name = MAFW_GST_RENDERER_WORKER_VOLUME_ROLE_PREFIX
		MAFW_GST_RENDERER_WORKER_VOLUME_ROLE;
	info.channel_map.channels = 1;
	info.channel_map.map[0] = PA_CHANNEL_POSITION_MONO;
	info.device = NULL;
	info.mute = mute;
	info.volume_is_absolute = TRUE;
        infos[0] = &info;

	pa_cvolume_init(&info.volume);
	pa_cvolume_set(&info.volume, info.channel_map.channels,
		       _pa_volume_from_per_one(volume));

	g_debug("setting volume to %lf", volume);

	pa_ext_stream_restore2_write(wvolume->context, PA_UPDATE_REPLACE,
				     (const pa_ext_stream_restore2_info *
				      const *)infos,
                                     1, TRUE, _success_cb, NULL);
}

gdouble mafw_gst_renderer_worker_volume_get(
	MafwGstRendererWorkerVolume *wvolume)
{
	g_debug("getting volume; %lf", wvolume->volume);

	return wvolume->volume;
}

gboolean mafw_gst_renderer_worker_volume_is_muted(
	MafwGstRendererWorkerVolume *wvolume)
{
	g_debug("getting mute; %d", wvolume->mute);

	return wvolume->mute;
}

void mafw_gst_renderer_worker_volume_destroy(
	MafwGstRendererWorkerVolume *wvolume)
{
	g_debug("disconnecting");

	pa_ext_stream_restore_set_subscribe_cb(wvolume->context, NULL, NULL);
	pa_context_set_state_callback(wvolume->context, _state_cb_destroy,
				      wvolume);
	pa_context_disconnect(wvolume->context);
}
