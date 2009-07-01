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
#include "config.h"

#undef  G_LOG_DOMAIN
#define G_LOG_DOMAIN "mafw-gst-renderer-worker-volume"

#define MAFW_GST_RENDERER_WORKER_VOLUME_SERVER NULL

#define MAFW_GST_RENDERER_WORKER_VOLUME_ROLE_PROPERTY "PULSE_PROP_media.role"
#define MAFW_GST_RENDERER_WORKER_VOLUME_ROLE_PREFIX "sink-input-by-media-role:"
#define MAFW_GST_RENDERER_WORKER_VOLUME_ROLE "x-maemo"

#define MAFW_GST_RENDERER_WORKER_SET_TIMEOUT 200


struct _MafwGstRendererWorkerVolume {
	pa_glib_mainloop *mainloop;
	pa_context *context;
	gdouble pulse_volume;
	gboolean pulse_mute;
	MafwGstRendererWorkerVolumeChangedCb cb;
	gpointer user_data;
	MafwGstRendererWorkerVolumeMuteCb mute_cb;
	gpointer mute_user_data;
	gdouble current_volume;
	gboolean current_mute;
	guint change_request_id;
	pa_operation *pa_operation;
};

typedef struct {
	MafwGstRendererWorkerVolume *wvolume;
	MafwGstRendererWorkerVolumeInitCb cb;
	gpointer user_data;
} InitCbClosure;

#define _pa_volume_to_per_one(volume) \
	((guint) ((((gdouble)(volume) / (gdouble) PA_VOLUME_NORM) + \
		   (gdouble) 0.005) * (gdouble) 100.0) / (gdouble) 100.0)
#define _pa_volume_from_per_one(volume) \
	((pa_volume_t)((gdouble)(volume) * (gdouble) PA_VOLUME_NORM))

#define _pa_operation_running(wvolume) \
	(wvolume->pa_operation != NULL && \
	 pa_operation_get_state(wvolume->pa_operation) == PA_OPERATION_RUNNING)

static void _state_cb_init(pa_context *c, void *data);


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

	if (eol < 0) {
		g_critical("eol parameter should not be < 1. "
			   "Discarding volume event");
		return;
	}

	if (_pa_operation_running(wvolume)) {
		g_debug("volume notification, but operation running, ignoring");
		return;
	}

	if (i == NULL ||
	    strcmp(i->name, MAFW_GST_RENDERER_WORKER_VOLUME_ROLE_PREFIX
		   MAFW_GST_RENDERER_WORKER_VOLUME_ROLE) != 0)
		return;

	wvolume->pulse_volume =
		_pa_volume_to_per_one(pa_cvolume_max(&i->volume));
	wvolume->pulse_mute = i->mute != 0 ? TRUE : FALSE;

	/* EMIT VOLUME */
	g_debug("ext stream volume is %lf (mute: %d) for role %s in device %s",
		wvolume->pulse_volume, wvolume->pulse_mute, i->name, i->device);
	if (wvolume->pulse_volume != wvolume->current_volume) {
		wvolume->current_volume = wvolume->pulse_volume;
		if (wvolume->cb != NULL) {
			g_debug("signalling volume");
			wvolume->cb(wvolume, wvolume->current_volume,
				    wvolume->user_data);
		}
	}
	if (wvolume->pulse_mute != wvolume->current_mute) {
		wvolume->current_mute = wvolume->pulse_mute;
		if (wvolume->mute_cb != NULL) {
			g_debug("signalling mute");
			wvolume->mute_cb(wvolume, wvolume->current_mute,
					 wvolume->mute_user_data);
		}
	}
}

static void _destroy_context(MafwGstRendererWorkerVolume *wvolume)
{
	if (wvolume->pa_operation != NULL) {
		if (pa_operation_get_state(wvolume->pa_operation) ==
		    PA_OPERATION_RUNNING) {
			pa_operation_cancel(wvolume->pa_operation);
		}
		pa_operation_unref(wvolume->pa_operation);
		wvolume->pa_operation = NULL;
	}
	pa_context_unref(wvolume->context);
}

