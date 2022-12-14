/* GStreamer
 *
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *               2006 Edgard Lima <edgard.lima@gmail.com>
 *               2019 LG Electronics, Inc.
 *
 * gstv4l2scalerobject.c: base class for LG's V4L2 scaler element
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. This library is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU Library General Public License for more details.
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301,
 * USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <fcntl.h>
#include <sys/mman.h>

#include "gstv4l2scalerobject.h"
#include "gst/gst-i18n-plugin.h"
#include <linux/v4l2-ext/v4l2-controls-ext.h>

GST_DEBUG_CATEGORY_EXTERN (v4l2_debug);
#define GST_CAT_DEFAULT v4l2_debug

/* Support for 32bit off_t, this wrapper is casting off_t to gint64 */
#ifdef HAVE_LIBV4L2
#if SIZEOF_OFF_T < 8

static gpointer
v4l2_mmap_wrapper (gpointer start, gsize length, gint prot, gint flags, gint fd,
    off_t offset)
{
  return v4l2_mmap (start, length, prot, flags, fd, (gint64) offset);
}

#define v4l2_mmap v4l2_mmap_wrapper

#endif /* SIZEOF_OFF_T < 8 */
#endif /* HAVE_LIBV4L2 */

#ifndef V4L2_CID_EXT_GPSCALER_INPUT_FRAME_SIZE
#define V4L2_CID_EXT_GPSCALER_INPUT_FRAME_SIZE (V4L2_CID_USER_EXT_GPSCALER_BASE + 2)
#endif

GstV4l2ScalerObject *
gst_v4l2_scaler_object_new (GstElement * element,
    GstObject * debug_object,
    enum v4l2_buf_type type,
    const char *default_device,
    GstV4l2GetInOutFunction get_in_out_func,
    GstV4l2SetInOutFunction set_in_out_func,
    GstV4l2UpdateFpsFunction update_fps_func)
{
  GstV4l2ScalerObject *v4l2scalerobject;
  GstV4l2Object *v4l2object;

  /*
   * some default values
   */
  v4l2scalerobject = g_new0 (GstV4l2ScalerObject, 1);

  v4l2object = (GstV4l2Object *) v4l2scalerobject;

  v4l2object->type = type;
  v4l2object->formats = NULL;

  v4l2object->element = element;
  v4l2object->dbg_obj = debug_object;
  v4l2object->get_in_out_func = get_in_out_func;
  v4l2object->set_in_out_func = set_in_out_func;
  v4l2object->update_fps_func = update_fps_func;

  v4l2object->change_resolution = gst_v4l2_scaler_object_change_resolution;

  v4l2object->video_fd = -1;
  v4l2object->active = FALSE;
  v4l2object->videodev = g_strdup (default_device);

  v4l2object->norms = NULL;
  v4l2object->channels = NULL;
  v4l2object->colors = NULL;

  v4l2object->keep_aspect = TRUE;

  v4l2object->n_v4l2_planes = 0;

  v4l2object->no_initial_format = FALSE;

  /* We now disable libv4l2 by default, but have an env to enable it. */
#ifdef HAVE_LIBV4L2
  if (g_getenv ("GST_V4L2_USE_LIBV4L2")) {
    v4l2object->fd_open = v4l2_fd_open;
    v4l2object->close = v4l2_close;
    v4l2object->dup = v4l2_dup;
    v4l2object->ioctl = v4l2_ioctl;
    v4l2object->read = v4l2_read;
    v4l2object->mmap = v4l2_mmap;
    v4l2object->munmap = v4l2_munmap;
  } else
#endif
  {
    v4l2object->fd_open = NULL;
    v4l2object->close = close;
    v4l2object->dup = dup;
    v4l2object->ioctl = ioctl;
    v4l2object->read = read;
    v4l2object->mmap = mmap;
    v4l2object->munmap = munmap;
  }

  v4l2scalerobject->vdec_index = 0;
  v4l2scalerobject->max_width = 0;
  v4l2scalerobject->max_height = 0;
  v4l2scalerobject->scalable = TRUE;
  v4l2scalerobject->destination_caps = NULL;
  v4l2scalerobject->vdo_fd = -1;
  v4l2scalerobject->input_width = 0;
  v4l2scalerobject->input_height = 0;

  return v4l2scalerobject;
}

static gboolean gst_v4l2_scaler_object_clear_format_list (GstV4l2ScalerObject *
    v4l2scalerobject);

