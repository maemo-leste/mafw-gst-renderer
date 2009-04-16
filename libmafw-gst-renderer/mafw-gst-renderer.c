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

#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <dbus/dbus.h>

#include <libmafw/mafw.h>
#include "mafw-gst-renderer.h"
#include "mafw-gst-renderer-utils.h"
#include "mafw-gst-renderer-worker.h"

#include "mafw-gst-renderer-state-playing.h"
#include "mafw-gst-renderer-state-stopped.h"
#include "mafw-gst-renderer-state-paused.h"
#include "mafw-gst-renderer-state-transitioning.h"

#include "blanking.h"

#ifdef HAVE_CONIC
#include <conicconnectionevent.h>
#endif

#undef  G_LOG_DOMAIN
#define G_LOG_DOMAIN "mafw-gst-renderer"

#define is_current_uri_stream(self) \
	(((self)->media != NULL) && ((self)->media->uri != NULL) &&	\
	 uri_is_stream((self)->media->uri))

/*----------------------------------------------------------------------------
  Static variable definitions
  ----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
  Plugin initialization
  ----------------------------------------------------------------------------*/

static gboolean mafw_gst_renderer_initialize(MafwRegistry *registry,
					   GError **error);
static void mafw_gst_renderer_deinitialize(GError **error);

/*----------------------------------------------------------------------------
  GObject initialization
  ----------------------------------------------------------------------------*/

static void mafw_gst_renderer_dispose(GObject *object);
static void mafw_gst_renderer_finalize(GObject *object);

/*----------------------------------------------------------------------------
  Hal callbacks
  ----------------------------------------------------------------------------*/
static void _device_condition(LibHalContext *ctx, const char *udi,
			      const char *name, const char *detail);
static void _property_modified(LibHalContext *ctx, const char *udi,
                               const char *key, dbus_bool_t is_removed,
                               dbus_bool_t is_added);
static gboolean _tv_out_is_connected(LibHalContext *ctx, const char *udi);
/*----------------------------------------------------------------------------
  Playback
  ----------------------------------------------------------------------------*/

static void _signal_state_changed(MafwGstRenderer * self);
static void _signal_media_changed(MafwGstRenderer * self);
static void _signal_playlist_changed(MafwGstRenderer * self);
static void _signal_transport_actions_property_changed(MafwGstRenderer * self);

/*----------------------------------------------------------------------------
  Properties
  ----------------------------------------------------------------------------*/

static void _set_error_policy(MafwGstRenderer *renderer, MafwRendererErrorPolicy policy);
static MafwRendererErrorPolicy _get_error_policy(MafwGstRenderer *renderer);

static void mafw_gst_renderer_set_property(MafwExtension *self, const gchar *key,
					 const GValue *value);
static void mafw_gst_renderer_get_property(MafwExtension *self, const gchar *key,
					 MafwExtensionPropertyCallback callback,
					 gpointer user_data);

/*----------------------------------------------------------------------------
  Metadata
  ----------------------------------------------------------------------------*/

static void _notify_metadata(MafwSource *cb_source,
			     const gchar *cb_object_id,
			     GHashTable *cb_metadata,
			     gpointer cb_user_data,
			     const GError *cb_error);

/*----------------------------------------------------------------------------
  Notification operations
  ----------------------------------------------------------------------------*/

static void _notify_play(MafwGstRendererWorker *worker, gpointer owner);
static void _notify_pause(MafwGstRendererWorker *worker, gpointer owner);
static void _notify_seek(MafwGstRendererWorker *worker, gpointer owner);
static void _notify_buffer_status(MafwGstRendererWorker *worker, gpointer owner,
				  gdouble percent);
static void _notify_eos(MafwGstRendererWorker *worker, gpointer owner);
static void _error_handler(MafwGstRendererWorker *worker, gpointer owner,
			   const GError *error);

#ifdef HAVE_CONIC
/*----------------------------------------------------------------------------
  Connection
  ----------------------------------------------------------------------------*/

static void _connection_init(MafwGstRenderer *renderer);
#endif

/*----------------------------------------------------------------------------
  GIO event handlers
  ----------------------------------------------------------------------------*/

static void _pre_unmount_handler(GVolumeMonitor *volume_monitor,
				 GMount *mount,
				 MafwGstRenderer *renderer);

/*----------------------------------------------------------------------------
  Plugin initialization
  ----------------------------------------------------------------------------*/

/*
 * Registers the plugin descriptor making this plugin available to the
 * framework and applications
 */
G_MODULE_EXPORT MafwPluginDescriptor mafw_gst_renderer_plugin_description = {
	{ .name		= MAFW_GST_RENDERER_PLUGIN_NAME },
	.initialize	= mafw_gst_renderer_initialize,
	.deinitialize	= mafw_gst_renderer_deinitialize,
};

static gboolean mafw_gst_renderer_initialize(MafwRegistry *registry,
					   GError **error)
{
	MafwGstRenderer *self;

	g_assert(registry != NULL);
	self = MAFW_GST_RENDERER(mafw_gst_renderer_new(registry));
	mafw_registry_add_extension(registry, MAFW_EXTENSION(self));

	return TRUE;
}

static void mafw_gst_renderer_deinitialize(GError **error)
{
}

/*----------------------------------------------------------------------------
  GObject initialization
  ----------------------------------------------------------------------------*/

G_DEFINE_TYPE(MafwGstRenderer, mafw_gst_renderer, MAFW_TYPE_RENDERER);

static void mafw_gst_renderer_class_init(MafwGstRendererClass *klass)
{
	GObjectClass *gclass = NULL;
	MafwRendererClass *renderer_class = NULL;
	const gchar *preloaded_plugins[] = {"playback", "uridecodebin",
		"coreelements", "typefindfunctions", "omx", "selector",
		"autodetect", "pulseaudio", "audioconvert", "audioresample",
		"xvimagesink", "ffmpegcolorspace", "videoscale", NULL};
	gint i = 0;
	GObject *plugin;

	gclass = G_OBJECT_CLASS(klass);
	g_return_if_fail(gclass != NULL);

	renderer_class = MAFW_RENDERER_CLASS(klass);
	g_return_if_fail(renderer_class != NULL);

	/* GObject */

	gclass->dispose = mafw_gst_renderer_dispose;
	gclass->finalize = mafw_gst_renderer_finalize;

	/* Playback */

	renderer_class->play = mafw_gst_renderer_play;
	renderer_class->play_object = mafw_gst_renderer_play_object;
	renderer_class->stop = mafw_gst_renderer_stop;
	renderer_class->pause = mafw_gst_renderer_pause;
	renderer_class->resume = mafw_gst_renderer_resume;
	renderer_class->get_status = mafw_gst_renderer_get_status;

	/* Playlist operations */

	renderer_class->assign_playlist = mafw_gst_renderer_assign_playlist;
	renderer_class->next = mafw_gst_renderer_next;
	renderer_class->previous = mafw_gst_renderer_previous;
	renderer_class->goto_index = mafw_gst_renderer_goto_index;

	/* Playback position */

	renderer_class->set_position = mafw_gst_renderer_set_position;
	renderer_class->get_position = mafw_gst_renderer_get_position;

	/* Properties */

	MAFW_EXTENSION_CLASS(klass)->get_extension_property =
		(gpointer) mafw_gst_renderer_get_property;
	MAFW_EXTENSION_CLASS(klass)->set_extension_property =
		(gpointer) mafw_gst_renderer_set_property;

	gst_init(NULL, NULL);
	gst_pb_utils_init();

	/* Pre-load some common plugins */
	while (preloaded_plugins[i])
	{
		plugin = G_OBJECT(gst_plugin_load_by_name(preloaded_plugins[i]));
		if (plugin)
			g_object_unref(plugin);
		else
			g_debug("Can not load plugin: %s", preloaded_plugins[i]);
		i++;
	}
}

