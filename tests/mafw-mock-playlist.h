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

#ifndef MAFW_MOCK_PLAYLIST_H
#define MAFW_MOCK_PLAYLIST_H

#include <glib-object.h>
#include <libmafw/mafw-playlist.h>
#include <libmafw/mafw-errors.h>

/*----------------------------------------------------------------------------
  GObject type conversion macros
  ----------------------------------------------------------------------------*/

#define MAFW_TYPE_MOCK_PLAYLIST \
	(mafw_mock_playlist_get_type())

#define MAFW_MOCK_PLAYLIST(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST(obj, MAFW_TYPE_MOCK_PLAYLIST, \
				    MafwMockPlaylist))

#define MAFW_MOCK_PLAYLIST_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST(klass, MAFW_TYPE_MOCK_PLAYLIST, \
				 MafwMockPlaylistClass))

#define MAFW_IS_MOCK_PLAYLIST(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE(obj, MAFW_TYPE_MOCK_PLAYLIST))

#define MAFW_IS_MOCK_PLAYLIST_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_TYPE((klass), MAFW_TYPE_MOCK_PLAYLIST))

#define MAFW_MOCK_PLAYLIST_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS((obj), MAFW_TYPE_MOCK_PLAYLIST, \
				   MafwMockPlaylistClass))

/*----------------------------------------------------------------------------
  GObject type definitions
  ----------------------------------------------------------------------------*/

typedef struct _MafwMockPlaylist MafwMockPlaylist;
typedef struct _MafwMockPlaylistClass MafwMockPlaylistClass;


struct _MafwMockPlaylist
{
	GObject parent_instance;

};

struct _MafwMockPlaylistClass
{
	GObjectClass parent_class;

};

/*----------------------------------------------------------------------------
  Shared playlist-specific functions
  ----------------------------------------------------------------------------*/

GType mafw_mock_playlist_get_type(void);
GObject *mafw_mock_playlist_new(void);

#endif