void
gst_v4l2_scaler_object_destroy (GstV4l2ScalerObject * v4l2scalerobject)
{
  GstV4l2Object *v4l2object;

  g_return_if_fail (v4l2scalerobject != NULL);

  v4l2object = (GstV4l2Object *) v4l2scalerobject;

  g_free (v4l2object->videodev);

  g_free (v4l2object->channel);

  if (v4l2object->formats) {
    gst_v4l2_scaler_object_clear_format_list (v4l2scalerobject);
  }

  if (v4l2object->probed_caps) {
    gst_caps_unref (v4l2object->probed_caps);
  }

  if (v4l2object->extra_controls) {
    gst_structure_free (v4l2object->extra_controls);
  }

  if (v4l2scalerobject->destination_caps)
    gst_caps_unref (v4l2scalerobject->destination_caps);

  g_free (v4l2scalerobject);
}

static gboolean
gst_v4l2_scaler_object_clear_format_list (GstV4l2ScalerObject *
    v4l2scalerobject)
{
  GstV4l2Object *v4l2object = (GstV4l2Object *) v4l2scalerobject;

  g_slist_foreach (v4l2object->formats, (GFunc) g_free, NULL);
  g_slist_free (v4l2object->formats);
  v4l2object->formats = NULL;

  return TRUE;
}

GstCaps *
gst_v4l2_scaler_object_get_caps (GstV4l2ScalerObject * v4l2scalerobject,
    GstCaps * filter)
{
  GstCaps *ret, *tmp;
  GstV4l2Object *v4l2object = (GstV4l2Object *) v4l2scalerobject;

  if (v4l2object->probed_caps == NULL)
    v4l2object->probed_caps = gst_v4l2_object_probe_caps (v4l2object, NULL);

  if (v4l2scalerobject->destination_caps) {
    tmp = gst_caps_intersect_full (v4l2scalerobject->destination_caps,
        v4l2object->probed_caps, GST_CAPS_INTERSECT_FIRST);
  } else {
    tmp = gst_caps_ref (v4l2object->probed_caps);
  }

  if (filter) {
    ret = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
  } else {
    ret = gst_caps_ref (tmp);
  }

  gst_caps_unref (tmp);
  return ret;
}

static gboolean
gst_v4l2_scaler_object_is_dmabuf_supported (GstV4l2ScalerObject *
    v4l2scalerobject)
{
  GstV4l2Object *v4l2object = (GstV4l2Object *) v4l2scalerobject;
  gboolean ret = TRUE;
  struct v4l2_exportbuffer expbuf = {
    .type = v4l2object->type,
    .index = -1,
    .plane = -1,
    .flags = O_CLOEXEC | O_RDWR,
  };

  if (v4l2object->fmtdesc &&
      v4l2object->fmtdesc->flags & V4L2_FMT_FLAG_EMULATED) {
    GST_WARNING_OBJECT (v4l2object->dbg_obj,
        "libv4l2 converter detected, disabling DMABuf");
    ret = FALSE;
  }

  /* Expected to fail, but ENOTTY tells us that it is not implemented. */
  v4l2object->ioctl (v4l2object->video_fd, VIDIOC_EXPBUF, &expbuf);
  if (errno == ENOTTY)
    ret = FALSE;

  return ret;
}

static void
gst_v4l2_scaler_get_driver_min_buffers (GstV4l2ScalerObject * v4l2scalerobject)
{
  GstV4l2Object *v4l2object = (GstV4l2Object *) v4l2scalerobject;
  struct v4l2_control control = { 0, };

  g_return_if_fail (GST_V4L2_IS_OPEN (v4l2object));

  if (V4L2_TYPE_IS_OUTPUT (v4l2object->type))
    control.id = V4L2_CID_MIN_BUFFERS_FOR_OUTPUT;
  else
    control.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;

  if (v4l2object->ioctl (v4l2object->video_fd, VIDIOC_G_CTRL, &control) == 0) {
    GST_DEBUG_OBJECT (v4l2object->dbg_obj,
        "driver requires a minimum of %d buffers", control.value);
    v4l2object->min_buffers = control.value;
  } else {
    v4l2object->min_buffers = 0;
  }
}

