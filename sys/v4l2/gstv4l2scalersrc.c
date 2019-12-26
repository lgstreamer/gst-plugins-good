/* GStreamer
 *
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *               2006 Edgard Lima <edgard.lima@gmail.com>
 *               2019 LG Electronics, Inc.
 *
 * gstv4l2scalersrc.c: Video4Linux2 scaler source element
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
 * SECTION:element-v4l2scalersrc
 *
 * v4l2scalersrc is for capturing video from scaler v4l2 devices, it provides
 * scaled video output.
 *
 * The v4l2scalersrc has a dependency on the media pipeline and it developed for
 * graphic playback and can not be used for other purposes. Therefore, we do
 * not recommend expanding or folking this plugin in anyway.
 * Final goal is to write the scaler plugin as a gstv4l2transform type instead of
 * using this v4l2scalersrc plugin.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "ext/videodev2.h"

#include <fcntl.h>
#include <linux/v4l2-ext/videodev2-ext.h>
#include <linux/v4l2-ext/v4l2-controls-ext.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include "gstv4l2scalersrc.h"

#include "gst/gst-i18n-plugin.h"

GST_DEBUG_CATEGORY (v4l2scalersrc_debug);
#define GST_CAT_DEFAULT v4l2scalersrc_debug

#define DEFAULT_PROP_DEVICE  V4L2_EXT_DEV_PATH_GPSCALER

enum
{
  PROP_0,
  V4L2_STD_OBJECT_PROPS,
  PROP_VDEC_INDEX,
  PROP_MAX_WIDTH,
  PROP_MAX_HEIGHT,
  PROP_SCALABLE,
  PROP_CAPS,
  PROP_LAST
};

/* signals and args */
enum
{
  SIGNAL_PRE_SET_FORMAT,
  LAST_SIGNAL
};

static guint gst_v4l2_signals[LAST_SIGNAL] = { 0 };

static void gst_v4l2_scaler_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

#define gst_v4l2_scaler_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstV4l2ScalerSrc, gst_v4l2_scaler_src,
    GST_TYPE_PUSH_SRC, G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER,
        gst_v4l2_scaler_src_uri_handler_init));

static void gst_v4l2_scaler_src_finalize (GstV4l2ScalerSrc * v4l2scalersrc);

/* element methods */
static GstStateChangeReturn gst_v4l2_scaler_src_change_state (GstElement *
    element, GstStateChange transition);

/* basesrc methods */
static gboolean gst_v4l2_scaler_src_start (GstBaseSrc * src);
static gboolean gst_v4l2_scaler_src_unlock (GstBaseSrc * src);
static gboolean gst_v4l2_scaler_src_unlock_stop (GstBaseSrc * src);
static gboolean gst_v4l2_scaler_src_stop (GstBaseSrc * src);
static GstCaps *gst_v4l2_scaler_src_get_caps (GstBaseSrc * src,
    GstCaps * filter);
static gboolean gst_v4l2_scaler_src_query (GstBaseSrc * bsrc, GstQuery * query);
static gboolean gst_v4l2_scaler_src_decide_allocation (GstBaseSrc * src,
    GstQuery * query);
static GstFlowReturn gst_v4l2_scaler_src_create (GstPushSrc * src,
    GstBuffer ** out);
static GstCaps *gst_v4l2_scaler_src_fixate (GstBaseSrc * basesrc,
    GstCaps * caps, GstStructure * pref_s);
static gboolean gst_v4l2_scaler_src_negotiate (GstBaseSrc * basesrc);

static void gst_v4l2_scaler_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_v4l2_scaler_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_v4l2_scaler_src_class_init (GstV4l2ScalerSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseSrcClass *basesrc_class;
  GstPushSrcClass *pushsrc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  basesrc_class = GST_BASE_SRC_CLASS (klass);
  pushsrc_class = GST_PUSH_SRC_CLASS (klass);

  gobject_class->finalize = (GObjectFinalizeFunc) gst_v4l2_scaler_src_finalize;
  gobject_class->set_property = gst_v4l2_scaler_src_set_property;
  gobject_class->get_property = gst_v4l2_scaler_src_get_property;

  element_class->change_state = gst_v4l2_scaler_src_change_state;

  gst_v4l2_object_install_properties_helper (gobject_class,
      DEFAULT_PROP_DEVICE);

  g_object_class_install_property (gobject_class, PROP_VDEC_INDEX,
      g_param_spec_int ("vdec-index", "VDEC index",
          "VDEC instance number", 0,
          7, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_MAX_WIDTH,
      g_param_spec_int ("max-width", "Max frame size",
          "Max width of the frame", 0,
          1920, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_MAX_HEIGHT,
      g_param_spec_int ("max-height", "Max frame size",
          "Max height of the frame", 0,
          1080, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_CONTROLLABLE));

  g_object_class_install_property (gobject_class, PROP_SCALABLE,
      g_param_spec_boolean ("scalable", "Scalable",
          "Able to scale", TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CAPS,
      g_param_spec_boxed ("caps", "src caps",
          "The caps of srcpad. It is used to notify and configure as a proper "
          "destination window size to the pipeline", GST_TYPE_CAPS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstV4l2ScalerSrc::prepare-format:
   * @v4l2scalersrc: the v4l2scalersrc instance
   * @fd: the file descriptor of the current device
   * @caps: the caps of the format being set
   *
   * This signal gets emitted before calling the v4l2 VIDIOC_S_FMT ioctl
   * (set format). This allows for any custom configuration of the device to
   * happen prior to the format being set.
   * This is mostly useful for UVC H264 encoding cameras which need the H264
   * Probe & Commit to happen prior to the normal Probe & Commit.
   */
  gst_v4l2_signals[SIGNAL_PRE_SET_FORMAT] = g_signal_new ("prepare-format",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_INT, GST_TYPE_CAPS);

  gst_element_class_set_static_metadata (element_class,
      "Video (video4linux2) Source", "Source/Video",
      "Reads frames from a Video4Linux2 device",
      "Edgard Lima <edgard.lima@gmail.com>, "
      "Stefan Kost <ensonic@users.sf.net>");

  gst_element_class_add_pad_template
      (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          gst_v4l2_object_get_all_caps ()));

  basesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_v4l2_scaler_src_get_caps);
  basesrc_class->start = GST_DEBUG_FUNCPTR (gst_v4l2_scaler_src_start);
  basesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_v4l2_scaler_src_unlock);
  basesrc_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_v4l2_scaler_src_unlock_stop);
  basesrc_class->stop = GST_DEBUG_FUNCPTR (gst_v4l2_scaler_src_stop);
  basesrc_class->query = GST_DEBUG_FUNCPTR (gst_v4l2_scaler_src_query);
  basesrc_class->negotiate = GST_DEBUG_FUNCPTR (gst_v4l2_scaler_src_negotiate);
  basesrc_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_v4l2_scaler_src_decide_allocation);

  pushsrc_class->create = GST_DEBUG_FUNCPTR (gst_v4l2_scaler_src_create);

  klass->v4l2_class_devices = NULL;

  GST_DEBUG_CATEGORY_INIT (v4l2scalersrc_debug, "v4l2scalersrc", 0,
      "V4L2 scaler source element");
}

