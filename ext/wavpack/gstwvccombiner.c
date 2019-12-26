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

/**
 * SECTION:element-wvccombiner
 *
 * This element combines a lossily encoded WavPack stream with the matching
 * correction file so that the decoder can restore the original lossless output.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 filesrc location=test_suite/hybrid_bitrates/128kbps.wv
 *  ! wavpackparse ! wvccombiner name=combiner ! wavpackdec ! pulsesink
 *  filesrc location=test_suite/hybrid_bitrates/128kbps.wvc ! wavpackparse ! combiner.wvc_sink
 * ]| This pipeline combines the correction data from the .wvc file with the
 * lossily compressed wavpack data from the .wv file and feeds both to the
 * wavpack decoder which will decode the stream and restore the original
 * lossless audio data using the correction data on top of the lossily
 * compressed audio data.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwvccombiner.h"
#include "gstwvcmeta.h"

/* we only extract the fields we need here */
typedef struct
{
  guint16 version;
  guint64 index;
  guint32 samples;              /* 0 = non-audio block */
  enum
  {
    MODE_LOSSLESS,
    MODE_HYBRID
  } mode;
} BlockHeader;

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-wavpack(meta:GstWVCorrection), "
        "depth = (int) [ 1, 32 ], "
        "channels = (int) [ 1, 8 ], "
        "rate = (int) [ 6000, 192000 ], " "framed = (boolean) true;"));

static GstStaticPadTemplate wv_sink_template =
    GST_STATIC_PAD_TEMPLATE ("wv_sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-wavpack, "
        "depth = (int) [ 1, 32 ], "
        "channels = (int) [ 1, 8 ], "
        "rate = (int) [ 6000, 192000 ], " "framed = (boolean) true;"));

static GstStaticPadTemplate wvc_sink_template =
    GST_STATIC_PAD_TEMPLATE ("wvc_sink", GST_PAD_SINK, GST_PAD_REQUEST,
    GST_STATIC_CAPS ("audio/x-wavpack-correction, framed = (boolean) true;"));

GST_DEBUG_CATEGORY_STATIC (gst_wvc_combiner_debug);
#define GST_CAT_DEFAULT gst_wvc_combiner_debug

G_DEFINE_TYPE (GstWvcCombiner, gst_wvc_combiner, GST_TYPE_AGGREGATOR);

static GstFlowReturn gst_wvc_combiner_aggregate (GstAggregator * agg,
    gboolean timeout);
static GstAggregatorPad *gst_wvc_combiner_create_new_pad (GstAggregator * agg,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps);

