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

/**
 * SECTION:element-dcaparse
 * @short_description: DCA (DTS Coherent Acoustics) parser
 * @see_also: #GstAmrParse, #GstAACParse, #GstAc3Parse
 *
 * This is a DCA (DTS Coherent Acoustics) parser.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 filesrc location=abc.dts ! dcaparse ! dtsdec ! audioresample ! audioconvert ! autoaudiosink
 * ]|
 * </refsect2>
 */

/* TODO:
 *  - should accept framed and unframed input (needs decodebin fixes first)
 *  - seeking in raw .dts files doesn't seem to work, but duration estimate ok
 *
 *  - if frames have 'odd' durations, the frame durations (plus timestamps)
 *    aren't adjusted up occasionally to make up for rounding error gaps.
 *    (e.g. if 512 samples per frame @ 48kHz = 10.666666667 ms/frame)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstdcaparse.h"
#include <gst/base/base.h>
#include <gst/pbutils/pbutils.h>

GST_DEBUG_CATEGORY_STATIC (dca_parse_debug);
#define GST_CAT_DEFAULT dca_parse_debug

enum _dts_profile
{
  DTS_INVALID = -1,
  DTS_CORE = 1,
  DTS_DTSH,
  DTS_DTSL,
  DTS_DTSE
} dts_profile;

enum _dts_stream_type
{
  DTS_NONE = 0,
  DTS_CORE_ONLY,
  DTS_CORE_PLUS_EXT_SUB,
  DTS_EXT_SUB_ONLY
} dts_stream_type;


static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-dts,"
        " framed = (boolean) true,"
        " channels = (int) [ 1, 9 ],"
        " rate = (int) [ 8000, 192000 ],"
        " depth = (int) { 14, 16 },"
        " endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
        " block-size = (int) [ 1, MAX], " " frame-size = (int) [ 1, MAX];"
        "audio/x-dtsh,"
        " framed = (boolean) true,"
        " channels = (int) [ 1, 9 ],"
        " rate = (int) [ 8000, 192000 ],"
        " depth = (int) { 14, 16 },"
        " endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
        " block-size = (int) [ 1, MAX], " " frame-size = (int) [ 1, MAX];"
        "audio/x-dtsl,"
        " framed = (boolean) true,"
        " channels = (int) [ 1, 9 ],"
        " rate = (int) [ 8000, 192000 ],"
        " depth = (int) { 14, 16 },"
        " endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
        " block-size = (int) [ 1, MAX], " " frame-size = (int) [ 1, MAX];"
        "audio/x-dtse,"
        " framed = (boolean) true,"
        " channels = (int) [ 1, 9 ],"
        " rate = (int) [ 8000, 192000 ],"
        " depth = (int) { 14, 16 },"
        " endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
        " block-size = (int) [ 1, MAX], " " frame-size = (int) [ 1, MAX];"));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-dts; " "audio/x-dtsh; " "audio/x-dtsl; "
        "audio/x-dtse; " "audio/x-private1-dts"));

static void gst_dca_parse_finalize (GObject * object);

static gboolean gst_dca_parse_start (GstBaseParse * parse);
static gboolean gst_dca_parse_stop (GstBaseParse * parse);
static GstFlowReturn gst_dca_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize);
static GstFlowReturn gst_dca_parse_pre_push_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame);
static GstCaps *gst_dca_parse_get_sink_caps (GstBaseParse * parse,
    GstCaps * filter);
static gboolean gst_dca_parse_set_sink_caps (GstBaseParse * parse,
    GstCaps * caps);

static gboolean
gst_dca_parse_dtsc_header_parse (GstDcaParse * dcaparse,
    const GstByteReader * reader, guint * frame_size,
    guint * sample_rate, guint * channels, guint * depth,
    gint * endianness, guint * num_blocks, guint * samples_per_block,
    gboolean * terminator);

static gboolean
gst_dca_parse_header_extsstreams (GstDcaParse * dcaparse,
    const GstByteReader * reader, guint * ext_size, guint * sample_rate,
    guint * channels, guint * depth, gint * endianness, guint * num_blocks,
    guint * samples_per_block, gboolean * terminator);

static gboolean
gst_dca_parse_find_sync_core (GstDcaParse * dcaparse, GstByteReader * reader,
    gsize bufsize, guint32 * best_sync, guint * best_offset);

static gboolean
gst_dca_parse_find_sync_extsstream (GstDcaParse * dcaparse,
    GstByteReader * reader, gsize bufsize, guint32 * best_sync,
    guint * best_offset);

#define gst_dca_parse_parent_class parent_class
G_DEFINE_TYPE (GstDcaParse, gst_dca_parse, GST_TYPE_BASE_PARSE);

static void
gst_dca_parse_class_init (GstDcaParseClass * klass)
{
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (dca_parse_debug, "dcaparse", 0,
      "DCA audio stream parser");

  object_class->finalize = gst_dca_parse_finalize;

  parse_class->start = GST_DEBUG_FUNCPTR (gst_dca_parse_start);
  parse_class->stop = GST_DEBUG_FUNCPTR (gst_dca_parse_stop);
  parse_class->handle_frame = GST_DEBUG_FUNCPTR (gst_dca_parse_handle_frame);
  parse_class->pre_push_frame =
      GST_DEBUG_FUNCPTR (gst_dca_parse_pre_push_frame);
  parse_class->get_sink_caps = GST_DEBUG_FUNCPTR (gst_dca_parse_get_sink_caps);
  parse_class->set_sink_caps = GST_DEBUG_FUNCPTR (gst_dca_parse_set_sink_caps);

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "DTS Coherent Acoustics audio stream parser", "Codec/Parser/Audio",
      "DCA parser", "Tim-Philipp Müller <tim centricular net>");
}

static void
gst_dca_parse_reset (GstDcaParse * dcaparse)
{
  dcaparse->channels = -1;
  dcaparse->rate = -1;
  dcaparse->depth = -1;
  dcaparse->endianness = -1;
  dcaparse->block_size = -1;
  dcaparse->frame_size = -1;
  dcaparse->last_sync = 0;
  dcaparse->sent_codec_tag = FALSE;
  dcaparse->dts_stream_type = DTS_NONE;
  dcaparse->mime_type = NULL;
}

static void
gst_dca_parse_init (GstDcaParse * dcaparse)
{
  gst_base_parse_set_min_frame_size (GST_BASE_PARSE (dcaparse),
      DCA_MIN_FRAMESIZE);
  gst_dca_parse_reset (dcaparse);
  dcaparse->baseparse_chainfunc =
      GST_BASE_PARSE_SINK_PAD (GST_BASE_PARSE (dcaparse))->chainfunc;

  GST_PAD_SET_ACCEPT_INTERSECT (GST_BASE_PARSE_SINK_PAD (dcaparse));
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_BASE_PARSE_SINK_PAD (dcaparse));
}

static void
gst_dca_parse_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_dca_parse_start (GstBaseParse * parse)
{
  GstDcaParse *dcaparse = GST_DCA_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "starting");

  gst_dca_parse_reset (dcaparse);

  return TRUE;
}

static gboolean
gst_dca_parse_stop (GstBaseParse * parse)
{
  GST_DEBUG_OBJECT (parse, "stopping");

  return TRUE;
}

/**
 * gst_dca_parse_parse_header:
 * @dcaparse: #GstDcaParse.
 * @reader: instance of GstByteReader
 * @frame_size: Size of frame in bytes
 * @sample_rate: Audio sample rate
 * @channels: Num of channels
 * @depth: Bit depth
 * @endianness: Endianness of bitstream
 * @num_blocks: Num of blocks
 * @samples_per_block: Samples in a block
 * @terminator: frame termination flag
 *
 * Based on dts stream type, parse the header and set the frame parameters.
 *
 * Returns: TRUE if parsed successfully.
 */
