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

/*
 * check-gst-renderer.c
 *
 * Gst Renderer unit tests
 *
 * Copyright (C) 2007 Nokia Corporation
 *
 */

#include <glib.h>

#include <check.h>
#include <string.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <gst/tag/tag.h>

#include <libmafw/mafw.h>
#include <checkmore.h>

#include "config.h"

#include "mafw-gst-renderer.h"
#include "mafw-mock-playlist.h"
#include "mafw-mock-pulseaudio.h"

#undef  G_LOG_DOMAIN
#define G_LOG_DOMAIN "check-mafw-gstreamer-renderer"

#define SAMPLE_AUDIO_CLIP "test.wav"
#define SAMPLE_VIDEO_CLIP "test.avi"
#define SAMPLE_IMAGE "testframe.png"

/* Base timeout used when waiting for state transitions or execution of
   user function callbacks associated to each mafw-renderer function */
#define DEFAULT_WAIT_TOUT 2000

/* EOS timeout must be longer than the clip duration */
#define EOS_TIMEOUT 7000

SRunner *configure_tests(void);

typedef struct {
	gint index;
	MafwPlayState state;
} RendererInfo;

typedef struct {
	gboolean called;
	gboolean error;
	gint err_code;
	gchar *err_msg;
	gint seek_position;
	gboolean error_signal_expected;
	GError *error_signal_received;
	const gchar *property_expected;
	GValue *property_received;
} CallbackInfo;		

typedef struct {
	const gchar *expected_key;
	GValue *value;
} MetadataChangedInfo;

typedef struct {
	const gchar *expected;
	GValue *received;
} PropertyChangedInfo;

typedef struct {
	gboolean requested;
	gboolean received;
	gfloat value;
} BufferingInfo;

static gint wait_tout_val;

/* Globals. */

static MafwRenderer *g_gst_renderer = NULL;

/* Error messages. */

static const gchar *callback_err_msg = "Error received when %s: (%d) %s";
static const gchar *callback_no_err_msg = "No error received when %s: (%d) %s";
static const gchar *no_callback_msg = "We forgot to call the user callback";
static const gchar *state_err_msg = "Call %s didn't change state to %s. " \
"Current state is: %d";
static const gchar *index_err_msg = "Actual index is (%d) instead of the " \
"expected index (%d)";


/*----------------------------------------------------------------------------
  Signal handlers
  ----------------------------------------------------------------------------*/
 

static void error_cb(MafwRenderer *s, GQuark domain, gint code, gchar *msg, 
		     gpointer user_data)
{
	CallbackInfo* c = (CallbackInfo*) user_data;

	/* "MafwExtension::error" signal handler */	
	if (user_data == NULL || !c->error_signal_expected) {
		fail("Signal error received: (%d) %s", code, msg);
	} else {
		if (c->error_signal_received != NULL) {
			fail("Error received already initialized");
		} else {
			c->error_signal_received =
				g_error_new_literal(domain, code, msg);
		}
	}
}

static void state_changed_cb(MafwRenderer *s,  MafwPlayState state,
			     gpointer user_data)
{
	/* "MafwRenderer::state-changed" signal handler */
	RendererInfo *si = (RendererInfo *) user_data;
	gchar *states[] = {"Stopped","Playing","Paused","Transitioning"};

	si->state = state;
	g_debug("state changed (%s) ---", states[state]);
}

static gboolean media_changed_called;

static void media_changed_cb(MafwRenderer *s, gint index, gchar *objectid,
			     gpointer user_data)
{
	/* "MafwRenderer::media-changed" signal handler */
	RendererInfo *si = (RendererInfo *) user_data;

	si->index = index;
	g_debug("media changed (%d) ---", index);
	media_changed_called = TRUE;
}
static void playlist_changed_cb (MafwRenderer *self,
                                                        GObject       *playlist,
                                                        gpointer       user_data)
{
	g_debug("playlist changed");
	fail_if(media_changed_called, "At first playlist-changed should be called");
}

static void metadata_changed_cb(MafwRenderer *self, const gchar *key,
				GValueArray *value, gpointer user_data)
{
	MetadataChangedInfo *m = user_data;

	if (m->expected_key != NULL && strcmp(key, m->expected_key) == 0)
	{
		GValue *original;

		if (G_IS_VALUE(value)) {
			original = (GValue *) value;
		} else {
			original = g_value_array_get_nth(value, 0);
		}

		m->value = g_new0(GValue, 1);
		g_value_init(m->value, G_VALUE_TYPE(original));
		g_value_copy(original, m->value);
	}
}

static void property_changed_cb(MafwExtension *extension, const gchar *name,
				const GValue *value, gpointer user_data)
{
	PropertyChangedInfo* p = (PropertyChangedInfo*) user_data;
	gchar *value_string;

	value_string = g_strdup_value_contents(value);

	g_debug("property_changed_cb: %s (%s)", name, value_string);
	g_free(value_string);

	if (p->expected != NULL &&
	    strcmp(p->expected, name) == 0) {
		p->received = g_new0(GValue, 1);
		g_value_init(p->received, G_VALUE_TYPE(value));
		g_value_copy(value, p->received);
	}
}

static void buffering_info_cb(MafwRenderer *self, gfloat status,
			      gpointer user_data)
{
	BufferingInfo *b = user_data;

	if (b->requested) {
		b->received = TRUE;
		b->value = status;
	}
}

/*----------------------------------------------------------------------------
  Function callbacks
  ----------------------------------------------------------------------------*/


static void status_cb(MafwRenderer* renderer, MafwPlaylist* playlist, guint index,
		      MafwPlayState state,
		      const gchar* object_id, 
		      gpointer user_data,
		      const GError *error)
{
	/* MafwRendererStatusCB */
	RendererInfo* s = (RendererInfo*) user_data;
	g_assert(s != NULL);

	if (error != NULL) {
		fail("Error received while trying to get renderer status: (%d) %s",
		     error->code, error->message);
	} 
	s->state = state;

}

static void playback_cb(MafwRenderer* renderer, gpointer user_data, const GError* error)
{
	/* MafwRendererPlaybackCB:
	   
	Called after mafw_renderer_play(), mafw_renderer_play_uri(),
	mafw_renderer_play_object(), mafw_renderer_stop(), mafw_renderer_pause(),
	mafw_renderer_resume(), mafw_renderer_next(),	mafw_renderer_previous() or 
	mafw_renderer_goto_index() has been called. */
	CallbackInfo* c = (CallbackInfo*) user_data;
	g_assert(c != NULL);

	c->called = TRUE;
	if (error != NULL) {
		c->error = TRUE;
		c->err_code = error->code;
		c->err_msg = g_strdup(error->message);
	}
}

static void seek_cb (MafwRenderer *self, gint position, gpointer user_data,
		     const GError *error)
{
	/* Called when seeking */

	CallbackInfo* c = (CallbackInfo*) user_data;
	g_assert(c != NULL);

	c->called = TRUE;
	c->seek_position = position;
	if (error != NULL) {
		c->error = TRUE;
		c->err_code = error->code;
		c->err_msg = g_strdup(error->message);
	}
}

static void  get_position_cb(MafwRenderer *self, gint position,
			     gpointer user_data, const GError *error)
{
	CallbackInfo* c = (CallbackInfo*) user_data;

	g_debug("get position cb: %d", position);

	if (error != NULL) {
		c->error = TRUE;
		c->err_code = error->code;
		c->err_msg = g_strdup(error->message);
	}
	c->called = TRUE;
}

static void get_property_cb(MafwExtension *self,
			    const gchar *name,
			    GValue *value,
			    gpointer user_data,
			    const GError *error)
{
	CallbackInfo* c = (CallbackInfo*) user_data;
	gchar *value_string;

	value_string = g_strdup_value_contents(value);

	g_debug("get property cb: %s (%s)", name, value_string);
	g_free(value_string);

	if (error != NULL) {
		c->error = TRUE;
		c->err_code = error->code;
		c->err_msg = g_strdup(error->message);
	}

	if (c->property_expected != NULL &&
	    strcmp(c->property_expected, name) == 0) {
		c->property_received = g_new0(GValue, 1);
		g_value_init(c->property_received, G_VALUE_TYPE(value));
		g_value_copy(value, c->property_received);

		c->called = TRUE;
	}
}

/*----------------------------------------------------------------------------
  Helpers
  ----------------------------------------------------------------------------*/

static gchar *get_sample_clip_path(const gchar *clip)
{
	gchar *my_dir, *media;

	/* Makefile.am sets TESTS_DIR, required for VPATH builds (like make
	 * distcheck).  Otherwise assume we are running in-place. */
	my_dir = g_strdup(g_getenv("TESTS_DIR"));
	if (!my_dir)
		my_dir = g_get_current_dir();
	media = g_strconcat("file://", my_dir, G_DIR_SEPARATOR_S,
			    "media" G_DIR_SEPARATOR_S, clip,
			    NULL);
	g_free(my_dir);
	return media;
}

static gchar *get_sample_clip_objectid(const gchar *clip)
{
	gchar *path = NULL;
	gchar *objectid = NULL;

	path = get_sample_clip_path(clip);
        objectid = mafw_source_create_objectid(path);
	g_free(path);

	return objectid;
}

static gboolean stop_wait_timeout(gpointer user_data) 
{
	gboolean *do_stop = (gboolean *) user_data;
	g_debug("stop wait timeout");
	*do_stop = TRUE;

	return FALSE;
}

static gboolean wait_until_timeout_finishes(guint millis)
{
	guint timeout = 0;
	gboolean stop_wait = FALSE;
	gboolean result = FALSE;

	g_debug("Init wait_");
	/* We'll wait a limitted ammount of time */
	timeout = g_timeout_add(millis, stop_wait_timeout, &stop_wait);
	while(!stop_wait) {
		result= g_main_context_iteration(NULL, TRUE);
	}

	g_debug("End wait_");
	return TRUE;
}

static gboolean wait_for_state(RendererInfo *renderer_info,
			       MafwPlayState expected_state, guint millis)
{
	guint timeout = 0;
	gboolean stop_wait = FALSE;
	
	g_debug("Init wait for state");
	/* We'll wait a limitted ammount of time */
	timeout = g_timeout_add(millis, stop_wait_timeout, &stop_wait);

	while(renderer_info->state != expected_state  && !stop_wait) {
			g_main_context_iteration(NULL, TRUE);
	}
	
	if (!stop_wait) {
		g_source_remove(timeout);
	}

	g_debug("End wait for state");
	return (renderer_info->state == expected_state);
}

static gboolean wait_for_callback(CallbackInfo *callback, guint millis)
{
	guint timeout = 0;
	gboolean stop_wait = FALSE;

	g_debug("Init wait for callback");
	/* We'll wait a limitted ammount of time */
	timeout = g_timeout_add(millis, stop_wait_timeout, &stop_wait);

	while (callback->called == FALSE && !stop_wait) {
		g_main_context_iteration(NULL, TRUE);
	}
	if (!stop_wait) {
		g_source_remove(timeout);
	}
	g_debug("End wait for callback");
	return callback->called;
}

static gboolean wait_for_metadata(MetadataChangedInfo *callback, guint millis)
{
	guint timeout = 0;
	gboolean stop_wait = FALSE;

	g_debug("Init wait for metadata");
	/* We'll wait a limitted ammount of time */
	timeout = g_timeout_add(millis, stop_wait_timeout, &stop_wait);

	while (callback->value == NULL && !stop_wait) {
		g_main_context_iteration(NULL, TRUE);
	}
	if (!stop_wait) {
		g_source_remove(timeout);
	}
	g_debug("End wait for metadata");
	return callback->value != NULL;
}

