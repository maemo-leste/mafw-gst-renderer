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

#include "mafw-playlist-iterator.h"
#include "mafw-gst-renderer-marshal.h"

#undef  G_LOG_DOMAIN
#define G_LOG_DOMAIN "mafw-gst-renderer-playlist-iterator"

struct _MafwPlaylistIteratorPrivate {
	MafwPlaylist *playlist;
	gint current_index;
	gchar *current_objectid;
	gint size;
};

typedef gboolean (*movement_function) (MafwPlaylist *playlist,
				       guint *index,
				       gchar **objectid,
				       GError **error);

enum {
	PLAYLIST_CHANGED = 0,
	LAST_SIGNAL,
};

static guint mafw_playlist_iterator_signals[LAST_SIGNAL];

G_DEFINE_TYPE(MafwPlaylistIterator, mafw_playlist_iterator, G_TYPE_OBJECT);

static void
mafw_playlist_iterator_dispose(GObject *object)
{
	MafwPlaylistIterator *iterator = (MafwPlaylistIterator *) object;

	g_return_if_fail(MAFW_IS_PLAYLIST_ITERATOR(iterator));

	mafw_playlist_iterator_invalidate(iterator);

	G_OBJECT_CLASS(mafw_playlist_iterator_parent_class)->dispose(object);
}

static void
mafw_playlist_iterator_class_init(MafwPlaylistIteratorClass *klass)
{
	GObjectClass *gclass = NULL;

	gclass = G_OBJECT_CLASS(klass);
	g_return_if_fail(gclass != NULL);

	g_type_class_add_private(klass, sizeof(MafwPlaylistIteratorPrivate));

	gclass->dispose = mafw_playlist_iterator_dispose;

	mafw_playlist_iterator_signals[PLAYLIST_CHANGED] =
	    g_signal_new("playlist-changed",
			 G_TYPE_FROM_CLASS(klass),
			 G_SIGNAL_RUN_FIRST,
			 G_STRUCT_OFFSET(MafwPlaylistIteratorClass, playlist_changed),
			 NULL,
			 NULL,
			 mafw_gst_renderer_marshal_VOID__BOOLEAN_UINT_INT_STRING,
			 G_TYPE_NONE,
			 4,
			 G_TYPE_BOOLEAN,
			 G_TYPE_UINT, G_TYPE_INT, G_TYPE_STRING);
}

static void
mafw_playlist_iterator_init(MafwPlaylistIterator *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
						 MAFW_TYPE_PLAYLIST_ITERATOR,
						 MafwPlaylistIteratorPrivate);
}

static void
mafw_playlist_iterator_set_data(MafwPlaylistIterator *iterator, gint index,
				 gchar *objectid)
{
	g_assert(mafw_playlist_iterator_is_valid(iterator));

	g_free(iterator->priv->current_objectid);
	iterator->priv->current_index = index;
	iterator->priv->current_objectid = objectid;
}

static MafwPlaylistIteratorMovementResult
mafw_playlist_iterator_move_to_next_in_direction(MafwPlaylistIterator *iterator,
						  movement_function get_next_in_direction,
						  GError **error)
{
	gint index;
	gchar *objectid = NULL;
	GError *new_error = NULL;
	gboolean playlist_movement_result = FALSE;
	MafwPlaylistIteratorMovementResult iterator_movement_result =
		MAFW_PLAYLIST_ITERATOR_MOVE_RESULT_OK;

	g_return_val_if_fail(mafw_playlist_iterator_is_valid(iterator),
			     MAFW_PLAYLIST_ITERATOR_MOVE_RESULT_INVALID);

	index = iterator->priv->current_index;

	playlist_movement_result =
		get_next_in_direction (iterator->priv->playlist,
				       (guint *) &index,
				       &objectid, &new_error);

	if (new_error != NULL) {
		g_propagate_error(error, new_error);
		iterator_movement_result =
			MAFW_PLAYLIST_ITERATOR_MOVE_RESULT_ERROR;
	} else if (playlist_movement_result) {
		mafw_playlist_iterator_set_data(iterator, index, objectid);
	} else {
		iterator_movement_result =
			MAFW_PLAYLIST_ITERATOR_MOVE_RESULT_LIMIT;
	}

	return iterator_movement_result;
}