static gboolean
gst_dca_parse_parse_header (GstDcaParse * dcaparse,
    const GstByteReader * reader, guint * frame_size,
    guint * sample_rate, guint * channels, guint * depth,
    gint * endianness, guint * num_blocks, guint * samples_per_block,
    gboolean * terminator)
{
  guint32 core_size = 0, ext_size = 0;
  GstByteReader r = *reader;

  switch (dcaparse->dts_stream_type) {

    case DTS_CORE_ONLY:
      if (!gst_dca_parse_dtsc_header_parse (dcaparse, &r, &core_size,
              sample_rate, channels, depth, endianness, num_blocks,
              samples_per_block, terminator)) {
        GST_LOG_OBJECT (dcaparse, "Valid core header not found");
        return FALSE;
      }
      break;

    case DTS_CORE_PLUS_EXT_SUB:
      if (!gst_dca_parse_dtsc_header_parse (dcaparse, &r, &core_size,
              sample_rate, channels, depth, endianness, num_blocks,
              samples_per_block, terminator)) {
        GST_LOG_OBJECT (dcaparse,
            "Valid core header not found, parse extension");
      }
      r = *reader;
      gst_byte_reader_skip_unchecked (&r, core_size);
      if (!gst_dca_parse_header_extsstreams (dcaparse, &r, &ext_size,
              sample_rate, channels, depth, endianness,
              num_blocks, samples_per_block, terminator)) {
        GST_LOG_OBJECT (dcaparse,
            "Valid extension header not found, return core only if valid");
        if (core_size == 0 && ext_size == 0)
          return FALSE;
      }
      break;

    case DTS_EXT_SUB_ONLY:
      if (!gst_dca_parse_header_extsstreams (dcaparse, &r, &ext_size,
              sample_rate, channels, depth, endianness,
              num_blocks, samples_per_block, terminator)) {
        GST_LOG_OBJECT (dcaparse, "Valid extension header not found");
        return FALSE;
      }
      break;
    default:
      GST_LOG_OBJECT (dcaparse, "Valid stream format not found");
      return FALSE;
      break;
  }

  *frame_size = core_size + ext_size;
  GST_DEBUG_OBJECT (dcaparse,
      "frame_size(%d) core_size(%d) ext_size (%d)", *frame_size,
      core_size, ext_size);
  return TRUE;
}

/**
 * gst_dca_parse_dtsc_header_parse:
 * @dcaparse: #GstDcaParse.
 * @reader: instance of GstByteReader
 * @core_size: Size of core data in bytes
 * @sample_rate: Audio sample rate
 * @channels: Num of channels
 * @depth: Bit depth
 * @endianness: Endianness of bitstream
 * @num_blocks: Num of blocks
 * @samples_per_block: Samples in a block
 * @terminator: frame termination flag
 *
 * Parse the DTS core header and set the frame parameters.
 *
 * Returns: TRUE if parsed successfully.
 */