static void mafw_gst_renderer_init(MafwGstRenderer *self)
{
	MafwGstRenderer *renderer = NULL;
	g_return_if_fail(MAFW_IS_GST_RENDERER(self));

	renderer = MAFW_GST_RENDERER(self);
	g_return_if_fail(renderer != NULL);

	mafw_extension_add_property(MAFW_EXTENSION(self), "volume", G_TYPE_UINT);
	mafw_extension_add_property(MAFW_EXTENSION(self), "mute", G_TYPE_BOOLEAN);
	mafw_extension_add_property(MAFW_EXTENSION(self), "xid", G_TYPE_UINT);
	mafw_extension_add_property(MAFW_EXTENSION(self), "error-policy", G_TYPE_UINT);
	MAFW_EXTENSION_SUPPORTS_AUTOPAINT(self);
	MAFW_EXTENSION_SUPPORTS_COLORKEY(self);
#ifdef HAVE_GDKPIXBUF
	mafw_extension_add_property(MAFW_EXTENSION(self),
				     "current-frame-on-pause",
				     G_TYPE_BOOLEAN);
#endif
 	MAFW_EXTENSION_SUPPORTS_TRANSPORT_ACTIONS(self);
	renderer->media = g_new0(MafwGstRendererMedia, 1);
	renderer->media->seekability = SEEKABILITY_UNKNOWN;
	renderer->current_state = Stopped;

	renderer->playlist = NULL;
	renderer->iterator = NULL;
	renderer->seeking_to = -1;
        renderer->update_playcount_id = 0;

        self->worker = mafw_gst_renderer_worker_new(self);

        /* Set notification handlers for worker */
        renderer->worker->notify_play_handler = _notify_play;
        renderer->worker->notify_pause_handler = _notify_pause;
        renderer->worker->notify_seek_handler = _notify_seek;
        renderer->worker->notify_error_handler = _error_handler;
        renderer->worker->notify_eos_handler = _notify_eos;
	renderer->worker->notify_buffer_status_handler = _notify_buffer_status;

	renderer->states = g_new0 (MafwGstRendererState*, _LastMafwPlayState);
	renderer->states[Stopped] =
		MAFW_GST_RENDERER_STATE(mafw_gst_renderer_state_stopped_new(self));
	renderer->states[Transitioning] =
		MAFW_GST_RENDERER_STATE(
			mafw_gst_renderer_state_transitioning_new(self));
	renderer->states[Playing] =
		MAFW_GST_RENDERER_STATE(mafw_gst_renderer_state_playing_new(self));
	renderer->states[Paused] =
		MAFW_GST_RENDERER_STATE(mafw_gst_renderer_state_paused_new(self));

	renderer->current_state = Stopped;
	renderer->resume_playlist = FALSE;
	renderer->playback_mode = MAFW_GST_RENDERER_MODE_PLAYLIST;

#ifdef HAVE_CONIC
	renderer->connected = FALSE;
	renderer->connection = NULL;

	_connection_init(renderer);
#endif

	renderer->volume_monitor = g_volume_monitor_get();
	g_signal_connect(G_OBJECT(renderer->volume_monitor),
			 "mount-pre-unmount",
			 G_CALLBACK(_pre_unmount_handler),
			 renderer);
}

static void mafw_gst_renderer_dispose(GObject *object)
{
	MafwGstRenderer *renderer;

	g_return_if_fail(MAFW_IS_GST_RENDERER(object));

	renderer = MAFW_GST_RENDERER(object);

	if (renderer->worker != NULL) {
		mafw_gst_renderer_worker_exit(renderer->worker);
		renderer->seek_pending = FALSE;
		g_free(renderer->worker);
		renderer->worker = NULL;
	}

	if (renderer->registry != NULL) {
		g_object_unref(renderer->registry);
		renderer->registry = NULL;
	}

	if (renderer->states != NULL) {
		guint i = 0;

		for (i = 0; i < _LastMafwPlayState; i++) {
			if (renderer->states[i] != NULL)
				g_object_unref(renderer->states[i]);
		}
		g_free(renderer->states);
		renderer->states = NULL;
	}

	if (renderer->hal_ctx != NULL) {
		libhal_ctx_shutdown(renderer->hal_ctx, NULL);
		libhal_ctx_free(renderer->hal_ctx);
	}

#ifdef HAVE_CONIC
	if (renderer->connection != NULL) {
		g_object_unref(renderer->connection);
		renderer->connection = NULL;
	}
#endif

	G_OBJECT_CLASS(mafw_gst_renderer_parent_class)->dispose(object);
}

static void mafw_gst_renderer_finalize(GObject *object)
{
	MafwGstRenderer *self = (MafwGstRenderer*) object;

	g_return_if_fail(MAFW_IS_GST_RENDERER(self));

	mafw_gst_renderer_clear_media(self);

	if (self->media)
	{
		g_free(self->media);
		self->media = NULL;
	}

	G_OBJECT_CLASS(mafw_gst_renderer_parent_class)->finalize(object);
}

/**
 * mafw_gst_renderer_new:
 * @registry: The registry that owns this renderer.
 *
 * Creates a new MafwGstRenderer object
 */
GObject *mafw_gst_renderer_new(MafwRegistry* registry)
{
	GObject* object;
	LibHalContext *ctx;
	DBusConnection *conn;
	DBusError err;
        char **jackets;
        char **jack;
        gint num_jacks;

	object = g_object_new(MAFW_TYPE_GST_RENDERER,
			      "uuid", MAFW_GST_RENDERER_UUID,
			      "name", MAFW_GST_RENDERER_NAME,
			      "plugin", MAFW_GST_RENDERER_PLUGIN_NAME,
			      NULL);
	g_assert(object != NULL);
	MAFW_GST_RENDERER(object)->registry = g_object_ref(registry);

	/* Set default error policy */
	MAFW_GST_RENDERER(object)->error_policy =
		MAFW_RENDERER_ERROR_POLICY_CONTINUE;

	/* Setup hal connection for reacting usb cable connected event */
	dbus_error_init(&err);
	conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);

        if (dbus_error_is_set(&err)) {
		g_warning("Couldn't setup HAL connection: %s", err.message);
                dbus_error_free(&err);

		goto err1;
        }
	ctx = libhal_ctx_new();
	libhal_ctx_set_dbus_connection(ctx, conn);
	libhal_ctx_set_user_data(ctx, object);

	if (libhal_ctx_init(ctx, &err) == FALSE) {
		if (dbus_error_is_set(&err)) {
			g_warning("Could not initialize hal: %s", err.message);
			dbus_error_free(&err);
		} else {
			g_warning("Could not initialize hal");
		}
		goto err2;
	}

	/* TODO, should watch only specific messages, also check if watch
	 * should be removed on dispose() */
	libhal_device_property_watch_all(ctx, &err);

	if (dbus_error_is_set(&err)) {
		g_warning("Could not start watching usb device: %s", err.message);
		dbus_error_free(&err);

		goto err3;
	}
	libhal_ctx_set_device_condition(ctx, _device_condition);
        libhal_ctx_set_device_property_modified(ctx, _property_modified);

        /* Initializes blanking policy */
        jackets = libhal_find_device_by_capability(ctx,
                                                   "input.jack.videoout",
                                                   &num_jacks, NULL);
	if (jackets != NULL) {
                jack = jackets;
                while (*jack) {
                        if (_tv_out_is_connected(ctx, *jack)) {
                                break;
                        }
                        jack++;
                }

                blanking_control(*jack == NULL);
                libhal_free_string_array(jackets);
        }

	MAFW_GST_RENDERER(object)->hal_ctx = ctx;

	return object;
err3:
	libhal_ctx_shutdown(ctx, NULL);
err2:
	libhal_ctx_free(ctx);
err1:
	return object;
}

/**
 * mafw_gst_renderer_error_quark:
 *
 * Fetches the quark representing the domain of the errors in the
 * gst renderer
 *
 * Return value: a quark identifying the error domain of the
 * #MafwGstRenderer objects.
 *
 **/
GQuark mafw_gst_renderer_error_quark(void)
{
	return g_quark_from_static_string("mafw-gst-renderer-error-quark");
}

void mafw_gst_renderer_set_playback_mode(MafwGstRenderer *self,
				       MafwGstRendererPlaybackMode mode)
{
	g_return_if_fail(MAFW_IS_GST_RENDERER(self));
	self->playback_mode = mode;
}

MafwGstRendererPlaybackMode mafw_gst_renderer_get_playback_mode(
	MafwGstRenderer *self)
{
	g_return_val_if_fail(MAFW_IS_GST_RENDERER(self),
			     MAFW_GST_RENDERER_MODE_STANDALONE);
	return self->playback_mode;
}

