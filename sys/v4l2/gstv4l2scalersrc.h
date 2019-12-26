/* GStreamer
 *
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *               2006 Edgard Lima <edgard.lima@gmail.com>
 *               2019 LG Electronics, Inc.
 *
 * gstv4l2scalersrc.h: V4L2 scaler source element
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

#ifndef __GST_V4L2_SCALER_SRC_H__
#define __GST_V4L2_SCALER_SRC_H__

#include <gstv4l2scalerobject.h>
#include <gstv4l2bufferpool.h>

G_BEGIN_DECLS

#define GST_TYPE_V4L2_SCALER_SRC \
  (gst_v4l2_scaler_src_get_type())
#define gst_v4l2_scaler_src(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_V4L2_SCALER_SRC,GstV4l2ScalerSrc))
#define gst_v4l2_scaler_src_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_V4L2_SCALER_SRC,GstV4l2ScalerSrcClass))
#define GST_IS_V4L2_SCALER_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_V4L2_SCALER_SRC))
#define GST_IS_V4L2_SCALER_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_V4L2_SCALER_SRC))

typedef struct _GstV4l2ScalerSrc GstV4l2ScalerSrc;
typedef struct _GstV4l2ScalerSrcClass GstV4l2ScalerSrcClass;

/**
 * GstV4l2ScalerSrc:
 *
 * Opaque object.
 */
struct _GstV4l2ScalerSrc
{
  GstPushSrc pushsrc;

  /*< private >*/
  GstV4l2ScalerObject * v4l2scalerobject;

  guint64 offset;

  /* offset adjust after renegotiation */
  guint64 renegotiation_adjust;

  GstClockTime ctrl_time;

  gboolean pending_set_fmt;

  /* Timestamp sanity check */
  GstClockTime last_timestamp;
  gboolean has_bad_timestamp;
};

struct _GstV4l2ScalerSrcClass
{
  GstPushSrcClass parent_class;

  GList *v4l2_class_devices;
};

GType gst_v4l2_scaler_src_get_type (void);

G_END_DECLS

#endif /* __GST_V4L2_SCALER_SRC_H__ */