static gboolean
gst_dca_parse_dtsc_header_parse (GstDcaParse * dcaparse,
    const GstByteReader * reader, guint * frame_size,
    guint * sample_rate, guint * channels, guint * depth,
    gint * endianness, guint * num_blocks, guint * samples_per_block,
    gboolean * terminator)
{
  static const int sample_rates[16] = { 0, 8000, 16000, 32000, 0, 0, 11025,
    22050, 44100, 0, 0, 12000, 24000, 48000, 96000, 192000
  };
  static const guint8 channels_table[16] = { 1, 2, 2, 2, 2, 3, 3, 4, 4, 5,
    6, 6, 6, 7, 8, 8
  };
  GstByteReader r = *reader;
  guint16 hdr[8];
  guint32 marker;
  guint chans, lfe, i;
  if (gst_byte_reader_get_remaining (&r) < (4 + sizeof (hdr)))
    return FALSE;

  marker = gst_byte_reader_peek_uint32_be_unchecked (&r);

  /* raw big endian or 14-bit big endian */
  if (marker == 0x7FFE8001 || marker == 0x1FFFE800) {
    for (i = 0; i < G_N_ELEMENTS (hdr); ++i)
      hdr[i] = gst_byte_reader_get_uint16_be_unchecked (&r);
  } else
    /* raw little endian or 14-bit little endian */
  if (marker == 0xFE7F0180 || marker == 0xFF1F00E8) {
    for (i = 0; i < G_N_ELEMENTS (hdr); ++i)
      hdr[i] = gst_byte_reader_get_uint16_le_unchecked (&r);
  } else
    /* Sync word for core Sub stream Extension 0x02b09261 */
  if (marker == 0x02b09261) {
    for (i = 0; i < G_N_ELEMENTS (hdr); ++i)
      hdr[i] = gst_byte_reader_get_uint16_be_unchecked (&r);
  } else {
    return FALSE;
  }

  GST_LOG_OBJECT (dcaparse, "dts sync marker 0x%08x at offset %u", marker,
      gst_byte_reader_get_pos (reader));

  /* 14-bit mode */
  if (marker == 0x1FFFE800 || marker == 0xFF1F00E8) {
    if ((hdr[2] & 0xFFF0) != 0x07F0)
      return FALSE;
    /* discard top 2 bits (2 void), shift in 2 */
    hdr[0] = (hdr[0] << 2) | ((hdr[1] >> 12) & 0x0003);
    /* discard top 4 bits (2 void, 2 shifted into hdr[0]), shift in 4 etc. */
    hdr[1] = (hdr[1] << 4) | ((hdr[2] >> 10) & 0x000F);
    hdr[2] = (hdr[2] << 6) | ((hdr[3] >> 8) & 0x003F);
    hdr[3] = (hdr[3] << 8) | ((hdr[4] >> 6) & 0x00FF);
    hdr[4] = (hdr[4] << 10) | ((hdr[5] >> 4) & 0x03FF);
    hdr[5] = (hdr[5] << 12) | ((hdr[6] >> 2) & 0x0FFF);
    hdr[6] = (hdr[6] << 14) | ((hdr[7] >> 0) & 0x3FFF);
    g_assert (hdr[0] == 0x7FFE && hdr[1] == 0x8001);
  }

  GST_LOG_OBJECT (dcaparse, "frame header: %04x%04x%04x%04x",
      hdr[2], hdr[3], hdr[4], hdr[5]);

  *terminator = (hdr[2] & 0x80) ? FALSE : TRUE;
  *samples_per_block = ((hdr[2] >> 10) & 0x1f) + 1;
  *num_blocks = ((hdr[2] >> 2) & 0x7F) + 1;
  *frame_size = (((hdr[2] & 0x03) << 12) | (hdr[3] >> 4)) + 1;
  chans = ((hdr[3] & 0x0F) << 2) | (hdr[4] >> 14);
  *sample_rate = sample_rates[(hdr[4] >> 10) & 0x0F];
  lfe = (hdr[5] >> 9) & 0x03;

  GST_LOG_OBJECT (dcaparse, "frame_size %u, num_blocks %u, rate %u, "
      "samples per block %u", *frame_size, *num_blocks, *sample_rate,
      *samples_per_block);

  if (*num_blocks < 6 || *frame_size < 96 || *sample_rate == 0)
    return FALSE;

  if (chans < G_N_ELEMENTS (channels_table))
    *channels = channels_table[chans] + ((lfe) ? 1 : 0);
  else
    return FALSE;

  if (depth)
    *depth = (marker == 0x1FFFE800 || marker == 0xFF1F00E8) ? 14 : 16;
  if (endianness)
    *endianness = (marker == 0xFE7F0180 || marker == 0xFF1F00E8) ?
        G_LITTLE_ENDIAN : G_BIG_ENDIAN;

  GST_LOG_OBJECT (dcaparse, "frame_size %u, channels %u, rate %u, "
      "num_blocks %u, samples_per_block %u", *frame_size, *channels,
      *sample_rate, *num_blocks, *samples_per_block);

  return TRUE;
}

/**
 * gst_dca_parse_ext_header:
 * @dcaparse: #GstDcaParse.
 * @reader: instance of GstByteReader
 * @header_size: Ext sub stream header size
 * @ext_frame_size: Ext sub stream size
 *
 * Parse Extension Substream Header to find the header size and frame size.
 *
 * Returns: TRUE if parsed successfully.
 */
static gboolean
gst_dca_parse_ext_header (GstDcaParse * dcaparse,
    const GstByteReader * reader, guint16 * header_size, guint * ext_frame_size,
    guint8 * nExtSSIndex)
{
  GstByteReader r = *reader;
  guint16 hdr[8], nuExtSSHeaderSize;
  guint32 nuExtSSFSize;
  guint8 i;
  gboolean bHeaderSizeType;
  gboolean bStaticFieldsPresent;
  static const guint RefClockPeriod[3] = { 32000, 44100, 48000 };
  guint8 nuRefClockCode;
  guint32 nuExSSFrameDurationCode;
  GstClockTime ExSSFrameDuration = GST_CLOCK_TIME_NONE;
  GstBaseParse *parse = &(dcaparse->baseparse);
  guint32 marker = 0;
  if (!gst_byte_reader_peek_uint32_be (reader, &marker)) {
    GST_LOG_OBJECT (dcaparse, "Fail to find the marker");
    return FALSE;
  }

  /* raw big endian or 14-bit big endian */
  if (marker == DTS_SYNCWORD_SUBSTREAM) {
    for (i = 0; i < G_N_ELEMENTS (hdr); ++i)
      hdr[i] = gst_byte_reader_get_uint16_be_unchecked (&r);
  } else {
    GST_LOG_OBJECT (dcaparse, "Fail to find the Extension Substream syncword");
    return FALSE;
  }

  GST_LOG_OBJECT (dcaparse, "dts sync marker 0x%08x at offset %u", marker,
      gst_byte_reader_get_pos (reader));

  GST_LOG_OBJECT (dcaparse, "frame header: %04x%04x%04x%04x",
      hdr[2], hdr[3], hdr[4], hdr[5]);

  *nExtSSIndex = (hdr[2] & 0x00c0) >> 6;

  /* bHeaderSizeType is 1 bit at 5th position  */
  bHeaderSizeType = hdr[2] & 0x0020;
  if (bHeaderSizeType == 0) {
    /*Extract next 8-bit if bHeaderSizeType is 0 */
    nuExtSSHeaderSize =
        (((hdr[2] & 0x001f) << 3) | ((hdr[3] & 0xe000) >> 13)) + 1;
    /*Extract next 16 bit for Ext Frame size */
    nuExtSSFSize = ((hdr[3] & 0x1fff) << 3 | ((hdr[4] & 0xe000) >> 13)) + 1;
    /* Extract StaticFields */
    bStaticFieldsPresent = (hdr[4] & 0x1000) >> 12;
    if (bStaticFieldsPresent) {
      nuRefClockCode = (hdr[4] & 0x0c00) >> 10;
      if (nuRefClockCode < 3) {
        nuExSSFrameDurationCode = 512 * (((hdr[4] & 0x0380) >> 7) + 1);
        ExSSFrameDuration =
            gst_util_uint64_scale (GST_SECOND, nuExSSFrameDurationCode,
            RefClockPeriod[nuRefClockCode]);

        GST_LOG_OBJECT (dcaparse,
            "nuRefClockCode: (%u), nuExSSFrameDurationCode: (%u), ExSSFrameDur: %"
            G_GINT64_FORMAT "s", nuRefClockCode, nuExSSFrameDurationCode,
            ExSSFrameDuration);
      } else {
        GST_LOG_OBJECT (dcaparse, "nuRefClockCode is %u in invalid range",
            nuRefClockCode);
      }
    }

  } else {
    /*Extract next 12 bits if bHeaderSizeType is 1 */
    nuExtSSHeaderSize =
        (((hdr[2] & 0x001f) << 7) | ((hdr[3] & 0xfe00) >> 9)) + 1;
    /*Extract next 20 bits for Ext Frame size */
    nuExtSSFSize =
        (((hdr[3] & 0x000001ff) << 11) | ((hdr[4] & 0x0000ffe0) >> 5)) + 1;
    /* Extract StaticFields */
    bStaticFieldsPresent = (hdr[4] & 0x10) >> 4;
    if (bStaticFieldsPresent) {
      nuRefClockCode = (hdr[4] & 0x0c) >> 2;
      if (nuRefClockCode < 3) {
        nuExSSFrameDurationCode =
            512 * (((hdr[4] & 0x03) | ((hdr[5] & 0x8000) >> 15)) + 1);
        ExSSFrameDuration =
            gst_util_uint64_scale (GST_SECOND, nuExSSFrameDurationCode,
            RefClockPeriod[nuRefClockCode]);

        GST_LOG_OBJECT (dcaparse,
            "nuRefClockCode: (%u), nuExSSFrameDurationCode: (%u), ExSSFrameDur: %"
            G_GINT64_FORMAT "s", nuRefClockCode, nuExSSFrameDurationCode,
            ExSSFrameDuration);
      } else {
        GST_LOG_OBJECT (dcaparse, "nuRefClockCode is %u in invalid range",
            nuRefClockCode);
      }
    }
  }
  if (bStaticFieldsPresent && (nuRefClockCode < 3))
    gst_base_parse_set_frame_rate (parse, RefClockPeriod[nuRefClockCode],
        nuExSSFrameDurationCode, 0, 0);

  GST_LOG_OBJECT (dcaparse,
      "nuExtSSHeaderSize(%04x) nuExtSSFSize(%08x) nExtSSIndex(%u)",
      nuExtSSHeaderSize, nuExtSSFSize, *nExtSSIndex);

  *header_size = nuExtSSHeaderSize;
  *ext_frame_size = nuExtSSFSize;

  return TRUE;
}

