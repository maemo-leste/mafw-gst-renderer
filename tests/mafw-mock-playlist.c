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

#include <stdlib.h>
#include <string.h>

#include <libmafw/mafw-playlist.h>
#include "mafw-mock-playlist.h"

static GList *pl_list;
static gchar *pl_name;
static gboolean pl_rep;
static gboolean pl_shuffle;

/* Item manipulation */

static gboolean mafw_mock_playlist_insert_item(MafwPlaylist *playlist,
						     guint index,
						     const gchar *objectid,
						     GError **error);

static gboolean mafw_mock_playlist_remove_item(MafwPlaylist *playlist,
						     guint index,
						     GError **error);

static gchar *mafw_mock_playlist_get_item(MafwPlaylist *playlist,
					       	guint index, GError **error);

static gboolean mafw_mock_playlist_move_item(MafwPlaylist *playlist,
						   guint from, guint to,
						   GError **error);

static guint mafw_mock_playlist_get_size(MafwPlaylist *playlist,
					       GError **error);

static gboolean mafw_mock_playlist_clear(MafwPlaylist *playlist,
					       GError **error);

static gboolean mafw_mock_playlist_increment_use_count(MafwPlaylist *playlist,
							GError **error);

static gboolean mafw_mock_playlist_decrement_use_count(MafwPlaylist *playlist,
							GError **error);
gboolean mafw_mock_playlist_get_prev(MafwPlaylist *playlist, guint *index,
        			gchar **object_id, GError **error);
gboolean mafw_mock_playlist_get_next(MafwPlaylist *playlist, guint *index,
        			gchar **object_id, GError **error);
static void mafw_mock_playlist_get_starting_index(MafwPlaylist *playlist, guint *index,
        				gchar **object_id, GError **error);
static void mafw_mock_playlist_get_last_index(MafwPlaylist *playlist,
					       guint *index, gchar **object_id,
					       GError **error);

enum {
	PROP_0,
	PROP_NAME,
	PROP_REPEAT,
	PROP_IS_SHUFFLED,
};

static void set_prop(MafwMockPlaylist *playlist, guint prop,
		     const GValue *value, GParamSpec *spec)
{
	if (prop == PROP_NAME) {
		pl_name = g_value_dup_string(value);
	} else if (prop == PROP_REPEAT) {
		pl_rep = g_value_get_boolean(value);
	} else
		G_OBJECT_WARN_INVALID_PROPERTY_ID(playlist, prop, spec);
}

static void get_prop(MafwMockPlaylist *playlist, guint prop,
		     GValue *value, GParamSpec *spec)
{
	if (prop == PROP_NAME) {
		g_value_take_string(value, pl_name);
	} else if (prop == PROP_REPEAT) {
		g_value_set_boolean(value, pl_rep);
	} else if (prop == PROP_IS_SHUFFLED) {
		g_value_set_boolean(value, pl_shuffle);
	} else
		G_OBJECT_WARN_INVALID_PROPERTY_ID(playlist, prop, spec);
}

static void mafw_mock_playlist_get_starting_index(MafwPlaylist *playlist, guint *index,
        				gchar **object_id, GError **error)
{
	if (g_list_length(pl_list) > 0) {
		*index = 0;
		*object_id = g_strdup(g_list_nth_data(pl_list, 0));
	}
}

static void mafw_mock_playlist_get_last_index(MafwPlaylist *playlist,
					       guint *index, gchar **object_id,
					       GError **error)
{
	*index = g_list_length(pl_list) - 1;
	*object_id = g_strdup(g_list_nth_data(pl_list, *index));
}


gboolean mafw_mock_playlist_get_next(MafwPlaylist *playlist, guint *index,
        			gchar **object_id, GError **error)
{
	gint size;
	gboolean return_value = TRUE;

	size = g_list_length(pl_list);
	
	g_return_val_if_fail(size != 0, FALSE);

	if (*index == (size - 1)) {
		return_value = FALSE;
	} else {
		*object_id = g_strdup(g_list_nth_data(pl_list, ++(*index)));
	}
	
	return return_value;
}

gboolean mafw_mock_playlist_get_prev(MafwPlaylist *playlist, guint *index,
        			gchar **object_id, GError **error)
{
	gint size;
	gboolean return_value = TRUE;

	size = g_list_length(pl_list);
	
	g_return_val_if_fail(size != 0, FALSE);

	if (*index == 0) {
		return_value = FALSE;
	} else {
		*object_id = g_strdup(g_list_nth_data(pl_list, --(*index)));
	}

	return return_value;
}