static gboolean wait_for_property(PropertyChangedInfo *callback, guint millis)
{
	guint timeout = 0;
	gboolean stop_wait = FALSE;

	g_debug("Init wait for property changed");
	/* We'll wait a limitted ammount of time */
	timeout = g_timeout_add(millis, stop_wait_timeout, &stop_wait);

	while (callback->received == NULL && !stop_wait) {
		g_main_context_iteration(NULL, TRUE);
	}
	if (!stop_wait) {
		g_source_remove(timeout);
	}
	g_debug("End wait for callback");
	return callback->received != NULL;
}

static gboolean wait_for_buffering(BufferingInfo *callback, guint millis)
{
	guint timeout = 0;
	gboolean stop_wait = FALSE;

	g_debug("Init wait for buffering info");
	/* We'll wait a limitted ammount of time */
	timeout = g_timeout_add(millis, stop_wait_timeout, &stop_wait);

	while (!callback->received && !stop_wait) {
		g_main_context_iteration(NULL, TRUE);
	}
	if (!stop_wait) {
		g_source_remove(timeout);
	}
	g_debug("End wait for buffering info");
	return callback->received;
}

static void reset_callback_info(CallbackInfo *callback_info)
{
	if (callback_info->err_msg != NULL) 
		g_free(callback_info->err_msg);

	callback_info->err_msg = NULL;
	callback_info->called = FALSE;
	callback_info->error = FALSE;
	callback_info->seek_position = 0;
	callback_info->error_signal_expected = FALSE;
	if (callback_info->error_signal_received != NULL) {
		g_error_free(callback_info->error_signal_received);
		callback_info->error_signal_received = NULL;
	}
	callback_info->property_expected = NULL;
	if (callback_info->property_received != NULL) {
		g_value_unset(callback_info->property_received);
		callback_info->property_received = NULL;
	}
}

/*----------------------------------------------------------------------------
  Fixtures
  ----------------------------------------------------------------------------*/

static void fx_setup_dummy_gst_renderer(void)
{
	MafwRegistry *registry;

	/* Setup GLib */
	g_type_init();

	/* Create a gst renderer instance */
	registry = MAFW_REGISTRY(mafw_registry_get_instance());
	fail_if(registry == NULL,
		"Error: cannot get MAFW registry");
		
	g_gst_renderer = MAFW_RENDERER(mafw_gst_renderer_new(registry));
	fail_if(!MAFW_IS_GST_RENDERER(g_gst_renderer),
		"Could not create gst renderer instance");
}

static void fx_teardown_dummy_gst_renderer(void)
{
	g_object_unref(g_gst_renderer);
}

/*----------------------------------------------------------------------------
  Mockups
  ----------------------------------------------------------------------------*/

/* GStreamer mock */

GstElement * gst_element_factory_make(const gchar * factoryname,
				      const gchar * name)
{
	GstElementFactory *factory;
	GstElement *element;
        const gchar *use_factoryname;
	
	g_return_val_if_fail(factoryname != NULL, NULL);
	
        /* For testing, use playbin instead of playbin2 */
        if (g_ascii_strcasecmp(factoryname, "playbin2") == 0)
                use_factoryname = "playbin";
        else
                use_factoryname = factoryname;

	GST_LOG("gstelementfactory: make \"%s\" \"%s\"",
		use_factoryname, GST_STR_NULL (name));

	factory = gst_element_factory_find(use_factoryname);
	if (factory == NULL) {
		/* No factory */
		GST_INFO("no such element factory \"%s\"!", use_factoryname);
		return NULL;	
	}

	GST_LOG_OBJECT(factory, "found factory %p", factory);
	if (g_ascii_strcasecmp(use_factoryname, "pulsesink") == 0) {
		element = gst_element_factory_make("fakesink", "pulsesink");
		g_object_set(G_OBJECT(element), "sync", TRUE, NULL);
	} else if (g_ascii_strcasecmp(use_factoryname, "xvimagesink") == 0) {
		element = gst_element_factory_make("fakesink", "xvimagesink");
		g_object_set(G_OBJECT(element), "sync", TRUE, NULL);
	} else {
		element = gst_element_factory_create(factory, name);
	}
	gst_object_unref(factory);

	if (element == NULL) {
		/* Create failed */
		GST_INFO_OBJECT(factory, "couldn't create instance!");
		return NULL;
	}

	GST_LOG("gstelementfactory: make \"%s\" \"%s\"",use_factoryname,
		GST_STR_NULL(name));

	/* Playbin will use fake renderer */
	if (g_ascii_strcasecmp(use_factoryname, "playbin") == 0) {
		GstElement *audiorenderer = gst_element_factory_make("fakesink",
								 "audiorenderer");
		
		g_object_set(G_OBJECT(audiorenderer), "sync", TRUE, NULL);
		g_object_set(G_OBJECT(element),
			     "audio-sink",
			     audiorenderer,
			     NULL);
		g_object_set(G_OBJECT(element),
			     "video-sink",
			     audiorenderer,
			     NULL);
	}
 
	return element;
}


/*----------------------------------------------------------------------------
  Test cases
  ----------------------------------------------------------------------------*/

START_TEST(test_basic_playback)
{
	RendererInfo s;
	CallbackInfo c;     
	MetadataChangedInfo m;
	GstBus *bus = NULL;
	GstMessage *message = NULL;

	/* Initialize callback info */
    	c.err_msg = NULL;
	c.error_signal_expected = FALSE;
	c.error_signal_received = NULL;
	m.expected_key = NULL;
	m.value = NULL;
	c.property_expected = NULL;
	c.property_received = NULL;

	/* Connect to renderer signals */
	g_signal_connect(g_gst_renderer, "error",
			 G_CALLBACK(error_cb),
			 &c);
	g_signal_connect(g_gst_renderer, "state-changed",
			 G_CALLBACK(state_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "media-changed",
			 G_CALLBACK(media_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "metadata-changed",
			 G_CALLBACK(metadata_changed_cb),
			 &m);

	/* --- Get initial status --- */

	reset_callback_info(&c);

	g_debug("get status...");
	mafw_renderer_get_status(g_gst_renderer, status_cb, &s);

	/* --- Play --- */

	reset_callback_info(&c);

	g_debug("play...");
	mafw_renderer_play(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		/* No media item has been set so, we should get an error */
		if (c.error == FALSE)
		       	fail("Play of unset media did not return an error");
	} else {
		fail(no_callback_msg);
	}
	
	/* --- Play object --- */

	reset_callback_info(&c);

	gchar *objectid = get_sample_clip_objectid(SAMPLE_AUDIO_CLIP);
	g_debug("play_object... %s", objectid);
	mafw_renderer_play_object(g_gst_renderer, objectid, playback_cb, &c);


	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
		       	fail(callback_err_msg, "playing an object", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Transitioning",
		     s.state);
	}
        	
	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Playing",
		     s.state);
	}

	g_free(objectid);

	/* --- Get position --- */

	reset_callback_info(&c);

	mafw_renderer_get_position(g_gst_renderer, get_position_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "get_position", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	/* --- Duration emission --- */

	m.expected_key = MAFW_METADATA_KEY_DURATION;

	bus = MAFW_GST_RENDERER(g_gst_renderer)->worker->bus;
	fail_if(bus == NULL, "No GstBus");

	message = gst_message_new_duration(NULL, GST_FORMAT_TIME,
					   5 * GST_SECOND);
	gst_bus_post(bus, message);

	if (wait_for_metadata(&m, wait_tout_val) == FALSE) {
		fail("Expected " MAFW_METADATA_KEY_DURATION
		     ", but not received");
	}

	fail_if(m.value == NULL, "Metadata " MAFW_METADATA_KEY_DURATION
		" not received");

	g_value_unset(m.value);
	g_free(m.value);
	m.value = NULL;
	m.expected_key = NULL;

	/* --- Pause --- */

	reset_callback_info(&c);

	g_debug("pause...");
	mafw_renderer_pause(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "pausing", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Paused, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_pause", "Paused", s.state);
	}

	/* --- Resume --- */

	reset_callback_info(&c);

	g_debug("resume...");
	mafw_renderer_resume(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "resuming", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_resume", "Playing", s.state);
	}

	/* --- Stop --- */

	reset_callback_info(&c);

	g_debug("stop...");
	mafw_renderer_stop(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "stopping", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Stopped, wait_tout_val) == FALSE) {
		fail(state_err_msg,"mafw_renderer_stop", "Stopped", s.state);
	}
	
}
END_TEST