static void
gst_v4l2_scaler_src_init (GstV4l2ScalerSrc * v4l2scalersrc)
{
  GstV4l2Object *v4l2object;
  /* fixme: give an update_fps_function */
  v4l2scalersrc->v4l2scalerobject =
      gst_v4l2_scaler_object_new (GST_ELEMENT (v4l2scalersrc),
      GST_OBJECT (GST_BASE_SRC_PAD (v4l2scalersrc)),
      V4L2_BUF_TYPE_VIDEO_CAPTURE, DEFAULT_PROP_DEVICE, gst_v4l2_get_input,
      gst_v4l2_set_input, NULL);

  v4l2object = (GstV4l2Object *) v4l2scalersrc->v4l2scalerobject;

  /* Avoid the slow probes */
  v4l2object->skip_try_fmt_probes = TRUE;

  gst_base_src_set_format (GST_BASE_SRC (v4l2scalersrc), GST_FORMAT_TIME);
}


static void
gst_v4l2_scaler_src_finalize (GstV4l2ScalerSrc * v4l2scalersrc)
{
  gst_v4l2_scaler_object_destroy (v4l2scalersrc->v4l2scalerobject);

  G_OBJECT_CLASS (parent_class)->finalize ((GObject *) (v4l2scalersrc));
}


static void
gst_v4l2_scaler_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstV4l2ScalerSrc *v4l2scalersrc = gst_v4l2_scaler_src (object);

  if (!gst_v4l2_object_set_property_helper (
          (GstV4l2Object *) v4l2scalersrc->v4l2scalerobject,
          prop_id, value, pspec)) {
    switch (prop_id) {
      case PROP_VDEC_INDEX:{
        v4l2scalersrc->v4l2scalerobject->vdec_index = g_value_get_int (value);
        break;
      }
      case PROP_MAX_WIDTH:{
        v4l2scalersrc->v4l2scalerobject->max_width = g_value_get_int (value);
        break;
      }
      case PROP_MAX_HEIGHT:{
        v4l2scalersrc->v4l2scalerobject->max_height = g_value_get_int (value);
        break;
      }
      case PROP_SCALABLE:{
        v4l2scalersrc->v4l2scalerobject->scalable = g_value_get_boolean (value);
        break;
      }
      case PROP_CAPS:{
        GstCaps *new_caps;
        GstCaps *old_caps;
        const GstCaps *new_caps_val = gst_value_get_caps (value);

        if (new_caps_val == NULL) {
          new_caps = gst_caps_new_any ();
        } else {
          new_caps = (GstCaps *) new_caps_val;
          gst_caps_ref (new_caps);
        }

        GST_OBJECT_LOCK (v4l2scalersrc);
        old_caps = v4l2scalersrc->v4l2scalerobject->destination_caps;
        v4l2scalersrc->v4l2scalerobject->destination_caps = new_caps;
        GST_OBJECT_UNLOCK (v4l2scalersrc);

        if (old_caps)
          gst_caps_unref (old_caps);

        GST_DEBUG_OBJECT (v4l2scalersrc, "set new caps %" GST_PTR_FORMAT,
            new_caps);

        gst_pad_mark_reconfigure (GST_BASE_SRC_PAD (v4l2scalersrc));
        break;
      }
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
  }
}

static void
gst_v4l2_scaler_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstV4l2ScalerSrc *v4l2scalersrc = gst_v4l2_scaler_src (object);

  if (!gst_v4l2_object_get_property_helper (
          (GstV4l2Object *) v4l2scalersrc->v4l2scalerobject,
          prop_id, value, pspec)) {
    switch (prop_id) {
      case PROP_VDEC_INDEX:{
        g_value_set_int (value, v4l2scalersrc->v4l2scalerobject->vdec_index);
        break;
      }
      case PROP_MAX_WIDTH:{
        g_value_set_int (value, v4l2scalersrc->v4l2scalerobject->max_width);
        break;
      }
      case PROP_MAX_HEIGHT:{
        g_value_set_int (value, v4l2scalersrc->v4l2scalerobject->max_height);
        break;
      }
      case PROP_SCALABLE:{
        g_value_set_boolean (value, v4l2scalersrc->v4l2scalerobject->scalable);
        break;
      }
      case PROP_CAPS:
        GST_OBJECT_LOCK (v4l2scalersrc);
        gst_value_set_caps (value,
            v4l2scalersrc->v4l2scalerobject->destination_caps);
        GST_OBJECT_UNLOCK (v4l2scalersrc);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
  }
}

