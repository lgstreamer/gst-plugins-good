/* GStreamer
 *
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *               2006 Edgard Lima <edgard.lima@gmail.com>
 *               2019 LG Electronics, Inc.
 *
 * gstv4l2scalerobject.h: base class for LG's V4L2 scaler elements
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

#ifndef __GST_V4L2_SCALER_OBJECT_H__
#define __GST_V4L2_SCALER_OBJECT_H__

#include <gstv4l2object.h>

typedef struct _GstV4l2ScalerObject GstV4l2ScalerObject;
typedef struct _GstV4l2ScalerObjectClassHelper GstV4l2ScalerObjectClassHelper;

struct _GstV4l2ScalerObject {
  GstV4l2Object parent;

  gint vdec_index;
  guint max_width;
  guint max_height;
  gboolean scalable;
  GstCaps *destination_caps;
  gint vdo_fd;
  guint input_width;
  guint input_height;
};

struct _GstV4l2ScalerObjectClassHelper {
  GstV4l2ObjectClassHelper parent;
};

GType gst_v4l2_scaler_object_get_type (void);

/* create/destroy */
GstV4l2ScalerObject*  gst_v4l2_scaler_object_new    (GstElement * element,
                                                     GstObject * dbg_obj,
                                                     enum v4l2_buf_type  type,
                                                     const char * default_device,
                                                     GstV4l2GetInOutFunction get_in_out_func,
                                                     GstV4l2SetInOutFunction set_in_out_func,
                                                     GstV4l2UpdateFpsFunction update_fps_func);

void         gst_v4l2_scaler_object_destroy         (GstV4l2ScalerObject * v4l2scalerobject);

GstCaps*    gst_v4l2_scaler_object_get_caps        (GstV4l2ScalerObject * v4l2scalerobject, GstCaps * filter);

gboolean gst_v4l2_scaler_object_decide_allocation   (GstV4l2ScalerObject * v4l2scalerobject,
                                                     GstQuery * query);

GstFlowReturn gst_v4l2_scaler_object_change_resolution (GstV4l2Object * v4l2object);

G_END_DECLS

#endif /* __GST_V4L2_SCALER_OBJECT_H__ */