START_TEST(test_playlist_playback)
{
	MafwPlaylist *playlist = NULL;
	gint i = 0;
	RendererInfo s = {0, };
	CallbackInfo c = {0, };
	gchar *cur_item_oid = NULL;
       
	/* Initialize callback info */
	c.err_msg = NULL;
	c.error_signal_expected = FALSE;
	c.error_signal_received = NULL;
	c.property_expected = NULL;
	c.property_received = NULL;

	/* Connect to renderer signals */
	g_signal_connect(g_gst_renderer, "error",
			 G_CALLBACK(error_cb),
			 &c);

	g_signal_connect(g_gst_renderer, "state-changed",
			 G_CALLBACK(state_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "media-changed",
			 G_CALLBACK(media_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "playlist-changed",
			 G_CALLBACK(playlist_changed_cb),
			 NULL);

	/* --- Create and assign a playlist --- */

	g_debug("assign playlist...");
	playlist = MAFW_PLAYLIST(mafw_mock_playlist_new());
	cur_item_oid =
		get_sample_clip_objectid(SAMPLE_AUDIO_CLIP);
	for (i=0; i<10; i++) {
		mafw_playlist_insert_item(
			playlist, i, cur_item_oid, NULL);
	}
	g_free(cur_item_oid);
	cur_item_oid = 	get_sample_clip_objectid("unexisting.wav");
	mafw_playlist_insert_item(playlist, 9, cur_item_oid, NULL);
	g_free(cur_item_oid);

	media_changed_called = FALSE;
	if (!mafw_renderer_assign_playlist(g_gst_renderer, playlist, NULL))
	{
		fail("Assign playlist failed");
	}

	wait_for_state(&s, Stopped, wait_tout_val);

	/* --- Play --- */

	reset_callback_info(&c);

	g_debug("play...");
	mafw_renderer_play(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
		       	fail(callback_err_msg, "playing", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play", "Playing", s.state);
	}

	/* --- Stop --- */

	reset_callback_info(&c);

	g_debug("stop...");
	mafw_renderer_stop(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
		       	fail(callback_err_msg, "stopping", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Stopped, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_stop", "Stopped", s.state);
	}

	/* --- Next --- */

	/* Get actual index */

	gint initial_index = s.index;

	for (i=0; i<3; i++) {

		reset_callback_info(&c);

		g_debug("move to next...");
		mafw_renderer_next(g_gst_renderer, playback_cb, &c);

		if (wait_for_callback(&c, wait_tout_val)) {
			if (c.error)
				fail(callback_err_msg, "moving to next", c.err_code,
				     c.err_msg);
		} else {
			fail(no_callback_msg);
		}
	
		/* Check if the playlist index is correct */
		fail_if(s.index != initial_index + (i+1), index_err_msg, s.index,
			initial_index + (i+1));        		
	}


	/* --- Prev --- */

	/* Get actual index */
	initial_index = s.index;

	for (i=0; i<3; i++) {

		reset_callback_info(&c);

		g_debug("move to prev...");
		mafw_renderer_previous(g_gst_renderer, playback_cb, &c);

		if (wait_for_callback(&c, wait_tout_val)) {
			if (c.error)
				fail(callback_err_msg, "moving to prev", c.err_code,
				     c.err_msg);
		} else {
			fail(no_callback_msg);
		}		

		/* Check if the playlist index is correct */
		fail_if(s.index != initial_index - (i+1), index_err_msg, s.index,
			initial_index - (i+1));        		
	}

	/* Check if renderer remains in Stopped state after some Prev operations */
	fail_if(s.state != Stopped, "Gst renderer didn't remain in Stopped state "
		"after doing prev. The actual state is %s and must be %s",
		s.state, "Stopped");

	/* --- Stop --- */

	reset_callback_info(&c);

	g_debug("stop...");
	mafw_renderer_stop(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "stopping playback",
			     c.err_code, c.err_msg);
	} else {
		fail(no_callback_msg);
	}
	
	if (wait_for_state(&s, Stopped, wait_tout_val) == FALSE) {
		fail(state_err_msg,"mafw_renderer_stop","Stopped", s.state);
	}

	/* --- Go to index in Stopped state --- */

	reset_callback_info(&c);

	g_debug("goto index 3...");
	mafw_renderer_goto_index(g_gst_renderer, 3, playback_cb, &c);
		
	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "going to index 3", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}
	
	/* Check if the playlist index is correct */
	fail_if(s.index != 3, index_err_msg, s.index, 3);

	/* Check if renderer remains in Stopped state after running go to index */
	fail_if(s.state != Stopped, "Gst renderer didn't remain in Stopped state "
		"after running go to index. The actual state is %s and must be"
		" %s", s.state, "Stopped");

	/* --- Play (playlist index is 3) --- */

	reset_callback_info(&c);

	g_debug("play...");
	mafw_renderer_play(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "playing", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play", "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play", "Playing", s.state);
	}
	
	/* --- Goto index in Playing state --- */

	reset_callback_info(&c);

	g_debug("goto index 5...");
	mafw_renderer_goto_index(g_gst_renderer, 5, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "going to index", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_goto_index", "Playing", s.state);
	}

	/* Check if the index if correct */
	fail_if(s.index != 5, index_err_msg, s.index, 5);

	/* Check if renderer remains in Playing state after running go to index */
	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail("Gst renderer didn't remain in Playing state after running "
		     "go to index. The actual state is %s and must be %s",
		     s.state, "Playing");
	}

	/* --- Goto an invalid index --- */

	reset_callback_info(&c);

	g_debug("goto the invalid index 20...");
	mafw_renderer_goto_index(g_gst_renderer, 20, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error == FALSE)
			fail("Error not received when we go to an incorrect" 
			     "index");
	} else {
		fail(no_callback_msg);
	}

	/* Check if the previous index (5) remains after an incorrect go to
	   index request */
	fail_if(s.index != 5, index_err_msg, 5, s.index);
	
	reset_callback_info(&c);

	/* --- Reassigning playlist --- */

	media_changed_called = FALSE;
	if (!mafw_renderer_assign_playlist(g_gst_renderer,
					   g_object_ref(playlist), NULL))
	{
		fail("Assign playlist failed");
	}

	wait_for_state(&s, Stopped, wait_tout_val);

	/* --- Go to index with invalid media --- */

	reset_callback_info(&c);

	g_debug("goto index 9...");
	mafw_renderer_goto_index(g_gst_renderer, 9, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "going to index 9", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	/* Check if the playlist index is correct */
	fail_if(s.index != 9, index_err_msg, s.index, 9);

	/* Check if renderer remains in Stopped state after running go
	 * to index */
	fail_if(s.state != Stopped, "Gst renderer didn't remain in Stopped "
		"state after running go to index. The actual state is %d and "
		"must be %s", s.state, "Stopped");

	/* --- Play (playlist index is 9) --- */

	reset_callback_info(&c);

	c.error_signal_expected = TRUE;

	g_debug("play...");
	mafw_renderer_play(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "playing", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play", "Transitioning",
		     s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play", "Playing", s.state);
	}

	fail_if(c.error_signal_received == NULL ||
		!g_error_matches(c.error_signal_received, MAFW_RENDERER_ERROR,
				 MAFW_RENDERER_ERROR_INVALID_URI),
		"No error received or incorrect one");

	if (c.error_signal_received != NULL) {
		g_error_free(c.error_signal_received);
		c.error_signal_received = NULL;
	}
	c.error_signal_expected = FALSE;

	/* --- Stop --- */

	reset_callback_info(&c);

	g_debug("stop...");
	mafw_renderer_stop(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "stopping playback",
			     c.err_code, c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Stopped, wait_tout_val) == FALSE) {
		fail(state_err_msg,"mafw_renderer_stop","Stopped", s.state);
	}

	/* --- Remove last media --- */

	mafw_playlist_remove_item(playlist, 10, NULL);

	/* --- Go to index with invalid media --- */

	reset_callback_info(&c);

	g_debug("goto index 9...");
	mafw_renderer_goto_index(g_gst_renderer, 9, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "going to index 9", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	/* Check if the playlist index is correct */
	fail_if(s.index != 9, index_err_msg, s.index, 9);

	/* Check if renderer remains in Stopped state after running go
	 * to index */
	fail_if(s.state != Stopped, "Gst renderer didn't remain in Stopped "
		"state after running go to index. The actual state is %d and "
		"must be %s", s.state, "Stopped");

	/* --- Play (playlist index is 9) --- */

	reset_callback_info(&c);

	c.error_signal_expected = TRUE;

	g_debug("play...");
	mafw_renderer_play(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "playing", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play", "Transitioning",
		     s.state);
	}

	if (wait_for_state(&s, Stopped, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play", "Stopped", s.state);
	}

	fail_if(c.error_signal_received == NULL ||
		!g_error_matches(c.error_signal_received, MAFW_RENDERER_ERROR,
				 MAFW_RENDERER_ERROR_INVALID_URI),
		"No error received or incorrect one");

	if (c.error_signal_received != NULL) {
		g_error_free(c.error_signal_received);
		c.error_signal_received = NULL;
	}
	c.error_signal_expected = FALSE;

	/* --- Play incorrect object --- */

	reset_callback_info(&c);

	c.error_signal_expected = TRUE;

	gchar *objectid = get_sample_clip_objectid("unexisting.wav");
	g_debug("play_object... %s", objectid);
	mafw_renderer_play_object(g_gst_renderer, objectid, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "playing an object", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object",
		     "Transitioning",
		     s.state);
	}

	if (wait_for_state(&s, Stopped, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Stopped",
		     s.state);
	}

	fail_if(c.error_signal_received == NULL ||
		!g_error_matches(c.error_signal_received, MAFW_RENDERER_ERROR,
				 MAFW_RENDERER_ERROR_INVALID_URI),
		"No error received or incorrect one");

	if (c.error_signal_received != NULL) {
		g_error_free(c.error_signal_received);
		c.error_signal_received = NULL;
	}
	c.error_signal_expected = FALSE;

	g_free(objectid);

}
END_TEST


START_TEST(test_repeat_mode_playback)
{
	MafwPlaylist *playlist = NULL;
	gint i = 0;
	RendererInfo s = {0, };;
	CallbackInfo c = {0, };;

	/* Initialize callback info */
	c.err_msg = NULL;
	c.error_signal_expected = FALSE;
	c.error_signal_received = NULL;
	c.property_expected = NULL;
	c.property_received = NULL;

	/* Connect to renderer signals */

	g_signal_connect(g_gst_renderer, "error",
			 G_CALLBACK(error_cb),
			 &c);
	g_signal_connect(g_gst_renderer, "state-changed",
			 G_CALLBACK(state_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "media-changed",
			 G_CALLBACK(media_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "playlist-changed",
			 G_CALLBACK(playlist_changed_cb),
			 NULL);

	/* --- Create playlist --- */

	playlist = MAFW_PLAYLIST(mafw_mock_playlist_new());
	for (i=0; i<10; i++) {
		gchar *cur_item_oid =
			get_sample_clip_objectid(SAMPLE_AUDIO_CLIP);
		mafw_playlist_insert_item(
			playlist, i, cur_item_oid, NULL);
		g_free(cur_item_oid);
	}
        
        /* Set repeat mode */
	mafw_playlist_set_repeat(playlist, TRUE);

	/* --- Assign playlist --- */	

	g_debug("assign playlist...");
	media_changed_called = FALSE;
	if (!mafw_renderer_assign_playlist(g_gst_renderer, playlist, NULL))
	{
		fail("Assign playlist failed");
	}

	wait_for_state(&s, Stopped, wait_tout_val);

	/* --- Play --- */

	reset_callback_info(&c);

	g_debug("play...");
	mafw_renderer_play(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
		       	fail(callback_err_msg, "playing", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play", "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play", "Playing", s.state);
	}

	/* --- Go to index --- */

	reset_callback_info(&c);

	g_debug("goto index 9...");
	/* go to the end of the playlist */
	mafw_renderer_goto_index(g_gst_renderer, 9, playback_cb, &c);
	
	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
		       	fail(callback_err_msg, "going to index 9", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}
	
	/* check if the movement was successful */
	fail_if(s.index != 9, index_err_msg, 9, s.index);
	
	/* ---  Stop --- */

	reset_callback_info(&c);

	g_debug("stop...");
	mafw_renderer_stop(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
		       	fail(callback_err_msg, "stopping playback", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Stopped, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_stop", "Stopped", s.state);
	}
 
	/* --- Next --- */

	reset_callback_info(&c);

	g_debug("next...");
	/* The actual index is 9 */
	mafw_renderer_next(g_gst_renderer, playback_cb, &c);

       	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
		       	fail(callback_err_msg, "moving to next", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	/* check if the movement was successful */
	fail_if(s.index != 0, index_err_msg, s.index, 0);

	/* Check if renderer remains in Stopped state after moving to next */
	fail_if(s.state != Stopped, "Gst renderer didn't remain in Stopped state "
		"after doing next. The actual state is %s and must be %s",
		s.state, "Stopped");

	/* --- Prev --- */

	reset_callback_info(&c);

	g_debug("prev...");
	/* The actual index is 0 */
	mafw_renderer_previous(g_gst_renderer, playback_cb, &c);

  	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
		       	fail(callback_err_msg, "moving to prev", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	/* check if the movement was successful */
	fail_if(s.index != 9, index_err_msg, s.index, 9);

	/* Check if renderer remains in Stopped state after moving to next */
	fail_if(s.state != Stopped, "Gst renderer didn't remain in Stopped state "
		"after doing next. The actual state is %s and must be %s",
		s.state, "Stopped");
}
END_TEST


START_TEST(test_gst_renderer_mode)
{
	MafwPlaylist *playlist = NULL;
	MafwGstRenderer *renderer = NULL;
	MafwGstRendererPlaybackMode play_mode;
	gchar *objectid = NULL;
	gint i = 0;
	RendererInfo s = {0, };;
	CallbackInfo c = {0, };;
	gchar *modes[] = {"MAFW_GST_RENDERER_MODE_PLAYLIST",
			  "MAFW_GST_RENDERER_MODE_STANDALONE"};

	renderer = MAFW_GST_RENDERER(g_gst_renderer);

	/* Initiliaze callback info */
	c.err_msg = NULL;
	c.error_signal_expected = FALSE;
	c.error_signal_received = NULL;
	c.property_expected = NULL;
	c.property_received = NULL;

	/* Connect to renderer signals */

	g_signal_connect(g_gst_renderer, "error",
			 G_CALLBACK(error_cb),
			 &c);
	g_signal_connect(g_gst_renderer, "state-changed",
			 G_CALLBACK(state_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "media-changed",
			 G_CALLBACK(media_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "playlist-changed",
			 G_CALLBACK(playlist_changed_cb),
			 NULL);

	/* --- Create playlist --- */

	playlist = MAFW_PLAYLIST(mafw_mock_playlist_new());
	for (i=0; i<10; i++) {
		gchar *cur_item_oid =
			get_sample_clip_objectid(SAMPLE_AUDIO_CLIP);
		mafw_playlist_insert_item(
			playlist, i, cur_item_oid, NULL);
		g_free(cur_item_oid);
	}

	/* --- Assign playlist --- */	

	g_debug("assign playlist...");
	media_changed_called = FALSE;
	if (!mafw_renderer_assign_playlist(g_gst_renderer, playlist, NULL))
	{
		fail("Assign playlist failed");
	}

	wait_for_state(&s, Stopped, wait_tout_val);

	/* --- Play --- */

	reset_callback_info(&c);

	g_debug("play...");

	mafw_renderer_play(g_gst_renderer, playback_cb, &c);
	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
		       	fail(callback_err_msg, "playing", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play", "Transitioning", s.state);
	}

	/* Check that renderer is playing a playlist */
	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play", "Playing", s.state);
	}
	play_mode =  mafw_gst_renderer_get_playback_mode(renderer);
	fail_if(play_mode != MAFW_GST_RENDERER_MODE_PLAYLIST,
		"Incorrect value of playback_mode: %s", modes[play_mode]);

	/* --- Play object --- */

	reset_callback_info(&c);

	objectid = get_sample_clip_objectid(SAMPLE_AUDIO_CLIP);
	g_debug("play_object... %s",objectid);
	mafw_renderer_play_object(g_gst_renderer, objectid, playback_cb, &c);
	g_free(objectid);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
		       	fail(callback_err_msg, "playing an object", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Transitioning",
		     s.state);
	}

	/* Check that renderer is playing an object */        	
	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Playing",
		     s.state);
	}

	play_mode =  mafw_gst_renderer_get_playback_mode(renderer);
	fail_if(play_mode != MAFW_GST_RENDERER_MODE_STANDALONE,
		"Incorrect value of playback_mode: %s", modes[play_mode]);

	/* Wait EOS_TIMEOUT to ensure that the play_object has finished */
	wait_until_timeout_finishes(EOS_TIMEOUT);

	/* Check that after playing the object, renderer returns to the playlist
	 playback */
	play_mode =  mafw_gst_renderer_get_playback_mode(renderer);
	fail_if(play_mode != MAFW_GST_RENDERER_MODE_PLAYLIST,
		"Incorrect value of playback_mode: %s", modes[play_mode]);
	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Playing",
		     s.state);
	}

	/* --- Play object --- */

	reset_callback_info(&c);

	objectid = get_sample_clip_objectid(SAMPLE_AUDIO_CLIP);
	g_debug("play_object... %s", objectid);
	mafw_renderer_play_object(g_gst_renderer, objectid, playback_cb, &c);
	g_free(objectid);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
		       	fail(callback_err_msg, "playing an object", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Transitioning",
		     s.state);
	}
        	
	/* Check that renderer is playing an object */
	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Playing",
		     s.state);
	}
	play_mode =  mafw_gst_renderer_get_playback_mode(renderer);
	fail_if(play_mode != MAFW_GST_RENDERER_MODE_STANDALONE,
		"Incorrect value of playback_mode: %s", modes[play_mode]);


 	/* --- Move to next when renderer is playing an object --- */

	reset_callback_info(&c);

	g_debug("next...");
	mafw_renderer_next(g_gst_renderer, playback_cb, &c);

       	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
		       	fail(callback_err_msg, "moving to next", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	/* Check that "next" function finishes the object playback and returns
	   to the playlist playback */
	play_mode =  mafw_gst_renderer_get_playback_mode(renderer);
	fail_if(play_mode != MAFW_GST_RENDERER_MODE_PLAYLIST,
		"Incorrect value of playback_mode: %s", modes[play_mode]);

	/* Check that renderer is still in Playing state */
	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Playing",
		     s.state);
	}

	/* --- Stop --- */

	reset_callback_info(&c);

	g_debug("stop...");
	mafw_renderer_stop(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "stopping", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Stopped, wait_tout_val) == FALSE) {
		fail(state_err_msg,"mafw_renderer_stop", "Stopped", s.state);
	}

	/* --- Play object --- */

	reset_callback_info(&c);

	objectid = get_sample_clip_objectid(SAMPLE_AUDIO_CLIP);
	g_debug("play_object... %s", objectid);
	mafw_renderer_play_object(g_gst_renderer, objectid, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
		       	fail(callback_err_msg, "playing an object", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Transitioning",
		     s.state);
	}

	/* Check that renderer is playing an object */        
	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Playing",
		     s.state);
	}
	play_mode =  mafw_gst_renderer_get_playback_mode(renderer);
	fail_if(play_mode != MAFW_GST_RENDERER_MODE_STANDALONE,
		"Incorrect value of playback_mode: %s", modes[play_mode]);
	
	/* Wait EOS_TIMEOUT to ensure that object playback finishes */
	wait_until_timeout_finishes(EOS_TIMEOUT);

	/* Check if renderer is in playlist mode and the renderer state is the state before
	   playing the object */
	if (wait_for_state(&s, Stopped, wait_tout_val) == FALSE) {
		fail(state_err_msg,"mafw_renderer_stop", "Stopped", s.state);
	}
	play_mode =  mafw_gst_renderer_get_playback_mode(renderer);
	fail_if(play_mode != MAFW_GST_RENDERER_MODE_PLAYLIST,
		"Incorrect value of playback_mode: %s", modes[play_mode]);

	g_free(objectid);
}
END_TEST

