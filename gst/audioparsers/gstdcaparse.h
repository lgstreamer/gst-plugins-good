/* GStreamer DCA parser
 * Copyright (C) 2010 Tim-Philipp Müller <tim centricular net>
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

#ifndef __GST_DCA_PARSE_H__
#define __GST_DCA_PARSE_H__

#include <gst/gst.h>
#include <gst/base/gstbaseparse.h>

G_BEGIN_DECLS
#define GST_TYPE_DCA_PARSE \
  (gst_dca_parse_get_type())
#define GST_DCA_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DCA_PARSE, GstDcaParse))
#define GST_DCA_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DCA_PARSE, GstDcaParseClass))
#define GST_IS_DCA_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DCA_PARSE))
#define GST_IS_DCA_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DCA_PARSE))
#define DCA_MIN_FRAMESIZE 96
#define DCA_MAX_FRAMESIZE 18725 /* 16384*16/14 */
#define DTS_SYNCWORD_CORE_BE_16B           0x7FFE8001
#define DTS_SYNCWORD_CORE_LE_16B           0xFE7F0180
#define DTS_SYNCWORD_CORE_BE_14B           0x1FFFE800
#define DTS_SYNCWORD_CORE_LE_14B           0xFF1F00E8
#define DTS_SYNCWORD_XCH                   0x5A5A5A5A
#define DTS_SYNCWORD_XXCH                  0x47004A03
#define DTS_SYNCWORD_X96K                  0x1D95F262
#define DTS_SYNCWORD_XBR                   0x655E315E
#define DTS_SYNCWORD_LBR                   0x0A801921
#define DTS_SYNCWORD_XLL                   0x41A29547
#define DTS_SYNCWORD_SUBSTREAM             0x64582025
#define DTS_SYNCWORD_SUBSTREAM_CORE        0x02b09261
typedef struct _GstDcaParse GstDcaParse;
typedef struct _GstDcaParseClass GstDcaParseClass;

/**
 * GstDcaParse:
 *
 * The opaque GstDcaParse object
 */
struct _GstDcaParse
{
  GstBaseParse baseparse;

  /*< private > */
  gint rate;
  gint channels;
  gint depth;
  gint endianness;
  gint block_size;
  gint frame_size;

  gboolean              sent_codec_tag;

  guint32 last_sync;
  gint dts_stream_type;
  const gchar *mime_type;

  GstPadChainFunction baseparse_chainfunc;
};

/**
 * GstDcaParseClass:
 * @parent_class: Element parent class.
 *
 * The opaque GstDcaParseClass data structure.
 */
struct _GstDcaParseClass
{
  GstBaseParseClass baseparse_class;
};

GType gst_dca_parse_get_type (void);

G_END_DECLS
#endif /* __GST_DCA_PARSE_H__ */