static void playlist_iface_init(MafwPlaylistIface *iface)
{
	iface->get_item = mafw_mock_playlist_get_item;
	iface->insert_item = mafw_mock_playlist_insert_item;
	iface->clear = mafw_mock_playlist_clear;
	iface->get_size = mafw_mock_playlist_get_size;
	iface->remove_item = mafw_mock_playlist_remove_item;
	iface->move_item = mafw_mock_playlist_move_item;
	iface->get_starting_index = mafw_mock_playlist_get_starting_index;
	iface->get_last_index = mafw_mock_playlist_get_last_index;
	iface->get_next = mafw_mock_playlist_get_next;
	iface->get_prev = mafw_mock_playlist_get_prev;
	iface->increment_use_count = mafw_mock_playlist_increment_use_count;
	iface->decrement_use_count = mafw_mock_playlist_decrement_use_count;
}


static void mafw_mock_playlist_finalize(GObject *object)
{
	g_debug(__FUNCTION__);
	
	while (pl_list)
	{
		g_free(pl_list->data);
		pl_list = g_list_delete_link(pl_list, pl_list);
	}
	
}

static void mafw_mock_playlist_class_init(
					MafwMockPlaylistClass *klass)
{
	GObjectClass *oclass = NULL;

	oclass = G_OBJECT_CLASS(klass);

	oclass->set_property = (gpointer)set_prop;
	oclass->get_property = (gpointer)get_prop;
	g_object_class_override_property(oclass, PROP_NAME, "name");
	g_object_class_override_property(oclass, PROP_REPEAT, "repeat");
	g_object_class_override_property(oclass,
					 PROP_IS_SHUFFLED, "is-shuffled");
	
	oclass -> finalize = mafw_mock_playlist_finalize;
}

static void mafw_mock_playlist_init(MafwMockPlaylist *self)
{
}



G_DEFINE_TYPE_WITH_CODE(MafwMockPlaylist, mafw_mock_playlist,
			G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(MAFW_TYPE_PLAYLIST,
					      playlist_iface_init));


GObject *mafw_mock_playlist_new(void)
{
	MafwMockPlaylist *self;

	self = g_object_new(MAFW_TYPE_MOCK_PLAYLIST, NULL);
	
	return G_OBJECT(self);
}

gboolean mafw_mock_playlist_insert_item(MafwPlaylist *self, guint index,
					      const gchar *objectid,
					      GError **error)
{
	gint size;

	size = g_list_length(pl_list);

	pl_list = g_list_insert(pl_list, g_strdup(objectid), index);

	g_signal_emit_by_name(self, "contents-changed", index, 0,
			      size == index ? 0 : 1);
	
	return TRUE;
}

gboolean mafw_mock_playlist_remove_item(MafwPlaylist *self, guint index,
					 GError **error)
{
	GList *element;

	g_return_val_if_fail(g_list_length(pl_list) > 0, FALSE);

	element = g_list_nth(pl_list, index);
	g_free(element->data);
	pl_list = g_list_delete_link(pl_list, element);

	g_signal_emit_by_name(self, "contents-changed", index, 1, 0);

	return TRUE;
}

gchar *mafw_mock_playlist_get_item(MafwPlaylist *self, guint index,
					 GError **error)
{
	gchar *oid = g_list_nth_data(pl_list, index);
	
	if (oid)
		oid = g_strdup(oid);
	
	return oid;
}

guint mafw_mock_playlist_get_size(MafwPlaylist *self, GError **error)
{
	return g_list_length(pl_list);
}

static gboolean mafw_mock_playlist_move_item(MafwPlaylist *playlist,
						   guint from, guint to,
						   GError **error)
{
	GList *element_from, *element_to;
	gpointer data;
	gint size;

	size = g_list_length(pl_list);

	g_return_val_if_fail(size > 0, FALSE);
	g_return_val_if_fail(from != to, FALSE);
	g_return_val_if_fail((from < size) && (to < size), FALSE);

	element_from = g_list_nth(pl_list, from);
	element_to = g_list_nth(pl_list, to);

	data = element_from->data;
	element_from->data = element_to->data;
	element_to->data = data;

	g_signal_emit_by_name(playlist, "item-moved", from, to);

	return TRUE;
}

static gboolean mafw_mock_playlist_increment_use_count(MafwPlaylist *playlist,
							GError **error)
{
	return TRUE;
}

static gboolean mafw_mock_playlist_decrement_use_count(MafwPlaylist *playlist,
							 GError **error)
{
	return TRUE;	
}

gboolean mafw_mock_playlist_clear(MafwPlaylist *self, GError **error)
{
	mafw_mock_playlist_finalize(NULL);
	
	return TRUE;
}

/* vi: set noexpandtab ts=8 sw=8 cino=t0,(0: */