#define MOCK_SOURCE(o)							\
	(G_TYPE_CHECK_INSTANCE_CAST((o),				\
				    mock_source_get_type(),		\
				    MockSource))

typedef struct {
	MafwSourceClass parent;
} MockSourceClass;

typedef struct {
	MafwSource parent;


} MockSource;

GType mock_source_get_type(void);
GObject* mock_source_new(void);

G_DEFINE_TYPE(MockSource, mock_source, MAFW_TYPE_SOURCE);

static GHashTable *get_md_ht;		/* Metadata hash-table for the metadata result */
static GError *get_md_err;		/* Error value for the metadata result */
static gboolean set_mdata_called;	/* Whether set_metadata was called or not */
static gboolean get_mdata_called;	/* Whether get_metadata was called or not */
static gint reference_pcount;		/* Reference playcount, what should come in set_metadata */
static gboolean set_for_playcount;	/* TRUE, when the set_metadata is called to modify the playcount */
static gboolean set_for_lastplayed;	/* TRUE, when the set_metadata is called to modify the last-played */

static void get_metadata(MafwSource *self,
			     const gchar *object_id,
			     const gchar *const *metadata,
			     MafwSourceMetadataResultCb callback,
			     gpointer user_data)
{
	get_mdata_called = TRUE;
	fail_if(strcmp(object_id, "mocksource::test"));
	callback(self, object_id, get_md_ht, user_data, get_md_err);
}

static void set_metadata(MafwSource *self, const gchar *object_id,
			     GHashTable *metadata,
			     MafwSourceMetadataSetCb callback,
			     gpointer user_data)
{
	GValue *curval;
	gint htsize = 0;

	if (set_for_playcount)
		htsize++;
	if (set_for_lastplayed)
		htsize++;
	fail_if(strcmp(object_id, "mocksource::test"));
	fail_if(!metadata);
	fail_if(g_hash_table_size(metadata) != htsize, "Hash table size: %d vs %d", g_hash_table_size(metadata), htsize);
	if (set_for_playcount)
	{
		curval = mafw_metadata_first(metadata,
				MAFW_METADATA_KEY_PLAY_COUNT);
		fail_if(!curval);
		fail_if(g_value_get_int(curval) != reference_pcount);
	}
	if (set_for_lastplayed)
	{
		curval = mafw_metadata_first(metadata,
				MAFW_METADATA_KEY_LAST_PLAYED);
		fail_if(!curval);
		fail_if(!G_VALUE_HOLDS(curval, G_TYPE_LONG));
	}
	set_mdata_called = TRUE;
}

static void mock_source_class_init(MockSourceClass *klass)
{
	MafwSourceClass *sclass = MAFW_SOURCE_CLASS(klass);

	sclass->get_metadata = get_metadata;
	sclass->set_metadata = set_metadata;

}

static void mock_source_init(MockSource *source)
{
	/* NOP */
}

GObject* mock_source_new(void)
{
	GObject* object;
	object = g_object_new(mock_source_get_type(),
			      "plugin", "mockland",
			      "uuid", "mocksource",
			      "name", "mocksource",
			      NULL);
	return object;
}


START_TEST(test_update_stats)
{
	MafwGstRenderer *renderer = NULL;
	MafwSource *src;
	MafwRegistry *registry;

	registry = MAFW_REGISTRY(mafw_registry_get_instance());
	fail_if(registry == NULL,
		"Error: cannot get MAFW registry");
		

	renderer = MAFW_GST_RENDERER(g_gst_renderer);
	src = MAFW_SOURCE(mock_source_new());
	
	mafw_registry_add_extension(registry, MAFW_EXTENSION(src));

	/* Error on get_mdata_cb*/
	set_for_playcount = FALSE;
	set_for_lastplayed = FALSE;
        get_md_err = NULL;
	g_set_error(&get_md_err, MAFW_SOURCE_ERROR,
                    MAFW_SOURCE_ERROR_INVALID_OBJECT_ID,
                    "Wrong object id mocksource::test");
	renderer->media->object_id = g_strdup("mocksource::test");
	mafw_gst_renderer_update_stats(renderer);
        g_error_free(get_md_err);
	fail_if(set_mdata_called);
	fail_if(!get_mdata_called);

	/* get_mdata ok, but HashTable is NULL */
	reference_pcount = 1;
	get_mdata_called = FALSE;
	set_for_lastplayed = TRUE;
	set_for_playcount = TRUE;
	get_md_err = NULL;
	mafw_gst_renderer_update_stats(renderer);
	fail_if(!set_mdata_called);
	fail_if(!get_mdata_called);
	
	/* get_mdata ok, but HashTable is empty */
	get_mdata_called = FALSE;
	set_mdata_called = FALSE;
	set_for_lastplayed = TRUE;
	set_for_playcount = TRUE;
	get_md_ht = mafw_metadata_new();
	mafw_gst_renderer_update_stats(renderer);
	fail_if(!set_mdata_called);
	fail_if(!get_mdata_called);
	
	/* get_mdata ok, but HashTable has valid value */
	get_mdata_called = FALSE;
	set_mdata_called = FALSE;
	set_for_lastplayed = TRUE;
	set_for_playcount = TRUE;
	mafw_metadata_add_int(get_md_ht,
						MAFW_METADATA_KEY_PLAY_COUNT,
						1);
	reference_pcount = 2;
	mafw_gst_renderer_update_stats(renderer);
	fail_if(!set_mdata_called);
	fail_if(!get_mdata_called);
}
END_TEST

