/* GStreamer AC4 parser
 * Copyright (C) 2016 LG Electronics, Inc.
 * Author: Dinesh Anand K <dinesh.k@lge.com>
 *         Kumar Vijay Vikram <kumar.vikram@lge.com>
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

#ifndef __GST_AC4_PARSE_H__
#define __GST_AC4_PARSE_H__

#include <gst/gst.h>
#include <gst/base/gstbaseparse.h>

G_BEGIN_DECLS
#define GST_TYPE_AC4_PARSE \
  (gst_ac4_parse_get_type())
#define GST_AC4_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AC4_PARSE, GstAc4Parse))
#define GST_AC4_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AC4_PARSE, GstAc4ParseClass))
#define GST_IS_AC4_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AC4_PARSE))
#define GST_IS_AC4_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AC4_PARSE))
typedef struct _GstAc4Parse GstAc4Parse;
typedef struct _GstAc4ParseClass GstAc4ParseClass;

enum
{
  GST_AC4_PARSE_ALIGN_NONE,
  GST_AC4_PARSE_ALIGN_FRAME,
};

/**
 * GstAc4Parse:
 *
 * The opaque GstAc4Parse object
 */
struct _GstAc4Parse
{
  GstBaseParse baseparse;
  /* AC-4 codec parser variables */
  guint16             n_presentations;
  guint               bitstream_version;
  /* AC-4 parser element variables */
  gboolean            sent_codec_tag;
  gboolean            is_framed;
  gint                sink_cap_ch;
  /* previous frame state variables */
  guint               bsversion;
  gint                sample_rate;
  gint                channels;
  guint               fps_num;
  guint               fps_den;
  GstPadChainFunction baseparse_chainfunc;
};

/**
 * GstAc4ParseClass:
 * @parent_class: Element parent class.
 *
 * The opaque GstAc4ParseClass data structure.
 */
struct _GstAc4ParseClass
{
  GstBaseParseClass baseparse_class;
};

GType gst_ac4_parse_get_type (void);

G_END_DECLS
#endif /* __GST_AC4_PARSE_H__ */