static gboolean
gst_v4l2_scaler_object_setup_pool (GstV4l2ScalerObject * v4l2scalerobject,
    GstCaps * caps)
{
  GstV4l2Object *v4l2object = (GstV4l2Object *) v4l2scalerobject;
  GstV4l2IOMode mode;

  GST_DEBUG_OBJECT (v4l2object->dbg_obj, "initializing the %s system",
      V4L2_TYPE_IS_OUTPUT (v4l2object->type) ? "output" : "capture");

  GST_V4L2_CHECK_OPEN (v4l2object);
  GST_V4L2_CHECK_NOT_ACTIVE (v4l2object);

  /* find transport */
  mode = v4l2object->req_mode;

  if (v4l2object->device_caps & V4L2_CAP_READWRITE) {
    if (v4l2object->req_mode == GST_V4L2_IO_AUTO)
      mode = GST_V4L2_IO_RW;
  } else if (v4l2object->req_mode == GST_V4L2_IO_RW)
    goto method_not_supported;

  if (v4l2object->device_caps & V4L2_CAP_STREAMING) {
    if (v4l2object->req_mode == GST_V4L2_IO_AUTO) {
      if (!V4L2_TYPE_IS_OUTPUT (v4l2object->type) &&
          gst_v4l2_scaler_object_is_dmabuf_supported (v4l2scalerobject)) {
        mode = GST_V4L2_IO_DMABUF;
      } else {
        mode = GST_V4L2_IO_MMAP;
      }
    }
  } else if (v4l2object->req_mode == GST_V4L2_IO_MMAP)
    goto method_not_supported;

  /* if still no transport selected, error out */
  if (mode == GST_V4L2_IO_AUTO)
    goto no_supported_capture_method;

  GST_INFO_OBJECT (v4l2object->dbg_obj, "accessing buffers via mode %d", mode);
  v4l2object->mode = mode;

  /* If min_buffers is not set, the driver either does not support the control or
     it has not been asked yet via propose_allocation/decide_allocation. */
  if (!v4l2object->min_buffers)
    gst_v4l2_scaler_get_driver_min_buffers (v4l2scalerobject);

  /* Map the buffers */
  GST_LOG_OBJECT (v4l2object->dbg_obj, "initiating buffer pool");

  if (!(v4l2object->pool = gst_v4l2_buffer_pool_new (v4l2object, caps)))
    goto buffer_pool_new_failed;

  GST_V4L2_SET_ACTIVE (v4l2object);

  return TRUE;

  /* ERRORS */
buffer_pool_new_failed:
  {
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, READ,
        (_("Could not map buffers from device '%s'"),
            v4l2object->videodev),
        ("Failed to create buffer pool: %s", g_strerror (errno)));
    return FALSE;
  }
method_not_supported:
  {
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, READ,
        (_("The driver of device '%s' does not support the IO method %d"),
            v4l2object->videodev, mode), (NULL));
    return FALSE;
  }
no_supported_capture_method:
  {
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, READ,
        (_("The driver of device '%s' does not support any known IO "
                "method."), v4l2object->videodev), (NULL));
    return FALSE;
  }
}