START_TEST(test_play_state)
{
	MafwPlaylist *playlist = NULL;
	gint i = 0;
	RendererInfo s = {0, };;
	CallbackInfo c = {0, };;
	gchar *objectid = NULL;

	/* Initialize callback info */
	c.err_msg = NULL;
	c.error_signal_expected = FALSE;
	c.error_signal_received = NULL;
	c.property_expected = NULL;
	c.property_received = NULL;

	/* Connect to renderer signals */
	g_signal_connect(g_gst_renderer, "error",
			 G_CALLBACK(error_cb),
			 &c);

	g_signal_connect(g_gst_renderer, "state-changed",
			 G_CALLBACK(state_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "media-changed",
			 G_CALLBACK(media_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "playlist-changed",
			 G_CALLBACK(playlist_changed_cb),
			 NULL);

	/* --- Get initial status --- */

	reset_callback_info(&c);

	g_debug("get status...");
	mafw_renderer_get_status(g_gst_renderer, status_cb, &s);

	/* --- Play object --- */

	reset_callback_info(&c);

	objectid = get_sample_clip_objectid(SAMPLE_AUDIO_CLIP);
	g_debug("play_object... %s", objectid);
	mafw_renderer_play_object(g_gst_renderer, objectid, playback_cb,
				   &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "playing an object", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object",
		     "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Playing",
		     s.state);
	}

	if (wait_for_state(&s, Stopped, 3000) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Stop",
		     s.state);
	}

	g_free(objectid);


	/* --- Create and assign a playlist --- */

	g_debug("assign playlist...");
	playlist = MAFW_PLAYLIST(mafw_mock_playlist_new());
	for (i=0; i<10; i++) {
		gchar *cur_item_oid =
			get_sample_clip_objectid(SAMPLE_AUDIO_CLIP);
		mafw_playlist_insert_item(
			playlist, i, cur_item_oid, NULL);
		g_free(cur_item_oid);
	}
	mafw_playlist_set_repeat(playlist, FALSE);

	media_changed_called = FALSE;
	if (!mafw_renderer_assign_playlist(g_gst_renderer, playlist,
					    NULL))
	{
		fail("Assign playlist failed");
	}

	wait_for_state(&s, Stopped, wait_tout_val);

	/* --- Play --- */

	reset_callback_info(&c);

	g_debug("play...");
	mafw_renderer_play(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail("Play after assigning playlist failed");
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play",
		     "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play", "Playing",
		     s.state);
	}

	/* --- Prev --- */

	reset_callback_info(&c);

	g_debug("move to prev...");
	mafw_renderer_previous(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "moving to prev", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	fail_if(s.index != 9, index_err_msg, s.index, 9);

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_prev",
		     "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_prev", "Playing",
		     s.state);
	}

	/* Removing last element */

	g_debug("removing last element...");
	fail_if(mafw_playlist_get_size(playlist, NULL) != 10,
		"Playlist should have 10 elements");
	mafw_playlist_remove_item(playlist, 9, NULL);
	fail_if(mafw_playlist_get_size(playlist, NULL) != 9,
		"Playlist should have 9 elements");
	fail_if(s.index != 8, index_err_msg, s.index, 8);

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_playlist_remove_element",
		     "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_playlist_remove_element", "Playing",
		     s.state);
	}

	/* --- Next --- */

	reset_callback_info(&c);

	g_debug("move to next...");
	mafw_renderer_next(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "moving to next", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	fail_if(s.index != 0, index_err_msg, s.index, 0);

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_next",
		     "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_next", "Playing",
		     s.state);
	}

	/* --- Go to index --- */

	reset_callback_info(&c);

	g_debug("goto index 8...");
	mafw_renderer_goto_index(g_gst_renderer, 8, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "going to index 8", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	fail_if(s.index != 8, index_err_msg, s.index, 8);

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_goto_index",
		     "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_goto_index", "Playing",
		     s.state);
	}

	/* --- Seeking --- */

	reset_callback_info(&c);

	g_debug("seeking...");
	mafw_renderer_set_position(g_gst_renderer, SeekAbsolute, 1,
				    seek_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "seeking failed", c.err_code,
			     c.err_msg);
		if (c.seek_position != 1) {
			fail("seeking failed");
		}
	} else {
		fail(no_callback_msg);
	}

	/* --- Waiting EOS --- */

	if (wait_for_state(&s, Stopped, 2000) == FALSE) {
		fail(state_err_msg, "EOS", "Stop",
		     s.state);
	}
}
END_TEST

START_TEST(test_pause_state)
{
	MafwPlaylist *playlist = NULL;
	gint i = 0;
	RendererInfo s = {0, };;
	CallbackInfo c = {0, };;
	gchar *objectid = NULL;

	/* Initialize callback info */
	c.err_msg = NULL;
	c.error_signal_expected = FALSE;
	c.error_signal_received = NULL;
	c.property_expected = NULL;
	c.property_received = NULL;

	/* Connect to renderer signals */
	g_signal_connect(g_gst_renderer, "error",
			 G_CALLBACK(error_cb),
			 &c);

	g_signal_connect(g_gst_renderer, "state-changed",
			 G_CALLBACK(state_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "media-changed",
			 G_CALLBACK(media_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "playlist-changed",
			 G_CALLBACK(playlist_changed_cb),
			 NULL);

	/* --- Get initial status --- */

	reset_callback_info(&c);

	g_debug("get status...");
	mafw_renderer_get_status(g_gst_renderer, status_cb, &s);

	/* --- Create and assign a playlist --- */

	g_debug("assign playlist...");
	playlist = MAFW_PLAYLIST(mafw_mock_playlist_new());
	for (i=0; i<10; i++) {
		gchar *cur_item_oid =
			get_sample_clip_objectid(SAMPLE_AUDIO_CLIP);
		mafw_playlist_insert_item(
			playlist, i, cur_item_oid, NULL);
		g_free(cur_item_oid);
	}
	mafw_playlist_set_repeat(playlist, FALSE);

	media_changed_called = FALSE;
	if (!mafw_renderer_assign_playlist(g_gst_renderer, playlist,
					    NULL))
	{
		fail("Assign playlist failed");
	}

	wait_for_state(&s, Stopped, wait_tout_val);

	/* --- Play --- */

	reset_callback_info(&c);

	g_debug("play...");
	mafw_renderer_play(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail("Play failed");
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play",
		     "Transitioning", s.state);
	}

	/* Testing pause in transitioning */

	reset_callback_info(&c);

	g_debug("pause...");
	mafw_renderer_pause(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "pausing", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	/* Testing resume in transitioning */

	reset_callback_info(&c);

	g_debug("resume...");
	mafw_renderer_resume(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "resuming", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	reset_callback_info(&c);

	/* Testing resume without having paused in transitioning */

	reset_callback_info(&c);

	g_debug("resume...");
	mafw_renderer_resume(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (!c.error)
			fail(callback_no_err_msg, "resuming", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	reset_callback_info(&c);

	g_debug("pause...");
	mafw_renderer_pause(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "pausing", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Paused, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_pause", "Paused",
		     s.state);
	}

	/* --- Play object in pause --- */

	reset_callback_info(&c);

	objectid = get_sample_clip_objectid(SAMPLE_AUDIO_CLIP);
	g_debug("play_object... %s", objectid);
	mafw_renderer_play_object(g_gst_renderer, objectid, playback_cb,
				   &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "playing an object", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object",
		     "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Playing",
		     s.state);
	}

	reset_callback_info(&c);

	g_debug("pause...");
	mafw_renderer_pause(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "pausing", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Paused, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_pause", "Paused",
		     s.state);
	}

	g_free(objectid);

	/* --- Play --- */

	reset_callback_info(&c);

	g_debug("play...");
	mafw_renderer_play(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail("Play failed");
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play",
		     "Transitioning", s.state);
	}

	reset_callback_info(&c);

	g_debug("pause...");
	mafw_renderer_pause(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "pausing", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Paused, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_pause", "Paused",
		     s.state);
	}

	/* --- Prev --- */

	reset_callback_info(&c);

	g_debug("move to prev...");
	mafw_renderer_previous(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "moving to prev", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	/* Check if the playlist index is correct */
	fail_if(s.index != 9, index_err_msg, s.index, 9);

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_prev",
		     "Transitioning", s.state);
	}

	reset_callback_info(&c);

	g_debug("pause...");
	mafw_renderer_pause(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "pausing", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Paused, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_prev", "Playing",
		     s.state);
	}

	/* Removing last element */

	g_debug("removing last element...");
	fail_if(mafw_playlist_get_size(playlist, NULL) != 10,
		"Playlist should have 10 elements");
	mafw_playlist_remove_item(playlist, 9, NULL);
	fail_if(mafw_playlist_get_size(playlist, NULL) != 9,
		"Playlist should have 9 elements");
	fail_if(s.index != 8, index_err_msg, s.index, 8);

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_playlist_remove_item",
		     "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_playlist_remove_item", "Playing",
		     s.state);
	}

	reset_callback_info(&c);

	g_debug("pause...");
	mafw_renderer_pause(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "pausing", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Paused, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_playlist_remove_item", "Playing",
		     s.state);
	}

	/* --- Next --- */

	reset_callback_info(&c);

	g_debug("move to next...");
	mafw_renderer_next(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "moving to next", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	/* Check if the playlist index is correct */
	fail_if(s.index != 0, index_err_msg, s.index, 0);

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_next",
		     "Transitioning", s.state);
	}

	reset_callback_info(&c);

	g_debug("pause...");
	mafw_renderer_pause(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "pausing", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Paused, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_next", "Playing",
		     s.state);
	}

	/* --- Go to index --- */

	reset_callback_info(&c);

	g_debug("goto index 8...");
	mafw_renderer_goto_index(g_gst_renderer, 8, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "going to index 8", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	/* Check if the playlist index is correct */
	fail_if(s.index != 8, index_err_msg, s.index, 8);

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_goto_index",
		     "Transitioning", s.state);
	}

	reset_callback_info(&c);

	g_debug("pause...");
	mafw_renderer_pause(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "pausing", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Paused, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_goto_index", "Playing",
		     s.state);
	}

	/* --- Seeking --- */

	reset_callback_info(&c);

	mafw_renderer_set_position(g_gst_renderer, SeekAbsolute, 1,
				    seek_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "seeking", c.err_code,
			     c.err_msg);
		if (c.seek_position != 1) {
			fail("seeking failed");
		}
	} else {
		fail(no_callback_msg);
	}

	/* --- Stop --- */

	reset_callback_info(&c);

	g_debug("stop...");
	mafw_renderer_stop(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "stopping", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Stopped, wait_tout_val) == FALSE) {
		fail(state_err_msg,"mafw_renderer_stop", "Stopped", s.state);
	}
}
END_TEST