/*----------------------------------------------------------------------------
  Set Media
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_get_metadata(MafwGstRenderer* self,
				  const gchar* objectid,
				  GError **error)
{
	MafwSource* source;
	gchar* sourceid = NULL;

	g_assert(self != NULL);
	g_assert(objectid != NULL);

	/*
	 * Any error here is an error when trying to Play, so
	 * it must be handled by error policy.
	 * Problem: if we get an error here and we are not in
	 * Transitioning yet (maybe we are still in Stopped state)
	 * then the policy may move to next and stay Stopped (instead of
	 * trying to play), so  errors need to be handled by the policy
         * in an idle callback, so that any error that may happen here
         * is not processed until we have moved to Transitioning state
	 */

	/* Attempt to find a source that provided the object ID */
	mafw_source_split_objectid(self->media->object_id, &sourceid, NULL);
	source = MAFW_SOURCE(mafw_registry_get_extension_by_uuid(self->registry,
							    sourceid));
	g_free(sourceid);
	if (source != NULL)
	{
		/* List of metadata keys that we are interested in when going to
		   Transitioning state */
		static const gchar * const keys[] =
			{ MAFW_METADATA_KEY_URI,
			  MAFW_METADATA_KEY_IS_SEEKABLE, NULL };

		/* Source found, get metadata */
		mafw_source_get_metadata(source, objectid,
					 keys,
					 _notify_metadata,
					 self);

 	}
	else
	{
		/* This is a playback error: execute error policy */
		MafwGstRendererErrorClosure *error_closure;
		error_closure = g_new0(MafwGstRendererErrorClosure, 1);
		error_closure->renderer = self;
		g_set_error (&(error_closure->error),
			     MAFW_EXTENSION_ERROR,
			     MAFW_EXTENSION_ERROR_EXTENSION_NOT_AVAILABLE,
			     "Unable to find source for current object ID");
		g_idle_add(mafw_gst_renderer_manage_error_idle, error_closure);
	}
}

void mafw_gst_renderer_set_object(MafwGstRenderer *self, const gchar *object_id)
{
	MafwGstRenderer *renderer = (MafwGstRenderer *) self;

	g_return_if_fail(MAFW_IS_GST_RENDERER(self));
	g_return_if_fail(object_id != NULL);

	/* This is intended to be called only when using play_object(),
	 * as for playlists we use set_media_playlist()
	 */

	/* Stop any ongoing playback */
	mafw_gst_renderer_clear_media(renderer);

	/* Set new object */
	renderer->media->object_id = g_strdup(object_id);

	/* Signal media changed */
	_signal_media_changed(renderer);
}


/**
 * mafw_gst_renderer_clear_media:
 *
 * @renderer A #MafwGstRenderer whose media to clear
 *
 * Clears & frees the renderer's current media details
 **/
void mafw_gst_renderer_clear_media(MafwGstRenderer *self)
{
	g_return_if_fail(MAFW_IS_GST_RENDERER(self));
	g_return_if_fail(self->media != NULL);

	g_free(self->media->object_id);
	self->media->object_id = NULL;

	g_free(self->media->uri);
	self->media->uri = NULL;

	g_free(self->media->title);
	self->media->title = NULL;

	g_free(self->media->artist);
	self->media->artist = NULL;

	g_free(self->media->album);
	self->media->album = NULL;

	self->media->duration = 0;
	self->media->position = 0;
}


/**
 * mafw_gst_renderer_set_media_playlist:
 *
 * @self A #MafwGstRenderer, whose media to set
 *
 * Set current media from the renderer's playlist, using the current playlist index.
 **/
void mafw_gst_renderer_set_media_playlist(MafwGstRenderer* self)
{
	g_return_if_fail(MAFW_IS_GST_RENDERER(self));

	/* Get rid of old media details */
	mafw_gst_renderer_clear_media(self);

        if (self->playlist != NULL &&
            mafw_playlist_iterator_get_size(self->iterator, NULL) > 0) {
                /* Get the current item from playlist */
                self->media->object_id =
			g_strdup(mafw_playlist_iterator_get_current_objectid(self->iterator));
        } else {
                self->media->object_id = NULL;
	}

	_signal_media_changed(self);
}

#ifdef HAVE_CONIC
/*----------------------------------------------------------------------------
  Connection
  ----------------------------------------------------------------------------*/

static void
_con_ic_status_handler(ConIcConnection *conn, ConIcConnectionEvent *event,
		       gpointer data)
{
	MafwGstRenderer *renderer = (MafwGstRenderer *) data;

	g_assert(MAFW_IS_GST_RENDERER(renderer));

	renderer->connected =
		con_ic_connection_event_get_status(event) ==
		CON_IC_STATUS_CONNECTED;
}

static void
_connection_init(MafwGstRenderer *renderer)
{
	g_assert (MAFW_IS_GST_RENDERER(renderer));

	if (renderer->connection == NULL) {
		renderer->connection = con_ic_connection_new();
		renderer->connected = FALSE;

		g_assert(renderer->connection != NULL);
	}

	g_object_set(renderer->connection, "automatic-connection-events",
                     TRUE, NULL);
        g_signal_connect(renderer->connection, "connection-event",
			 G_CALLBACK (_con_ic_status_handler), renderer);

	con_ic_connection_connect(renderer->connection,
				  CON_IC_CONNECT_FLAG_AUTOMATICALLY_TRIGGERED);
}
#endif

/*----------------------------------------------------------------------------
  Hal callbacks
  ----------------------------------------------------------------------------*/

static void _device_condition(LibHalContext *ctx, const char *udi,
			      const char *name, const char *detail)
{
	MafwGstRenderer* self = MAFW_GST_RENDERER(
                libhal_ctx_get_user_data(ctx));

	g_assert(NULL != self);

	/* If not playing anything, bail out */
	if (!self->media->uri) 
		return;

	g_debug("Codition change event from HAL: name=%s, detail=%s",
                name, detail);
	if (!strcmp(name, "ButtonPressed") && !strcmp(detail, "usb.cable")) {
		g_debug("Usb cable connected! Currently playing %s.",
                        self->media->uri);

		/* TODO: check if we can always expect this prefix */
		if (g_str_has_prefix(self->media->uri, "file:///media/mmc")) {
			g_debug("We are playing from mmc, stopping");
			mafw_renderer_stop(MAFW_RENDERER(self), NULL, NULL);
		}
		else {
			g_debug("Not playing from mmc, continuing");
		}
	}
}

static gboolean _tv_out_is_connected(LibHalContext *ctx, const char *udi)
{
        gboolean is_tv_out_jack = FALSE;
        char **jack_types;
        char **jack;

        if (udi == NULL) {
                return FALSE;
        }

        jack_types = libhal_device_get_property_strlist(ctx, udi,
                                                        "input.jacket.type",
                                                        NULL);
        if (jack_types == NULL) {
                return FALSE;
        }

        jack = jack_types;
        while (*jack) {
                if (strcmp(*jack, "videoout") == 0) {
                        is_tv_out_jack = TRUE;
                        break;
                } else {
                        jack++;
                }
        }

        libhal_free_string_array(jack_types);

        return is_tv_out_jack;
}

static void _property_modified(LibHalContext *ctx, const char *udi,
                               const char *key, dbus_bool_t is_removed,
                               dbus_bool_t is_added)
{
        /* Check if the property changed affects the jack */
        if (strcmp(key, "input.jack.type") == 0) {
                blanking_control(_tv_out_is_connected(ctx, udi) == FALSE);
        }
}

/*----------------------------------------------------------------------------
  Signals
  ----------------------------------------------------------------------------*/


/**
 * _signal_state_changed:
 * @self: A #MafwGstRenderer
 *
 * Signals state_changed to all UIs
 **/
static void _signal_state_changed(MafwGstRenderer * self)
{
	g_return_if_fail(MAFW_IS_GST_RENDERER(self));

	g_signal_emit_by_name(MAFW_RENDERER(self),
			      "state-changed", self->current_state);
}

/**
 * _signal_playlist_changed:
 * @self: A #MafwGstRenderer
 *
 * Signals playlist update to all UIs
 **/
static void _signal_playlist_changed(MafwGstRenderer * self)
{
	g_return_if_fail(MAFW_IS_GST_RENDERER(self));

	g_signal_emit_by_name(MAFW_RENDERER(self),
			      "playlist-changed", self->playlist);
}

/**
 * _signal_media_changed:
 * @self: A #MafwGstRenderer
 *
 * Signals media_changed to all UIs
 **/
static void _signal_media_changed(MafwGstRenderer *self)
{

	MafwGstRendererPlaybackMode mode;
	gint index;

	g_return_if_fail(MAFW_IS_GST_RENDERER(self));

	mode = mafw_gst_renderer_get_playback_mode(MAFW_GST_RENDERER(self));
	if ((mode == MAFW_GST_RENDERER_MODE_STANDALONE) ||
            (self->iterator == NULL)) {
		index = -1;
	} else {
		index = mafw_playlist_iterator_get_current_index(self->iterator);
	}

	g_signal_emit_by_name(MAFW_RENDERER(self),
			      "media-changed",
			      index,
			      self->media->object_id);
}

/**
 * _signal_transport_actions_property_changed:
 * @self: A #MafwGstRenderer
 *
 * Signals transport_actions property_changed to all UIs
 **/
