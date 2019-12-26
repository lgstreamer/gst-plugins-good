/* GStreamer AC4 parser
 * Copyright (C) 2016 LG Electronics, Inc.
 * Author: Dinesh Anand K <dinesh.k@lge.com>
 *         Kumar Vijay Vikram <kumar.vikram@lge.com>
 *
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

/*
 * spec: ETSI TS 103 190-2 Annex C.2
 * AC-4 Sync Frame structure (i.e. Raw Frame encapsulation),
 *
 *  -------------------------        -------------------------------
 * |Sync word| Frame | Raw   |      |Sync word| Frame | Raw   | CRC |
 * |(0xAC40) | Size  | Frame | (or) |(0xAC41) | Size  | Frame |     |
 *  -------------------------        -------------------------------
 *
 * spec : ETSI TS 103 190 - 4.2.1
 * AC-4 Raw Frame structure
 *
 *  ----------------------------------------------------
 * | TOC | substream 0 | substream1 | ... | substream N |
 *  ----------------------------------------------------
 */

/**
 * SECTION:element-ac4parse
 * @short_description: AC4 parser
 * @see_also: #GstAmrParse, #GstAACParse
 *
 * This is an AC4 parser.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 filesrc location=abc.ac4 ! ac4parse ! a52dec ! audioresample ! audioconvert ! autoaudiosink
 * ]|
 * </refsect2>
 */

/* TODO:
 *  - audio/ac4 to audio/x-private1-ac4 is not implemented (done in the muxer)
 *  - should accept framed and unframed input (needs decodebin fixes first)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstac4parse.h"
#include <gst/base/base.h>
#include <gst/pbutils/pbutils.h>

GST_DEBUG_CATEGORY_STATIC (ac4_parse_debug);
#define GST_CAT_DEFAULT ac4_parse_debug

#define AC4_SYNC_WORD1 0xAC40   /*sync frame with CRC disabled */
#define AC4_SYNC_WORD2 0xAC41   /*sync frame with CRC enabled */

/*spec: 4.3.3.2.2*/
#define AC4_SEQ_CNTR_WRAP_VAL 1020      /*seq. counter wrap value */

#define AC4_BS_VER_SUPPORTED 2  /*bitstream version till which supported */

/* Not specified in spec. based on experiment*/
#define AC4_MIN_FRAME_SIZE 12   /* min frame size req. for parsing */

/* spec:4.3.3.2.6*/
/* gets the frame rate based on fs base & fps */
#define AC4_GET_FPS(fs, fps_index, fr_num, fr_den) \
    *fr_num = -1;\
    *fr_den = -1;\
    if(fs == 48000 && fps_index < 14) {\
      *fr_num = fps_table_48K[fps_index][0];\
      *fr_den = fps_table_48K[fps_index][1];\
    }else if(fs == 44100 && fps_index == 13) {\
      *fr_num = 11025;\
      *fr_den = 512;\
    }\

/* spec: 4.3.3.2.5 */
static const guint fs_base[2] = { 44100, 48000 };

/* spec: 4.3.3.2.6 */
/* frame rate(numerator, denominator) table for base fs, 48KHz */
static const guint fps_table_48K[16][2] = {
  {0x44AA2000, 0x2DD2780}, {0x00119400, 0x0000BB80}, {0x000BB800, 0x00007800},
  {0x44AA2000, 0x24A8600}, {0x00119400, 0x00009600}, {0x44AA2000, 0x016E93C0},
  {0x00119400, 0x0005DC0}, {0x000BB800, 0x00003C00}, {0x44AA2000, 0x01254300},
  {0x00119400, 0x0004B00}, {0x000BB800, 0x00001E00}, {0x44AA2000, 0x0092A180},
  {0x00119400, 0x0002580}, {0x0000BB80, 0x00000800}
  /*frame_rate_index(14 & 15) are reserved */
};


static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-ac4, framed = (boolean) true, "
        " channels = (int) [ 1, 12 ], rate = (int) [ 8000, 48000 ], "
        " frame-format = (string) {SYNC, RAW}, "
        " alignment = (string) { frame}; "));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-ac4; " "audio/ac4 "));

static void gst_ac4_parse_finalize (GObject * object);

static gboolean gst_ac4_parse_start (GstBaseParse * parse);
static gboolean gst_ac4_parse_stop (GstBaseParse * parse);
static GstFlowReturn gst_ac4_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize);
static GstFlowReturn gst_ac4_parse_pre_push_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame);
static GstCaps *gst_ac4_parse_get_sink_caps (GstBaseParse * parse,
    GstCaps * filter);