gboolean
gst_v4l2_scaler_object_decide_allocation (GstV4l2ScalerObject *
    v4l2scalerobject, GstQuery * query)
{
  GstV4l2Object *obj = (GstV4l2Object *) v4l2scalerobject;
  GstCaps *caps;
  GstBufferPool *pool = NULL, *other_pool = NULL;
  GstStructure *config;
  guint size, min, max, own_min = 0, own_max = 0;
  gboolean update;
  gboolean has_video_meta;
  gboolean can_share_own_pool = FALSE, pushing_from_our_pool = FALSE;
  GstAllocator *allocator = NULL;
  GstAllocationParams params = { 0 };

  GST_DEBUG_OBJECT (obj->dbg_obj, "decide allocation");

  g_return_val_if_fail (obj->type == V4L2_BUF_TYPE_VIDEO_CAPTURE ||
      obj->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, FALSE);

  gst_query_parse_allocation (query, &caps, NULL);

  if (obj->pool == NULL) {
    if (!gst_v4l2_scaler_object_setup_pool (v4l2scalerobject, caps))
      goto pool_failed;
  }

  if (gst_query_get_n_allocation_params (query) > 0)
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    update = TRUE;
  } else {
    pool = NULL;
    min = max = 0;
    size = 0;
    update = FALSE;
  }

  GST_DEBUG_OBJECT (obj->dbg_obj, "allocation: size:%u min:%u max:%u pool:%"
      GST_PTR_FORMAT, size, min, max, pool);

  has_video_meta =
      gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  can_share_own_pool = (has_video_meta || !obj->need_video_meta);

  gst_v4l2_scaler_get_driver_min_buffers (v4l2scalerobject);
  /* We can't share our own pool, if it exceed V4L2 capacity */
  if (min + obj->min_buffers + 1 > VIDEO_MAX_FRAME)
    can_share_own_pool = FALSE;

  /* select a pool */
  switch (obj->mode) {
    case GST_V4L2_IO_RW:
      if (pool) {
        /* in READ/WRITE mode, prefer a downstream pool because our own pool
         * doesn't help much, we have to write to it as well */
        GST_DEBUG_OBJECT (obj->dbg_obj,
            "read/write mode: using downstream pool");
        /* use the bigest size, when we use our own pool we can't really do any
         * other size than what the hardware gives us but for downstream pools
         * we can try */
        size = MAX (size, obj->info.size);
      } else if (can_share_own_pool) {
        /* no downstream pool, use our own then */
        GST_DEBUG_OBJECT (obj->dbg_obj,
            "read/write mode: no downstream pool, using our own");
        pool = gst_object_ref (obj->pool);
        size = obj->info.size;
        pushing_from_our_pool = TRUE;
      }
      break;

    case GST_V4L2_IO_USERPTR:
    case GST_V4L2_IO_DMABUF_IMPORT:
      /* in importing mode, prefer our own pool, and pass the other pool to
       * our own, so it can serve itself */
      if (pool == NULL)
        goto no_downstream_pool;
      gst_v4l2_buffer_pool_set_other_pool (GST_V4L2_BUFFER_POOL (obj->pool),
          pool);
      other_pool = pool;
      gst_object_unref (pool);
      pool = gst_object_ref (obj->pool);
      size = obj->info.size;
      break;

    case GST_V4L2_IO_MMAP:
    case GST_V4L2_IO_DMABUF:
      /* in streaming mode, prefer our own pool */
      /* Check if we can use it ... */
      if (can_share_own_pool) {
        if (pool)
          gst_object_unref (pool);
        pool = gst_object_ref (obj->pool);
        size = obj->info.size;
        GST_DEBUG_OBJECT (obj->dbg_obj,
            "streaming mode: using our own pool %" GST_PTR_FORMAT, pool);
        pushing_from_our_pool = TRUE;
      } else if (pool) {
        GST_DEBUG_OBJECT (obj->dbg_obj,
            "streaming mode: copying to downstream pool %" GST_PTR_FORMAT,
            pool);
      } else {
        GST_DEBUG_OBJECT (obj->dbg_obj,
            "streaming mode: no usable pool, copying to generic pool");
        size = MAX (size, obj->info.size);
      }
      break;
    case GST_V4L2_IO_AUTO:
    default:
      GST_WARNING_OBJECT (obj->dbg_obj, "unhandled mode");
      break;
  }

  if (size == 0)
    goto no_size;

  /* We are going to use the minimum buffer size reported by the driver, and
   * limit max to this. */
  own_min = MAX (obj->min_buffers, GST_V4L2_MIN_BUFFERS);
  own_max = own_min;
  min = own_min;
  max = own_max;
  if (pushing_from_our_pool) {
    GST_DEBUG_OBJECT (obj->dbg_obj,
        "force to set min_buffers to %d, max_buffers to %d", min, max);
    if (!update) {
      gst_v4l2_buffer_pool_copy_at_threshold (GST_V4L2_BUFFER_POOL (pool),
          TRUE);
    } else {
      gst_v4l2_buffer_pool_copy_at_threshold (GST_V4L2_BUFFER_POOL (pool),
          FALSE);
    }
  }

  /* Request a bigger max, if one was suggested but it's too small */
  if (max != 0)
    max = MAX (min, max);

  /* First step, configure our own pool */
  config = gst_buffer_pool_get_config (obj->pool);

  if (obj->need_video_meta || has_video_meta) {
    GST_DEBUG_OBJECT (obj->dbg_obj, "activate Video Meta");
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }

  gst_buffer_pool_config_set_allocator (config, allocator, &params);
  gst_buffer_pool_config_set_params (config, caps, size, own_min, own_max);

  GST_DEBUG_OBJECT (obj->dbg_obj, "setting own pool config to %"
      GST_PTR_FORMAT, config);

  /* Our pool often need to adjust the value */
  if (!gst_buffer_pool_set_config (obj->pool, config)) {
    config = gst_buffer_pool_get_config (obj->pool);

    GST_DEBUG_OBJECT (obj->dbg_obj, "own pool config changed to %"
        GST_PTR_FORMAT, config);

    /* our pool will adjust the maximum buffer, which we are fine with */
    if (!gst_buffer_pool_set_config (obj->pool, config))
      goto config_failed;
  }

  /* Now configure the other pool if different */
  if (obj->pool != pool)
    other_pool = pool;

  if (other_pool) {
    config = gst_buffer_pool_get_config (other_pool);
    gst_buffer_pool_config_set_allocator (config, allocator, &params);
    gst_buffer_pool_config_set_params (config, caps, size, min, max);

    GST_DEBUG_OBJECT (obj->dbg_obj, "setting other pool config to %"
        GST_PTR_FORMAT, config);

    /* if downstream supports video metadata, add this to the pool config */
    if (has_video_meta) {
      GST_DEBUG_OBJECT (obj->dbg_obj, "activate Video Meta");
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_META);
    }

    if (!gst_buffer_pool_set_config (other_pool, config)) {
      config = gst_buffer_pool_get_config (other_pool);

      if (!gst_buffer_pool_config_validate_params (config, caps, size, min,
              max)) {
        gst_structure_free (config);
        goto config_failed;
      }

      if (!gst_buffer_pool_set_config (other_pool, config))
        goto config_failed;
    }
  }

  if (pool) {
    /* For simplicity, simply read back the active configuration, so our base
     * class get the right information */
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, NULL, &size, &min, &max);
    gst_structure_free (config);
  }

  if (update)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  if (allocator)
    gst_object_unref (allocator);

  if (pool)
    gst_object_unref (pool);

  return TRUE;