START_TEST(test_stop_state)
{
	MafwPlaylist *playlist = NULL;
	gint i = 0;
	RendererInfo s = {0, };;
	CallbackInfo c = {0, };;

	/* Initialize callback info */
	c.err_msg = NULL;
	c.error_signal_expected = FALSE;
	c.error_signal_received = NULL;
	c.property_expected = NULL;
	c.property_received = NULL;

	/* Connect to renderer signals */
	g_signal_connect(g_gst_renderer, "error",
			 G_CALLBACK(error_cb),
			 &c);

	g_signal_connect(g_gst_renderer, "state-changed",
			 G_CALLBACK(state_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "media-changed",
			 G_CALLBACK(media_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "playlist-changed",
			 G_CALLBACK(playlist_changed_cb),
			 NULL);

	/* --- Get initial status --- */

	reset_callback_info(&c);

	g_debug("get status...");
	mafw_renderer_get_status(g_gst_renderer, status_cb, &s);

	/* --- Prev --- */

	reset_callback_info(&c);

	g_debug("move to prev...");
	mafw_renderer_previous(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (!c.error)
			fail(callback_no_err_msg, "moving to prev", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	/* --- Next --- */

	reset_callback_info(&c);

	g_debug("move to next...");
	mafw_renderer_next(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (!c.error)
			fail(callback_no_err_msg, "moving to next", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	/* --- Go to index --- */

	reset_callback_info(&c);

	g_debug("goto index 8...");
	mafw_renderer_goto_index(g_gst_renderer, 8, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (!c.error)
			fail(callback_no_err_msg, "going to index 8",
			     c.err_code, c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	/* --- Create and assign a playlist --- */

	g_debug("assign playlist...");
	playlist = MAFW_PLAYLIST(mafw_mock_playlist_new());
	for (i=0; i<10; i++) {
		gchar *cur_item_oid =
			get_sample_clip_objectid(SAMPLE_AUDIO_CLIP);
		mafw_playlist_insert_item(
			playlist, i, cur_item_oid, NULL);
		g_free(cur_item_oid);
	}
	mafw_playlist_set_repeat(playlist, FALSE);

	media_changed_called = FALSE;
	if (!mafw_renderer_assign_playlist(g_gst_renderer, playlist,
					    NULL))
	{
		fail("Assign playlist failed");
	}

	wait_for_state(&s, Stopped, wait_tout_val);

	/* Removing last element */

	g_debug("removing last element...");
	fail_if(mafw_playlist_get_size(playlist, NULL) != 10,
		"Playlist should have 10 elements");
	mafw_playlist_remove_item(playlist, 9, NULL);
	fail_if(mafw_playlist_get_size(playlist, NULL) != 9,
		"Playlist should have 9 elements");

	/* --- Go to index --- */

	reset_callback_info(&c);

	g_debug("goto index 9...");
	mafw_renderer_goto_index(g_gst_renderer, 9, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (!c.error)
			fail(callback_no_err_msg, "going to index 9",
			     c.err_code, c.err_msg);
	} else {
		fail(no_callback_msg);
	}
	reset_callback_info(&c);
}
END_TEST

START_TEST(test_transitioning_state)
{
	MafwPlaylist *playlist = NULL;
	gint i = 0;
	RendererInfo s = {0, };;
	CallbackInfo c = {0, };;
	gchar *objectid = NULL;

	/* Initialize callback info */
	c.err_msg = NULL;
	c.error_signal_expected = FALSE;
	c.error_signal_received = NULL;
	c.property_expected = NULL;
	c.property_received = NULL;

	/* Connect to renderer signals */
	g_signal_connect(g_gst_renderer, "error",
			 G_CALLBACK(error_cb),
			 &c);

	g_signal_connect(g_gst_renderer, "state-changed",
			 G_CALLBACK(state_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "media-changed",
			 G_CALLBACK(media_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "playlist-changed",
			 G_CALLBACK(playlist_changed_cb),
			 NULL);

	/* --- Get initial status --- */

	reset_callback_info(&c);

	g_debug("get status...");
	mafw_renderer_get_status(g_gst_renderer, status_cb, &s);

	/* --- Create and assign a playlist --- */

	g_debug("assign playlist...");
	playlist = MAFW_PLAYLIST(mafw_mock_playlist_new());
	for (i=0; i<10; i++) {
		gchar *cur_item_oid =
			get_sample_clip_objectid(SAMPLE_AUDIO_CLIP);
		mafw_playlist_insert_item(
			playlist, i, cur_item_oid, NULL);
		g_free(cur_item_oid);
	}
	mafw_playlist_set_repeat(playlist, FALSE);

	media_changed_called = FALSE;
	if (!mafw_renderer_assign_playlist(g_gst_renderer, playlist,
					    NULL))
	{
		fail("Assign playlist failed");
	}

	wait_for_state(&s, Stopped, wait_tout_val);

	/* --- Play --- */

	reset_callback_info(&c);

	g_debug("play...");
	mafw_renderer_play(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail("Play after assigning playlist failed");
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play",
		     "Transitioning", s.state);
	}

	/* --- Play object --- */

	reset_callback_info(&c);

	objectid = get_sample_clip_objectid(SAMPLE_AUDIO_CLIP);
	g_debug("play_object... %s", objectid);
	mafw_renderer_play_object(g_gst_renderer, objectid, playback_cb,
				   &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "playing an object", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object",
		     "Transitioning", s.state);
	}

	g_free(objectid);


	/* --- Prev --- */

	reset_callback_info(&c);

	g_debug("move to prev...");
	mafw_renderer_previous(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "moving to prev", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	fail_if(s.index != 9, index_err_msg, s.index, 9);

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_prev",
		     "Transitioning", s.state);
	}

	/* Removing last element */

	g_debug("removing last element...");
	fail_if(mafw_playlist_get_size(playlist, NULL) != 10,
		"Playlist should have 10 elements");
	mafw_playlist_remove_item(playlist, 9, NULL);
	fail_if(mafw_playlist_get_size(playlist, NULL) != 9,
		"Playlist should have 9 elements");
	fail_if(s.index != 8, index_err_msg, s.index, 8);

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_playlist_remove_element",
		     "Transitioning", s.state);
	}

	/* --- Next --- */

	reset_callback_info(&c);

	g_debug("move to next...");
	mafw_renderer_next(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "moving to next", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	fail_if(s.index != 0, index_err_msg, s.index, 0);

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_next",
		     "Transitioning", s.state);
	}

	/* --- Go to index --- */

	reset_callback_info(&c);

	g_debug("goto index 8...");
	mafw_renderer_goto_index(g_gst_renderer, 8, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "going to index 8", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	fail_if(s.index != 8, index_err_msg, s.index, 8);

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_goto_index",
		     "Transitioning", s.state);
	}
}
END_TEST

START_TEST(test_state_class)
{
	MafwPlaylist *playlist = NULL;
	gint i = 0;
	RendererInfo s = {0, };;
	CallbackInfo c = {0, };;
	gchar *objectid = NULL;

	/* Initialize callback info */
	c.err_msg = NULL;
	c.error_signal_expected = FALSE;
	c.error_signal_received = NULL;
	c.property_expected = NULL;
	c.property_received = NULL;

	/* Connect to renderer signals */
	g_signal_connect(g_gst_renderer, "error",
			 G_CALLBACK(error_cb),
			 &c);

	g_signal_connect(g_gst_renderer, "state-changed",
			 G_CALLBACK(state_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "media-changed",
			 G_CALLBACK(media_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "playlist-changed",
			 G_CALLBACK(playlist_changed_cb),
			 NULL);

	/* --- Get initial status --- */

	reset_callback_info(&c);

	g_debug("get status...");
	mafw_renderer_get_status(g_gst_renderer, status_cb, &s);

	/* --- Play object --- */

	reset_callback_info(&c);

	objectid = get_sample_clip_objectid(SAMPLE_AUDIO_CLIP);
	g_debug("play_object... %s", objectid);
	mafw_renderer_play_object(g_gst_renderer, objectid, playback_cb,
				   &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "playing an object", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object",
		     "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Playing",
		     s.state);
	}

	/* --- Prev --- */

	reset_callback_info(&c);

	g_debug("move to prev...");
	mafw_renderer_previous(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (!c.error)
			fail(callback_no_err_msg, "moving to prev", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	/* --- Play object --- */

	reset_callback_info(&c);

	g_debug("play_object... %s", objectid);
	mafw_renderer_play_object(g_gst_renderer, objectid, playback_cb,
				   &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "playing an object", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object",
		     "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Playing",
		     s.state);
	}

	/* --- Next --- */

	reset_callback_info(&c);

	g_debug("move to next...");
	mafw_renderer_next(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (!c.error)
			fail(callback_no_err_msg, "moving to next", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	/* --- Play object --- */

	reset_callback_info(&c);

	g_debug("play_object... %s", objectid);
	mafw_renderer_play_object(g_gst_renderer, objectid, playback_cb,
				   &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "playing an object", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object",
		     "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Playing",
		     s.state);
	}

	/* --- Go to index --- */

	reset_callback_info(&c);

	g_debug("goto index 8...");
	mafw_renderer_goto_index(g_gst_renderer, 8, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (!c.error)
			fail(callback_err_msg, "going to index 8", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	/* --- Create and assign a playlist --- */

	g_debug("assign playlist...");
	playlist = MAFW_PLAYLIST(mafw_mock_playlist_new());
	for (i=0; i<10; i++) {
		gchar *cur_item_oid =
			get_sample_clip_objectid(SAMPLE_AUDIO_CLIP);
		mafw_playlist_insert_item(
			playlist, i, cur_item_oid, NULL);
		g_free(cur_item_oid);
	}
	mafw_playlist_set_repeat(playlist, FALSE);

	media_changed_called = FALSE;
	if (!mafw_renderer_assign_playlist(g_gst_renderer, playlist,
					    NULL))
	{
		fail("Assign playlist failed");
	}

	wait_for_state(&s, Stopped, wait_tout_val);

	/* --- Play object --- */

	reset_callback_info(&c);

	g_debug("play_object... %s", objectid);
	mafw_renderer_play_object(g_gst_renderer, objectid, playback_cb,
				   &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "playing an object", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object",
		     "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Playing",
		     s.state);
	}

	/* --- Next --- */

	reset_callback_info(&c);

	g_debug("move to next...");
	mafw_renderer_next(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "moving to next", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	fail_if(s.index != 1, index_err_msg, s.index, 1);

	if (wait_for_state(&s, Stopped, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_next", "Playing",
		     s.state);
	}

	/* --- Play object --- */

	reset_callback_info(&c);

	g_debug("play_object... %s", objectid);
	mafw_renderer_play_object(g_gst_renderer, objectid, playback_cb,
				   &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "playing an object", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object",
		     "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Playing",
		     s.state);
	}

	/* --- Go to index --- */

	reset_callback_info(&c);

	g_debug("goto index 8...");
	mafw_renderer_goto_index(g_gst_renderer, 8, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "going to index 8", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	fail_if(s.index != 8, index_err_msg, s.index, 8);

	if (wait_for_state(&s, Stopped, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_goto_index", "Playing",
		     s.state);
	}

	/* --- Play object --- */

	reset_callback_info(&c);

	g_debug("play_object... %s", objectid);
	mafw_renderer_play_object(g_gst_renderer, objectid, playback_cb,
				   &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "playing an object", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object",
		     "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Playing",
		     s.state);
	}

	/* --- Prev --- */

	reset_callback_info(&c);

	g_debug("move to prev...");
	mafw_renderer_previous(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "moving to prev", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	fail_if(s.index != 7, index_err_msg, s.index, 7);

	if (wait_for_state(&s, Stopped, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_prev", "Playing",
		     s.state);
	}

	/* --- Play --- */

	reset_callback_info(&c);

	g_debug("play...");
	mafw_renderer_play(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail("Play after assigning playlist failed");
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play",
		     "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play", "Playing",
		     s.state);
	}

	/* --- Prev --- */

	reset_callback_info(&c);

	g_debug("move to prev...");
	mafw_renderer_previous(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "moving to prev", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	fail_if(s.index != 6, index_err_msg, s.index, 6);

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_prev",
		     "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_prev",
		     "Transitioning", s.state);
	}

	/* --- Seeking --- */

	reset_callback_info(&c);

	g_debug("seeking...");
	mafw_renderer_set_position(g_gst_renderer, SeekRelative, 1,
				    seek_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "seeking failed", c.err_code,
			     c.err_msg);
		if (c.seek_position != 1) {
			fail("seeking failed");
		}
	} else {
		fail(no_callback_msg);
	}

	/* --- Seeking --- */

	reset_callback_info(&c);

	g_debug("seeking...");
	mafw_renderer_set_position(g_gst_renderer, SeekAbsolute, -1,
				    seek_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "seeking failed", c.err_code,
			     c.err_msg);
		if (c.seek_position != -1) {
			fail("seeking failed");
		}
	} else {
		fail(no_callback_msg);
	}

	/* --- Seeking --- */

	reset_callback_info(&c);

	g_debug("seeking...");
	mafw_renderer_set_position(g_gst_renderer, SeekAbsolute, 1,
				    seek_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "seeking failed", c.err_code,
			     c.err_msg);
		if (c.seek_position != 1) {
			fail("seeking failed");
		}
	} else {
		fail(no_callback_msg);
	}
}
END_TEST

START_TEST(test_playlist_iterator)
{
	MafwPlaylist *playlist = NULL;
	gint i = 0;
	CallbackInfo c = {0, };;
	MafwPlaylistIterator *iterator = NULL;
	GError *error = NULL;
	gint size;
	gint index;

	/* Initialize callback info */
	c.err_msg = NULL;
	c.error = FALSE;
	reset_callback_info(&c);

	/* --- Create and assign a playlist --- */

	g_debug("assign playlist...");
	playlist = MAFW_PLAYLIST(mafw_mock_playlist_new());

	iterator = mafw_playlist_iterator_new();
	mafw_playlist_iterator_initialize(iterator, playlist, &error);
	if (error != NULL) {
		fail("Error found: %s, %d, %s",
		     g_quark_to_string(error->domain),
		     error->code, error->message);
	}

	for (i = 0; i < 3; i++) {
		gchar *cur_item_oid = NULL;
		cur_item_oid =
			get_sample_clip_objectid(SAMPLE_AUDIO_CLIP);
		mafw_playlist_insert_item(playlist, 0, cur_item_oid, NULL);
		g_free(cur_item_oid);
	}

	size = mafw_playlist_iterator_get_size(iterator, NULL);
	fail_if(size != 3, "Playlist should have 3 elements and it has %d",
		size);
	index = mafw_playlist_iterator_get_current_index(iterator);
	fail_if(index != 2, "Index should be 2 and it is %d", index);

	mafw_playlist_move_item(playlist, 1, 2, NULL);
	index = mafw_playlist_iterator_get_current_index(iterator);
	fail_if(index != 1, "Index should be 1 and it is %d", index);

	mafw_playlist_move_item(playlist, 2, 1, NULL);
	index = mafw_playlist_iterator_get_current_index(iterator);
	fail_if(index != 2, "Index should be 2 and it is %d", index);

	mafw_playlist_move_item(playlist, 2, 1, NULL);
	index = mafw_playlist_iterator_get_current_index(iterator);
	fail_if(index != 1, "Index should be 1 and it is %d", index);

	mafw_playlist_remove_item(playlist, 0, &error);
	if (error != NULL) {
		fail("Error found: %s, %d, %s",
		     g_quark_to_string(error->domain),
		     error->code, error->message);
	}

	size = mafw_playlist_iterator_get_size(iterator, NULL);
	fail_if(size != 2, "Playlist should have 2 elements and it has %d",
		size);
	index = mafw_playlist_iterator_get_current_index(iterator);
	fail_if(index != 0, "Index should be 0 and it is %d", index);

	mafw_playlist_iterator_reset(iterator, NULL);
	index = mafw_playlist_iterator_get_current_index(iterator);
	fail_if(index != 0, "Index should be 0 and it is %d", index);

	mafw_playlist_remove_item(playlist, 0, &error);
	if (error != NULL) {
		fail("Error found: %s, %d, %s",
		     g_quark_to_string(error->domain),
		     error->code, error->message);
	}

	size = mafw_playlist_iterator_get_size(iterator, NULL);
	fail_if(size != 1, "Playlist should have 1 elements and it has %d",
		size);
	index = mafw_playlist_iterator_get_current_index(iterator);
	fail_if(index != 0, "Index should be 0 and it is %d", index);

	mafw_playlist_remove_item(playlist, 0, &error);
	if (error != NULL) {
		fail("Error found: %s, %d, %s",
		     g_quark_to_string(error->domain),
		     error->code, error->message);
	}

	size = mafw_playlist_iterator_get_size(iterator, NULL);
	fail_if(size != 0, "Playlist should have 0 elements and it has %d",
		size);
	index = mafw_playlist_iterator_get_current_index(iterator);
	fail_if(index != -1, "Index should be -1 and it is %d", index);

	g_object_unref(iterator);
}
END_TEST

START_TEST(test_video)
{
	RendererInfo s = {0, };;
	CallbackInfo c = {0, };;
	MetadataChangedInfo m;
	gchar *objectid = NULL;
	GstBus *bus = NULL;
	GstStructure *structure = NULL;
	GstMessage *message = NULL;

	/* Initialize callback info */
	c.err_msg = NULL;
	c.error_signal_expected = FALSE;
	c.error_signal_received = NULL;
	m.expected_key = NULL;
	m.value = NULL;
	c.property_expected = NULL;
	c.property_received = NULL;

	/* Connect to renderer signals */
	g_signal_connect(g_gst_renderer, "error",
			 G_CALLBACK(error_cb),
			 &c);

	g_signal_connect(g_gst_renderer, "state-changed",
			 G_CALLBACK(state_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "media-changed",
			 G_CALLBACK(media_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "playlist-changed",
			 G_CALLBACK(playlist_changed_cb),
			 NULL);
	g_signal_connect(g_gst_renderer, "metadata-changed",
			 G_CALLBACK(metadata_changed_cb),
			 &m);

#ifdef HAVE_GDKPIXBUF
	mafw_extension_set_property_boolean(
		MAFW_EXTENSION(g_gst_renderer),
		MAFW_PROPERTY_GST_RENDERER_CURRENT_FRAME_ON_PAUSE,
		TRUE);
#endif

	/* --- Get initial status --- */

	reset_callback_info(&c);

	g_debug("get status...");
	mafw_renderer_get_status(g_gst_renderer, status_cb, &s);

	/* --- Play object --- */

	reset_callback_info(&c);

	objectid = get_sample_clip_objectid(SAMPLE_VIDEO_CLIP);
	g_debug("play_object... %s", objectid);
	mafw_renderer_play_object(g_gst_renderer, objectid, playback_cb,
				   &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "playing an object", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object",
		     "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Playing",
		     s.state);
	}

	MAFW_GST_RENDERER(g_gst_renderer)->worker->xid = 0x1;
	bus = MAFW_GST_RENDERER(g_gst_renderer)->worker->bus;
	fail_if(bus == NULL, "No GstBus");

	structure = gst_structure_new("prepare-xwindow-id", "width",
				      G_TYPE_INT, 64, "height", G_TYPE_INT, 32,
				      NULL);
	message = gst_message_new_element(NULL, structure);
	gst_bus_post(bus, message);

	/* --- Pause --- */

	reset_callback_info(&c);

	m.expected_key = MAFW_METADATA_KEY_PAUSED_THUMBNAIL_URI;

	g_debug("pause...");
	mafw_renderer_pause(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "pausing", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Paused, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_prev", "Playing",
		     s.state);
	}

	if (wait_for_metadata(&m, wait_tout_val) == FALSE) {
		fail("Expected " MAFW_METADATA_KEY_PAUSED_THUMBNAIL_URI
		     ", but not received");
	}

	fail_if(m.value == NULL, "Metadata "
		MAFW_METADATA_KEY_PAUSED_THUMBNAIL_URI " not received");

	g_value_unset(m.value);
	g_free(m.value);
	m.value = NULL;
	m.expected_key = NULL;

	/* --- Resume --- */

	reset_callback_info(&c);

	g_debug("resume...");
	mafw_renderer_resume(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "resuming", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Playing",
		     s.state);
	}

	/* --- EOS --- */

	if (wait_for_state(&s, Stopped, 3000) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Stop",
		     s.state);
	}

	g_free(objectid);
}
END_TEST

START_TEST(test_media_art)
{
	RendererInfo s = {0, };;
	CallbackInfo c = {0, };;
	MetadataChangedInfo m;
	gchar *objectid = NULL;
	GstBus *bus = NULL;
	GstMessage *message = NULL;
	GstTagList *list = NULL;
	GstBuffer *buffer = NULL;
	guchar *image = NULL;
	gchar *image_path = NULL;
	gsize image_length;
	GstCaps *caps = NULL;

	/* Initialize callback info */
	c.err_msg = NULL;
	c.error_signal_expected = FALSE;
	m.expected_key = NULL;
	m.value = NULL;
	c.property_expected = NULL;
	c.property_received = NULL;

	/* Connect to renderer signals */
	g_signal_connect(g_gst_renderer, "error",
			 G_CALLBACK(error_cb),
			 &c);

	g_signal_connect(g_gst_renderer, "state-changed",
			 G_CALLBACK(state_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "media-changed",
			 G_CALLBACK(media_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "playlist-changed",
			 G_CALLBACK(playlist_changed_cb),
			 NULL);
	g_signal_connect(g_gst_renderer, "metadata-changed",
			 G_CALLBACK(metadata_changed_cb),
			 &m);

	/* --- Get initial status --- */

	reset_callback_info(&c);

	g_debug("get status...");
	mafw_renderer_get_status(g_gst_renderer, status_cb, &s);

	/* --- Play object --- */

	reset_callback_info(&c);

	objectid = get_sample_clip_objectid(SAMPLE_AUDIO_CLIP);
	g_debug("play_object... %s", objectid);
	mafw_renderer_play_object(g_gst_renderer, objectid, playback_cb,
				   &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "playing an object", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	/* --- Pause --- */

	reset_callback_info(&c);

	g_debug("pause...");
	mafw_renderer_pause(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "pausing", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object",
		     "Transitioning", s.state);
	}

	if (wait_for_state(&s, Paused, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_prev", "Playing",
		     s.state);
	}

	/* Emit image */

	bus = MAFW_GST_RENDERER(g_gst_renderer)->worker->bus;
	fail_if(bus == NULL, "No GstBus");

	m.expected_key = MAFW_METADATA_KEY_RENDERER_ART_URI;

	image_path = get_sample_clip_path(SAMPLE_IMAGE);
	fail_if(!g_file_get_contents(image_path + 7, (gchar **) &image,
				     &image_length, NULL),
		"Could not load test image");
	g_free(image_path);

	buffer = gst_buffer_new();
	gst_buffer_set_data(buffer, image, image_length);
	caps = gst_caps_new_simple("image/png", "image-type",
				   GST_TYPE_TAG_IMAGE_TYPE,
				   GST_TAG_IMAGE_TYPE_FRONT_COVER, NULL);
	gst_buffer_set_caps(buffer, caps);
	gst_caps_unref(caps);

	list = gst_tag_list_new();
	gst_tag_list_add(list, GST_TAG_MERGE_APPEND, GST_TAG_IMAGE, buffer,
			 NULL);

	message = gst_message_new_tag(NULL, list);
	gst_bus_post(bus, message);

	/* --- Resume --- */

	reset_callback_info(&c);

	g_debug("resume...");
	mafw_renderer_resume(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "resuming", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Playing",
		     s.state);
	}

	if (wait_for_metadata(&m, wait_tout_val) == FALSE) {
		fail("Expected " MAFW_METADATA_KEY_RENDERER_ART_URI
		     ", but not received");
	}

	fail_if(m.value == NULL, "Metadata "
		MAFW_METADATA_KEY_RENDERER_ART_URI " not received");

	g_value_unset(m.value);
	g_free(m.value);
	m.value = NULL;
	m.expected_key = NULL;

	/* --- EOS --- */

	if (wait_for_state(&s, Stopped, 3000) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Stop",
		     s.state);
	}

	g_free(objectid);
}
END_TEST

START_TEST(test_properties_management)
{
	RendererInfo s;
	CallbackInfo c = {0, };;
	PropertyChangedInfo p;

	/* Initialize callback info */
	c.err_msg = NULL;
	c.error_signal_expected = FALSE;
	c.error_signal_received = NULL;
	c.property_expected = NULL;
	c.property_received = NULL;
	p.expected = NULL;
	p.received = NULL;

	/* Connect to renderer signals */
	g_signal_connect(g_gst_renderer, "state-changed",
			 G_CALLBACK(state_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "property-changed",
			 G_CALLBACK(property_changed_cb),
			 &p);

	/* Wait for the volume manager to be initialized */

	/* Volume */

	p.expected = MAFW_PROPERTY_RENDERER_VOLUME;

	if (!wait_for_property(&p, wait_tout_val)) {
		fail("No property %s received", p.expected);
	}

	fail_if(p.received == NULL, "No property %s received",
		p.expected);
	fail_if(p.received != NULL &&
		g_value_get_uint(p.received) != 48,
		"Property with value %d and %d expected",
		g_value_get_uint(p.received), 48);

	if (p.received != NULL) {
		g_value_unset(p.received);
		g_free(p.received);
		p.received = NULL;
	}
	p.expected = NULL;

	/* --- mute --- */

	reset_callback_info(&c);

	c.property_expected = MAFW_PROPERTY_RENDERER_MUTE;

	mafw_extension_set_property_boolean(MAFW_EXTENSION(g_gst_renderer),
					    c.property_expected, TRUE);

	p.expected = MAFW_PROPERTY_RENDERER_MUTE;

#ifdef MAFW_GST_RENDERER_ENABLE_MUTE
	if (!wait_for_property(&p, wait_tout_val)) {
		fail("No property %s received", p.expected);
	}

	fail_if(p.received == NULL, "No property %s received",
		p.expected);
	fail_if(p.received != NULL &&
		g_value_get_boolean(p.received) != TRUE,
		"Property with value %d and %d expected",
		g_value_get_boolean(p.received), TRUE);
#else
	if (wait_for_property(&p, wait_tout_val)) {
		fail("Property %s received and it should not have been",
		     p.expected);
	}

	fail_if(p.received != NULL,
		"Property %s received and it should not have been",
		p.expected);
#endif

	if (p.received != NULL) {
		g_value_unset(p.received);
		g_free(p.received);
		p.received = NULL;
	}
	p.expected = NULL;

	mafw_extension_get_property(MAFW_EXTENSION(g_gst_renderer),
				    c.property_expected, get_property_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "get_property", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	fail_if(c.property_received == NULL,
		"No property %s received and expected", c.property_expected);
#ifdef MAFW_GST_RENDERER_ENABLE_MUTE
	fail_if(c.property_received != NULL &&
		g_value_get_boolean(c.property_received) != TRUE,
		"Property with value %d and %d expected",
		g_value_get_boolean(c.property_received), TRUE);
#else
	fail_if(c.property_received != NULL &&
		g_value_get_boolean(c.property_received) != FALSE,
		"Property with value %d and %d expected",
		g_value_get_boolean(c.property_received), FALSE);
#endif

	/* --- xid --- */

	reset_callback_info(&c);

	c.property_expected = MAFW_PROPERTY_RENDERER_XID;

	mafw_extension_set_property_uint(MAFW_EXTENSION(g_gst_renderer),
					 c.property_expected, 50);

	mafw_extension_get_property(MAFW_EXTENSION(g_gst_renderer),
				    c.property_expected, get_property_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "get_property", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	fail_if(c.property_received == NULL,
		"No property %s received and expected", c.property_expected);
	fail_if(c.property_received != NULL &&
		g_value_get_uint(c.property_received) != 50,
		"Property with value %d and %d expected",
		g_value_get_uint(c.property_received), 50);

	/* --- error policy --- */

	reset_callback_info(&c);

	c.property_expected = MAFW_PROPERTY_RENDERER_ERROR_POLICY;

	mafw_extension_set_property_uint(MAFW_EXTENSION(g_gst_renderer),
					 c.property_expected, 1);

	mafw_extension_get_property(MAFW_EXTENSION(g_gst_renderer),
				    c.property_expected, get_property_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "get_property", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	fail_if(c.property_received == NULL,
		"No property %s received and expected", c.property_expected);
	fail_if(c.property_received != NULL &&
		g_value_get_uint(c.property_received) != 1,
		"Property with value %d and %d expected",
		g_value_get_uint(c.property_received), 1);

	/* --- autopaint --- */

	reset_callback_info(&c);

	c.property_expected = MAFW_PROPERTY_RENDERER_AUTOPAINT;

	mafw_extension_set_property_boolean(MAFW_EXTENSION(g_gst_renderer),
					    c.property_expected, TRUE);

	mafw_extension_get_property(MAFW_EXTENSION(g_gst_renderer),
				    c.property_expected, get_property_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "get_property", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	fail_if(c.property_received == NULL,
		"No property %s received and expected", c.property_expected);
	fail_if(c.property_received != NULL &&
		g_value_get_boolean(c.property_received) != TRUE,
		"Property with value %d and %d expected",
		g_value_get_boolean(c.property_received), TRUE);

	/* --- colorkey --- */

	reset_callback_info(&c);

	c.property_expected = MAFW_PROPERTY_RENDERER_COLORKEY;

	mafw_extension_get_property(MAFW_EXTENSION(g_gst_renderer),
				    c.property_expected, get_property_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "get_property", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	fail_if(c.property_received == NULL,
		"No property %s received and expected", c.property_expected);
	fail_if(c.property_received != NULL &&
		g_value_get_int(c.property_received) != -1,
		"Property with value %d and %d expected",
		g_value_get_int(c.property_received), -1);

	/* --- current frame on pause --- */

	reset_callback_info(&c);

	c.property_expected = MAFW_PROPERTY_GST_RENDERER_CURRENT_FRAME_ON_PAUSE;

	mafw_extension_set_property_boolean(MAFW_EXTENSION(g_gst_renderer),
					    c.property_expected, TRUE);

	mafw_extension_get_property(MAFW_EXTENSION(g_gst_renderer),
				    c.property_expected, get_property_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "get_property", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	fail_if(c.property_received == NULL,
		"No property %s received and expected", c.property_expected);
	fail_if(c.property_received != NULL &&
		g_value_get_boolean(c.property_received) != TRUE,
		"Property with value %d and %d expected",
		g_value_get_boolean(c.property_received), TRUE);

	/* --- volume --- */

	p.expected = MAFW_PROPERTY_RENDERER_VOLUME;

	mafw_extension_set_property_uint(MAFW_EXTENSION(g_gst_renderer),
					 p.expected, 50);

	if (!wait_for_property(&p, wait_tout_val)) {
		fail("No property %s received", p.expected);
	}

	fail_if(p.received == NULL, "No property %s received",
		p.expected);
	fail_if(p.received != NULL &&
		g_value_get_uint(p.received) != 50,
		"Property with value %d and %d expected",
		g_value_get_uint(p.received), 50);

	if (p.received != NULL) {
		g_value_unset(p.received);
		g_free(p.received);
		p.received = NULL;
	}
	p.expected = NULL;

	c.property_expected = MAFW_PROPERTY_RENDERER_VOLUME;

	mafw_extension_get_property(MAFW_EXTENSION(g_gst_renderer),
				    c.property_expected, get_property_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "get_property", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	fail_if(c.property_received == NULL,
		"No property %s received and expected", c.property_expected);
	fail_if(c.property_received != NULL &&
		g_value_get_uint(c.property_received) != 50,
		"Property with value %d and %d expected",
		g_value_get_uint(c.property_received), 50);

#ifndef MAFW_GST_RENDERER_DISABLE_PULSE_VOLUME
	/* Test reconnection to pulse */

	pa_context_disconnect(pa_context_get_instance());

	/* Wait for the volume manager to be reinitialized */

	/* Volume */

	p.expected = MAFW_PROPERTY_RENDERER_VOLUME;

	if (!wait_for_property(&p, wait_tout_val)) {
		fail("No property %s received", p.expected);
	}

	fail_if(p.received == NULL, "No property %s received",
		p.expected);
	fail_if(p.received != NULL &&
		g_value_get_uint(p.received) != 48,
		"Property with value %d and %d expected",
		g_value_get_uint(p.received), 48);

	if (p.received != NULL) {
		g_value_unset(p.received);
		g_free(p.received);
		p.received = NULL;
	}
	p.expected = NULL;

	reset_callback_info(&c);
#endif
}
END_TEST

START_TEST(test_buffering)
{
	RendererInfo s;
	CallbackInfo c;
	BufferingInfo b;
	GstBus *bus = NULL;
	GstMessage *message = NULL;

	/* Initialize callback info */
	c.err_msg = NULL;
	c.error_signal_expected = FALSE;
	c.error_signal_received = NULL;
	c.property_expected = NULL;
	c.property_received = NULL;
	b.requested = FALSE;
	b.received = FALSE;
	b.value = 0.0;

	/* Connect to renderer signals */
	g_signal_connect(g_gst_renderer, "error",
			 G_CALLBACK(error_cb),
			 &c);
	g_signal_connect(g_gst_renderer, "state-changed",
			 G_CALLBACK(state_changed_cb),
			 &s);
	g_signal_connect(g_gst_renderer, "buffering-info",
			 G_CALLBACK(buffering_info_cb),
			 &b);

	/* --- Get initial status --- */

	reset_callback_info(&c);

	g_debug("get status...");
	mafw_renderer_get_status(g_gst_renderer, status_cb, &s);

	/* --- Play object --- */

	reset_callback_info(&c);

	gchar *objectid = get_sample_clip_objectid(SAMPLE_AUDIO_CLIP);
	g_debug("play_object... %s", objectid);
	mafw_renderer_play_object(g_gst_renderer, objectid, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "playing an object", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Transitioning, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object",
		     "Transitioning", s.state);
	}

	if (wait_for_state(&s, Playing, wait_tout_val) == FALSE) {
		fail(state_err_msg, "mafw_renderer_play_object", "Playing",
		     s.state);
	}

	g_free(objectid);

	/* --- Buffering info --- */

	b.requested = TRUE;

	bus = MAFW_GST_RENDERER(g_gst_renderer)->worker->bus;
	fail_if(bus == NULL, "No GstBus");

	message = gst_message_new_buffering(NULL, 50);
	gst_bus_post(bus, message);

	if (wait_for_buffering(&b, wait_tout_val) == FALSE) {
		fail("Expected buffering message but not received");
	}

	fail_if(b.value != 0.5, "Expected buffering 0.50 and received %1.2f",
		b.value);

	b.requested = FALSE;
	b.received = FALSE;
	b.value = 0;

	/* --- Buffering info --- */

	b.requested = TRUE;

	message = gst_message_new_buffering(NULL, 100);
	gst_bus_post(bus, message);

	if (wait_for_buffering(&b, wait_tout_val) == FALSE) {
		fail("Expected buffering message but not received");
	}

	fail_if(b.value != 1.0, "Expected buffering 1.00 and received %1.2f",
		b.value);

	b.requested = FALSE;
	b.received = FALSE;
	b.value = 0;

	/* --- Stop --- */

	reset_callback_info(&c);

	g_debug("stop...");
	mafw_renderer_stop(g_gst_renderer, playback_cb, &c);

	if (wait_for_callback(&c, wait_tout_val)) {
		if (c.error)
			fail(callback_err_msg, "stopping", c.err_code,
			     c.err_msg);
	} else {
		fail(no_callback_msg);
	}

	if (wait_for_state(&s, Stopped, wait_tout_val) == FALSE) {
		fail(state_err_msg,"mafw_renderer_stop", "Stopped", s.state);
	}
}
END_TEST

/*----------------------------------------------------------------------------
  Suit creation
  ----------------------------------------------------------------------------*/

SRunner * configure_tests(void)
{
	SRunner *sr = NULL;
	Suite *s = NULL;
	const gchar *tout = g_getenv("WAIT_TIMEOUT");
	
	if (!tout)
		wait_tout_val = DEFAULT_WAIT_TOUT;
	else
	{
		wait_tout_val = (gint)strtol(tout, NULL, 0);
		if (wait_tout_val<=0)
			wait_tout_val = DEFAULT_WAIT_TOUT;
	}

	checkmore_wants_dbus();
	mafw_log_init(":error");
	/* Create the suite */
	s = suite_create("MafwGstRenderer");

	/* Create test cases */
	TCase *tc1 = tcase_create("Playback");

	/* Create unit tests for test case "Playback" */
	tcase_add_checked_fixture(tc1, fx_setup_dummy_gst_renderer,
				  fx_teardown_dummy_gst_renderer);
if (1)	tcase_add_test(tc1, test_basic_playback);
if (1)	tcase_add_test(tc1, test_playlist_playback);
if (1)	tcase_add_test(tc1, test_repeat_mode_playback);
if (1)	tcase_add_test(tc1, test_gst_renderer_mode);
if (1)	tcase_add_test(tc1, test_update_stats);
if (1)  tcase_add_test(tc1, test_play_state);
if (1)  tcase_add_test(tc1, test_pause_state);
if (1)  tcase_add_test(tc1, test_stop_state);
if (1)  tcase_add_test(tc1, test_transitioning_state);
if (1)  tcase_add_test(tc1, test_state_class);
if (1)  tcase_add_test(tc1, test_playlist_iterator);
if (1)  tcase_add_test(tc1, test_video);
if (1)  tcase_add_test(tc1, test_media_art);
if (1)  tcase_add_test(tc1, test_properties_management);
if (1)  tcase_add_test(tc1, test_buffering);

	tcase_set_timeout(tc1, 0);

	suite_add_tcase(s, tc1);

	/* Create srunner object with the test suite */
	sr = srunner_create(s);

	return sr;
}

/* vi: set noexpandtab ts=8 sw=8 cino=t0,(0: */