static void
mafw_playlist_iterator_playlist_contents_changed_handler(MafwPlaylist *playlist,
							  guint from,
							  guint nremove,
							  guint nreplace,
							  gpointer user_data)
{
	gint play_index;
	gboolean clip_changed = FALSE;
	GError *error = NULL;
	MafwPlaylistIterator *iterator = (MafwPlaylistIterator*) user_data;

	g_return_if_fail(MAFW_IS_PLAYLIST(playlist));
	g_return_if_fail(MAFW_IS_PLAYLIST_ITERATOR(iterator));

	iterator->priv->size = -1;

	if (iterator->priv->playlist == NULL) {
		g_critical("Got playlist:contents-changed but renderer has no" \
			   "playlist assigned!. Skipping...");
		return;
	}

	play_index = iterator->priv->current_index;

	if (nremove > 0) {
		/* Items have been removed from the playlist */
		if ((play_index >= from) &&
		    (play_index < from + nremove)) {
			/* The current index has been removed */
			guint pls_size =
				mafw_playlist_iterator_get_size(iterator,
								 &error);
			if (error == NULL) {
				/* Is the current index invalid now? If not,
				   set current item to the last in the playlist,
				   otherwise the keep the index and update the
				   media */
				if (pls_size == 0) {
					mafw_playlist_iterator_set_data(iterator, -1, NULL);
				} else if (play_index >= pls_size) {
					mafw_playlist_iterator_move_to_index(iterator,
									      pls_size - 1,
									      &error);
				} else {
					mafw_playlist_iterator_update(iterator,
								       &error);
				}

				clip_changed = TRUE;
			}
		} else if (from < play_index) {
			/* The current index has been moved towards
			   the head of the playlist */
			play_index -= nremove;
			if (play_index < 0) {
				play_index = 0;
			}
			mafw_playlist_iterator_move_to_index(iterator,
							      play_index,
							      &error);
		}
	} else if (nremove == 0) {
		/* Items have been inserted in the playlist */
		if (play_index == -1) {
			/* First item has been added to an empty playlist */
			mafw_playlist_iterator_reset(iterator,
						      &error);
			clip_changed = TRUE;
		} else if (play_index >= from) {
			/* The current item has been moved towards the
			   tail of the playlist */
			mafw_playlist_iterator_move_to_index(iterator,
							      play_index + nreplace,
							      &error);
		}
	}

	if (error != NULL) {
		g_critical("playlist::contents-changed handler failed "
			   "with \"%s\"", error->message);
		g_signal_emit(iterator,
			      mafw_playlist_iterator_signals[PLAYLIST_CHANGED],
			      0, FALSE, error->domain, error->code, error->message);
		g_error_free (error);
	} else {
		g_signal_emit(iterator,
			      mafw_playlist_iterator_signals[PLAYLIST_CHANGED],
			      0, clip_changed, 0, 0, NULL);
	}
}

static void
mafw_playlist_iterator_playlist_item_moved_handler(MafwPlaylist *playlist,
						    guint from,
						    guint to,
						    gpointer user_data)
{
	MafwPlaylistIterator *iterator = (MafwPlaylistIterator *) user_data;
	gint play_index;
	GError *error = NULL;

	g_return_if_fail(MAFW_IS_PLAYLIST(playlist));
	g_return_if_fail(MAFW_IS_PLAYLIST_ITERATOR(iterator));

	if (iterator->priv->playlist == NULL) {
		g_critical("Got playlist:item-moved but renderer has not a " \
			  "playlist assigned! Skipping...");
		return;
	}

	play_index = iterator->priv->current_index;

	if (play_index == from) {
		/* So the current item has been moved, let's update the
		   the current index to the new location  */
		mafw_playlist_iterator_move_to_index(iterator, to, &error);
	} else if (play_index > from && play_index <= to) {
		/* So we current item  has been pushed one position towards
		   the head, let's update the current index */
		mafw_playlist_iterator_move_to_prev(iterator, &error);
	}  else if (play_index >= to && play_index < from) {
		/* So we current item  has been pushed one position towards
		   the head, let's update the current index */
		mafw_playlist_iterator_move_to_next(iterator, &error);
	}

	if (error != NULL) {
		g_critical("playlist::item-moved handler failed "
			   "with \"%s\"", error->message);
		g_error_free (error);
	}
}