/**
 * gst_dca_parse_header_dtsl:
 * @dcaparse: #GstDcaParse.
 * @reader: instance of GstByteReader
 * @sample_rate: Audio sample rate
 * @channels: Num of channels
 * @depth: Bit depth
 * @endianness: Endianness of bitstream
 * @num_blocks: Num of blocks
 * @samples_per_block: Samples in a block
 * @terminator: frame termination flag
 *
 * Parse the DTS Lossless Extension header and set the frame parameters.
 *
 * Returns: TRUE if parsed successfully.
 */
static gboolean
gst_dca_parse_header_dtsl (GstDcaParse * dcaparse,
    GstByteReader * reader, guint * sample_rate,
    guint * channels, guint * depth, gint * endianness, guint * num_blocks,
    guint * samples_per_block, gboolean * terminator)
{
  static const int sample_rates[16] = { 8000, 16000, 32000, 64000, 128000,
    22050, 44100, 88200, 176400, 352800,
    12000, 24000, 48000, 96000, 192000, 384000
  };

  guint16 hdr[16];
  guint i, bit_depth, byte_order;
  guint nTempSize, nLLFrameSize, nChSetLLChannel = 0;
  guint8 nHeaderSize, nBits4FrameFsize, nHeaderIndex = 0, nBitIndex = 0;
  guint16 sFreqIndex = 0;
  guint nFirst = 0, nSecond = 0;
  guint32 marker = gst_byte_reader_peek_uint32_be_unchecked (reader);


  /* raw big endian */
  if (marker == DTS_SYNCWORD_XLL) {
    for (i = 0; i < G_N_ELEMENTS (hdr); ++i)
      hdr[i] = gst_byte_reader_get_uint16_be_unchecked (reader);
  } else {
    return FALSE;
  }

  GST_DEBUG_OBJECT (dcaparse, "dts sync marker 0x%08x at offset %u", marker,
      gst_byte_reader_get_pos (reader));

  nHeaderSize = ((hdr[2] & 0x0FF0) >> 4) + 1;
  nBits4FrameFsize = (((hdr[2] & 0x000F) << 1) | ((hdr[3] & 0x8000) >> 15)) + 1;

  nTempSize =
      ((hdr[3] & 0x7FFF) << 17) | (hdr[4] << 1) | ((hdr[5] & 0x8000) >> 15);

  nLLFrameSize =
      ((nTempSize & (((1L << nBits4FrameFsize) - 1) << (32 -
                  nBits4FrameFsize))) >> (32 - nBits4FrameFsize)) + 1;
  nHeaderIndex = 3 + ((nBits4FrameFsize + 1) / 16);
  nBitIndex = ((1 + nBits4FrameFsize) % 16);
  if ((nBitIndex + 4) >= 16) {
    nSecond = (nBitIndex + 4) - 16;
    nFirst = 4 - nSecond;
    nBitIndex = nSecond;
    ++nHeaderIndex;
  } else {
    nBitIndex += 4;
  }

  /* Jump to parse sub-header */
  /* Using a 16 bit reader, hence need to skip half the size */
  nHeaderIndex = (nHeaderSize / 2);
  if ((nHeaderIndex + 1) >= 16) {
    /* To fix klocworks defect. Actually we need to use only till 12th element at max */
    GST_INFO_OBJECT (dcaparse, "Array out bound");
  } else {
    if ((nHeaderSize % 2) == 0) {
      nChSetLLChannel = ((hdr[nHeaderIndex] & 0x003C) >> 2) + 1;

      /* Skip 10 + 4 + nChSetLLChannel +  5 + 5 */
      nHeaderIndex += ((10 + 4 + nChSetLLChannel + 5 + 5) / 16);
      nBitIndex = ((10 + 4 + nChSetLLChannel + 5 + 5) % 16);
    } else {
      nChSetLLChannel = ((hdr[nHeaderIndex + 1] & 0x3C00) >> 10) + 1;
      /* Skip 8 + 10 + 4 + nChSetLLChannel +  5 + 5 */
      nHeaderIndex += ((18 + 4 + nChSetLLChannel + 5 + 5) / 16);
      nBitIndex = ((18 + 4 + nChSetLLChannel + 5 + 5) % 16);
    }
    if ((nHeaderIndex + 1) >= 16) {
      /* To fix klocworks defect. Actually we need to use only till 12th element at max */
      GST_INFO_OBJECT (dcaparse, "Array out bound");
    } else {
      if ((nBitIndex + 4) >= 16) {
        nSecond = (nBitIndex + 4) - 16;
        nFirst = 4 - nSecond;
        sFreqIndex = (((hdr[nHeaderIndex] & ((1L << nFirst) - 1)) << nSecond)
            | ((hdr[nHeaderIndex + 1] & (((1L << nSecond) - 1) << (16 -
                            nSecond))) >> (16 - nSecond)));
      } else
        sFreqIndex =
            ((hdr[nHeaderIndex] & (((1 << 4) - 1) << (16 - 4 -
                        nBitIndex))) >> (16 - 4 - nBitIndex));
    }
  }

  bit_depth = 16;
  byte_order = G_BIG_ENDIAN;

  if (depth)
    *depth = 16;
  if (endianness)
    *endianness = G_BIG_ENDIAN;
  *sample_rate = sample_rates[sFreqIndex];
  if (*sample_rate < 16000)
    *samples_per_block = 1024;
  else if (*sample_rate < 32000)
    *samples_per_block = 2048;
  else
    *samples_per_block = 4096;
  *channels = nChSetLLChannel;
  *num_blocks = 0;              /*Default */

  GST_LOG_OBJECT (dcaparse,
      "After parsing, values are nLLHeaderSize = %d\tnBits4FrameFsize=%d\tnLLFrameSize=%d\t\
       nChSetLLChannel=%d\tsFreqIndex=%d\tdepth=%d\tendianness=%d",
      nHeaderSize, nBits4FrameFsize, nLLFrameSize, nChSetLLChannel, sFreqIndex, bit_depth, byte_order);

  return TRUE;

}