static gboolean gst_ac4_parse_set_sink_caps (GstBaseParse * parse,
    GstCaps * caps);

#define gst_ac4_parse_parent_class parent_class
G_DEFINE_TYPE (GstAc4Parse, gst_ac4_parse, GST_TYPE_BASE_PARSE);

static void
gst_ac4_parse_class_init (GstAc4ParseClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (ac4_parse_debug, "ac4parse", 0,
      "AC4 audio stream parser");

  object_class->finalize = gst_ac4_parse_finalize;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_static_metadata (element_class,
      "AC4 audio stream parser", "Codec/Parser/Converter/Audio",
      "AC4 parser", "Dinesh Anand K <dinesh.k@lge.com>");

  parse_class->start = GST_DEBUG_FUNCPTR (gst_ac4_parse_start);
  parse_class->stop = GST_DEBUG_FUNCPTR (gst_ac4_parse_stop);
  parse_class->handle_frame = GST_DEBUG_FUNCPTR (gst_ac4_parse_handle_frame);
  parse_class->pre_push_frame =
      GST_DEBUG_FUNCPTR (gst_ac4_parse_pre_push_frame);
  parse_class->set_sink_caps = GST_DEBUG_FUNCPTR (gst_ac4_parse_set_sink_caps);
  parse_class->get_sink_caps = GST_DEBUG_FUNCPTR (gst_ac4_parse_get_sink_caps);
}

static void
gst_ac4_parse_reset (GstAc4Parse * ac4parse)
{
  /* Initialize the state variables */
  ac4parse->n_presentations = 0;
  ac4parse->bitstream_version = 0;

  ac4parse->sink_cap_ch = 1;
  ac4parse->channels = -1;
  ac4parse->sample_rate = -1;
  ac4parse->fps_num = -1;
  ac4parse->fps_den = -1;
  ac4parse->bsversion = 0;
  ac4parse->is_framed = FALSE;
  ac4parse->sent_codec_tag = FALSE;
}

static void
gst_ac4_parse_init (GstAc4Parse * ac4parse)
{
  /*TODO: Minimum frame size has to be decided */
  gst_base_parse_set_min_frame_size (GST_BASE_PARSE (ac4parse),
      AC4_MIN_FRAME_SIZE);
  gst_ac4_parse_reset (ac4parse);
  ac4parse->baseparse_chainfunc =
      GST_BASE_PARSE_SINK_PAD (GST_BASE_PARSE (ac4parse))->chainfunc;
  GST_PAD_SET_ACCEPT_INTERSECT (GST_BASE_PARSE_SINK_PAD (ac4parse));
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_BASE_PARSE_SINK_PAD (ac4parse));
}

static void
gst_ac4_parse_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_ac4_parse_start (GstBaseParse * parse)
{
  GstAc4Parse *ac4parse = GST_AC4_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "starting");

  gst_ac4_parse_reset (ac4parse);

  return TRUE;
}

static gboolean
gst_ac4_parse_stop (GstBaseParse * parse)
{
  GST_DEBUG_OBJECT (parse, "stopping");

  return TRUE;
}

/**
 * variable_bits_read:
 * @bit_reader: #GstBitReader
 * @n_bits: minimum element length of variable lenght fields
 *
 * spec: 4.3.2
 *
 * TODO: Check whether the buffer of 32 bit is sufficient
 *       to decode the variable lenght field value.
 *
 * Returns: variable lenght field value
 */
static guint
variable_bits_read (GstBitReader * bit_reader, guint16 n_bits)
{
  gboolean read_more_flag = FALSE;
  guint value = 0;
  guint buffer1 = 0;
  guint buffer1_mask;

  /* Read the first group(i.e. n_bits+1 bits)into the variable buffer1 */
  gst_bit_reader_get_bits_uint32 (bit_reader, &buffer1, (n_bits + 1));
  /* Compute the mask value for buffer1 */
  buffer1_mask = ((guint) 1 << n_bits) - 1;

  do {
    /* get the value from buffer1 */
    value |= (buffer1 & buffer1_mask);
    /*skip the read bits in buffer1 */
    buffer1 >>= n_bits;
    /* read the read_more_flag */
    read_more_flag = (buffer1 & 0x1);
    /* compute the value of variable length field */
    value <<= (n_bits * read_more_flag);
    value += (read_more_flag << n_bits);
    /* Read the next group in case of read_more_flag set */
    gst_bit_reader_get_bits_uint32 (bit_reader, &buffer1,
        read_more_flag * (n_bits + 1));
  } while (read_more_flag);

  return value;
}