struct PreferedCapsInfo
{
  gint width;
  gint height;
  gint fps_n;
  gint fps_d;
};

static gboolean
gst_vl42_src_fixate_fields (GQuark field_id, GValue * value, gpointer user_data)
{
  GstStructure *s = user_data;

  if (field_id == g_quark_from_string ("interlace-mode"))
    return TRUE;

  if (field_id == g_quark_from_string ("colorimetry"))
    return TRUE;

  gst_structure_fixate_field (s, g_quark_to_string (field_id));

  return TRUE;
}

static void
gst_v4l2_src_fixate_struct_with_preference (GstStructure * s,
    struct PreferedCapsInfo *pref)
{
  if (gst_structure_has_field (s, "width"))
    gst_structure_fixate_field_nearest_int (s, "width", pref->width);

  if (gst_structure_has_field (s, "height"))
    gst_structure_fixate_field_nearest_int (s, "height", pref->height);

  if (gst_structure_has_field (s, "framerate"))
    gst_structure_fixate_field_nearest_fraction (s, "framerate", pref->fps_n,
        pref->fps_d);

  /* Finally, fixate everything else except the interlace-mode and colorimetry
   * which still need further negotiation as it wasn't probed */
  gst_structure_map_in_place (s, gst_vl42_src_fixate_fields, s);
}

static void
gst_v4l2_src_parse_fixed_struct (GstStructure * s,
    gint * width, gint * height, gint * fps_n, gint * fps_d)
{
  if (gst_structure_has_field (s, "width") && width)
    gst_structure_get_int (s, "width", width);

  if (gst_structure_has_field (s, "height") && height)
    gst_structure_get_int (s, "height", height);

  if (gst_structure_has_field (s, "framerate") && fps_n && fps_d)
    gst_structure_get_fraction (s, "framerate", fps_n, fps_d);
}

/* TODO Consider framerate */
static gint
gst_v4l2_scaler_src_fixed_caps_compare (GstCaps * caps_a, GstCaps * caps_b,
    struct PreferedCapsInfo *pref)
{
  GstStructure *a, *b;
  gint aw = G_MAXINT, ah = G_MAXINT, ad = G_MAXINT;
  gint bw = G_MAXINT, bh = G_MAXINT, bd = G_MAXINT;
  gint ret;

  a = gst_caps_get_structure (caps_a, 0);
  b = gst_caps_get_structure (caps_b, 0);

  gst_v4l2_src_parse_fixed_struct (a, &aw, &ah, NULL, NULL);
  gst_v4l2_src_parse_fixed_struct (b, &bw, &bh, NULL, NULL);

  /* When both are smaller then pref, just append to the end */
  if ((bw < pref->width || bh < pref->height)
      && (aw < pref->width || ah < pref->height)) {
    ret = 1;
    goto done;
  }

  /* If a is smaller then pref and not b, then a goes after b */
  if (aw < pref->width || ah < pref->height) {
    ret = 1;
    goto done;
  }

  /* If b is smaller then pref and not a, then a goes before b */
  if (bw < pref->width || bh < pref->height) {
    ret = -1;
    goto done;
  }

  /* Both are larger or equal to the preference, prefer the smallest */
  ad = MAX (1, aw - pref->width) * MAX (1, ah - pref->height);
  bd = MAX (1, bw - pref->width) * MAX (1, bh - pref->height);

  /* Adjust slightly in case width/height matched the preference */
  if (aw == pref->width)
    ad -= 1;

  if (ah == pref->height)
    ad -= 1;

  if (bw == pref->width)
    bd -= 1;

  if (bh == pref->height)
    bd -= 1;

  /* If the choices are equivalent, maintain the order */
  if (ad == bd)
    ret = 1;
  else
    ret = ad - bd;

done:
  GST_TRACE ("Placing %ix%i (%s) %s %ix%i (%s)", aw, ah,
      gst_structure_get_string (a, "format"), ret > 0 ? "after" : "before", bw,
      bh, gst_structure_get_string (b, "format"));
  return ret;
}

static gboolean
gst_v4l2_scaler_src_set_format (GstV4l2ScalerSrc * v4l2scalersrc,
    GstCaps * caps, GstV4l2Error * error)
{
  GstV4l2Object *obj;

  obj = (GstV4l2Object *) v4l2scalersrc->v4l2scalerobject;

  /* make sure we stop capturing and dealloc buffers */
  if (!gst_v4l2_object_stop (obj))
    return FALSE;

  g_signal_emit (v4l2scalersrc, gst_v4l2_signals[SIGNAL_PRE_SET_FORMAT], 0,
      obj->video_fd, caps);

  return gst_v4l2_object_set_format (obj, caps, error);
}

