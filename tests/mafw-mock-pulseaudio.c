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

#include <glib.h>
#include "mafw-mock-pulseaudio.h"

typedef void pa_glib_mainloop;
typedef void pa_mainloop_api;
typedef void (*pa_context_notify_cb_t)(pa_context *c, void *userdata);
typedef guint pa_context_flags_t;
typedef void pa_spawn_api;
typedef void pa_operation;
typedef guint32 pa_volume_t;
typedef struct {
	guint8 channels;
	guint map[32];
} pa_channel_map;
typedef struct {
	guint8 channels;
	pa_volume_t values[32];
} pa_cvolume;
typedef struct {
	const gchar *name;
	pa_channel_map channel_map;
	pa_cvolume volume;
	const char *device;
	gint mute;
	gboolean volume_is_absolute;
} pa_ext_stream_restore_info;
typedef void (*pa_ext_stream_restore_read_cb_t)(
	pa_context *c,
	const pa_ext_stream_restore_info *info, int eol, void *userdata);
typedef void (*pa_ext_stream_restore_subscribe_cb_t)(pa_context *c,
						     void *userdata);
typedef void (*pa_context_success_cb_t)(pa_context *c, int success,
					void *userdata);
enum pa_context_state {
  PA_CONTEXT_UNCONNECTED = 0,
  PA_CONTEXT_CONNECTING,
  PA_CONTEXT_AUTHORIZING,
  PA_CONTEXT_SETTING_NAME,
  PA_CONTEXT_READY,
  PA_CONTEXT_FAILED,
  PA_CONTEXT_TERMINATED
};
struct _pa_context {
	pa_context_notify_cb_t state_cb;
	gpointer state_cb_userdata;
	enum pa_context_state state;
	pa_ext_stream_restore_read_cb_t read_cb;
	gpointer read_cb_userdata;
	pa_ext_stream_restore_subscribe_cb_t subscribe_cb;
	gpointer subscribe_cb_userdata;
	pa_cvolume volume;
	gboolean mute;
};

static pa_context *context = NULL;

pa_glib_mainloop *pa_glib_mainloop_new(GMainContext *c);
char *pa_get_binary_name(char *s, size_t l);
pa_mainloop_api *pa_glib_mainloop_get_api(pa_glib_mainloop *g);
pa_context *pa_context_new(pa_mainloop_api *mainloop, const char *name);
void pa_context_set_state_callback(pa_context *c, pa_context_notify_cb_t cb,
				   void *userdata);
int pa_context_connect(pa_context *c, const char *server,
		       pa_context_flags_t flags, const pa_spawn_api *api);
gint pa_context_get_state(pa_context *c);
pa_operation *pa_ext_stream_restore2_read(pa_context *c,
					  pa_ext_stream_restore_read_cb_t cb,
					  void *userdata);
void pa_operation_unref(pa_operation *o);
pa_volume_t pa_cvolume_max(const pa_cvolume *volume);
void pa_ext_stream_restore_set_subscribe_cb(
	pa_context *c,
	pa_ext_stream_restore_subscribe_cb_t cb, void *userdata);
gint pa_operation_get_state(pa_operation *o);
void pa_operation_cancel(pa_operation *o);
void pa_glib_mainloop_free(pa_glib_mainloop *g);
pa_cvolume *pa_cvolume_init(pa_cvolume *a);
pa_cvolume *pa_cvolume_set(pa_cvolume *a, unsigned channels, pa_volume_t v);
pa_operation *pa_ext_stream_restore2_write(
	pa_context *c, gint mode, const pa_ext_stream_restore_info *data[],
	unsigned n, int apply_immediately, pa_context_success_cb_t cb,
	void *userdata);
pa_operation *pa_ext_stream_restore_subscribe(pa_context *c, int enable,
					      pa_context_success_cb_t cb,
					      void *userdata);
void pa_context_unref(pa_context *c);

pa_glib_mainloop *pa_glib_mainloop_new(GMainContext *c)
{
	return (gpointer) 0x1;
}

char *pa_get_binary_name(char *s, size_t l)
{
	g_strlcpy(s, "mafw-gst-renderer-tests", l);

	return NULL;
}

pa_mainloop_api *pa_glib_mainloop_get_api(pa_glib_mainloop *g)
{
	return (gpointer) 0x1;
}