static void _signal_transport_actions_property_changed(MafwGstRenderer * self)
{
	GValue *value;

	g_return_if_fail(MAFW_IS_GST_RENDERER(self));

	value = mafw_gst_renderer_state_get_property_value(
		MAFW_GST_RENDERER_STATE(
			self->states[self->current_state]),
		MAFW_PROPERTY_RENDERER_TRANSPORT_ACTIONS);

	mafw_extension_emit_property_changed(
		MAFW_EXTENSION(self),
		MAFW_PROPERTY_RENDERER_TRANSPORT_ACTIONS,
		value);

}


/*----------------------------------------------------------------------------
  State pattern support
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_set_state(MafwGstRenderer *self, MafwPlayState state)
{
	g_return_if_fail(MAFW_IS_GST_RENDERER(self));

	self->current_state = state;
	_signal_state_changed(self);
	_signal_transport_actions_property_changed(self);
}

void mafw_gst_renderer_play(MafwRenderer *self, MafwRendererPlaybackCB callback,
			  gpointer user_data)
{
	MafwGstRenderer *renderer = (MafwGstRenderer*) self;
	GError *error = NULL;

	g_return_if_fail(MAFW_IS_GST_RENDERER(self));

	g_return_if_fail((renderer->states != 0) &&
			 (renderer->current_state != _LastMafwPlayState) &&
			 (renderer->states[renderer->current_state] != NULL));

	mafw_gst_renderer_state_play(
		MAFW_GST_RENDERER_STATE(renderer->states[renderer->current_state]),
		&error);

	if (callback != NULL)
		callback(self, user_data, error);
	if (error)
		g_error_free(error);
}

void mafw_gst_renderer_play_object(MafwRenderer *self,
				 const gchar *object_id,
				 MafwRendererPlaybackCB callback,
				 gpointer user_data)
{
	MafwGstRenderer *renderer = (MafwGstRenderer*) self;
	GError *error = NULL;

	g_return_if_fail(MAFW_IS_GST_RENDERER(self));
	g_return_if_fail(object_id != NULL);

	g_return_if_fail((renderer->states != 0) &&
			 (renderer->current_state != _LastMafwPlayState) &&
			 (renderer->states[renderer->current_state] != NULL));

	mafw_gst_renderer_state_play_object(
		MAFW_GST_RENDERER_STATE(renderer->states[renderer->current_state]),
		object_id,
		&error);

	if (callback != NULL)
		callback(self, user_data, error);
	if (error)
		g_error_free(error);
}

void mafw_gst_renderer_stop(MafwRenderer *self, MafwRendererPlaybackCB callback,
			  gpointer user_data)
{
	MafwGstRenderer *renderer = (MafwGstRenderer *) self;
	GError *error = NULL;

	g_return_if_fail(MAFW_IS_GST_RENDERER(self));

	g_return_if_fail((renderer->states != 0) &&
			 (renderer->current_state != _LastMafwPlayState) &&
			 (renderer->states[renderer->current_state] != NULL));

	renderer->play_failed_count = 0;
	mafw_gst_renderer_state_stop(
		MAFW_GST_RENDERER_STATE(renderer->states[renderer->current_state]),
		&error);

	if (callback != NULL)
		callback(self, user_data, error);
	if (error)
		g_error_free(error);
}


void mafw_gst_renderer_pause(MafwRenderer *self, MafwRendererPlaybackCB callback,
			   gpointer user_data)
{
	MafwGstRenderer *renderer = (MafwGstRenderer *) self;
	GError *error = NULL;

	g_return_if_fail(MAFW_IS_GST_RENDERER(self));

	g_return_if_fail((renderer->states != 0) &&
			 (renderer->current_state != _LastMafwPlayState) &&
			 (renderer->states[renderer->current_state] != NULL));

	mafw_gst_renderer_state_pause(
		MAFW_GST_RENDERER_STATE(renderer->states[renderer->current_state]),
		&error);

	if (callback != NULL)
		callback(self, user_data, error);
	if (error)
		g_error_free(error);
}

void mafw_gst_renderer_resume(MafwRenderer *self, MafwRendererPlaybackCB callback,
			    gpointer user_data)
{
	MafwGstRenderer *renderer = (MafwGstRenderer *) self;
	GError *error = NULL;

	g_return_if_fail(MAFW_IS_GST_RENDERER(self));

	g_return_if_fail((renderer->states != 0) &&
			 (renderer->current_state != _LastMafwPlayState) &&
			 (renderer->states[renderer->current_state] != NULL));

	mafw_gst_renderer_state_resume(
		MAFW_GST_RENDERER_STATE (renderer->states[renderer->current_state]),
		&error);

	if (callback != NULL)
		callback(self, user_data, error);
	if (error)
		g_error_free(error);
}

void mafw_gst_renderer_next(MafwRenderer *self, MafwRendererPlaybackCB callback,
			  gpointer user_data)
{
	MafwGstRenderer *renderer = (MafwGstRenderer *) self;
	GError *error = NULL;

	g_return_if_fail(MAFW_IS_GST_RENDERER(self));

	g_return_if_fail((renderer->states != 0) &&
			 (renderer->current_state != _LastMafwPlayState) &&
			 (renderer->states[renderer->current_state] != NULL));

	renderer->play_failed_count = 0;
	mafw_gst_renderer_state_next(
		MAFW_GST_RENDERER_STATE(renderer->states[renderer->current_state]),
		&error);

	if (callback != NULL)
		callback(self, user_data, error);
	if (error)
		g_error_free(error);
}

void mafw_gst_renderer_previous(MafwRenderer *self, MafwRendererPlaybackCB callback,
			      gpointer user_data)
{
	MafwGstRenderer *renderer = (MafwGstRenderer *) self;
	GError *error = NULL;

	g_return_if_fail(MAFW_IS_GST_RENDERER(self));

	g_return_if_fail((renderer->states != 0) &&
			 (renderer->current_state != _LastMafwPlayState) &&
			 (renderer->states[renderer->current_state] != NULL));

	renderer->play_failed_count = 0;
	mafw_gst_renderer_state_previous(
		MAFW_GST_RENDERER_STATE(renderer->states[renderer->current_state]),
		&error);

	if (callback != NULL)
		callback(self, user_data, error);
	if (error)
		g_error_free(error);
}

void mafw_gst_renderer_goto_index(MafwRenderer *self, guint index,
				MafwRendererPlaybackCB callback,
				gpointer user_data)
{
	MafwGstRenderer *renderer = (MafwGstRenderer *) self;
	GError *error = NULL;

	g_return_if_fail(MAFW_IS_GST_RENDERER(self));

	g_return_if_fail((renderer->states != 0) &&
			 (renderer->current_state != _LastMafwPlayState) &&
			 (renderer->states[renderer->current_state] != NULL));

	renderer->play_failed_count = 0;
	mafw_gst_renderer_state_goto_index(
		MAFW_GST_RENDERER_STATE(renderer->states[renderer->current_state]),
		index,
		&error);

	if (callback != NULL)
		callback(self, user_data, error);
	if (error)
		g_error_free(error);
}

void mafw_gst_renderer_set_position(MafwRenderer *self, MafwRendererSeekMode mode,
				   gint seconds, MafwRendererPositionCB callback,
				   gpointer user_data)
{
	MafwGstRenderer *renderer = (MafwGstRenderer *) self;
	GError *error = NULL;

	g_return_if_fail(MAFW_IS_GST_RENDERER(self));

	g_return_if_fail((renderer->states != 0) &&
			 (renderer->current_state != _LastMafwPlayState) &&
			 (renderer->states[renderer->current_state] != NULL));

	mafw_gst_renderer_state_set_position(
		MAFW_GST_RENDERER_STATE (renderer->states[renderer->current_state]),
		mode,
		seconds,
		&error);

	if (callback != NULL)
		callback(self, seconds, user_data, error);
	if (error)
		g_error_free(error);
}

gboolean mafw_gst_renderer_manage_error_idle(gpointer data)
{
        MafwGstRendererErrorClosure *mec = (MafwGstRendererErrorClosure *) data;

        mafw_gst_renderer_manage_error(mec->renderer, mec->error);
	if (mec->error)
        	g_error_free(mec->error);
        g_free(mec);

        return FALSE;
}

static void _run_error_policy(MafwGstRenderer *self, const GError *in_err,
			      GError **out_err)
{
        g_return_if_fail(MAFW_IS_GST_RENDERER(self));

        gboolean play_next = FALSE;

        /* Check what to do on error */
	if (in_err->code == MAFW_EXTENSION_ERROR_OUT_OF_MEMORY) {
                play_next = FALSE;
	} else {
		MafwGstRendererPlaybackMode mode;

		mode = mafw_gst_renderer_get_playback_mode(self);

		if (mode == MAFW_GST_RENDERER_MODE_PLAYLIST) {
			/* In playlist mode we try to play next if
			   error policy suggests so */
			play_next =
				(_get_error_policy(self) ==
				 MAFW_RENDERER_ERROR_POLICY_CONTINUE);
		} else {
			/* In standalone mode, then switch back to playlist
			   mode and resume if necessary or move to Stopped
			   otherwise */
			mafw_gst_renderer_set_playback_mode(
				self, MAFW_GST_RENDERER_MODE_PLAYLIST);
			mafw_gst_renderer_set_media_playlist(self);
			if (self->resume_playlist) {
				mafw_gst_renderer_play(MAFW_RENDERER(self),
						     NULL, NULL);
			} else {
				mafw_gst_renderer_worker_stop(self->worker);
				mafw_gst_renderer_set_state(self, Stopped);
			}
			if (out_err) *out_err = g_error_copy(in_err);

			/* Bail out, he have already managed the error
			   for the case of standalone mode */
			return;
		}
	}

	if (play_next) {
		if (self->playlist){
			MafwPlaylistIteratorMovementResult result;

			result = mafw_playlist_iterator_move_to_next(self->iterator,
								      NULL);
			self->play_failed_count++;

			if (mafw_playlist_iterator_get_size(self->iterator,
				NULL) <=
				self->play_failed_count)
			{
				mafw_gst_renderer_state_stop(
					MAFW_GST_RENDERER_STATE(self->states[self->current_state]),
					NULL);
				self->play_failed_count = 0;
				mafw_gst_renderer_set_media_playlist(self);
			} else if (result !=
				   MAFW_PLAYLIST_ITERATOR_MOVE_RESULT_OK) {
				mafw_playlist_iterator_reset(self->iterator, NULL);
				mafw_gst_renderer_set_media_playlist(self);
				mafw_gst_renderer_stop(MAFW_RENDERER(self), NULL, NULL);
			} else {
				mafw_gst_renderer_set_media_playlist(self);
				mafw_gst_renderer_play(MAFW_RENDERER(self), NULL, NULL);
			}

			if (out_err) *out_err = g_error_copy(in_err);
		}
	} else {
		/* We cannot move to next in the playlist or decided
		   we do not want to do it, just stop on error */
                mafw_gst_renderer_stop(MAFW_RENDERER(self), NULL, NULL);
                if (out_err) *out_err = g_error_copy(in_err);
	}
}