static GstCaps *
gst_v4l2_scaler_src_fixate (GstBaseSrc * basesrc, GstCaps * caps,
    GstStructure * pref_s)
{
  /* Let's prefer a good resolutiion as of today's standard. */
  struct PreferedCapsInfo pref = {
    3840, 2160, 120, 1
  };
  GstV4l2ScalerSrc *v4l2scalersrc = gst_v4l2_scaler_src (basesrc);
  GstV4l2Object *obj = (GstV4l2Object *) v4l2scalersrc->v4l2scalerobject;
  GList *caps_list = NULL;
  GstStructure *s;
  gint i = G_MAXINT;
  GstV4l2Error error = GST_V4L2_ERROR_INIT;
  GstCaps *fcaps = NULL;

  GST_DEBUG_OBJECT (basesrc, "fixating caps %" GST_PTR_FORMAT, caps);

  /* We consider the first structure from peercaps to be a preference. This is
   * useful for matching a reported native display, or simply to avoid
   * transformation to happen downstream. */
  if (pref_s) {
    pref_s = gst_structure_copy (pref_s);
    gst_v4l2_src_fixate_struct_with_preference (pref_s, &pref);
    gst_v4l2_src_parse_fixed_struct (pref_s, &pref.width, &pref.height,
        &pref.fps_n, &pref.fps_d);
    gst_structure_free (pref_s);
  }

  GST_DEBUG_OBJECT (basesrc, "Prefered size %ix%i", pref.width, pref.height);

  /* Sort the structures to get the caps that is nearest to our preferences,
   * first, Use single struct caps for sorting so we preserver the features. */
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstCaps *tmp = gst_caps_copy_nth (caps, i);

    s = gst_caps_get_structure (tmp, 0);
    gst_v4l2_src_fixate_struct_with_preference (s, &pref);
    caps_list = g_list_insert_sorted_with_data (caps_list, tmp,
        (GCompareDataFunc) gst_v4l2_scaler_src_fixed_caps_compare, &pref);
  }

  gst_caps_unref (caps);
  caps = gst_caps_new_empty ();

  while (caps_list) {
    GstCaps *tmp = caps_list->data;
    caps_list = g_list_delete_link (caps_list, caps_list);
    gst_caps_append (caps, tmp);
  }

  GST_DEBUG_OBJECT (basesrc, "sorted and normalized caps %" GST_PTR_FORMAT,
      caps);

  /* Each structure in the caps has been fixated, except for the
   * interlace-mode and colorimetry. Now normalize the caps so we can
   * enumerate the possibilities */
  caps = gst_caps_normalize (caps);

  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    gst_v4l2_clear_error (&error);
    if (fcaps)
      gst_caps_unref (fcaps);

    fcaps = gst_caps_copy_nth (caps, i);

    if (GST_V4L2_IS_ACTIVE (obj)) {
      /* try hard to avoid TRY_FMT since some UVC camera just crash when this
       * is called at run-time. */
      if (gst_v4l2_object_caps_is_subset (obj, fcaps)) {
        gst_caps_unref (fcaps);
        fcaps = gst_v4l2_object_get_current_caps (obj);
        break;
      }

      /* Just check if the format is acceptable, once we know
       * no buffers should be outstanding we try S_FMT.
       *
       * Basesrc will do an allocation query that
       * should indirectly reclaim buffers, after that we can
       * set the format and then configure our pool */
      if (gst_v4l2_object_try_format (obj, fcaps, &error)) {
        /* make sure the caps changed before doing anything */
        if (gst_v4l2_object_caps_equal (obj, fcaps))
          break;

        v4l2scalersrc->renegotiation_adjust = v4l2scalersrc->offset + 1;
        v4l2scalersrc->pending_set_fmt = TRUE;
        break;
      }
    } else {
      if (gst_v4l2_scaler_src_set_format (v4l2scalersrc, fcaps, &error))
        break;
    }

    /* Only EIVAL make sense, report any other errors, this way we don't keep
     * probing if the device got disconnected, or if it's firmware stopped
     * responding */
    if (error.error->code != GST_RESOURCE_ERROR_SETTINGS) {
      i = G_MAXINT;
      break;
    }
  }

  if (i >= gst_caps_get_size (caps)) {
    gst_v4l2_error (v4l2scalersrc, &error);
    if (fcaps)
      gst_caps_unref (fcaps);
    gst_caps_unref (caps);
    return NULL;
  }

  gst_caps_unref (caps);

  GST_DEBUG_OBJECT (basesrc, "fixated caps %" GST_PTR_FORMAT, fcaps);

  return fcaps;
}

static gboolean
gst_v4l2_scaler_src_negotiate (GstBaseSrc * basesrc)
{
  GstCaps *thiscaps;
  GstCaps *caps = NULL;
  GstCaps *peercaps = NULL;
  gboolean result = FALSE;

  /* first see what is possible on our source pad */
  thiscaps = gst_pad_query_caps (GST_BASE_SRC_PAD (basesrc), NULL);
  GST_DEBUG_OBJECT (basesrc, "caps of src: %" GST_PTR_FORMAT, thiscaps);

  /* nothing or anything is allowed, we're done */
  if (thiscaps == NULL || gst_caps_is_any (thiscaps))
    goto no_nego_needed;

  /* get the peer caps without a filter as we'll filter ourselves later on */
  peercaps = gst_pad_peer_query_caps (GST_BASE_SRC_PAD (basesrc), NULL);
  GST_DEBUG_OBJECT (basesrc, "caps of peer: %" GST_PTR_FORMAT, peercaps);
  if (peercaps && !gst_caps_is_any (peercaps)) {
    /* Prefer the first caps we are compatible with that the peer proposed */
    caps = gst_caps_intersect_full (peercaps, thiscaps,
        GST_CAPS_INTERSECT_FIRST);

    GST_DEBUG_OBJECT (basesrc, "intersect: %" GST_PTR_FORMAT, caps);

    gst_caps_unref (thiscaps);
  } else {
    /* no peer or peer have ANY caps, work with our own caps then */
    caps = thiscaps;
  }

  if (caps) {
    /* now fixate */
    if (!gst_caps_is_empty (caps)) {
      GstStructure *pref = NULL;

      if (peercaps && !gst_caps_is_any (peercaps))
        pref = gst_caps_get_structure (peercaps, 0);

      caps = gst_v4l2_scaler_src_fixate (basesrc, caps, pref);

      /* Fixating may fail as we now set the selected format */
      if (!caps) {
        result = FALSE;
        goto done;
      }

      GST_DEBUG_OBJECT (basesrc, "fixated to: %" GST_PTR_FORMAT, caps);

      if (gst_caps_is_any (caps)) {
        /* hmm, still anything, so element can do anything and
         * nego is not needed */
        result = TRUE;
      } else if (gst_caps_is_fixed (caps)) {
        /* yay, fixed caps, use those then */
        result = gst_base_src_set_caps (basesrc, caps);
      }
    }
    gst_caps_unref (caps);
  }

done:
  if (peercaps)
    gst_caps_unref (peercaps);

  return result;

no_nego_needed:
  {
    GST_DEBUG_OBJECT (basesrc, "no negotiation needed");
    if (thiscaps)
      gst_caps_unref (thiscaps);
    return TRUE;
  }
}

