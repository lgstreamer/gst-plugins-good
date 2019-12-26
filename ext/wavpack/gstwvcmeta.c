/* GStreamer Wavpack correction stream combiner
 * Copyright (c) 2018 Tim-Philipp Müller <tim@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the wvc_metaied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwvcmeta.h"

GST_DEBUG_CATEGORY_EXTERN (wavpack_debug);
#define GST_CAT_DEFAULT wavpack_debug

GType
gst_wvc_meta_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] = { NULL };        /* FIXME: review tags */

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstWVCMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static gboolean
gst_wvc_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstWVCMeta *wvc_meta = (GstWVCMeta *) meta;

  wvc_meta->wvc_buf = NULL;
  return TRUE;
}

static void
gst_wvc_meta_clear (GstMeta * meta, GstBuffer * buffer)
{
  GstWVCMeta *wvc_meta = (GstWVCMeta *) meta;

  gst_buffer_replace (&wvc_meta->wvc_buf, NULL);
}

static gboolean
gst_wvc_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstWVCMeta *smeta;
  GstWVCMeta *dmeta;

  smeta = (GstWVCMeta *) meta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    dmeta = gst_buffer_add_wvc_meta (dest, smeta->wvc_buf);
    if (!dmeta)
      return FALSE;
  } else {
    return FALSE;
  }

  return TRUE;
}

const GstMetaInfo *
gst_wvc_meta_get_info (void)
{
  static const GstMetaInfo *wvc_meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & wvc_meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_WVC_META_API_TYPE, "GstWVCMeta",
        sizeof (GstWVCMeta), gst_wvc_meta_init, gst_wvc_meta_clear,
        gst_wvc_meta_transform);
    g_once_init_leave ((GstMetaInfo **) & wvc_meta_info, (GstMetaInfo *) meta);
  }
  return wvc_meta_info;
}

/* Add Wavpack correction data to a buffer */
GstWVCMeta *
gst_buffer_add_wvc_meta (GstBuffer * buffer, GstBuffer * wvc_buf)
{
  GstWVCMeta *wvc_meta;
  GstWVCMeta *meta;

  g_return_val_if_fail (wvc_buf != NULL, NULL);

  if (wvc_buf == NULL)
    return NULL;

  meta = (GstWVCMeta *) gst_buffer_add_meta (buffer, GST_WVC_META_INFO, NULL);

  GST_LOG ("Adding %u bytes of WVC data to buffer %p",
      (guint) gst_buffer_get_size (wvc_buf), buffer);

  wvc_meta = (GstWVCMeta *) meta;
  wvc_meta->wvc_buf = gst_buffer_ref (wvc_buf);

  return meta;
}

GstWVCMeta *
gst_buffer_get_wvc_meta (GstBuffer * buffer)
{
  gpointer state = NULL;
  GstMeta *m;

  m = gst_buffer_iterate_meta_filtered (buffer, &state, GST_WVC_META_API_TYPE);

  return (GstWVCMeta *) m;
}