static gboolean
gst_ac4_parse_frame_header (GstAc4Parse * ac4parse, GstBuffer * buf,
    gint skip, guint * frame_size, guint * sampling_rate, guint * chans,
    gint * fps_num, gint * fps_den, guint * bsversion)
{
  GstBitReader bit_reader;
  GstMapInfo map;
  gboolean fs_index, crc_enable, ret = FALSE;
  guint8 frame_rate_index;
  guint16 uvalue16 = 0;
  guint uvalue32 = 0, base_sample_freq;

  /*TODO: Avoid using the gst_bit_reader functions */
  /* Initialize the bit reader */
  gst_buffer_map (buf, &map, GST_MAP_READ);
  gst_bit_reader_init (&bit_reader, map.data, map.size);

  /* Peek the sync word */
  gst_bit_reader_peek_bits_uint16 (&bit_reader, &uvalue16, 16);

  if (uvalue16 == AC4_SYNC_WORD1 || uvalue16 == AC4_SYNC_WORD2) {
    /* Read the sync word */
    gst_bit_reader_get_bits_uint16 (&bit_reader, &uvalue16, 16);

    GST_LOG_OBJECT (ac4parse, "AC4 Sync frame parsing ...");
    /* Read the CRC enable flag */
    crc_enable = (uvalue16 == AC4_SYNC_WORD2);
    /* Annex C.3: Read the frame size */
    gst_bit_reader_get_bits_uint16 (&bit_reader, &uvalue16, 16);
    /* framesize + sync word (2b) + framesize field (2b) + crc_word(2b) */
    *frame_size = uvalue16 + 2 + 2 + (crc_enable * 2);
    if (uvalue16 == 0xFFFF) {
      gst_bit_reader_get_bits_uint32 (&bit_reader, &uvalue32, 24);
      /* framesize + sync word (2b) + framesize field (5b) + crc_word(2b) */
      *frame_size = uvalue32 + 2 + 5 + (crc_enable * 2);
    }
    GST_LOG_OBJECT (ac4parse, "AC4 Sync frame size:%d", *frame_size);
  }
  GST_LOG_OBJECT (ac4parse, "AC4 Raw frame, TOC parsing ...");
  /* spec: 4.2.3.1 */
  /* bitstream version */
  gst_bit_reader_get_bits_uint16 (&bit_reader, &uvalue16, 2);
  uvalue16 += (uvalue16 == 0x3 ? variable_bits_read (&bit_reader, 2) : 0);
  ac4parse->bitstream_version = uvalue16;
  if (uvalue16 > AC4_BS_VER_SUPPORTED) {
    GST_LOG_OBJECT (ac4parse, "Invalid bitstream Ver:%d", uvalue16);
    goto cleanup;
  }
  GST_LOG_OBJECT (ac4parse, "TOC, bitstream Ver:%d", uvalue16);
  *bsversion = uvalue16;
  /* sequence counter */
  gst_bit_reader_get_bits_uint16 (&bit_reader, &uvalue16, 10);
  if (uvalue16 > AC4_SEQ_CNTR_WRAP_VAL) {
    GST_LOG_OBJECT (ac4parse, "Sequence counter:%d exceeds Max", uvalue16);
    goto cleanup;
  }
  GST_LOG_OBJECT (ac4parse, "TOC, Sequence counter:%d", uvalue16);
  /* wait frames information parsing */
  /*b_wait_frames */
  gst_bit_reader_get_bits_uint16 (&bit_reader, &uvalue16, 1);

  if (uvalue16) {
    /*wait_frames */
    gst_bit_reader_get_bits_uint16 (&bit_reader, &uvalue16, 3);
    /* br_code */
    if (uvalue16)
      gst_bit_reader_get_bits_uint16 (&bit_reader, &uvalue16, 2);
  }
  gst_bit_reader_get_bits_uint16 (&bit_reader, &uvalue16, 7);
  /* fs_index */
  fs_index = (uvalue16 & (0x1 << 6)) >> 6;
  base_sample_freq = fs_base[fs_index];
  GST_LOG_OBJECT (ac4parse, "TOC, base sampling freq:%d", base_sample_freq);
  *sampling_rate = base_sample_freq;
  /*frame_rate_index */
  frame_rate_index = (uvalue16 & (0xF << 2)) >> 2;
  if ((frame_rate_index > 13 && fs_index) ||
      (frame_rate_index != 13 && !fs_index)) {
    GST_LOG_OBJECT (ac4parse, "Invalid framerate index:%d", frame_rate_index);
    goto cleanup;
  }
  AC4_GET_FPS (base_sample_freq, frame_rate_index, fps_num, fps_den);
  GST_LOG_OBJECT (ac4parse, "TOC, fps_num:%d fps_den:%d", *fps_num, *fps_den);

  /* presentation information parsing */
  /*b_single_presentation */
  ac4parse->n_presentations = 1;
  if (!(uvalue16 & 0x1)) {
    /*b_more_presentations */
    gst_bit_reader_get_bits_uint16 (&bit_reader, &uvalue16, 1);
    /*n_presentations */
    ac4parse->n_presentations =
        (uvalue16 == 1) ? variable_bits_read (&bit_reader, 2) + 2 : 0;
  }

  /* payload base offset information parsing */
  gst_bit_reader_get_bits_uint16 (&bit_reader, &uvalue16, 1);
  if (uvalue16) {
    /*payload_base_minus1 */
    gst_bit_reader_get_bits_uint16 (&bit_reader, &uvalue16, 5);
    uvalue16 = uvalue16 + 1;
    /*b_payload_base */
    uvalue16 += (uvalue16 == 0x20) ? variable_bits_read (&bit_reader, 3) : 0;
  }
  ret = TRUE;


cleanup:
  GST_LOG_OBJECT (ac4parse, "Bytes consumed for AC4 header parsing:%u",
      (gst_bit_reader_get_pos (&bit_reader) >> 3));
  gst_buffer_unmap (buf, &map);

  return ret;
}