static GstCaps *
gst_v4l2_scaler_src_get_caps (GstBaseSrc * src, GstCaps * filter)
{
  GstV4l2ScalerSrc *v4l2scalersrc;
  GstV4l2Object *obj;
  GstCaps *ret;

  v4l2scalersrc = gst_v4l2_scaler_src (src);
  obj = (GstV4l2Object *) v4l2scalersrc->v4l2scalerobject;

  if (!GST_V4L2_IS_OPEN (obj)) {
    GstCaps *templ_caps =
        gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (v4l2scalersrc));

    GST_OBJECT_LOCK (v4l2scalersrc);
    if (!v4l2scalersrc->v4l2scalerobject->destination_caps) {
      GST_OBJECT_UNLOCK (v4l2scalersrc);
      return templ_caps;
    }

    ret = gst_caps_intersect_full (templ_caps,
        v4l2scalersrc->v4l2scalerobject->destination_caps,
        GST_CAPS_INTERSECT_FIRST);
    GST_OBJECT_UNLOCK (v4l2scalersrc);

    gst_caps_unref (templ_caps);
    return ret;
  }

  GST_OBJECT_LOCK (v4l2scalersrc);
  ret =
      gst_v4l2_scaler_object_get_caps (v4l2scalersrc->v4l2scalerobject, filter);
  GST_OBJECT_UNLOCK (v4l2scalersrc);

  return ret;
}

static gboolean
gst_v4l2_scaler_src_decide_allocation (GstBaseSrc * bsrc, GstQuery * query)
{
  GstV4l2ScalerSrc *src = gst_v4l2_scaler_src (bsrc);
  GstV4l2Object *obj = (GstV4l2Object *) src->v4l2scalerobject;
  gboolean ret = TRUE;

  if (src->pending_set_fmt) {
    GstCaps *caps = gst_pad_get_current_caps (GST_BASE_SRC_PAD (bsrc));
    GstV4l2Error error = GST_V4L2_ERROR_INIT;

    caps = gst_caps_make_writable (caps);
    if (!(ret = gst_v4l2_scaler_src_set_format (src, caps, &error)))
      gst_v4l2_error (src, &error);

    gst_caps_unref (caps);
    src->pending_set_fmt = FALSE;
  } else if (gst_buffer_pool_is_active (obj->pool)) {
    /* Trick basesrc into not deactivating the active pool. Renegotiating here
     * would otherwise turn off and on the camera. */
    GstAllocator *allocator;
    GstAllocationParams params;
    GstBufferPool *pool;

    gst_base_src_get_allocator (bsrc, &allocator, &params);
    pool = gst_base_src_get_buffer_pool (bsrc);

    if (gst_query_get_n_allocation_params (query))
      gst_query_set_nth_allocation_param (query, 0, allocator, &params);
    else
      gst_query_add_allocation_param (query, allocator, &params);

    if (gst_query_get_n_allocation_pools (query))
      gst_query_set_nth_allocation_pool (query, 0, pool, obj->info.size, 1, 0);
    else
      gst_query_add_allocation_pool (query, pool, obj->info.size, 1, 0);

    if (pool)
      gst_object_unref (pool);
    if (allocator)
      gst_object_unref (allocator);

    return GST_BASE_SRC_CLASS (parent_class)->decide_allocation (bsrc, query);
  }

  if (ret) {
    ret =
        gst_v4l2_scaler_object_decide_allocation (src->v4l2scalerobject, query);
    if (ret)
      ret = GST_BASE_SRC_CLASS (parent_class)->decide_allocation (bsrc, query);
  }

  if (ret) {
    if (!gst_buffer_pool_set_active (obj->pool, TRUE))
      goto activate_failed;
  }

  return ret;

activate_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS,
        (_("Failed to allocate required memory.")),
        ("Buffer pool activation failed"));
    return FALSE;
  }
}

static gboolean
gst_v4l2_scaler_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  GstV4l2ScalerSrc *src;
  GstV4l2Object *obj;
  gboolean res = FALSE;

  src = gst_v4l2_scaler_src (bsrc);
  obj = (GstV4l2Object *) src->v4l2scalerobject;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:{
      GstClockTime min_latency, max_latency;
      guint32 fps_n, fps_d;
      guint num_buffers = 0;

      /* device must be open */
      if (!GST_V4L2_IS_OPEN (obj)) {
        GST_WARNING_OBJECT (src,
            "Can't give latency since device isn't open !");
        goto done;
      }

      fps_n = GST_V4L2_FPS_N (obj);
      fps_d = GST_V4L2_FPS_D (obj);

      /* we must have a framerate */
      if (fps_n <= 0 || fps_d <= 0) {
        GST_WARNING_OBJECT (src,
            "Can't give latency since framerate isn't fixated !");
        goto done;
      }

      /* min latency is the time to capture one frame */
      min_latency = gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);

      /* max latency is total duration of the frame buffer */
      if (obj->pool != NULL)
        num_buffers = GST_V4L2_BUFFER_POOL_CAST (obj->pool)->max_latency;

      if (num_buffers == 0)
        max_latency = -1;
      else
        max_latency = num_buffers * min_latency;

      GST_DEBUG_OBJECT (bsrc,
          "report latency min %" GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
          GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

      /* we are always live, the min latency is 1 frame and the max latency is
       * the complete buffer of frames. */
      gst_query_set_latency (query, TRUE, min_latency, max_latency);

      res = TRUE;
      break;
    }
    default:
      res = GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
      break;
  }