static InitCbClosure *_init_cb_closure_new(MafwGstRendererWorkerVolume *wvolume,
					   MafwGstRendererWorkerVolumeInitCb cb,
					   gpointer user_data)
{
	InitCbClosure *closure;

	closure = g_new(InitCbClosure, 1);
	closure->wvolume = wvolume;
	closure->cb = cb;
	closure->user_data = user_data;

	return closure;
}

static void _connect(MafwGstRendererWorkerVolume *wvolume,
		     MafwGstRendererWorkerVolumeInitCb cb,
		     gpointer user_data)
{
	gchar *name = NULL;
	pa_mainloop_api *api = NULL;
	InitCbClosure *closure;

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

static gboolean _reconnect(gpointer user_data)
{
	InitCbClosure *closure = user_data;
	MafwGstRendererWorkerVolume *wvolume = closure->wvolume;

	g_warning("got disconnected from pulse, reconnecting");
	_destroy_context(wvolume);
	_connect(wvolume, closure->cb, closure->user_data);
	g_free(closure);

	return FALSE;
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
	{
		InitCbClosure *closure;

		closure = _init_cb_closure_new(wvolume, NULL, NULL);
		g_idle_add(_reconnect, closure);
		break;
	}
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

	if (eol < 0) {
		g_critical("eol parameter should not be < 1");
	}

	if (i == NULL ||
	    strcmp(i->name, MAFW_GST_RENDERER_WORKER_VOLUME_ROLE_PREFIX
		   MAFW_GST_RENDERER_WORKER_VOLUME_ROLE) != 0)
		return;

	closure->wvolume->pulse_volume =
		_pa_volume_to_per_one(pa_cvolume_max(&i->volume));
	closure->wvolume->pulse_mute = i->mute != 0 ? TRUE : FALSE;
	closure->wvolume->current_volume = closure->wvolume->pulse_volume;
	closure->wvolume->current_mute = closure->wvolume->pulse_mute;

	/* NOT EMIT VOLUME, BUT DEBUG */
	g_debug("ext stream volume is %lf (mute: %d) for role %s in device %s",
		closure->wvolume->pulse_volume, i->mute, i->name, i->device);

	if (closure->cb != NULL) {
		closure->cb(closure->wvolume, closure->user_data);
	} else {
		if (closure->wvolume->cb != NULL) {
			g_debug("signalling volume after reconnection");
			closure->wvolume->cb(closure->wvolume,
					     closure->wvolume->current_volume,
					     closure->wvolume->user_data);
		}
		if (closure->wvolume->mute_cb != NULL) {
			g_debug("signalling mute after reconnection");
			closure->wvolume->mute_cb(closure->wvolume,
						  closure->wvolume->
						  current_mute,
						  closure->wvolume->
						  mute_user_data);
		}
	}

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
		g_critical("Connection to pulse failed, reconnection in 1 "
			   "second");
		g_timeout_add_seconds(1, _reconnect, closure);
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

	_destroy_context(wvolume);
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

static void _remove_set_timeout(MafwGstRendererWorkerVolume *wvolume)
{
	if (wvolume->change_request_id != 0) {
		g_source_remove(wvolume->change_request_id);
	}
	wvolume->change_request_id = 0;
}

static gboolean _set_timeout(gpointer data)
{
	pa_ext_stream_restore2_info info;
        pa_ext_stream_restore2_info *infos[1];
	MafwGstRendererWorkerVolume *wvolume = data;

	if (wvolume->pulse_mute != wvolume->current_mute ||
	    wvolume->pulse_volume != wvolume->current_volume) {
		info.name = MAFW_GST_RENDERER_WORKER_VOLUME_ROLE_PREFIX
			MAFW_GST_RENDERER_WORKER_VOLUME_ROLE;
		info.channel_map.channels = 1;
		info.channel_map.map[0] = PA_CHANNEL_POSITION_MONO;
		info.device = NULL;
		info.volume_is_absolute = TRUE;
		infos[0] = &info;

		info.mute = wvolume->current_mute;
		pa_cvolume_init(&info.volume);
		pa_cvolume_set(&info.volume, info.channel_map.channels,
			       _pa_volume_from_per_one(wvolume->
						       current_volume));

		g_debug("setting volume to %lf and mute to %d",
			wvolume->current_volume, wvolume->current_mute);

		if (wvolume->pa_operation != NULL) {
			pa_operation_unref(wvolume->pa_operation);
		}

		wvolume->pa_operation = pa_ext_stream_restore2_write(
			wvolume->context,
			PA_UPDATE_REPLACE,
			(const pa_ext_stream_restore2_info*
			 const *)infos,
			1, TRUE, _success_cb, wvolume);

		if (wvolume->pa_operation == NULL) {
			g_critical("NULL operation when writing volume to "
				   "pulse");
			_remove_set_timeout(wvolume);
		}
	} else {
		g_debug("removing volume timeout");
		_remove_set_timeout(wvolume);
	}

	return wvolume->change_request_id != 0;
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

	g_return_if_fail(cb != NULL);

	g_assert(g_setenv(MAFW_GST_RENDERER_WORKER_VOLUME_ROLE_PROPERTY,
			  MAFW_GST_RENDERER_WORKER_VOLUME_ROLE, FALSE));

	g_debug("initializing volume manager");

	wvolume = g_new0(MafwGstRendererWorkerVolume, 1);

	wvolume->pulse_volume = 1.0;
	wvolume->pulse_mute = FALSE;
	wvolume->cb = changed_cb;
	wvolume->user_data = changed_user_data;
	wvolume->mute_cb = mute_cb;
	wvolume->mute_user_data = mute_user_data;

	wvolume->mainloop = pa_glib_mainloop_new(main_context);
	g_assert(wvolume->mainloop != NULL);

	_connect(wvolume, cb, user_data);
}

void mafw_gst_renderer_worker_volume_set(MafwGstRendererWorkerVolume *wvolume,
					 gdouble volume, gboolean mute)
{
	gboolean signal_volume, signal_mute;

	g_return_if_fail(wvolume != NULL);
	g_return_if_fail(pa_context_get_state(wvolume->context) ==
			 PA_CONTEXT_READY);

#ifndef MAFW_GST_RENDERER_ENABLE_MUTE
	mute = FALSE;
#endif

	signal_volume = wvolume->current_volume != volume &&
		wvolume->cb != NULL;
	signal_mute = wvolume->current_mute != mute && wvolume->mute_cb != NULL;

	wvolume->current_volume = volume;
	wvolume->current_mute = mute;

	g_debug("volume set: %lf (mute %d)", volume, mute);

	if (signal_volume) {
		g_debug("signalling volume");
		wvolume->cb(wvolume, volume, wvolume->user_data);
	}

	if (signal_mute) {
		g_debug("signalling mute");
		wvolume->mute_cb(wvolume, mute, wvolume->mute_user_data);
	}

	if ((signal_mute || signal_volume) && wvolume->change_request_id == 0) {
		wvolume->change_request_id =
			g_timeout_add(MAFW_GST_RENDERER_WORKER_SET_TIMEOUT,
				      _set_timeout, wvolume);

		_set_timeout(wvolume);
	}
}

gdouble mafw_gst_renderer_worker_volume_get(
	MafwGstRendererWorkerVolume *wvolume)
{
	g_return_val_if_fail(wvolume != NULL, 0.0);

	g_debug("getting volume; %lf", wvolume->current_volume);

	return wvolume->current_volume;
}

gboolean mafw_gst_renderer_worker_volume_is_muted(
	MafwGstRendererWorkerVolume *wvolume)
{
	g_return_val_if_fail(wvolume != NULL, FALSE);

	g_debug("getting mute; %d", wvolume->current_mute);

	return wvolume->current_mute;
}

void mafw_gst_renderer_worker_volume_destroy(
	MafwGstRendererWorkerVolume *wvolume)
{
	g_return_if_fail(wvolume != NULL);

	g_debug("disconnecting");

	pa_ext_stream_restore_set_subscribe_cb(wvolume->context, NULL, NULL);
	pa_context_set_state_callback(wvolume->context, _state_cb_destroy,
				      wvolume);
	pa_context_disconnect(wvolume->context);
}