/**
 * gst_dca_parse_header_lbr:
 * @dcaparse: #GstDcaParse.
 * @reader: instance of GstByteReader
 * @sample_rate: Audio sample rate
 * @channels: Num of channels
 * @depth: Bit depth
 * @endianness: Endianness of bitstream
 * @num_blocks: Num of blocks
 * @samples_per_block: Samples in a block
 * @terminator: frame termination flag
 *
 * Parse the DTS Low bit rate extension header and set the frame parameters.
 *
 * Returns: TRUE if parsed successfully.
 */
static gboolean
gst_dca_parse_header_lbr (GstDcaParse * dcaparse,
    GstByteReader * reader, guint * sample_rate,
    guint * channels, guint * depth, gint * endianness, guint * num_blocks,
    guint * samples_per_block, gboolean * terminator)
{

  static const int sample_rates[16] = { 8000, 16000, 32000, 0, 0,
    22050, 44100, 0, 0, 0,
    12000, 24000, 48000, 0, 0, 0
  };
  static const int speaker_mask[16] = { 1, 2, 2, 1, 1, 2, 2, 1,
    1, 2, 2, 2, 1, 2, 1, 2
  };
  guint i;
  guint16 hdr[8];
  guint32 marker;

  guint8 ucFmtInfoCode, nLBRSampleRateCode, nLBRBitRateMSnybbles;
  guint16 nLBROriginalBitRate_LSW, nLBRScaledBitRate_LSW, usLBRSpkrMask;
  guint32 nuOriginatBitrate, nuScaledBitrate;

  marker = gst_byte_reader_peek_uint32_be_unchecked (reader);
  /* raw big endian */
  if (marker == DTS_SYNCWORD_LBR) {
    for (i = 0; i < G_N_ELEMENTS (hdr); ++i)
      hdr[i] = gst_byte_reader_get_uint16_be_unchecked (reader);
  } else {
    return FALSE;
  }
  GST_DEBUG_OBJECT (dcaparse, "lbr sync marker 0x%08x at offset %u", marker,
      gst_byte_reader_get_pos (reader));
  ucFmtInfoCode = hdr[2] >> 8;
  if (ucFmtInfoCode == 2) {
    GST_LOG_OBJECT (dcaparse, "LBR decoder initialization data");
    nLBRSampleRateCode = (hdr[2] & 0x00ff);
    usLBRSpkrMask = (hdr[3] >> 8) | (hdr[3] << 8);
    nLBRBitRateMSnybbles = (hdr[5] & 0x00ff);
    nLBROriginalBitRate_LSW = (hdr[6] >> 8) | (hdr[6] << 8);
    nLBRScaledBitRate_LSW = (hdr[7] >> 8) | (hdr[7] << 8);
    nuOriginatBitrate =
        nLBROriginalBitRate_LSW | ((nLBRBitRateMSnybbles & 0x0F) << 16);
    nuScaledBitrate =
        nLBRScaledBitRate_LSW | ((nLBRBitRateMSnybbles & 0xF0) << 12);
    GST_LOG_OBJECT (dcaparse, "nuOriginatBitrate (%u) nuScaledBitrate (%u)",
        nuOriginatBitrate, nuScaledBitrate);
    *sample_rate = sample_rates[nLBRSampleRateCode];
    *endianness = G_BIG_ENDIAN;
    *depth = 16;
    if (*sample_rate < 16000)
      *samples_per_block = 1024;
    else if (*sample_rate < 32000)
      *samples_per_block = 2048;
    else
      *samples_per_block = 4096;
    *num_blocks = 0;            /*Default */

    *channels = 0;
    if (usLBRSpkrMask) {
      for (i = 0; i < 16; i++) {
        if ((0x0001 << i) & usLBRSpkrMask)
          *channels += speaker_mask[i];
      }
    } else {
      GST_LOG_OBJECT (dcaparse, "LBRSpeaker mask is not set.");
    }

    GST_LOG_OBJECT (dcaparse,
        "usLBRSpkrMask (%04x) sample_rate (%d) channels (%d)", usLBRSpkrMask,
        *sample_rate, *channels);
  } else if (ucFmtInfoCode != 1) {
    GST_LOG_OBJECT (dcaparse, "format information not valid");
    return FALSE;
  }
  return TRUE;
}

/**
 * gst_dca_parse_header_extsstreams:
 * @dcaparse: #GstDcaParse.
 * @reader: instance of GstByteReader
 * @ext_size: Size of Ext Sub data in bytes
 * @sample_rate: Audio sample rate
 * @channels: Num of channels
 * @depth: Bit depth
 * @endianness: Endianness of bitstream
 * @num_blocks: Num of blocks
 * @samples_per_block: Samples in a block
 * @terminator: frame termination flag
 *
 * Parse Extension Substream header and set the frame parameters.
 *
 * Returns: TRUE if parsed successfully.
 */