done:

  return res;
}

/* start and stop are not symmetric -- start will open the device, but not start
 * capture. it's setcaps that will start capture, which is called via basesrc's
 * negotiate method. stop will both stop capture and close the device.
 */
static gboolean
gst_v4l2_scaler_src_start (GstBaseSrc * src)
{
  GstV4l2ScalerSrc *v4l2scalersrc = gst_v4l2_scaler_src (src);

  v4l2scalersrc->offset = 0;
  v4l2scalersrc->renegotiation_adjust = 0;

  /* activate settings for first frame */
  v4l2scalersrc->ctrl_time = 0;
  gst_object_sync_values (GST_OBJECT (src), v4l2scalersrc->ctrl_time);

  v4l2scalersrc->has_bad_timestamp = FALSE;
  v4l2scalersrc->last_timestamp = 0;

  return TRUE;
}

static gboolean
gst_v4l2_scaler_src_unlock (GstBaseSrc * src)
{
  GstV4l2ScalerSrc *v4l2scalersrc = gst_v4l2_scaler_src (src);
  GstV4l2Object *obj = (GstV4l2Object *) v4l2scalersrc->v4l2scalerobject;

  return gst_v4l2_object_unlock (obj);
}

static gboolean
gst_v4l2_scaler_src_unlock_stop (GstBaseSrc * src)
{
  GstV4l2ScalerSrc *v4l2scalersrc = gst_v4l2_scaler_src (src);
  GstV4l2Object *obj = (GstV4l2Object *) v4l2scalersrc->v4l2scalerobject;

  v4l2scalersrc->last_timestamp = 0;

  return gst_v4l2_object_unlock_stop (obj);
}

static gboolean
gst_v4l2_scaler_src_stop (GstBaseSrc * src)
{
  GstV4l2ScalerSrc *v4l2scalersrc = gst_v4l2_scaler_src (src);
  GstV4l2Object *obj = (GstV4l2Object *) v4l2scalersrc->v4l2scalerobject;

  if (GST_V4L2_IS_ACTIVE (obj)) {
    if (!gst_v4l2_object_stop (obj))
      return FALSE;
  }

  v4l2scalersrc->pending_set_fmt = FALSE;

  return TRUE;
}

static gboolean
gst_v4l2_scaler_src_connect_vdo_to_vdec (GstV4l2ScalerSrc * src,
    guint control_id)
{
  gint fd = -1;
  struct v4l2_ext_controls ext_controls;
  struct v4l2_ext_control ext_control;
  struct v4l2_ext_vdec_vdo_connection vdo_con;

  if (control_id == V4L2_CID_EXT_VDO_VDEC_CONNECTING) {
    fd = open (V4L2_EXT_DEV_PATH_VDOGAV, O_RDWR);
    if (fd < 0) {
      GST_WARNING_OBJECT (src, "Could not open device '%s'", strerror (errno));
      return FALSE;
    }
    src->v4l2scalerobject->vdo_fd = fd;
  } else if (control_id == V4L2_CID_EXT_VDO_VDEC_DISCONNECTING) {
    fd = src->v4l2scalerobject->vdo_fd;
  } else {
    GST_WARNING_OBJECT (src, "Invalid control_id %d", control_id);
    return FALSE;
  }

  memset (&ext_controls, 0, sizeof (struct v4l2_ext_controls));
  memset (&ext_control, 0, sizeof (struct v4l2_ext_control));
  memset (&vdo_con, 0, sizeof (struct v4l2_ext_vdec_vdo_connection));

  vdo_con.vdo_port = 3;         //vdo port number
  vdo_con.vdec_port = src->v4l2scalerobject->vdec_index;        //vdec port number
  ext_controls.ctrl_class = V4L2_CTRL_CLASS_USER;
  ext_controls.count = 1;
  ext_controls.controls = &ext_control;
  ext_controls.controls->id = control_id;
  ext_controls.controls->ptr = (void *) &vdo_con;

  if (ioctl (fd, VIDIOC_S_EXT_CTRLS, &ext_controls) < 0) {
    GST_WARNING_OBJECT (src, "Failed to connect vdo to vdec '%s'",
        strerror (errno));
    return FALSE;
  }

  if (control_id == V4L2_CID_EXT_VDO_VDEC_DISCONNECTING)
    close (fd);

  return TRUE;
}

static gboolean
gst_v4l2_scaler_src_set_max_frame_size (GstV4l2ScalerSrc * src, gint max_width,
    gint max_height)
{
  GstV4l2Object *obj = (GstV4l2Object *) src->v4l2scalerobject;
  struct v4l2_control ctrl_arg;

  ctrl_arg.id = V4L2_CID_EXT_GPSCALER_MAX_FRAME_SIZE;
  ctrl_arg.value = max_width << 16 | max_height;

  if (ioctl (obj->video_fd, VIDIOC_S_CTRL, &ctrl_arg) < 0) {
    GST_WARNING_OBJECT (src, "Failed to set max frame size '%s'",
        strerror (errno));
    return FALSE;
  }

  return TRUE;
}