static GstFlowReturn
gst_ac4_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize)
{
  GstAc4Parse *ac4parse = GST_AC4_PARSE (parse);
  GstBuffer *buf = frame->buffer;
  GstByteReader reader;
  GstMapInfo map;
  GstFlowReturn res = GST_FLOW_OK;
  gboolean lost_sync, draining;
  gboolean ret = FALSE, update_rate = TRUE, is_syncframe = FALSE;
  guint marker, framesize = 0, bsversion = 0;
  guint sampling_rate = 0, num_chans = 0;
  gint fps_num = -1, fps_den = -1;

  gst_buffer_map (buf, &map, GST_MAP_READ);
  /*TODO: Minimum size required for parsing necessary info */
  if (G_UNLIKELY (map.size < AC4_MIN_FRAME_SIZE)) {
    *skipsize = 1;
    goto cleanup;
  }
  /* Initialize the frame size to buffer size */
  framesize = map.size;

  gst_byte_reader_init (&reader, map.data, map.size);

  marker = gst_byte_reader_peek_uint16_be_unchecked (&reader);

  /* check for valid AC4 sync word */
  if (marker == AC4_SYNC_WORD1 || marker == AC4_SYNC_WORD2) {
    is_syncframe = TRUE;
    GST_LOG_OBJECT (ac4parse, "AC4 sync marker 0x%02x at offset %u", marker,
        gst_byte_reader_get_pos (&reader));
  } else if (!ac4parse->is_framed) {
    *skipsize = 1;
    goto cleanup;
  }
  /* Initialize the num channels to sink cap channel value */
  num_chans = ac4parse->sink_cap_ch;
  /* parse AC4 frame header information and check for valid values */
  if (!gst_ac4_parse_frame_header (ac4parse, buf, 0, &framesize,
          &sampling_rate, &num_chans, &fps_num, &fps_den, &bsversion)) {
    *skipsize = 1;
    goto cleanup;
  }
  GST_LOG_OBJECT (parse, "AC4 frame parsing successful..");
  GST_LOG_OBJECT (parse, "Framesize: %u", framesize);
  GST_LOG_OBJECT (parse, "Bitstream version: %u", bsversion);
  GST_LOG_OBJECT (parse, "Sampling_rate:%u", sampling_rate);
  GST_LOG_OBJECT (parse, "Number of presentations:%u",
      ac4parse->n_presentations);
  GST_LOG_OBJECT (parse, "Number of channels:%u", num_chans);
  GST_LOG_OBJECT (parse, "Frame rate, fps_num:%d fps_den:%d)", fps_num,
      fps_den);

  GST_LOG_OBJECT (parse, "got frame");

  lost_sync = GST_BASE_PARSE_LOST_SYNC (parse);
  draining = GST_BASE_PARSE_DRAINING (parse);

  if (lost_sync && !draining && is_syncframe) {
    guint16 word = 0;

    GST_DEBUG_OBJECT (ac4parse, "Resyncing: checking for next frame syncword");

    if (!gst_byte_reader_skip (&reader, framesize) ||
        !gst_byte_reader_get_uint16_be (&reader, &word)) {
      GST_DEBUG_OBJECT (ac4parse, "... but not sufficient data");
      /*TODO: Minimum size required for parsing necessary info */
      gst_base_parse_set_min_frame_size (parse, framesize + AC4_MIN_FRAME_SIZE);
      *skipsize = 0;
      goto cleanup;
    } else {
      if (word != AC4_SYNC_WORD1 && word != AC4_SYNC_WORD2) {
        GST_DEBUG_OBJECT (ac4parse,
            "Invalid sync word:0x%x found at frame end...", word);
        /* skip the current frame */
        *skipsize = 1;
        goto cleanup;
      } else {
        /* got sync now, let's assume constant frame size */
        gst_base_parse_set_min_frame_size (parse, framesize);
      }
    }
  }

  /* expect to have found a frame here */
  g_assert (framesize);
  ret = TRUE;

  /* For same sampling rate, frame rate will vary based on framerate index */
  if (G_UNLIKELY (ac4parse->fps_num != fps_num || ac4parse->fps_den != fps_den)) {
    update_rate = TRUE;
    /* update the previous frame information */
    ac4parse->fps_num = fps_num;
    ac4parse->fps_den = fps_den;
  }

  if (G_UNLIKELY (ac4parse->sample_rate != sampling_rate ||
          ac4parse->channels != num_chans ||
          ac4parse->bsversion != bsversion)) {
    GstCaps *caps = gst_caps_new_simple ("audio/x-ac4",
        "framed", G_TYPE_BOOLEAN, TRUE,
        "rate", G_TYPE_INT, sampling_rate,
        "channels", G_TYPE_INT, num_chans, NULL);
    gst_caps_set_simple (caps, "framed", G_TYPE_BOOLEAN, TRUE,
        "bsversion", G_TYPE_INT, ac4parse->bsversion,
        "frame-format", G_TYPE_STRING, (is_syncframe ? "SYNC" : "RAW"), NULL);
    gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (parse), caps);
    gst_caps_unref (caps);

    /* update the previous frame information */
    ac4parse->sample_rate = sampling_rate;
    ac4parse->channels = num_chans;
    ac4parse->bsversion = bsversion;

    update_rate = TRUE;
  }

  if (G_UNLIKELY (update_rate))
    gst_base_parse_set_frame_rate (parse, fps_num, fps_den, 2, 2);

