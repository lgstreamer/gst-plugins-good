/* GStreamer Wavpack correction stream combiner
 * Copyright (c) 2018 Tim-Philipp Müller <tim@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <gst/gst.h>

#ifndef GST_WVC_META_H
#define GST_WVC_META_H

/* private meta shared between combiner and decoder */
typedef struct {
  /*< private >*/
  GstMeta meta;
  GstBuffer *wvc_buf;
} GstWVCMeta;

G_GNUC_INTERNAL GType gst_wvc_meta_get_type (void);
G_GNUC_INTERNAL GType gst_wvc_meta_api_get_type (void);
G_GNUC_INTERNAL const GstMetaInfo * gst_wvc_meta_get_info (void);

#define GST_WVC_META_API_TYPE  (gst_wvc_meta_api_get_type())
#define GST_WVC_META_INFO      (gst_wvc_meta_get_info())

/* Add Wavpack correction data to a buffer */
G_GNUC_INTERNAL
GstWVCMeta * gst_buffer_add_wvc_meta (GstBuffer * buffer, GstBuffer * wvc_buf);

G_GNUC_INTERNAL
GstWVCMeta * gst_buffer_get_wvc_meta (GstBuffer * buffer);

#endif /* GST_WVC_META_H */
