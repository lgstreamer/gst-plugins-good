/* GStreamer Wavpack File Source
 * Copyright (C) 2018 Tim-Philipp Müller <tim centricular com>
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

#ifndef __GST_WV_FILE_SRC_H__
#define __GST_WV_FILE_SRC_H__

#include <gst/gstbin.h>

G_BEGIN_DECLS

#define GST_TYPE_WV_FILE_SRC \
  (gst_wv_file_src_get_type())
#define GST_WV_FILE_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WV_FILE_SRC,GstWvFileSrc))
#define GST_WV_FILE_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_WV_FILE_SRC,GstWvFileSrcClass))
#define GST_IS_WV_FILE_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WV_FILE_SRC))
#define GST_IS_WV_FILE_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_WV_FILE_SRC))

typedef struct _GstWvFileSrc GstWvFileSrc;
typedef struct _GstWvFileSrcClass GstWvFileSrcClass;

typedef struct {
  GstElement *filesrc;
  GstElement *typefind;
  GstElement *filter;   /* errorignore or identity */
  GstElement *queue;
  GstPad *srcpad;
  GstUri *uri;
  gboolean ignore_notlinked;
  gchar *stream_id;
  guint group_id;
  GstStream *stream;
} WvFile;

struct _GstWvFileSrc
{
  GstBin parent;

  /*< private > */
  WvFile wv;
  WvFile wvc;

  GstStreamCollection *collection;
  gchar *unique_hash;
};

struct _GstWvFileSrcClass
{
  GstBinClass parent_class;
};

GType gst_wv_file_src_get_type (void);

gboolean gst_wv_file_src_plugin_init (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_WV_FILE_SRC_H__ */
