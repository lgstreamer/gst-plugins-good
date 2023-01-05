/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_QTDEMUX_H__
#define __GST_QTDEMUX_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/base/gstflowcombiner.h>
#include "gstisoff.h"

G_BEGIN_DECLS GST_DEBUG_CATEGORY_EXTERN (qtdemux_debug);
#define GST_CAT_DEFAULT qtdemux_debug

#define GST_TYPE_QTDEMUX \
  (gst_qtdemux_get_type())
#define GST_QTDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QTDEMUX,GstQTDemux))
#define GST_QTDEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QTDEMUX,GstQTDemuxClass))
#define GST_IS_QTDEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QTDEMUX))
#define GST_IS_QTDEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QTDEMUX))

#define GST_QTDEMUX_CAST(obj) ((GstQTDemux *)(obj))

/* qtdemux produces these for atoms it cannot parse */
#define GST_QT_DEMUX_PRIVATE_TAG "private-qt-tag"
#define GST_QT_DEMUX_CLASSIFICATION_TAG "classification"

#define GST_QTDEMUX_MAX_STREAMS         32

#define MP4_PUSHMODE_TRICK

#ifdef MP4_PUSHMODE_TRICK
#define TIME_ADJUST 100000000
#endif

#define MPEGDASH_MODE
#define ATSC3_MODE
#define DOLBYHDR_SUPPORT

typedef struct _GstQTDemux GstQTDemux;
typedef struct _GstQTDemuxClass GstQTDemuxClass;
typedef struct _QtDemuxStream QtDemuxStream;

struct _GstQTDemux
{
  GstElement element;

  /* pads */
  GstPad *sinkpad;

  GstStreamCollection *collection;

  QtDemuxStream *streams[GST_QTDEMUX_MAX_STREAMS];
  gint n_streams;
  gint n_video_streams;
  gint n_audio_streams;
  gint n_sub_streams;

  GstFlowCombiner *flowcombiner;

  gboolean have_group_id;
  guint group_id;

  guint major_brand;
  GstBuffer *comp_brands;
  GNode *moov_node;
  GNode *moov_node_compressed;

  guint32 timescale;
  GstClockTime duration;

  gboolean fragmented;
  gboolean fragmented_seek_pending;
  guint64 moof_offset;

  gint state;

  gboolean pullbased;
  gboolean posted_redirect;
  gboolean seek_to_key_frame;

  /* Protect pad exposing from flush event */
  GMutex expose_lock;

#ifdef MP4_PUSHMODE_TRICK
  gdouble demux_rate;
  gboolean pushed_Iframe;
  gboolean pushed_Audio;
  gboolean all_audio_pushed;
  gboolean segment_event_recvd;
  guint64 trick_offset;
  guint64 prev_seek_offset;
  guint64 prev_segment_position;
  /* For MCVT Trick Play */
  guint64 next_seek_offset;
  gboolean rate_changed;
#endif

  /* push based variables */
  GstClockTimeDiff upstream_basetime; /* basetime given by upstream to be added to output pts */
  GstClockTime upstream_basetime_offset; /* time offset derived by demux to be subtracted to output pts */
  gboolean new_collection;
  guint neededbytes;
  guint todrop;
  GstAdapter *adapter;
  GstBuffer *mdatbuffer;
  guint64 mdatleft;
  /* When restoring the mdat to the adatpter, this buffer
   * stores any trailing data that was after the last atom parsed as it
   * has to be restored later along with the correct offset. Used in
   * fragmented scenario where mdat/moof are one after the other
   * in any order.
   *
   * Check https://bugzilla.gnome.org/show_bug.cgi?id=710623 */
  GstBuffer *restoredata_buffer;
  guint64 restoredata_offset;

  guint64 offset;
  /* offset of the mdat atom */
  guint64 mdatoffset;
  guint64 first_mdat;
  gboolean got_moov;
  guint64 last_moov_offset;
  guint header_size;

  GstTagList *tag_list;

  /* configured playback region */
  GstSegment segment;
  GstEvent *pending_newsegment;
  guint32 segment_seqnum;
  gboolean upstream_format_is_time; /* qtdemux received upstream
                                     * newsegment in TIME format which likely
                                     * means that upstream is driving the pipeline
                                     * (adaptive demuxers / dlna) */
  guint32 offset_seek_seqnum;
  gint64 seek_offset;
  gint64 push_seek_start;
  gint64 push_seek_stop;

#if 0
  /* gst index support */
  GstIndex *element_index;
  gint index_id;
#endif

  gboolean upstream_seekable;
  gint64 upstream_size;

  /* MSS streams have a single media that is unspecified at the atoms, so
   * upstream provides it at the caps */
  GstCaps *media_caps;
  gboolean exposed;
  gboolean mss_mode; /* flag to indicate that we're working with a smoothstreaming fragment
                      * Mss doesn't have 'moov' or any information about the streams format,
                      * requiring qtdemux to expose and create the streams */
  guint64 fragment_start;
  guint64 fragment_start_offset;

  gint64 chapters_track_id;

  /* protection support */
  GPtrArray *protection_system_ids; /* Holds identifiers of all content protection systems for all tracks */
  GQueue protection_event_queue; /* holds copy of upstream protection events */
  guint64 cenc_aux_info_offset;
  guint8 *cenc_aux_info_sizes;
  guint32 cenc_aux_sample_count;

  /* mpu specific variables */
  guint64 mpu_offset;
  guint32 mpu_seq_num;
  gchar *asset_id;
  gboolean has_mmth;
  gboolean is_mmth_timed;
  gboolean ignore_hintsample;

  gboolean thumbnail_mode;
  gboolean isInterleaved;
  gboolean isBigData;
  gboolean isStartKeyFrame;

#if defined (MPEGDASH_MODE) || defined (ATSC3_MODE)
  /* mpegdash mode flag (jaehoon.shim@lge.com) */
  gboolean dash_mode;
  GstClockTimeDiff dash_pts_offset;
  guint64 dash_fragment_start;
  GstClockTime dash_segment_start;
  guint64 dash_period_start;
  gint64 dash_subtitle_offset;
  guint dash_subtitle_index;
  gboolean use_svp;
#endif

#ifdef ATSC3_MODE
  gboolean atsc3_mode;
  GstClockTime prev_decode_time;
  gboolean configure_dvr;
#endif

#ifdef DOLBYHDR_SUPPORT
  /* Dolby HDR */
  gboolean dolby_vision_support;
  gboolean is_dolby_hdr;
  gboolean has_dolby_bl_cand;
  gboolean has_dolby_el_cand;

  /* Dolby HDR dvcC info. */
  gint8 dv_profile;
  gboolean rpu_present_flag;
  gboolean el_present_flag;
  gboolean bl_present_flag;
#endif
  guint32 dlna_opval;

  gint highest_temporal_id;
  gint preselection_id;
};

struct _GstQTDemuxClass
{
  GstElementClass parent_class;

#if defined (MPEGDASH_MODE) || defined (ATSC3_MODE)
  void (*start_time) (GstQTDemux * qtdemux, gpointer start_time, gpointer updated_start_time, gpointer time);
#endif
};

GType gst_qtdemux_get_type (void);

G_END_DECLS
#endif /* __GST_QTDEMUX_H__ */