static gboolean
gst_dca_parse_header_extsstreams (GstDcaParse * dcaparse,
    const GstByteReader * reader, guint * ext_size, guint * sample_rate,
    guint * channels, guint * depth, gint * endianness, guint * num_blocks,
    guint * samples_per_block, gboolean * terminator)
{
  guint32 sync_word, core_substream_size, nuExtSSFSize = 0;
  guint16 nuExtSSHeaderSize = 0;
  gint dts_profile = DTS_INVALID;
  guint8 nExtSSIndex, SSIndex_lookup = 0;
  GstByteReader r = *reader;
  gboolean ret = TRUE;

  while (gst_dca_parse_ext_header (dcaparse, &r, &nuExtSSHeaderSize,
          &nuExtSSFSize, &nExtSSIndex)) {
    if (SSIndex_lookup & (0x01 << nExtSSIndex))
      break;

    SSIndex_lookup |= (0x01 << nExtSSIndex);

    (*ext_size) += nuExtSSFSize;

    if (dts_profile == DTS_INVALID
        && dcaparse->dts_stream_type == DTS_EXT_SUB_ONLY) {
      GstByteReader r1 = *reader;
      gst_byte_reader_skip (&r1, nuExtSSHeaderSize);
      sync_word = gst_byte_reader_peek_uint32_be_unchecked (&r1);
      GST_LOG_OBJECT (dcaparse, "Next Sync (%08x)", sync_word);
      switch (sync_word) {
        case DTS_SYNCWORD_LBR:
          GST_LOG_OBJECT (dcaparse, "Found sync for LBR");
          if (!gst_dca_parse_header_lbr (dcaparse, &r1,
                  sample_rate, channels, depth, endianness,
                  num_blocks, samples_per_block, terminator)) {
            ret = FALSE;
            return ret;
          }
          dts_profile = DTS_DTSE;
          break;
        case DTS_SYNCWORD_XLL:
          GST_LOG_OBJECT (dcaparse, "Found sync for XLL");
          if (!gst_dca_parse_header_dtsl (dcaparse, &r1,
                  sample_rate, channels, depth, endianness,
                  num_blocks, samples_per_block, terminator)) {
            ret = FALSE;
            return ret;
          }
          dts_profile = DTS_DTSL;
          break;
        case DTS_SYNCWORD_SUBSTREAM_CORE:
          GST_LOG_OBJECT (dcaparse, "Found sync for core substream");
          if (!gst_dca_parse_dtsc_header_parse (dcaparse, &r1,
                  &core_substream_size, sample_rate, channels, depth,
                  endianness, num_blocks, samples_per_block, terminator)) {
            ret = FALSE;
            return ret;
          }
          dts_profile = DTS_DTSH;
          break;
        default:
          GST_LOG_OBJECT (dcaparse, "No sync found, setting default as DTSH");
          dts_profile = DTS_DTSH;
          break;
      }
    }
    gst_byte_reader_skip (&r, nuExtSSFSize);
  }

  if (*ext_size == 0)
    ret = FALSE;

  return ret;
}

/**
 * gst_dca_parse_find_sync:
 * @dcaparse: #GstDcaParse.
 * @reader: instance of GstByteReader
 * @bufsize: Size of buffer
 * @sync: Sync word
 *
 * Find Sync word and its offset.
 *
 * Returns: Sync word offset or Fail if sync word is not found.
 */
static gint
gst_dca_parse_find_sync (GstDcaParse * dcaparse, GstByteReader * reader,
    gsize bufsize, guint32 * sync)
{
  guint32 best_sync = 0;
  guint best_offset = G_MAXUINT;

  /*find sync for core */
  if (gst_dca_parse_find_sync_core (dcaparse, reader,
          bufsize, &best_sync, &best_offset)) {
    GST_DEBUG_OBJECT (dcaparse, "found sync for core dts");
  }

  /*find sync for extension stream */
  if (gst_dca_parse_find_sync_extsstream (dcaparse, reader,
          bufsize, &best_sync, &best_offset)) {
    GST_DEBUG_OBJECT (dcaparse, "found sync for extension stream");
  }

  if (best_offset == G_MAXUINT)
    return -1;

  GST_DEBUG_OBJECT (dcaparse, "Syncword Returned(%08x)", best_sync);

  *sync = best_sync;
  return best_offset;

}


/**
 * gst_dca_parse_find_sync_core:
 * @dcaparse: #GstDcaParse.
 * @reader: instance of GstByteReader
 * @bufsize: Size of buffer
 * @best_sync: Sync word
 * @best_offset: Offset of sync word in the buffer
 *
 * Search for core sync word and its offset.
 *
 * Returns: True if core sync word is found or Fail if core sync word is not found.
 */
static gboolean
gst_dca_parse_find_sync_core (GstDcaParse * dcaparse, GstByteReader * reader,
    gsize bufsize, guint32 * best_sync, guint * best_offset)
{
  gint off;
  gboolean ret;

  /* FIXME: verify syncs via _parse_header() here already */

  /* Raw little endian */
  off = gst_byte_reader_masked_scan_uint32 (reader, 0xffffffff, 0xfe7f0180,
      0, bufsize);
  if (off >= 0 && off < *best_offset) {
    *best_offset = off;
    *best_sync = 0xfe7f0180;
  }

  /* Raw big endian */
  off = gst_byte_reader_masked_scan_uint32 (reader, 0xffffffff, 0x7ffe8001,
      0, bufsize);
  if (off >= 0 && off < *best_offset) {
    *best_offset = off;
    *best_sync = 0x7ffe8001;
  }

  /* FIXME: check next 2 bytes as well for 14-bit formats (but then don't
   * forget to adjust the *skipsize= in _check_valid_frame() */

  /* 14-bit little endian  */
  off = gst_byte_reader_masked_scan_uint32 (reader, 0xffffffff, 0xff1f00e8,
      0, bufsize);
  if (off >= 0 && off < *best_offset) {
    *best_offset = off;
    *best_sync = 0xff1f00e8;
  }

  /* 14-bit big endian  */
  off = gst_byte_reader_masked_scan_uint32 (reader, 0xffffffff, 0x1fffe800,
      0, bufsize);
  if (off >= 0 && off < *best_offset) {
    *best_offset = off;
    *best_sync = 0x1fffe800;
  }
  if (*best_offset == G_MAXUINT) {
    ret = FALSE;
  } else {
    dcaparse->dts_stream_type = DTS_CORE_ONLY;
    ret = TRUE;
  }
  return ret;
}

/**
 * gst_dca_parse_find_sync_extsstream:
 * @dcaparse: #GstDcaParse.
 * @reader: instance of GstByteReader
 * @bufsize: Size of buffer
 * @best_sync: Sync word
 * @best_offset: Offset of sync word in the buffer
 *
 * Search for Ext Sub stream sync word and its offset.
 *
 * Returns: True if core sync word is found or Fail if core sync word is not found.
 */