/**
 * mafw_gst_renderer_update_playcount_cb:
 * @data: user data
 *
 * Updates both playcount and lastplayed after a while.
 **/
gboolean mafw_gst_renderer_update_playcount_cb(gpointer data)
{
        MafwGstRenderer *renderer = (MafwGstRenderer *) data;
	
	if (renderer->media->object_id)
	{
		mafw_gst_renderer_increase_playcount(renderer,
                                             renderer->media->object_id);
		mafw_gst_renderer_update_lastplayed(renderer,
                                            renderer->media->object_id);
	}
        renderer->update_playcount_id = 0;
        return FALSE;
}

/**
 * _notify_metadata:
 * @source:   The #MafwSource that sent the metadata results
 * @objectid: The object ID, whose metadata results were received
 * @metadata: GHashTable containing metadata key-value pairs
 * @userdata: Optional user data pointer (self)
 * @error:    Set if any errors occurred during metadata browsing
 *
 * Receives the results of a metadata request.
 */
static void _notify_metadata (MafwSource *cb_source,
			      const gchar *cb_object_id,
			      GHashTable *cb_metadata,
			      gpointer cb_user_data,
			      const GError *cb_error)
{
	MafwGstRenderer *renderer = (MafwGstRenderer*) cb_user_data;
        GError *mafw_error = NULL;
	GError *error = NULL;
	GValue *mval;

	g_return_if_fail(MAFW_IS_GST_RENDERER(renderer));

	g_return_if_fail((renderer->states != 0) &&
			 (renderer->current_state != _LastMafwPlayState) &&
			 (renderer->states[renderer->current_state] != NULL));

	g_debug("running _notify_metadata...");

	mval = mafw_metadata_first(cb_metadata, MAFW_METADATA_KEY_URI);

	if (cb_error == NULL && mval != NULL) {
		mafw_gst_renderer_state_notify_metadata(
			MAFW_GST_RENDERER_STATE(
				renderer->states[renderer->current_state]),
			cb_object_id,
			cb_metadata,
			&error);
	}
	else {
                g_set_error(&mafw_error,
			    MAFW_RENDERER_ERROR,
			    MAFW_RENDERER_ERROR_URI_NOT_AVAILABLE, "%s",
			    cb_error ? cb_error->message : "URI not available");
		mafw_gst_renderer_manage_error(renderer, mafw_error);
                g_error_free(mafw_error);
	}
}

static void _notify_play(MafwGstRendererWorker *worker, gpointer owner)
{
	MafwGstRenderer *renderer = (MafwGstRenderer*) owner;
	GError *error = NULL;

	g_return_if_fail(MAFW_IS_GST_RENDERER(renderer));

	g_return_if_fail((renderer->states != 0) &&
			 (renderer->current_state != _LastMafwPlayState) &&
			 (renderer->states[renderer->current_state] != NULL));

	g_debug("running _notify_play...");

	mafw_gst_renderer_state_notify_play(renderer->states[renderer->current_state],
					  &error);

	if (error != NULL) {
		g_signal_emit_by_name(MAFW_EXTENSION (renderer), "error",
				      error->domain,
				      error->code,
				      error->message);
		g_error_free (error);
	}
}

static void _notify_pause(MafwGstRendererWorker *worker, gpointer owner)
{
	MafwGstRenderer *renderer = (MafwGstRenderer*) owner;
	GError *error = NULL;

	g_return_if_fail(MAFW_IS_GST_RENDERER (renderer));

	g_return_if_fail((renderer->states != 0) &&
			 (renderer->current_state != _LastMafwPlayState) &&
			 (renderer->states[renderer->current_state] != NULL));

	mafw_gst_renderer_state_notify_pause(renderer->states[renderer->current_state],
					   &error);

	if (error != NULL) {
		g_signal_emit_by_name(MAFW_EXTENSION (renderer), "error",
				      error->domain, error->code,
				      error->message);
		g_error_free(error);
	}
}

static void _notify_buffer_status (MafwGstRendererWorker *worker,
				   gpointer owner,
				   gdouble percent)
{
	MafwGstRenderer *renderer = (MafwGstRenderer*) owner;
	GError *error = NULL;

	g_return_if_fail(MAFW_IS_GST_RENDERER(renderer));

	g_return_if_fail((renderer->states != 0) &&
			 (renderer->current_state != _LastMafwPlayState) &&
			 (renderer->states[renderer->current_state] != NULL));

	mafw_gst_renderer_state_notify_buffer_status(
		renderer->states[renderer->current_state],
		percent,
		&error);

	if (error != NULL) {
		g_signal_emit_by_name(MAFW_EXTENSION (renderer), "error",
				      error->domain, error->code,
				      error->message);
		g_error_free(error);
	}
}

static void _notify_seek(MafwGstRendererWorker *worker, gpointer owner)
{
	MafwGstRenderer *renderer = (MafwGstRenderer*) owner;
	GError *error = NULL;

	g_return_if_fail(MAFW_IS_GST_RENDERER(renderer));

	g_return_if_fail((renderer->states != 0) &&
			 (renderer->current_state != _LastMafwPlayState) &&
			 (renderer->states[renderer->current_state] != NULL));

	mafw_gst_renderer_state_notify_seek(renderer->states[renderer->current_state],
					  &error);

	if (error != NULL) {
		g_signal_emit_by_name(MAFW_EXTENSION(renderer), "error",
				      error->domain, error->code,
				      error->message);
		g_error_free(error);
	}
}

static void _playlist_changed_handler(MafwPlaylistIterator *iterator,
				      gboolean clip_changed, GQuark domain,
				      gint code, const gchar *message,
				      gpointer user_data)
{
	MafwGstRenderer *renderer = (MafwGstRenderer*) user_data;

	g_return_if_fail(MAFW_IS_GST_RENDERER(renderer));

	g_return_if_fail((renderer->states != 0) &&
			 (renderer->current_state != _LastMafwPlayState) &&
			 (renderer->states[renderer->current_state] != NULL));

	/* We update the current index and media here,  for this is
	   the same for all the states. Then we delegate in the state
	   to finish the task (for example, start playback if needed) */

	if (renderer->playlist == NULL) {
		g_critical("Got iterator:contents-changed but renderer has no" \
			   "playlist assigned!. Skipping...");
		return;
	}

	if (domain != 0) {
		g_signal_emit_by_name(MAFW_EXTENSION(renderer), "error",
				      domain, code, message);
	} else {
		GError *error = NULL;
		MafwGstRendererPlaybackMode mode;
		
		mode = mafw_gst_renderer_get_playback_mode(renderer);

		/* Only in non-playobject mode */		
		if (clip_changed && mode == MAFW_GST_RENDERER_MODE_PLAYLIST)
			mafw_gst_renderer_set_media_playlist(renderer);

		/* We let the state know if the current clip has changed as
		   result of this operation, so it can do its work */
		mafw_gst_renderer_state_playlist_contents_changed_handler(
			renderer->states[renderer->current_state],
			clip_changed,
			&error);

		if (error != NULL) {
			g_signal_emit_by_name(MAFW_EXTENSION(renderer), "error",
					      error->domain, error->code,
					      error->message);
			g_error_free(error);
		}
	}
}

