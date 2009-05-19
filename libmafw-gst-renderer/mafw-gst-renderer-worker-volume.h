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

#ifndef MAFW_GST_RENDERER_WORKER_VOLUME_H
#define MAFW_GST_RENDERER_WORKER_VOLUME_H

#include <glib.h>

typedef struct _MafwGstRendererWorkerVolume MafwGstRendererWorkerVolume;

typedef void(*MafwGstRendererWorkerVolumeChangedCb)(
	MafwGstRendererWorkerVolume *wvolume, gdouble volume, gpointer data);

typedef void(*MafwGstRendererWorkerVolumeMuteCb)(
	MafwGstRendererWorkerVolume *wvolume, gboolean mute, gpointer data);

typedef void(*MafwGstRendererWorkerVolumeInitCb)(
	MafwGstRendererWorkerVolume *volume, gpointer data);

G_BEGIN_DECLS

void mafw_gst_renderer_worker_volume_init(GMainContext *main_context,
					  MafwGstRendererWorkerVolumeInitCb cb,
					  gpointer user_data,
					  MafwGstRendererWorkerVolumeChangedCb
					  changed_cb,
					  gpointer changed_user_data,
					  MafwGstRendererWorkerVolumeMuteCb
					  mute_cb, gpointer mute_user_data);

void mafw_gst_renderer_worker_volume_set(MafwGstRendererWorkerVolume *wvolume,
					 gdouble volume, gboolean mute);

gdouble mafw_gst_renderer_worker_volume_get(
	MafwGstRendererWorkerVolume *wvolume);
gboolean mafw_gst_renderer_worker_volume_is_muted(
	MafwGstRendererWorkerVolume *wvolume);

void mafw_gst_renderer_worker_volume_destroy(
	MafwGstRendererWorkerVolume *wvolume);

G_END_DECLS
#endif