static GstStateChangeReturn
gst_v4l2_scaler_src_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstV4l2ScalerSrc *v4l2scalersrc = gst_v4l2_scaler_src (element);
  GstV4l2Object *obj = (GstV4l2Object *) v4l2scalersrc->v4l2scalerobject;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* open the device */
      if (!gst_v4l2_object_open (obj))
        return GST_STATE_CHANGE_FAILURE;

      if (v4l2scalersrc->v4l2scalerobject->scalable) {
        /* connect video decoder with vdo(video decoder output) dedicated for
         * graphic rendering */
        if (!gst_v4l2_scaler_src_connect_vdo_to_vdec (v4l2scalersrc,
                V4L2_CID_EXT_VDO_VDEC_CONNECTING)) {
          gst_v4l2_object_close (obj);
          return GST_STATE_CHANGE_FAILURE;
        }

        if (v4l2scalersrc->v4l2scalerobject->max_width > 0
            && v4l2scalersrc->v4l2scalerobject->max_height > 0) {
          GST_DEBUG_OBJECT (v4l2scalersrc, "set maximum framesize to width %u, "
              "height %u", v4l2scalersrc->v4l2scalerobject->max_width,
              v4l2scalersrc->v4l2scalerobject->max_height);
          if (!gst_v4l2_scaler_src_set_max_frame_size (v4l2scalersrc,
                  v4l2scalersrc->v4l2scalerobject->max_width,
                  v4l2scalersrc->v4l2scalerobject->max_height)) {
            GST_WARNING_OBJECT (v4l2scalersrc,
                "failed to set maximum framesize");
            gst_v4l2_object_close (obj);
            return GST_STATE_CHANGE_FAILURE;
          }
        }
      }

      /* call gst_v4l2_set_input */
      if (!obj->set_in_out_func (obj,
              v4l2scalersrc->v4l2scalerobject->vdec_index)) {
        gst_v4l2_object_close (obj);
        return GST_STATE_CHANGE_FAILURE;
      }

      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* close the device */
      if (!gst_v4l2_object_close (obj)) {
        return GST_STATE_CHANGE_FAILURE;
      }

      if (v4l2scalersrc->v4l2scalerobject->scalable) {
        /* disconnect vdo to vdec */
        gst_v4l2_scaler_src_connect_vdo_to_vdec (v4l2scalersrc,
            V4L2_CID_EXT_VDO_VDEC_DISCONNECTING);
      }

      break;
    default:
      break;
  }

  return ret;
}