cleanup:
  gst_buffer_unmap (buf, &map);

  if (ret && framesize <= map.size) {
    res = gst_base_parse_finish_frame (parse, frame, framesize);
  }

  return res;
}


static GstFlowReturn
gst_ac4_parse_pre_push_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{
  GstAc4Parse *ac4parse = GST_AC4_PARSE (parse);

  if (!ac4parse->sent_codec_tag) {
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
    ac4parse->sent_codec_tag = TRUE;
  }

  return GST_FLOW_OK;
}

static GstCaps *
gst_ac4_parse_get_sink_caps (GstBaseParse * parse, GstCaps * filter)
{
  GstCaps *peercaps, *templ;
  GstCaps *res;

  templ = gst_pad_get_pad_template_caps (GST_BASE_PARSE_SINK_PAD (parse));
  if (filter) {
    GstCaps *fcopy = gst_caps_copy (filter);
    /* TODO: Remove the fields we convert */

    peercaps = gst_pad_peer_query_caps (GST_BASE_PARSE_SRC_PAD (parse), fcopy);
    gst_caps_unref (fcopy);
  } else
    peercaps = gst_pad_peer_query_caps (GST_BASE_PARSE_SRC_PAD (parse), NULL);

  if (peercaps) {

    peercaps = gst_caps_make_writable (peercaps);
    /* TODO: Remove the fields we convert */

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
gst_ac4_parse_set_sink_caps (GstBaseParse * parse, GstCaps * caps)
{
  GstAc4Parse *ac4parse = GST_AC4_PARSE (parse);
  const GValue *value;
  guint i, n;

  n = gst_caps_get_size (caps);

  for (i = 0; i < n; i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);

    if (gst_structure_has_field (s, "framed")) {
      value = gst_structure_get_value (s, "framed");
      ac4parse->is_framed = g_value_get_boolean (value);
    }
    if (gst_structure_has_field (s, "channels")) {
      value = gst_structure_get_value (s, "channels");
      ac4parse->sink_cap_ch = g_value_get_int (value);
    }
  }

  gst_pad_set_chain_function (parse->sinkpad, ac4parse->baseparse_chainfunc);

  return TRUE;
}