pool_failed:
  {
    /* setup_pool already send the error */
    goto cleanup;
  }
config_failed:
  {
    GST_ELEMENT_ERROR (obj->element, RESOURCE, SETTINGS,
        (_("Failed to configure internal buffer pool.")), (NULL));
    goto cleanup;
  }
no_size:
  {
    GST_ELEMENT_ERROR (obj->element, RESOURCE, SETTINGS,
        (_("Video device did not suggest any buffer size.")), (NULL));
    goto cleanup;
  }
cleanup:
  {
    if (allocator)
      gst_object_unref (allocator);

    if (pool)
      gst_object_unref (pool);
    return FALSE;
  }
no_downstream_pool:
  {
    GST_ELEMENT_ERROR (obj->element, RESOURCE, SETTINGS,
        (_("No downstream pool to import from.")),
        ("When importing DMABUF or USERPTR, we need a pool to import from"));
    return FALSE;
  }
}

GstFlowReturn
gst_v4l2_scaler_object_change_resolution (GstV4l2Object * v4l2object)
{
  GstV4l2ScalerObject *v4l2scalerobject = (GstV4l2ScalerObject *)v4l2object;
  struct v4l2_control control;
  guint16 width, height;

  control.id = V4L2_CID_EXT_GPSCALER_INPUT_FRAME_SIZE;
  if (v4l2object->ioctl (v4l2object->video_fd, VIDIOC_G_CTRL, &control) < 0) {
    GST_WARNING_OBJECT (v4l2object->dbg_obj,
        "Failed to get value for control %d on device '%s'.",
        control.id, v4l2object->videodev);
    return GST_FLOW_ERROR;
  }

  width = (control.value >> 16) & 0xffff;
  height = control.value & 0xffff;

  if (width > v4l2scalerobject->max_width) {
    height = height * v4l2scalerobject->max_width / width;
    width = v4l2scalerobject->max_width;
  }
  if (height > v4l2scalerobject->max_height) {
    width = width * v4l2scalerobject->max_height / height;
    height = v4l2scalerobject->max_height;
  }

  if ((v4l2scalerobject->input_width == width) && (v4l2scalerobject->input_height == height)) {
    GST_DEBUG_OBJECT (v4l2object->dbg_obj, "No change resolution");
    return GST_FLOW_OK;
  }

  GST_DEBUG_OBJECT (v4l2object->dbg_obj, "Change resolution %dx%d to %dx%d",
      v4l2scalerobject->input_width, v4l2scalerobject->input_height,
      width, height);
  v4l2scalerobject->input_width = width;
  v4l2scalerobject->input_height = height;

  return GST_V4L2_FLOW_SOURCE_CHANGE;
}