static void _error_handler(MafwGstRendererWorker *worker, gpointer owner,
			   const GError *error)
{
        MafwGstRenderer *renderer = MAFW_GST_RENDERER(owner);

        mafw_gst_renderer_manage_error(renderer, error);
}

void mafw_gst_renderer_manage_error(MafwGstRenderer *self, const GError *error)
{
        GError *new_err = NULL;
	GError *raise_error = NULL;
        GQuark new_err_domain = MAFW_RENDERER_ERROR;
        gint new_err_code = 0;

	g_return_if_fail(MAFW_IS_GST_RENDERER(self));

	g_return_if_fail((self->states != 0) &&
			 (self->current_state != _LastMafwPlayState) &&
			 (self->states[self->current_state] != NULL));

        /* Get a MAFW error */
	if (error->domain == GST_RESOURCE_ERROR) {
		/* handle RESOURCE errors */
		switch (error->code) {
		case GST_RESOURCE_ERROR_READ:
			if (is_current_uri_stream(self)) {
#ifdef HAVE_CONIC
				if (self->connected) {
					new_err_code = MAFW_RENDERER_ERROR_STREAM_DISCONNECTED;
				} else {
					new_err_domain = MAFW_EXTENSION_ERROR;
					new_err_code = MAFW_EXTENSION_ERROR_NETWORK_DOWN;
				}
#else
				/* Stream + cannot read resource ->
				   disconnected */
				new_err_code = MAFW_RENDERER_ERROR_STREAM_DISCONNECTED;
#endif
			} else {
				/* This shouldn't happen */
				/* Unknown RESOURCE error */
				new_err_domain = MAFW_EXTENSION_ERROR;
				new_err_code = MAFW_EXTENSION_ERROR_FAILED;
			}
			break;
		case GST_RESOURCE_ERROR_NOT_FOUND:
#ifdef HAVE_CONIC
			if (!is_current_uri_stream(self) || self->connected) {
				new_err_code =
					MAFW_RENDERER_ERROR_INVALID_URI;
			} else {
				new_err_domain = MAFW_EXTENSION_ERROR;
				new_err_code = MAFW_EXTENSION_ERROR_NETWORK_DOWN;
			}
#else
			new_err_code =
				MAFW_RENDERER_ERROR_INVALID_URI;
#endif
			break;
		case GST_RESOURCE_ERROR_OPEN_READ_WRITE:
		case GST_RESOURCE_ERROR_OPEN_READ:
#ifdef HAVE_CONIC
			if (!is_current_uri_stream(self) || self->connected) {
				new_err_code =
					MAFW_RENDERER_ERROR_MEDIA_NOT_FOUND;
			} else {
				new_err_domain = MAFW_EXTENSION_ERROR;
				new_err_code = MAFW_EXTENSION_ERROR_NETWORK_DOWN;
			}
#else
			new_err_code =
				MAFW_RENDERER_ERROR_MEDIA_NOT_FOUND;
#endif
			break;
		case GST_RESOURCE_ERROR_NO_SPACE_LEFT:
			new_err_domain = MAFW_EXTENSION_ERROR;
			new_err_code = MAFW_EXTENSION_ERROR_OUT_OF_MEMORY;
			break;
		case GST_RESOURCE_ERROR_WRITE:
            		/* DSP renderers send ERROR_WRITE when they find
			   corrupted data */
			new_err_code = MAFW_RENDERER_ERROR_CORRUPTED_FILE;
			break;
		case GST_RESOURCE_ERROR_SEEK:
			new_err_code = MAFW_RENDERER_ERROR_CANNOT_SET_POSITION;
			break;
		default:
			/* Unknown RESOURCE error */
			new_err_domain = MAFW_EXTENSION_ERROR;
			new_err_code = MAFW_EXTENSION_ERROR_FAILED;
		}

	} else if (error->domain == GST_STREAM_ERROR) {
		/* handle STREAM errors */
		switch (error->code) {
		case GST_STREAM_ERROR_TYPE_NOT_FOUND:
			new_err_code = MAFW_RENDERER_ERROR_TYPE_NOT_AVAILABLE;
			break;
		case GST_STREAM_ERROR_FORMAT:
		case GST_STREAM_ERROR_WRONG_TYPE:
		case GST_STREAM_ERROR_FAILED:
                        new_err_code = MAFW_RENDERER_ERROR_UNSUPPORTED_TYPE;
			break;
		case GST_STREAM_ERROR_DECODE:
		case GST_STREAM_ERROR_DEMUX:
                        new_err_code = MAFW_RENDERER_ERROR_CORRUPTED_FILE;
			break;
		case GST_STREAM_ERROR_CODEC_NOT_FOUND:
			new_err_code = MAFW_RENDERER_ERROR_CODEC_NOT_FOUND;
			break;
		case GST_STREAM_ERROR_DECRYPT:
		case GST_STREAM_ERROR_DECRYPT_NOKEY:
			new_err_code = MAFW_RENDERER_ERROR_DRM;
			break;
		default:
			/* Unknown STREAM error */
                        new_err_domain = MAFW_EXTENSION_ERROR;
                        new_err_code = MAFW_EXTENSION_ERROR_FAILED;
                }
	} else if (error->domain == MAFW_GST_RENDERER_ERROR) {
		/* Handle own errors. Errors that belong to this domain:
		      - MAFW_GST_RENDERER_ERROR_PLUGIN_NOT_FOUND,
		      - MAFW_GST_RENDERER_ERROR_VIDEO_CODEC_NOT_SUPPORTED,
		      - MAFW_GST_RENDERER_ERROR_AUDIO_CODEC_NOT_SUPPORTED */
		new_err_code = MAFW_RENDERER_ERROR_UNSUPPORTED_TYPE;
	} else if (error->domain == MAFW_RENDERER_ERROR) {
		/* Worker may have sent MAFW_RENDERER_ERROR as well.
		   No processing needed */
		new_err_code = error->code;
	} else {
                /* default */
		/* Unknown error */
		new_err_domain = MAFW_EXTENSION_ERROR;
		new_err_code = MAFW_EXTENSION_ERROR_FAILED;
	}

	g_set_error(&new_err, new_err_domain, new_err_code, "%s", error->message);

        _run_error_policy(self, new_err, &raise_error);
        g_error_free(new_err);

        if (raise_error) {
                g_signal_emit_by_name(MAFW_EXTENSION (self), "error",
				      raise_error->domain,
				      raise_error->code,
				      raise_error->message);
                g_error_free(raise_error);
        }
}

static void _notify_eos(MafwGstRendererWorker *worker, gpointer owner)
{
	MafwGstRenderer *renderer = (MafwGstRenderer*) owner;
	GError *error = NULL;

	g_return_if_fail(MAFW_IS_GST_RENDERER (renderer));

	g_return_if_fail((renderer->states != 0) &&
			 (renderer->current_state != _LastMafwPlayState) &&
			 (renderer->states[renderer->current_state] != NULL));

	mafw_gst_renderer_state_notify_eos(renderer->states[renderer->current_state],
					 &error);

	if (error != NULL) {
		g_signal_emit_by_name(MAFW_EXTENSION(renderer), "error",
				      error->domain, error->code,
				      error->message);
		g_error_free(error);
	}
}

/*----------------------------------------------------------------------------
  Status
  ----------------------------------------------------------------------------*/

void mafw_gst_renderer_get_position(MafwRenderer *self, MafwRendererPositionCB callback,
				  gpointer user_data)
{
	MafwGstRenderer *renderer;
	gint pos;

	g_return_if_fail(callback != NULL);
	g_return_if_fail(MAFW_IS_GST_RENDERER(self));
	renderer = MAFW_GST_RENDERER(self);

	pos = mafw_gst_renderer_worker_get_position(renderer->worker);

	/* TODO: Set error if something fails? */
	callback(self, pos, user_data, NULL);
}