static gboolean
gst_dca_parse_find_sync_extsstream (GstDcaParse * dcaparse,
    GstByteReader * reader, gsize bufsize, guint32 * best_sync,
    guint * best_offset)
{
  gint off;
  guint32 best_sync_ext_sub, best_offset_ext_sub;
  gboolean ret;

  best_sync_ext_sub = 0;
  best_offset_ext_sub = G_MAXUINT;

  /* Raw big endian */
  off = gst_byte_reader_masked_scan_uint32 (reader, 0xffffffff,
      DTS_SYNCWORD_SUBSTREAM, 0, bufsize);
  if (off >= 0 && off < best_offset_ext_sub) {
    best_offset_ext_sub = off;
    best_sync_ext_sub = DTS_SYNCWORD_SUBSTREAM;
  }

  if (best_offset_ext_sub != G_MAXUINT) {
    if (dcaparse->dts_stream_type == DTS_CORE_ONLY) {
      dcaparse->dts_stream_type = DTS_CORE_PLUS_EXT_SUB;
    } else {
      dcaparse->dts_stream_type = DTS_EXT_SUB_ONLY;
      *best_offset = best_offset_ext_sub;
      *best_sync = best_sync_ext_sub;
    }
    ret = TRUE;

  } else {
    ret = FALSE;
  }

  return ret;
}

static GstFlowReturn
gst_dca_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize)
{
  GstDcaParse *dcaparse = GST_DCA_PARSE (parse);
  GstBuffer *buf = frame->buffer;
  GstByteReader r;
  gboolean parser_in_sync;
  gboolean terminator;
  guint32 sync = 0;
  guint size = 0, rate, chans, num_blocks, samples_per_block, depth;
  gint block_size;
  gint endianness;
  gint off = -1;
  GstMapInfo map;
  GstFlowReturn ret = GST_FLOW_EOS;

  gst_buffer_map (buf, &map, GST_MAP_READ);

  if (G_UNLIKELY (map.size < 16)) {
    *skipsize = 1;
    goto cleanup;
  }

  parser_in_sync = !GST_BASE_PARSE_LOST_SYNC (parse);

  gst_byte_reader_init (&r, map.data, map.size);

  if (G_LIKELY (parser_in_sync && dcaparse->last_sync != 0)) {
    off = gst_byte_reader_masked_scan_uint32 (&r, 0xffffffff,
        dcaparse->last_sync, 0, map.size);
    sync = dcaparse->last_sync;
  }

  if (G_UNLIKELY (off < 0)) {
    off = gst_dca_parse_find_sync (dcaparse, &r, map.size, &sync);
  }

  /* didn't find anything that looks like a sync word, skip */
  if (off < 0) {
    *skipsize = map.size - 3;
    GST_DEBUG_OBJECT (dcaparse, "no sync, skipping %d bytes", *skipsize);
    goto cleanup;
  }

  GST_LOG_OBJECT (parse, "possible sync %08x at buffer offset %d", sync, off);

  /* possible frame header, but not at offset 0? skip bytes before sync */
  if (off > 0) {
    *skipsize = off;
    goto cleanup;
  }

  /* make sure the values in the frame header look sane */
  if (!gst_dca_parse_parse_header (dcaparse, &r, &size, &rate, &chans, &depth,
          &endianness, &num_blocks, &samples_per_block, &terminator)) {
    *skipsize = 4;
    goto cleanup;
  }

  GST_LOG_OBJECT (parse, "got frame, sync %08x, size %u, rate %d, channels %d",
      sync, size, rate, chans);

  dcaparse->last_sync = sync;

  /*
     Find out next sync word from the framesize obtained from frame header
     modify size if no  Core and substream syncword is found
   */
  if (map.size >= size + 4) {
    if (sync == 0x1FFFE800 || sync == 0xFF1F00E8) {
      guint32 marker_next;
      gst_byte_reader_init (&r, map.data, map.size);
      gst_byte_reader_skip_unchecked (&r, size);
      marker_next = gst_byte_reader_peek_uint32_be_unchecked (&r);
      if (marker_next != 0x7FFE8001 && marker_next != 0x1FFFE800 &&
          marker_next != 0xFE7F0180 && marker_next != 0xFF1F00E8 &&
          marker_next != 0x64582025 && marker_next != 0x02b09261 &&
          marker_next != 0x58642520 && marker_next != 0xb0026192) {
        if (((size * 16) % 14) != 0)
          size = size - 1;
        size = (size * 16) / 14;        /* FIXME: round up? */
        GST_DEBUG_OBJECT (dcaparse, "framesize recalculated to %u", size);
      }
    }
  } else
    goto cleanup;

  /* FIXME: Don't look for a second syncword, there are streams out there
   * that consistently contain garbage between every frame so we never ever
   * find a second consecutive syncword.
   * See https://bugzilla.gnome.org/show_bug.cgi?id=738237
   */
#if 0
  parser_draining = GST_BASE_PARSE_DRAINING (parse);

  if (!parser_in_sync && !parser_draining) {
    /* check for second frame to be sure */
    GST_DEBUG_OBJECT (dcaparse, "resyncing; checking next frame syncword");
    if (map.size >= (size + 16)) {
      guint s2, r2, c2, n2, s3;
      gboolean t;

      GST_MEMDUMP ("buf", map.data, size + 16);
      gst_byte_reader_init (&r, map.data, map.size);
      gst_byte_reader_skip_unchecked (&r, size);

      if (!gst_dca_parse_parse_header (dcaparse, &r, &s2, &r2, &c2, NULL, NULL,
              &n2, &s3, &t)) {
        GST_DEBUG_OBJECT (dcaparse, "didn't find second syncword");
        *skipsize = 4;
        goto cleanup;
      }

      /* ok, got sync now, let's assume constant frame size */
      gst_base_parse_set_min_frame_size (parse, size);
    } else {
      /* wait for some more data */
      GST_LOG_OBJECT (dcaparse,
          "next sync out of reach (%" G_GSIZE_FORMAT " < %u)", map.size,
          size + 16);
      goto cleanup;
    }
  }
#endif

  /* found frame */
  ret = GST_FLOW_OK;

  /* metadata handling */
  block_size = num_blocks * samples_per_block;

  if (G_UNLIKELY (dcaparse->rate != rate || dcaparse->channels != chans
          || dcaparse->depth != depth || dcaparse->endianness != endianness
          || (!terminator && block_size > 0
              && dcaparse->block_size != block_size))) {
    GstCaps *caps = NULL;
    GstStructure *s = NULL;

    if (dcaparse->mime_type == NULL) {
      caps = gst_pad_get_current_caps (parse->sinkpad);
      if (caps) {
        s = gst_caps_get_structure (caps, 0);
        if (gst_structure_has_name (s, "audio/x-dtse"))
          dcaparse->mime_type = "audio/x-dtse";
        else if (gst_structure_has_name (s, "audio/x-dtsh"))
          dcaparse->mime_type = "audio/x-dtsh";
        else if (gst_structure_has_name (s, "audio/x-dtsl"))
          dcaparse->mime_type = "audio/x-dtsl";
        else
          dcaparse->mime_type = "audio/x-dts";
        gst_caps_unref (caps);
      } else {
        GST_DEBUG_OBJECT (dcaparse, "Failed to get media type from upstream");
        return GST_FLOW_ERROR;
      }
    }

    caps = gst_caps_new_simple (dcaparse->mime_type,
        "framed", G_TYPE_BOOLEAN, TRUE,
        "rate", G_TYPE_INT, rate, "channels", G_TYPE_INT, chans,
        "endianness", G_TYPE_INT, endianness, "depth", G_TYPE_INT, depth,
        "block-size", G_TYPE_INT, block_size, "frame-size", G_TYPE_INT, size,
        NULL);

    if (!gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (parse), caps)) {
      GST_DEBUG_OBJECT (dcaparse, "Failed to set src cap");
      return GST_FLOW_ERROR;
    }
    gst_caps_unref (caps);

    dcaparse->rate = rate;
    dcaparse->channels = chans;
    dcaparse->depth = depth;
    dcaparse->endianness = endianness;
    dcaparse->block_size = block_size;
    dcaparse->frame_size = size;

    if (dcaparse->dts_stream_type == DTS_CORE_ONLY)
      gst_base_parse_set_frame_rate (parse, rate, block_size, 0, 0);
  }

