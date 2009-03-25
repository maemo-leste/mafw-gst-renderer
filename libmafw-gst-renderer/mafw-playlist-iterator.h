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

#ifndef MAFW_PLAYLIST_ITERATOR_H
#define MAFW_PLAYLIST_ITERATOR_H

#include <glib-object.h>
#include <libmafw/mafw.h>

G_BEGIN_DECLS

typedef struct _MafwPlaylistIteratorPrivate MafwPlaylistIteratorPrivate;

typedef struct {
	GObject g_object;

	MafwPlaylistIteratorPrivate *priv;
} MafwPlaylistIterator;

typedef struct {
	GObjectClass g_object_class;

	/* Signals */
	void (*playlist_changed)(MafwPlaylistIterator *iterator,
				 gboolean current_item_changed,
				 GQuark domain, gint code, const gchar *message);
} MafwPlaylistIteratorClass;

typedef enum {
	MAFW_PLAYLIST_ITERATOR_MOVE_RESULT_OK,
	MAFW_PLAYLIST_ITERATOR_MOVE_RESULT_LIMIT,
	MAFW_PLAYLIST_ITERATOR_MOVE_RESULT_INVALID,
	MAFW_PLAYLIST_ITERATOR_MOVE_RESULT_ERROR,
} MafwPlaylistIteratorMovementResult;

#define MAFW_TYPE_PLAYLIST_ITERATOR \
        (mafw_playlist_iterator_get_type())
#define MAFW_PLAYLIST_ITERATOR(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj), MAFW_TYPE_PLAYLIST_ITERATOR, MafwPlaylistIterator))
#define MAFW_IS_PLAYLIST_ITERATOR(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj), MAFW_TYPE_PLAYLIST_ITERATOR))
#define MAFW_PLAYLIST_ITERATOR_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass), MAFW_TYPE_PLAYLIST_ITERATOR, MafwPlaylistIterator))
#define MAFW_PLAYLIST_ITERATOR_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS((obj), MAFW_TYPE_PLAYLIST_ITERATOR, \
				   MafwPlaylistIteratorClass))
#define MAFW_IS_PLAYLIST_ITERATOR_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((klass), MAFW_TYPE_PLAYLIST_ITERATOR))

G_END_DECLS

GType mafw_playlist_iterator_get_type(void);
MafwPlaylistIterator *mafw_playlist_iterator_new(void);
void mafw_playlist_iterator_initialize(MafwPlaylistIterator *iterator,
					MafwPlaylist *playlist,
					GError **error);
void mafw_playlist_iterator_invalidate(MafwPlaylistIterator *iterator);
gboolean mafw_playlist_iterator_is_valid(MafwPlaylistIterator *iterator);
void mafw_playlist_iterator_reset(MafwPlaylistIterator *iterator, GError **error);
void mafw_playlist_iterator_move_to_last(MafwPlaylistIterator *iterator, GError **error);
MafwPlaylistIteratorMovementResult mafw_playlist_iterator_move_to_next(MafwPlaylistIterator *iterator,
									 GError **error);
MafwPlaylistIteratorMovementResult mafw_playlist_iterator_move_to_prev(MafwPlaylistIterator *iterator,
									 GError **error);
MafwPlaylistIteratorMovementResult mafw_playlist_iterator_move_to_index(MafwPlaylistIterator *iterator,
									  gint index,
									  GError **error);
void mafw_playlist_iterator_update(MafwPlaylistIterator *iterator, GError **error);
const gchar *mafw_playlist_iterator_get_current_objectid(MafwPlaylistIterator *iterator);
gint mafw_playlist_iterator_get_current_index(MafwPlaylistIterator *iterator);
gint mafw_playlist_iterator_get_size(MafwPlaylistIterator *iterator,
				      GError **error);

#endif