void mafw_gst_renderer_get_status(MafwRenderer *self, MafwRendererStatusCB callback,
				gpointer user_data)
{
	MafwGstRenderer* renderer;
	gint index;
	MafwGstRendererPlaybackMode mode;

	g_return_if_fail(MAFW_IS_GST_RENDERER(self));
	g_return_if_fail(callback != NULL);
	renderer = MAFW_GST_RENDERER(self);

	mode = mafw_gst_renderer_get_playback_mode(MAFW_GST_RENDERER(self));
	if ((mode == MAFW_GST_RENDERER_MODE_STANDALONE) || (renderer->iterator == NULL)) {
		index = -1;
	} else {
		index =
			mafw_playlist_iterator_get_current_index(renderer->iterator);
	}

	/* TODO: Set error parameter */
	callback(self, renderer->playlist, index, renderer->current_state,
		 (const gchar*) renderer->media->object_id, user_data, NULL);
}

/*----------------------------------------------------------------------------
  Playlist
  ----------------------------------------------------------------------------*/

static void
_playlist_contents_changed_handler(MafwPlaylist *playlist,
					guint from, guint nremove,
					guint nreplace,
					MafwGstRenderer *renderer)
{
	/* Item(s) added to playlist, so new playable items could come */
	if (nreplace)
		renderer->play_failed_count = 0;
}

gboolean mafw_gst_renderer_assign_playlist(MafwRenderer *self,
					   MafwPlaylist *playlist,
					   GError **error)
{
	MafwGstRenderer* renderer = (MafwGstRenderer*) self;

	g_return_val_if_fail(MAFW_IS_GST_RENDERER(self), FALSE);

	/* Get rid of previously assigned playlist  */
	if (renderer->playlist != NULL) {
		g_signal_handlers_disconnect_matched(renderer->iterator,
						     (GSignalMatchType) G_SIGNAL_MATCH_FUNC,
						     0, 0, NULL,
						     _playlist_changed_handler,
						     NULL);
		g_signal_handlers_disconnect_matched(renderer->playlist,
					(GSignalMatchType) G_SIGNAL_MATCH_FUNC,
					0, 0, NULL,
					G_CALLBACK(_playlist_contents_changed_handler),
					NULL);
		/* Decrement the use count of the previous playlist because the
		   renderer isn't going to use it more */
		mafw_playlist_decrement_use_count(renderer->playlist, NULL);

		g_object_unref(renderer->iterator);
		g_object_unref(renderer->playlist);
	}

	/* Assign the new playlist */
        if (playlist == NULL) {
		renderer->playlist = NULL;
		renderer->iterator = NULL;
	} else {
		GError *new_error = NULL;
		MafwPlaylistIterator *iterator = NULL;

		iterator = mafw_playlist_iterator_new();
		mafw_playlist_iterator_initialize(iterator, playlist,
						   &new_error);

		if (new_error == NULL) {

			renderer->playlist = g_object_ref(playlist);
			renderer->iterator = iterator;

			/* Increment the use_count to avoid the playlist destruction
			   while the playlist is assigned to some renderer */
			mafw_playlist_increment_use_count(renderer->playlist, NULL);

			g_signal_connect(iterator,
					 "playlist-changed",
					 G_CALLBACK(_playlist_changed_handler),
					 renderer);
			g_signal_connect(renderer->playlist,
					 "contents-changed",
					 G_CALLBACK(_playlist_contents_changed_handler),
					 renderer);
		}
		else {
			g_propagate_error (error, new_error);
		}
	}

	/* Set the new media and signal playlist changed signal */
	_signal_playlist_changed(renderer);
	mafw_gst_renderer_set_media_playlist(renderer);


	/* Stop playback */
	mafw_gst_renderer_stop(MAFW_RENDERER(renderer), NULL , NULL);

	return TRUE;
}

MafwGstRendererMovementResult mafw_gst_renderer_move(MafwGstRenderer *renderer,
						   MafwGstRendererMovementType type,
						   guint index,
						   GError **error)
{
	MafwGstRendererMovementResult value = MAFW_GST_RENDERER_MOVE_RESULT_OK;

        if (renderer->playlist == NULL) {
		value = MAFW_GST_RENDERER_MOVE_RESULT_NO_PLAYLIST;
        } else {
		MafwPlaylistIteratorMovementResult result;

		switch (type) {
		case MAFW_GST_RENDERER_MOVE_TYPE_INDEX:
			result =
				mafw_playlist_iterator_move_to_index(renderer->iterator,
								      index,
								      error);
			break;
		case MAFW_GST_RENDERER_MOVE_TYPE_PREV:
			result =
				mafw_playlist_iterator_move_to_prev(renderer->iterator,
								     error);
			break;
		case MAFW_GST_RENDERER_MOVE_TYPE_NEXT:
			result =
				mafw_playlist_iterator_move_to_next(renderer->iterator,
								     error);
			break;
		}

		switch (result) {
		case MAFW_PLAYLIST_ITERATOR_MOVE_RESULT_OK:
			value = MAFW_GST_RENDERER_MOVE_RESULT_OK;
			mafw_gst_renderer_set_media_playlist(renderer);
			break;
		case MAFW_PLAYLIST_ITERATOR_MOVE_RESULT_INVALID:
			g_critical("Iterator is invalid!");
			value = MAFW_GST_RENDERER_MOVE_RESULT_ERROR;
			break;
		case MAFW_PLAYLIST_ITERATOR_MOVE_RESULT_ERROR:
			value = MAFW_GST_RENDERER_MOVE_RESULT_ERROR;
			break;
		case MAFW_PLAYLIST_ITERATOR_MOVE_RESULT_LIMIT:
			value = MAFW_GST_RENDERER_MOVE_RESULT_PLAYLIST_LIMIT;
			break;
		}
	}

	return value;
}

/*----------------------------------------------------------------------------
  Properties
  ----------------------------------------------------------------------------*/

static void _set_error_policy(MafwGstRenderer *renderer, MafwRendererErrorPolicy policy)
{
	renderer->error_policy = policy;
}

static MafwRendererErrorPolicy _get_error_policy(MafwGstRenderer *renderer)
{
	return renderer->error_policy;
}

static void mafw_gst_renderer_get_property(MafwExtension *self,
					 const gchar *key,
					 MafwExtensionPropertyCallback callback,
					 gpointer user_data)
{
	MafwGstRenderer *renderer;
	GValue *value = NULL;
	GError *error = NULL;

	g_return_if_fail(MAFW_IS_GST_RENDERER(self));
	g_return_if_fail(callback != NULL);
	g_return_if_fail(key != NULL);

	renderer = MAFW_GST_RENDERER(self);
	if (!strcmp(key, MAFW_PROPERTY_RENDERER_VOLUME)) {
		guint volume;

		volume = mafw_gst_renderer_worker_get_volume(
			renderer->worker);

		value = g_new0(GValue, 1);
		g_value_init(value, G_TYPE_UINT);
		g_value_set_uint(value, volume);
	}
	else if (!strcmp(key, MAFW_PROPERTY_RENDERER_MUTE)) {
		gboolean mute;
		mute = mafw_gst_renderer_worker_get_mute(renderer->worker);
		value = g_new0(GValue, 1);
		g_value_init(value, G_TYPE_BOOLEAN);
		g_value_set_boolean(value, mute);
	}
	else if (!strcmp (key, MAFW_PROPERTY_RENDERER_XID)) {
		guint xid;
		xid = mafw_gst_renderer_worker_get_xid(renderer->worker);
		value = g_new0(GValue, 1);
		g_value_init(value, G_TYPE_UINT);
		g_value_set_uint(value, xid);
	}
	else if (!strcmp(key, MAFW_PROPERTY_RENDERER_ERROR_POLICY)) {
		guint policy;
		policy = _get_error_policy(renderer);
		value = g_new0(GValue, 1);
		g_value_init(value, G_TYPE_UINT);
		g_value_set_uint(value, policy);
	}
	else if (!strcmp(key, MAFW_PROPERTY_RENDERER_AUTOPAINT)) {
		value = g_new0(GValue, 1);
		g_value_init(value, G_TYPE_BOOLEAN);
		g_value_set_boolean(
			value,
			mafw_gst_renderer_worker_get_autopaint(
				renderer->worker));
	} else if (!strcmp(key, MAFW_PROPERTY_RENDERER_COLORKEY)) {
		value = g_new0(GValue, 1);
		g_value_init(value, G_TYPE_INT);
		g_value_set_int(
			value,
			mafw_gst_renderer_worker_get_colorkey(
				renderer->worker));
	}
#ifdef HAVE_GDKPIXBUF
	else if (!strcmp(key,
			 MAFW_PROPERTY_GST_RENDERER_CURRENT_FRAME_ON_PAUSE)) {
		gboolean current_frame_on_pause;
		current_frame_on_pause =
			mafw_gst_renderer_worker_get_current_frame_on_pause(renderer->worker);
		value = g_new0(GValue, 1);
		g_value_init(value, G_TYPE_BOOLEAN);
		g_value_set_boolean(value, current_frame_on_pause);
	}
#endif
	else if (!strcmp(key,
			 MAFW_PROPERTY_RENDERER_TRANSPORT_ACTIONS)){
		/* Delegate in the state. */
		value = mafw_gst_renderer_state_get_property_value(
			MAFW_GST_RENDERER_STATE(
				renderer->states[renderer->current_state]),
			MAFW_PROPERTY_RENDERER_TRANSPORT_ACTIONS);

		if (!value) {
			/* Something goes wrong. */
			error = g_error_new(
				MAFW_GST_RENDERER_ERROR,
				MAFW_EXTENSION_ERROR_GET_PROPERTY,
				"Error while getting the property value");
		}
	}
	else {
		/* Unsupported property */
		error = g_error_new(MAFW_GST_RENDERER_ERROR,
				    MAFW_EXTENSION_ERROR_GET_PROPERTY,
				    "Unsupported property");
	}

	callback(self, key, value, user_data, error);
}

