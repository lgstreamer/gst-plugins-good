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
#include <gst/base/base.h>

#ifndef GST_WAVPACK_CORRECTION_COMBINER_H
#define GST_WAVPACK_CORRECTION_COMBINER_H

#define GST_TYPE_WVC_COMBINER            (gst_wvc_combiner_get_type ())
#define GST_WVC_COMBINER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_WVC_COMBINER, GstWvcCombiner))
#define GST_WVC_COMBINER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_WVC_COMBINER, GstWvcCombinerClass))
#define GST_WVC_COMBINER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_WVC_COMBINER, GstWvcCombinerClass))

typedef struct _GstWvcCombiner GstWvcCombiner;
typedef struct _GstWvcCombinerClass GstWvcCombinerClass;

struct _GstWvcCombiner
{
  GstAggregator aggregator;

  /*< private >*/
  GstPad *wv_sink;
  GstPad *wvc_sink;
};

struct _GstWvcCombinerClass
{
  GstAggregatorClass aggregator_class;
};

GType     gst_wvc_combiner_get_type (void);

gboolean  gst_wvc_combiner_plugin_init (GstPlugin * plugin);

#endif /* GST_WAVPACK_CORRECTION_COMBINER_H */