static void
gst_wvc_combiner_class_init (GstWvcCombinerClass * klass)
{
  GstAggregatorClass *aggregator_class = (GstAggregatorClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;

  gst_element_class_add_static_pad_template_with_gtype (element_class,
      &src_template, GST_TYPE_AGGREGATOR_PAD);

  gst_element_class_add_static_pad_template (element_class, &wv_sink_template);
  gst_element_class_add_static_pad_template (element_class, &wvc_sink_template);

  gst_element_class_set_static_metadata (element_class, "WavPack Combiner",
      "Codec/Combiner/Audio", "WavPack Correction Stream Combiner",
      "Tim-Philipp Müller <tim@centricular.com>");

  aggregator_class->aggregate = gst_wvc_combiner_aggregate;
  aggregator_class->create_new_pad = gst_wvc_combiner_create_new_pad;
}

static void
gst_wvc_combiner_init (GstWvcCombiner * combiner)
{
  GstAggregator *aggregator = GST_AGGREGATOR_CAST (combiner);
  GstPadTemplate *templ = gst_static_pad_template_get (&wv_sink_template);

  combiner->wv_sink = g_object_new (GST_TYPE_AGGREGATOR_PAD,
      "name", "wv_sink", "direction", GST_PAD_SINK, "template", templ, NULL);
  gst_object_unref (templ);

  gst_element_add_pad (GST_ELEMENT (combiner), combiner->wv_sink);

  gst_segment_init (&GST_AGGREGATOR_PAD (aggregator->srcpad)->segment,
      GST_FORMAT_TIME);
}

static GstAggregatorPad *
gst_wvc_combiner_create_new_pad (GstAggregator * aggregator,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
  GstWvcCombiner *combiner = GST_WVC_COMBINER (aggregator);
  const gchar *templ_name = GST_PAD_TEMPLATE_NAME_TEMPLATE (templ);

  if (g_strcmp0 (templ_name, "wvc_sink") != 0) {
    GST_ERROR_OBJECT (combiner, "Unexpected pad template %s", templ_name);
    return NULL;
  }

  GST_OBJECT_LOCK (combiner);

  if (combiner->wvc_sink != NULL) {
    GST_ERROR_OBJECT (combiner, "Pad for template %s already exists, can only "
        "have one", templ_name);
    GST_OBJECT_UNLOCK (combiner);
    return NULL;
  }

  combiner->wvc_sink = g_object_new (GST_TYPE_AGGREGATOR_PAD,
      "name", templ_name, "direction", GST_PAD_SINK, "template", templ, NULL);

  GST_OBJECT_UNLOCK (combiner);

  return GST_AGGREGATOR_PAD_CAST (combiner->wvc_sink);
}

static gboolean
gst_wvc_combiner_parse_block_header (GstAggregatorPad * pad, BlockHeader * hdr,
    GstBuffer * buf)
{
  GstMapInfo map = GST_MAP_INFO_INIT;
  GstByteReader br;
  gboolean ret = FALSE;
  guint32 u32, flags;
  guint8 u8;

  if (!gst_buffer_map (buf, &map, GST_MAP_READ))
    return FALSE;

  if (map.size < 32)
    goto out;

  gst_byte_reader_init (&br, map.data, map.size);
  gst_byte_reader_skip (&br, 4 + 4);    /* id + size */
  hdr->version = gst_byte_reader_get_uint16_le_unchecked (&br);
  u8 = gst_byte_reader_get_uint8_unchecked (&br);
  gst_byte_reader_skip (&br, 1 + 4);    /* total_samples */
  u32 = gst_byte_reader_get_uint32_le_unchecked (&br);
  hdr->index = (((guint64) u8) << 32) | u32;
  hdr->samples = gst_byte_reader_get_uint32_le_unchecked (&br);
  flags = gst_byte_reader_get_uint32_le_unchecked (&br);
  hdr->mode = ((flags & 0x08)) ? MODE_HYBRID : MODE_LOSSLESS;

  GST_LOG_OBJECT (pad, "Block: index %" G_GINT64_MODIFIER "u, "
      "samples %u, mode %s, flags 0x%08x", hdr->index, hdr->samples,
      hdr->mode == MODE_HYBRID ? "hybrid" : "lossless", flags);

  ret = TRUE;

out:

  gst_buffer_unmap (buf, &map);
  return ret;
}

static GstFlowReturn
gst_wvc_combiner_aggregate (GstAggregator * aggregator, gboolean timeout)
{
  GstWvcCombiner *combiner = GST_WVC_COMBINER (aggregator);
  GstAggregatorPad *wv_sink = GST_AGGREGATOR_PAD (combiner->wv_sink);
  GstAggregatorPad *wvc_sink = NULL;
  GstBuffer *buf, *wvc_buf;
  BlockHeader hdr = { 0, };

  if (combiner->wvc_sink != NULL)
    wvc_sink = GST_AGGREGATOR_PAD (combiner->wvc_sink);

  if (gst_aggregator_pad_is_eos (wv_sink)) {
    if (wvc_sink != NULL && !gst_aggregator_pad_is_eos (wvc_sink)) {
      GST_WARNING_OBJECT (combiner, "Have more correction data, but main "
          "stream is already EOS, very unexpected!");
      gst_aggregator_pad_drop_buffer (wvc_sink);
    }
    return GST_FLOW_EOS;
  }

  buf = gst_aggregator_pad_pop_buffer (wv_sink);
  GST_LOG_OBJECT (wv_sink, "buffer %" GST_PTR_FORMAT, buf);

  if (!gst_wvc_combiner_parse_block_header (wv_sink, &hdr, buf)) {
    GST_WARNING_OBJECT (wv_sink, "Couldn't parse wavpack header from buffer");
    gst_buffer_unref (buf);
    return GST_FLOW_OK;
  }

  /* No need for correction data in lossless mode */
  if (hdr.mode == MODE_LOSSLESS)
    goto finish;

  /* Check if block contains audio data at all. If not we just push it out
   * without combining it with data from the correction stream, as the
   * correction stream won't have matching blocks for any non-audio blocks. */
  if (hdr.samples == 0) {
    GST_DEBUG_OBJECT (wv_sink, "Buffer has no audio data");
    goto finish;
  }

  /* Do we have correction data? */
  if (wvc_sink != NULL && gst_aggregator_pad_has_buffer (wvc_sink)) {
    BlockHeader wvc_hdr = { 0, };

    wvc_buf = gst_aggregator_pad_pop_buffer (wvc_sink);
    GST_LOG_OBJECT (wvc_sink, "buffer %" GST_PTR_FORMAT, wvc_buf);
    if (gst_wvc_combiner_parse_block_header (wvc_sink, &wvc_hdr, wvc_buf)) {
      if (wvc_hdr.index == hdr.index) {
        gst_buffer_add_wvc_meta (buf, wvc_buf);
      } else {
        GST_WARNING_OBJECT (wvc_sink, "Correction data offset mismatch");
      }
    } else {
      GST_WARNING_OBJECT (wvc_sink, "Couldn't parse wavpack header");
    }
    gst_buffer_unref (wvc_buf);
  }

finish:

  return gst_aggregator_finish_buffer (aggregator, buf);
}

gboolean
gst_wvc_combiner_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_wvc_combiner_debug, "wvccombiner", 0,
      "Wavpack correction data combiner");

  /* FIXME: set non-zero rank? */
  return gst_element_register (plugin, "wvccombiner", GST_RANK_SECONDARY,
      GST_TYPE_WVC_COMBINER);
}