pa_context *pa_context_new(pa_mainloop_api *mainloop, const char *name)
{
	pa_context *c = g_new0(pa_context, 1);

	pa_cvolume_set(&c->volume, 1, 32000);

	context = c;

	return c;
}

void pa_context_set_state_callback(pa_context *c, pa_context_notify_cb_t cb,
				   void *userdata)
{
	c->state_cb = cb;
	c->state_cb_userdata = userdata;
}

static gboolean _pa_context_connect_idle(gpointer userdata)
{
	pa_context *c = userdata;
	c->state++;
	if (c->state_cb != NULL) {
		c->state_cb(c, c->state_cb_userdata);
	}
	return c->state != PA_CONTEXT_READY;
}

int pa_context_connect(pa_context *c, const char *server,
		       pa_context_flags_t flags, const pa_spawn_api *api)
{
	g_idle_add(_pa_context_connect_idle, c);
	return 1;
}

gint pa_context_get_state(pa_context *c)
{
	return c->state;
}

static gboolean _pa_ext_stream_restore2_read_idle(gpointer userdata)
{
	pa_context *c = userdata;
	pa_ext_stream_restore_info info = { 0, };

	info.name = "sink-input-by-media-role:x-maemo";
	pa_cvolume_set(&info.volume, 1, c->volume.values[0]);
	info.mute = c->mute;

	c->read_cb(c, &info, 1, c->read_cb_userdata);

	return FALSE;
}

pa_operation *pa_ext_stream_restore2_read(pa_context *c,
					  pa_ext_stream_restore_read_cb_t cb,
					  void *userdata)
{
	c->read_cb = cb;
	c->read_cb_userdata = userdata;
	g_idle_add(_pa_ext_stream_restore2_read_idle, c);
	return (gpointer) 0x1;
}

void pa_operation_unref(pa_operation *o)
{
}

pa_volume_t pa_cvolume_max(const pa_cvolume *volume)
{
	return volume->values[0];
}

pa_operation *pa_ext_stream_restore_subscribe(pa_context *c, int enable,
					      pa_context_success_cb_t cb,
					      void *userdata)
{
	if (cb != NULL) {
		cb(c, TRUE, userdata);
	}
	return (gpointer) 0x1;
}

void pa_ext_stream_restore_set_subscribe_cb(
	pa_context *c,
	pa_ext_stream_restore_subscribe_cb_t cb, void *userdata)
{
	c->subscribe_cb = cb;
	c->subscribe_cb_userdata = userdata;
}

gint pa_operation_get_state(pa_operation *o)
{
	return 1;
}

void pa_operation_cancel(pa_operation *o)
{
}

void pa_context_unref(pa_context *c)
{
	g_free(c);
}

void pa_glib_mainloop_free(pa_glib_mainloop *g)
{
}

pa_cvolume *pa_cvolume_init(pa_cvolume *a)
{
	pa_cvolume_set(a, 1, 0);
	return a;
}

pa_cvolume *pa_cvolume_set(pa_cvolume *a, unsigned channels, pa_volume_t v)
{
	a->channels = 1;
	a->values[0] = v;
	return a;
}

static gboolean _pa_ext_stream_restore_write_idle(gpointer userdata)
{
	pa_context *c = userdata;

	if (c->subscribe_cb != NULL) {
		c->subscribe_cb(c, c->subscribe_cb_userdata);
	}

	return FALSE;
}

pa_operation *pa_ext_stream_restore2_write(
	pa_context *c, gint mode, const pa_ext_stream_restore_info *data[],
	unsigned n, int apply_immediately, pa_context_success_cb_t cb,
	void *userdata)
{
	const pa_ext_stream_restore_info *info = data[0];

	pa_cvolume_set(&c->volume, 1, info->volume.values[0]);
	c->mute = info->mute;

	g_idle_add(_pa_ext_stream_restore_write_idle, c);

	return (gpointer) 0x1;
}

static gboolean _pa_context_disconnect_idle(gpointer userdata)
{
	pa_context *c = userdata;
	c->state = PA_CONTEXT_TERMINATED;
	if (c->state_cb != NULL) {
		c->state_cb(c, c->state_cb_userdata);
	}
	return FALSE;
}

void pa_context_disconnect(pa_context *c)
{
	g_idle_add(_pa_context_disconnect_idle, c);
}

pa_context *pa_context_get_instance(void)
{
	return context;
}