static GstFlowReturn
gst_v4l2_scaler_src_create (GstPushSrc * src, GstBuffer ** buf)
{
  GstV4l2ScalerSrc *v4l2scalersrc = gst_v4l2_scaler_src (src);
  GstV4l2Object *obj = (GstV4l2Object *) v4l2scalersrc->v4l2scalerobject;
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL_CAST (obj->pool);
  GstFlowReturn ret;
  GstClock *clock;
  GstClockTime abs_time, base_time, timestamp, duration;
  GstClockTime delay;
  GstMessage *qos_msg;

  do {
    ret = GST_BASE_SRC_CLASS (parent_class)->alloc (GST_BASE_SRC (src), 0,
        obj->info.size, buf);

    if (G_UNLIKELY (ret != GST_FLOW_OK))
      goto alloc_failed;

    ret = gst_v4l2_buffer_pool_process (pool, buf);

  } while (ret == GST_V4L2_FLOW_CORRUPTED_BUFFER);

  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto error;

  timestamp = GST_BUFFER_TIMESTAMP (*buf);
  duration = obj->duration;

  /* timestamps, LOCK to get clock and base time. */
  /* FIXME: element clock and base_time is rarely changing */
  GST_OBJECT_LOCK (v4l2scalersrc);
  if ((clock = GST_ELEMENT_CLOCK (v4l2scalersrc))) {
    /* we have a clock, get base time and ref clock */
    base_time = GST_ELEMENT (v4l2scalersrc)->base_time;
    gst_object_ref (clock);
  } else {
    /* no clock, can't set timestamps */
    base_time = GST_CLOCK_TIME_NONE;
  }
  GST_OBJECT_UNLOCK (v4l2scalersrc);

  /* sample pipeline clock */
  if (clock) {
    abs_time = gst_clock_get_time (clock);
    gst_object_unref (clock);
  } else {
    abs_time = GST_CLOCK_TIME_NONE;
  }

retry:
  if (!v4l2scalersrc->has_bad_timestamp && timestamp != GST_CLOCK_TIME_NONE) {
    struct timespec now;
    GstClockTime gstnow;

    /* v4l2 specs say to use the system time although many drivers switched to
     * the more desirable monotonic time. We first try to use the monotonic time
     * and see how that goes */
    clock_gettime (CLOCK_MONOTONIC, &now);
    gstnow = GST_TIMESPEC_TO_TIME (now);

    if (timestamp > gstnow || (gstnow - timestamp) > (10 * GST_SECOND)) {
      GTimeVal now;

      /* very large diff, fall back to system time */
      g_get_current_time (&now);
      gstnow = GST_TIMEVAL_TO_TIME (now);
    }

    /* Detect buggy drivers here, and stop using their timestamp. Failing any
     * of these condition would imply a very buggy driver:
     *   - Timestamp in the future
     *   - Timestamp is going backward compare to last seen timestamp
     *   - Timestamp is jumping forward for less then a frame duration
     *   - Delay is bigger then the actual timestamp
     * */
    if (timestamp > gstnow) {
      GST_WARNING_OBJECT (v4l2scalersrc,
          "Timestamp in the future detected, ignoring driver timestamps");
      v4l2scalersrc->has_bad_timestamp = TRUE;
      goto retry;
    }

    if (v4l2scalersrc->last_timestamp > timestamp) {
      GST_WARNING_OBJECT (v4l2scalersrc,
          "Timestamp going backward, ignoring driver timestamps");
      v4l2scalersrc->has_bad_timestamp = TRUE;
      goto retry;
    }

    delay = gstnow - timestamp;

    if (delay > timestamp) {
      GST_WARNING_OBJECT (v4l2scalersrc,
          "Timestamp does not correlate with any clock, ignoring driver timestamps");
      v4l2scalersrc->has_bad_timestamp = TRUE;
      goto retry;
    }

    /* Save last timestamp for sanity checks */
    v4l2scalersrc->last_timestamp = timestamp;

    GST_DEBUG_OBJECT (v4l2scalersrc,
        "ts: %" GST_TIME_FORMAT " now %" GST_TIME_FORMAT " delay %"
        GST_TIME_FORMAT, GST_TIME_ARGS (timestamp), GST_TIME_ARGS (gstnow),
        GST_TIME_ARGS (delay));
  } else {
    /* we assume 1 frame latency otherwise */
    if (GST_CLOCK_TIME_IS_VALID (duration))
      delay = duration;
    else
      delay = 0;
  }

  /* set buffer metadata */

  if (G_LIKELY (abs_time != GST_CLOCK_TIME_NONE)) {
    /* the time now is the time of the clock minus the base time */
    timestamp = abs_time - base_time;

    /* adjust for delay in the device */
    if (timestamp > delay)
      timestamp -= delay;
    else
      timestamp = 0;
  } else {
    timestamp = GST_CLOCK_TIME_NONE;
  }

  /* activate settings for next frame */
  if (GST_CLOCK_TIME_IS_VALID (duration)) {
    v4l2scalersrc->ctrl_time += duration;
  } else {
    /* this is not very good (as it should be the next timestamp),
     * still good enough for linear fades (as long as it is not -1)
     */
    v4l2scalersrc->ctrl_time = timestamp;
  }
  gst_object_sync_values (GST_OBJECT (src), v4l2scalersrc->ctrl_time);

  GST_INFO_OBJECT (src, "sync to %" GST_TIME_FORMAT " out ts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (v4l2scalersrc->ctrl_time), GST_TIME_ARGS (timestamp));

  /* use generated offset values only if there are not already valid ones
   * set by the v4l2 device */
  if (!GST_BUFFER_OFFSET_IS_VALID (*buf)
      || !GST_BUFFER_OFFSET_END_IS_VALID (*buf)) {
    GST_BUFFER_OFFSET (*buf) = v4l2scalersrc->offset++;
    GST_BUFFER_OFFSET_END (*buf) = v4l2scalersrc->offset;
  } else {
    /* adjust raw v4l2 device sequence, will restart at null in case of renegotiation
     * (streamoff/streamon) */
    GST_BUFFER_OFFSET (*buf) += v4l2scalersrc->renegotiation_adjust;
    GST_BUFFER_OFFSET_END (*buf) += v4l2scalersrc->renegotiation_adjust;
    /* check for frame loss with given (from v4l2 device) buffer offset */
    if ((v4l2scalersrc->offset != 0)
        && (GST_BUFFER_OFFSET (*buf) != (v4l2scalersrc->offset + 1))) {
      guint64 lost_frame_count =
          GST_BUFFER_OFFSET (*buf) - v4l2scalersrc->offset - 1;
      GST_WARNING_OBJECT (v4l2scalersrc,
          "lost frames detected: count = %" G_GUINT64_FORMAT " - ts: %"
          GST_TIME_FORMAT, lost_frame_count, GST_TIME_ARGS (timestamp));

      qos_msg = gst_message_new_qos (GST_OBJECT_CAST (v4l2scalersrc), TRUE,
          GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE, timestamp,
          GST_CLOCK_TIME_IS_VALID (duration) ? lost_frame_count *
          duration : GST_CLOCK_TIME_NONE);
      gst_element_post_message (GST_ELEMENT_CAST (v4l2scalersrc), qos_msg);

    }
    v4l2scalersrc->offset = GST_BUFFER_OFFSET (*buf);
  }

  GST_BUFFER_TIMESTAMP (*buf) = timestamp;
  GST_BUFFER_DURATION (*buf) = duration;

  return ret;

  /* ERROR */
alloc_failed:
  {
    if (ret != GST_FLOW_FLUSHING)
      GST_ELEMENT_ERROR (src, RESOURCE, NO_SPACE_LEFT,
          ("Failed to allocate a buffer"), (NULL));
    return ret;
  }
error:
  {
    gst_buffer_replace (buf, NULL);
    if (ret == GST_V4L2_FLOW_LAST_BUFFER) {
      GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
          ("Driver returned a buffer with no payload, this most likely "
              "indicate a bug in the driver."), (NULL));
      ret = GST_FLOW_ERROR;
    } else {
      GST_DEBUG_OBJECT (src, "error processing buffer %d (%s)", ret,
          gst_flow_get_name (ret));
    }
    return ret;
  }
}


/* GstURIHandler interface */
static GstURIType
gst_v4l2_scaler_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_v4l2_scaler_src_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "v4l2", NULL };

  return protocols;
}

static gchar *
gst_v4l2_scaler_src_uri_get_uri (GstURIHandler * handler)
{
  GstV4l2ScalerSrc *v4l2scalersrc = gst_v4l2_scaler_src (handler);
  GstV4l2Object *obj = (GstV4l2Object *) v4l2scalersrc->v4l2scalerobject;

  if (obj->videodev != NULL) {
    return g_strdup_printf ("v4l2://%s", obj->videodev);
  }

  return g_strdup ("v4l2://");
}

static gboolean
gst_v4l2_scaler_src_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstV4l2ScalerSrc *v4l2scalersrc = gst_v4l2_scaler_src (handler);
  const gchar *device = DEFAULT_PROP_DEVICE;

  if (strcmp (uri, "v4l2://") != 0) {
    device = uri + 7;
  }
  g_object_set (v4l2scalersrc, "device", device, NULL);

  return TRUE;
}


static void
gst_v4l2_scaler_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_v4l2_scaler_src_uri_get_type;
  iface->get_protocols = gst_v4l2_scaler_src_uri_get_protocols;
  iface->get_uri = gst_v4l2_scaler_src_uri_get_uri;
  iface->set_uri = gst_v4l2_scaler_src_uri_set_uri;
}