MafwPlaylistIterator *
mafw_playlist_iterator_new(void)
{
	MafwPlaylistIterator *iterator = (MafwPlaylistIterator *)
		g_object_new(MAFW_TYPE_PLAYLIST_ITERATOR, NULL);

	g_assert(iterator != NULL);

	iterator->priv->playlist = NULL;
	iterator->priv->current_index = -1;
	iterator->priv->current_objectid = NULL;
	iterator->priv->size = -1;

	return iterator;
}

void
mafw_playlist_iterator_initialize(MafwPlaylistIterator *iterator,
				   MafwPlaylist *playlist, GError **error)
{
	gint index = -1;
	gchar *objectid = NULL;
	GError *new_error = NULL;

	g_return_if_fail(MAFW_IS_PLAYLIST_ITERATOR(iterator));
	g_return_if_fail(iterator->priv->playlist == NULL);

	iterator->priv->size = -1;

	mafw_playlist_get_starting_index(playlist, (guint *) &index, &objectid,
					  &new_error);

	if (new_error == NULL) {
		iterator->priv->playlist = g_object_ref(playlist);
		iterator->priv->current_index = index;
		iterator->priv->current_objectid = objectid;

		g_signal_connect(playlist,
				 "item-moved",
				 G_CALLBACK(mafw_playlist_iterator_playlist_item_moved_handler),
				 iterator);
		g_signal_connect(playlist,
				 "contents-changed",
				 G_CALLBACK(mafw_playlist_iterator_playlist_contents_changed_handler),
				 iterator);
	}
	else {
		g_propagate_error (error, new_error);
	}
}

void
mafw_playlist_iterator_invalidate(MafwPlaylistIterator *iterator)
{
	g_return_if_fail(MAFW_IS_PLAYLIST_ITERATOR(iterator));

	if (iterator->priv->playlist != NULL) {
		g_signal_handlers_disconnect_matched(iterator->priv->playlist,
						     (GSignalMatchType) G_SIGNAL_MATCH_FUNC,
						     0, 0, NULL,
						     mafw_playlist_iterator_playlist_item_moved_handler,
						     NULL);

		g_signal_handlers_disconnect_matched(iterator->priv->playlist,
						     (GSignalMatchType) G_SIGNAL_MATCH_FUNC,
						     0, 0, NULL,
						     mafw_playlist_iterator_playlist_contents_changed_handler,
						     NULL);

		g_object_unref(iterator->priv->playlist);
		g_free(iterator->priv->current_objectid);
		iterator->priv->playlist = NULL;
		iterator->priv->current_index = -1;
		iterator->priv->current_objectid = NULL;
		iterator->priv->size = -1;
	}
}

gboolean
mafw_playlist_iterator_is_valid(MafwPlaylistIterator *iterator)
{
	g_return_val_if_fail(MAFW_IS_PLAYLIST_ITERATOR(iterator), FALSE);

	return iterator->priv->playlist != NULL;
}

void
mafw_playlist_iterator_reset(MafwPlaylistIterator *iterator, GError **error)
{
	gint index = -1;
	gchar *objectid = NULL;
	GError *new_error = NULL;

	g_return_if_fail(mafw_playlist_iterator_is_valid(iterator));

	mafw_playlist_get_starting_index(iterator->priv->playlist,
					  (guint *) &index,
					  &objectid, &new_error);

	if (new_error == NULL) {
		mafw_playlist_iterator_set_data(iterator, index, objectid);
	}
	else {
		g_propagate_error (error, new_error);
	}
}