cleanup:
  gst_buffer_unmap (buf, &map);

  if (ret == GST_FLOW_OK && size <= map.size) {
    ret = gst_base_parse_finish_frame (parse, frame, size);
  } else {
    ret = GST_FLOW_OK;
  }

  return ret;
}

/*
 * MPEG-PS private1 streams add a 2 bytes "Audio Substream Headers" for each
 * buffer (not each frame) with the offset of the next frame's start.
 * These 2 bytes can be dropped safely as they do not include any timing
 * information, only the offset to the start of the next frame.
 * See gstac3parse.c for a more detailed description.
 * */

static GstFlowReturn
gst_dca_parse_chain_priv (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstDcaParse *dcaparse = GST_DCA_PARSE (parent);
  GstFlowReturn ret;
  GstBuffer *newbuf;
  gsize size;

  size = gst_buffer_get_size (buffer);
  if (size >= 2) {
    newbuf = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, 2, size - 2);
    gst_buffer_copy_into (newbuf, buffer, GST_BUFFER_COPY_METADATA, 0, -1);
    gst_buffer_unref (buffer);
    ret = dcaparse->baseparse_chainfunc (pad, parent, newbuf);
  } else {
    gst_buffer_unref (buffer);
    ret = GST_FLOW_OK;
  }

  return ret;
}

static void
remove_fields (GstCaps * caps)
{
  guint i, n;

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);

    gst_structure_remove_field (s, "framed");
  }
}

static GstCaps *
gst_dca_parse_get_sink_caps (GstBaseParse * parse, GstCaps * filter)
{
  GstCaps *peercaps, *templ;
  GstCaps *res;

  templ = gst_pad_get_pad_template_caps (GST_BASE_PARSE_SINK_PAD (parse));
  if (filter) {
    GstCaps *fcopy = gst_caps_copy (filter);
    /* Remove the fields we convert */
    remove_fields (fcopy);
    peercaps = gst_pad_peer_query_caps (GST_BASE_PARSE_SRC_PAD (parse), fcopy);
    gst_caps_unref (fcopy);
  } else
    peercaps = gst_pad_peer_query_caps (GST_BASE_PARSE_SRC_PAD (parse), NULL);

  if (peercaps) {
    /* Remove the framed field */
    peercaps = gst_caps_make_writable (peercaps);
    remove_fields (peercaps);

    res = gst_caps_intersect_full (peercaps, templ, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (peercaps);
    gst_caps_unref (templ);
  } else {
    res = templ;
  }

  if (filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, res, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (res);
    res = intersection;
  }

  return res;
}

static gboolean
gst_dca_parse_set_sink_caps (GstBaseParse * parse, GstCaps * caps)
{
  GstStructure *s;
  GstDcaParse *dcaparse = GST_DCA_PARSE (parse);

  s = gst_caps_get_structure (caps, 0);
  if (gst_structure_has_name (s, "audio/x-private1-dts")) {
    gst_pad_set_chain_function (parse->sinkpad, gst_dca_parse_chain_priv);
  } else {
    gst_pad_set_chain_function (parse->sinkpad, dcaparse->baseparse_chainfunc);
  }
  return TRUE;
}

static GstFlowReturn
gst_dca_parse_pre_push_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{
  GstDcaParse *dcaparse = GST_DCA_PARSE (parse);

  if (!dcaparse->sent_codec_tag) {
    GstTagList *taglist;
    GstCaps *caps;

    /* codec tag */
    caps = gst_pad_get_current_caps (GST_BASE_PARSE_SRC_PAD (parse));
    if (G_UNLIKELY (caps == NULL)) {
      if (GST_PAD_IS_FLUSHING (GST_BASE_PARSE_SRC_PAD (parse))) {
        GST_INFO_OBJECT (parse, "Src pad is flushing");
        return GST_FLOW_FLUSHING;
      } else {
        GST_INFO_OBJECT (parse, "Src pad is not negotiated!");
        return GST_FLOW_NOT_NEGOTIATED;
      }
    }

    taglist = gst_tag_list_new_empty ();
    gst_pb_utils_add_codec_description_to_tag_list (taglist,
        GST_TAG_AUDIO_CODEC, caps);
    gst_caps_unref (caps);

    gst_base_parse_merge_tags (parse, taglist, GST_TAG_MERGE_REPLACE);
    gst_tag_list_unref (taglist);

    /* also signals the end of first-frame processing */
    dcaparse->sent_codec_tag = TRUE;
  }

  frame->flags |= GST_BASE_PARSE_FRAME_FLAG_CLIP;

  return GST_FLOW_OK;
}
