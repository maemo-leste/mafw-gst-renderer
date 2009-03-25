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

#include "mafw-gst-renderer-utils.h"

#undef  G_LOG_DOMAIN
#define G_LOG_DOMAIN "mafw-gst-renderer-utils"

/**
 * convert_utf8:
 * @src: string.
 * @dst: location for utf8 version of @src.
 *
 * Tries to convert @src into UTF-8, placing it into @dst.
 *
 * Returns: TRUE on success.
 */
gboolean convert_utf8(const gchar *src, gchar **dst)
{
	GError *error;

	if (!src)
		return FALSE;
	if (g_utf8_validate(src, -1, NULL)) {
		*dst = g_strdup(src);
		return TRUE;
	}
	error = NULL;
	*dst = g_locale_to_utf8(src, -1, NULL, NULL, &error);
	if (error) {
		g_warning("utf8 conversion failed '%s' (%d: %s)",
			  src, error->code, error->message);
		g_error_free(error);
		return FALSE;
	}
	return TRUE;
}

gboolean uri_is_playlist(const gchar *uri) {
	/* TODO: Return if the uri is a playlist or not, using the mime type
	   instead of the file extension. */
	if ((g_str_has_suffix(uri, ".pls")) ||
	    (g_str_has_suffix(uri, ".m3u")) ||
	    (g_str_has_suffix(uri, ".smil")) ||
	    (g_str_has_suffix(uri, ".smi")) ||
	    (g_str_has_suffix(uri, ".wpl")) ||
	    (g_str_has_suffix(uri, ".wax")) ||
	    (g_str_has_suffix(uri, ".uni")) ||
	    (g_str_has_suffix(uri, ".ram")) ||
/* 	    (g_str_has_suffix(uri, ".ra")) || */
	    (g_str_has_suffix(uri, ".asx")) ||
	    (g_str_has_suffix(uri, ".rpm")))
		{
			return TRUE;
		}
	return FALSE;
}

/**
 * uri_is_stream:
 * @uri: the URI to be checked.
 *
 * Check if given URI is a stream (not a local resource).  To not depend on
 * gnomevfs for this, we assume everything that doesn't start with "file://" is
 * a stream.
 *
 * Returns: TRUE if the URI is not local.
 */
gboolean uri_is_stream(const gchar *uri)
{
	return !g_str_has_prefix(uri, "file://");
}

/* vi: set noexpandtab ts=8 sw=8 cino=t0,(0: */