void
mafw_playlist_iterator_move_to_last(MafwPlaylistIterator *iterator,
				     GError **error)
{
	GError *new_error = NULL;
	gint index = -1;
	gchar *objectid = NULL;

	g_return_if_fail(mafw_playlist_iterator_is_valid(iterator));

	mafw_playlist_get_last_index(iterator->priv->playlist,
				      (guint *) &index,
				      &objectid, &new_error);

	if (new_error == NULL) {
		mafw_playlist_iterator_set_data(iterator, index, objectid);
	}
	else {
		g_propagate_error (error, new_error);
	}
}

MafwPlaylistIteratorMovementResult
mafw_playlist_iterator_move_to_next(MafwPlaylistIterator *iterator,
				     GError **error)
{
	return  mafw_playlist_iterator_move_to_next_in_direction(iterator,
								  mafw_playlist_get_next,
								  error);
}

MafwPlaylistIteratorMovementResult
mafw_playlist_iterator_move_to_prev(MafwPlaylistIterator *iterator,
				     GError **error)
{
	return  mafw_playlist_iterator_move_to_next_in_direction(iterator,
								  mafw_playlist_get_prev,
								  error);
}

MafwPlaylistIteratorMovementResult
mafw_playlist_iterator_move_to_index(MafwPlaylistIterator *iterator,
				      gint index,
				      GError **error)
{
	GError *new_error = NULL;
	MafwPlaylistIteratorMovementResult iterator_movement_result =
		MAFW_PLAYLIST_ITERATOR_MOVE_RESULT_OK;
	gint playlist_size;

	g_return_val_if_fail(mafw_playlist_iterator_is_valid(iterator),
			     MAFW_PLAYLIST_ITERATOR_MOVE_RESULT_INVALID);

	playlist_size = mafw_playlist_iterator_get_size(iterator, &new_error);

	if (new_error != NULL) {
		g_propagate_error(error, new_error);
		iterator_movement_result =
			MAFW_PLAYLIST_ITERATOR_MOVE_RESULT_ERROR;
	} else if ((index < 0) || (index >= playlist_size)) {
		iterator_movement_result =
			MAFW_PLAYLIST_ITERATOR_MOVE_RESULT_LIMIT;
	} else {
		gchar *objectid =
			mafw_playlist_get_item(iterator->priv->playlist,
						index,
						&new_error);

		if (new_error != NULL) {
			g_propagate_error(error, new_error);
			iterator_movement_result =
				MAFW_PLAYLIST_ITERATOR_MOVE_RESULT_ERROR;
		} else {
			mafw_playlist_iterator_set_data(iterator, index, objectid);
		}
	}

	return iterator_movement_result;
}

void
mafw_playlist_iterator_update(MafwPlaylistIterator *iterator, GError **error)
{
	GError *new_error = NULL;
	gchar *objectid = NULL;

	objectid =
		mafw_playlist_get_item(iterator->priv->playlist,
					iterator->priv->current_index,
					&new_error);

	if (new_error != NULL) {
		g_propagate_error(error, new_error);
	} else {
		mafw_playlist_iterator_set_data(iterator,
						 iterator->priv->current_index,
						 objectid);
	}
}

const gchar *
mafw_playlist_iterator_get_current_objectid(MafwPlaylistIterator *iterator)
{
	g_return_val_if_fail(mafw_playlist_iterator_is_valid(iterator), NULL);

	return iterator->priv->current_objectid;
}

gint
mafw_playlist_iterator_get_current_index(MafwPlaylistIterator *iterator)
{
	g_return_val_if_fail(mafw_playlist_iterator_is_valid(iterator), 0);

	return iterator->priv->current_index;
}

gint
mafw_playlist_iterator_get_size(MafwPlaylistIterator *iterator,
				 GError **error)
{
	g_return_val_if_fail(mafw_playlist_iterator_is_valid(iterator), -1);

	if (iterator->priv->size == -1) {
		iterator->priv->size =
			mafw_playlist_get_size(iterator->priv->playlist,
						error);
	}

	return iterator->priv->size;
}
