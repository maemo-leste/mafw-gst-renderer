/* Small helper element for format conversion
 * (c) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * Portion Copyright © 2009 Nokia Corporation and/or its
 * subsidiary(-ies).* All rights reserved. *
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __BVW_FRAME_CONV_H__
#define __BVW_FRAME_CONV_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef void (*BvwFrameConvCb)(GstSample *result, gpointer user_data);

gboolean bvw_frame_conv_convert (GstSample *sample, GstCaps *to, gboolean xv,
				 BvwFrameConvCb cb, gpointer cb_data);

G_END_DECLS

#endif /* __BVW_FRAME_CONV_H__ */
