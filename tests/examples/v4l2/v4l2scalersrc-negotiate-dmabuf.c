/* GStreamer
 *
 * Copyright (C) 2019 LG Electronics. All rights reserved.
 *   Author: Wonchul Lee <w.lee@lge.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/allocators/gstdmabuf.h>

static const gchar *device = "/dev/video70";

static GOptionEntry entries[] = {
  {"device", 'd', 0, G_OPTION_ARG_STRING, &device, "V4L2 Device number",
      NULL},
  {NULL}
};

static gboolean
bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  switch (message->type) {
    case GST_MESSAGE_EOS:
      break;
    case GST_MESSAGE_ERROR:{
      GError *gerror;
      gchar *debug;

      gst_message_parse_error (message, &gerror, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
      g_error_free (gerror);
      g_free (debug);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

gint
main (gint argc, gchar ** argv)
{
  GstBus *bus;
  GError *error = NULL;
  GOptionContext *context;
  gchar *desc;
  gboolean ret, check_dmabuf = FALSE;
  GstElement *pipeline, *sink, *src;
  GstPad *sinkpad, *srcpad;
  GstCaps *caps_sink, *caps_src, *result;
  GstCapsFeatures *features;
  guint i, n;

  context = g_option_context_new ("- test v4l2scalersrc negotite dmabuf with "
      "waylandsink");
  g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
  g_option_context_add_group (context, gst_init_get_option_group ());
  ret = g_option_context_parse (context, &argc, &argv, &error);
  g_option_context_free (context);

  if (!ret) {
    g_print ("option parsing failed: %s\n", error->message);
    g_error_free (error);
    return 1;
  }

  desc =
      g_strdup_printf
      ("v4l2scalersrc name=src device=\"%s\" io-mode=\"dmabuf\" "
      "! waylandsink name=sink", device);
  pipeline = gst_parse_launch (desc, &error);
  g_free (desc);

  if (!pipeline) {
    g_print ("failed to create pipeline: %s", error->message);
    g_error_free (error);
    return 1;
  }

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, bus_callback, NULL);
  gst_object_unref (bus);

  gst_element_set_state (pipeline, GST_STATE_READY);

  if (gst_element_get_state (pipeline, NULL, NULL, 3 * GST_SECOND)
      == GST_STATE_CHANGE_FAILURE) {
    g_print ("failed to change pipeline state to READY\n");
    return -1;
  }

  src = gst_bin_get_by_name (GST_BIN (pipeline), "src");
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  sinkpad = gst_element_get_static_pad (sink, "sink");
  srcpad = gst_element_get_static_pad (src, "src");

  caps_sink = gst_pad_query_caps (sinkpad, NULL);
  caps_src = gst_pad_query_caps (srcpad, NULL);
  result = gst_caps_intersect (caps_sink, caps_src);

  if (result) {
    n = gst_caps_get_size (result);
    for (i = 0; i < n; ++i) {
      features = gst_caps_get_features (result, i);
      if (gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_DMABUF)) {
        check_dmabuf = TRUE;
        break;
      }
    }
    gst_caps_unref (result);
  }

  gst_caps_unref (caps_sink);
  gst_caps_unref (caps_src);
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (sink);
  gst_object_unref (src);

  /* stop and cleanup */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (pipeline));

  if (!check_dmabuf) {
    g_print ("test failed, failed to use dmabuf\n");
    return -1;
  }

  g_print ("test success\n");

  return 0;
}
