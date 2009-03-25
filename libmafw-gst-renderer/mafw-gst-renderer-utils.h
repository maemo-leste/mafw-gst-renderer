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
 */
#ifndef MAFW_GST_RENDERER_UTILS_H
#define MAFW_GST_RENDERER_UTILS_H

G_BEGIN_DECLS

gboolean convert_utf8(const gchar *src, gchar **dst);
gboolean uri_is_playlist(const gchar *uri);
gboolean uri_is_stream(const gchar *uri);

G_END_DECLS
#endif
/* vi: set noexpandtab ts=8 sw=8 cino=t0,(0: */