static void mafw_gst_renderer_set_property(MafwExtension *self,
					 const gchar *key,
					 const GValue *value)
{
	MafwGstRenderer *renderer;

	g_return_if_fail(MAFW_IS_GST_RENDERER(self));
	g_return_if_fail(key != NULL);

	renderer = MAFW_GST_RENDERER(self);

	if (!strcmp(key, MAFW_PROPERTY_RENDERER_VOLUME)) {
		guint volume = g_value_get_uint(value);
		volume = CLAMP(volume, 0, 100);
                mafw_gst_renderer_worker_set_volume(renderer->worker,
							   volume);
                /* Property-changed emision is done by worker */
                return;
	}
	else if (!strcmp(key, MAFW_PROPERTY_RENDERER_MUTE)) {
		gboolean mute = g_value_get_boolean(value);
                mafw_gst_renderer_worker_set_mute(renderer->worker, mute);
	}
	else if (!strcmp(key, MAFW_PROPERTY_RENDERER_XID)) {
		XID xid = g_value_get_uint(value);
		mafw_gst_renderer_worker_set_xid(renderer->worker, xid);
	}
	else if (!strcmp(key, MAFW_PROPERTY_RENDERER_ERROR_POLICY)) {
		MafwRendererErrorPolicy policy = g_value_get_uint(value);
		_set_error_policy(renderer, policy);
	}
	else if (!strcmp(key, MAFW_PROPERTY_RENDERER_AUTOPAINT)) {
		mafw_gst_renderer_worker_set_autopaint(
			renderer->worker,
			g_value_get_boolean(value));
	}
#ifdef HAVE_GDKPIXBUF
	else if (!strcmp(key,
			 MAFW_PROPERTY_GST_RENDERER_CURRENT_FRAME_ON_PAUSE)) {
		gboolean current_frame_on_pause = g_value_get_boolean(value);
		mafw_gst_renderer_worker_set_current_frame_on_pause(renderer->worker,
									   current_frame_on_pause);
	}
#endif
	else return;

	/* FIXME I'm not sure when to emit property-changed signals.
	 * Maybe we should let the worker do it, when the change
	 * reached the hardware... */
	mafw_extension_emit_property_changed(self, key, value);
}

static void _metadata_set_cb(MafwSource *self, const gchar *object_id,
				const gchar **failed_keys, gpointer user_data,
				const GError *error)
{
	if (error != NULL) {
		g_debug("Ignoring error received when setting metadata: "
			"%s (%d): %s", g_quark_to_string(error->domain),
			error->code, error->message);
	} else {
		g_debug("Metadata set correctly");
	}
}

/**
 * _update_playcount_metadata_cb:
 * @cb_source:   The #MafwSource that sent the metadata results
 * @cb_object_id: The object ID, whose metadata results were received
 * @cb_metadata: GHashTable containing metadata key-value pairs
 * @cb_user_data: Optional user data pointer (self)
 * @cb_error:    Set if any errors occurred during metadata browsing
 *
 * Receives the results of a metadata request about the playcount. It increases
 * it, or sets to 1, and sets the metadata to that.
 */
static void _update_playcount_metadata_cb (MafwSource *cb_source,
					   const gchar *cb_object_id,
					   GHashTable *cb_metadata,
					   gpointer cb_user_data,
					   const GError *cb_error)
{
	GValue *curval = NULL;
	gint curplaycount;

	if (cb_error == NULL) {
		if (cb_metadata)
			curval = mafw_metadata_first(cb_metadata,
				MAFW_METADATA_KEY_PLAY_COUNT);
		if (curval && !G_VALUE_HOLDS(curval, G_TYPE_INT))
			return;
		if (curval)
		{
			curplaycount = g_value_get_int(curval);
			curplaycount++;
			g_hash_table_ref(cb_metadata);
			g_value_set_int(curval, curplaycount);
		}
		else
		{ /* Playing at first time, or not supported... */
			cb_metadata = mafw_metadata_new();
			mafw_metadata_add_int(cb_metadata,
						MAFW_METADATA_KEY_PLAY_COUNT,
						1);
		}

		mafw_source_set_metadata(cb_source, cb_object_id, cb_metadata,
						_metadata_set_cb, NULL);
		g_hash_table_unref(cb_metadata);
		return;
	} else {
		g_warning("_playcount_metadata received an error: "
			  "%s (%d): %s", g_quark_to_string(cb_error->domain),
			  cb_error->code, cb_error->message);
	}
}

/**
 * mafw_gst_renderer_update_lastplayed:
 * @self:   Gst renderer
 * @object_id: The object ID of the touched object
 *
 * Sets the MAFW_METADATA_KEY_LAST_PLAYED metadata of the given item.
 */
void mafw_gst_renderer_update_lastplayed(MafwGstRenderer* self,
					const gchar *object_id)
{
	MafwSource* source;
	gchar* sourceid = NULL;

	g_assert(self != NULL);
	g_assert(object_id != NULL);

	/* Attempt to find a source that provided the object ID */
	mafw_source_split_objectid(object_id, &sourceid, NULL);
	source = MAFW_SOURCE(mafw_registry_get_extension_by_uuid(self->registry,
							    sourceid));
	g_free(sourceid);
	if (source != NULL)
	{
		GHashTable *metadata;
		GTimeVal timeval;

		g_get_current_time(&timeval);
		metadata = mafw_metadata_new();
		mafw_metadata_add_long(metadata,
						MAFW_METADATA_KEY_LAST_PLAYED,
						timeval.tv_sec);
		mafw_source_set_metadata(source, object_id, metadata,
						_metadata_set_cb, NULL);
		g_hash_table_unref(metadata);
 	}
}

/**
 * mafw_gst_renderer_increase_playcount:
 * @self:   Gst renderer
 * @object_id: The object ID of the touched object
 *
 * Increases the playcount of the given object.
 */
void mafw_gst_renderer_increase_playcount(MafwGstRenderer* self,
					const gchar *object_id)
{
	MafwSource* source;
	gchar* sourceid = NULL;

	g_assert(self != NULL);
	g_assert(object_id != NULL);

	/* Attempt to find a source that provided the object ID */
	mafw_source_split_objectid(object_id, &sourceid, NULL);
	source = MAFW_SOURCE(mafw_registry_get_extension_by_uuid(self->registry,
							    sourceid));
	g_free(sourceid);
	if (source != NULL)
	{
		static const gchar * const keys[] =
			{ MAFW_METADATA_KEY_PLAY_COUNT, NULL };

		mafw_source_get_metadata(source, object_id,
					 keys,
					 _update_playcount_metadata_cb,
					 NULL);

 	}
}

/* vi: set noexpandtab ts=8 sw=8 cino=t0,(0: */


/*----------------------------------------------------------------------------
  GIO event handlers
  ----------------------------------------------------------------------------*/

static void _pre_unmount_handler(GVolumeMonitor *volume_monitor,
				 GMount *mount,
				 MafwGstRenderer *renderer)
{
	GFile *mount_dir = g_mount_get_root(mount);
	gchar *mount_dir_path = g_file_get_path(mount_dir);
	const gchar *emmc_path = g_getenv("MMC_MOUNTPOINT");

	if (!g_strcmp0(mount_dir_path, emmc_path)) {
		/* External mmc pre-unmount. */
		mafw_gst_renderer_state_handle_pre_unmount(
			MAFW_GST_RENDERER_STATE(
				renderer->states[renderer->current_state]),
			mount_dir_path);
	}

	g_free(mount_dir_path);
	g_object_unref(mount_dir);
}
