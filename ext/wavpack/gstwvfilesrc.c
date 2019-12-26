/* GStreamer Wavpack File Source
 * Copyright (C) 2018 Tim-Philipp Müller <tim centricular com>
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
 * SECTION:element-wvfilesrc
 * @see_also: filesrc
 *
 * This element is only useful for testing the wvcombiner element in an
 * autoplugging context. It will create a filesrc operating in push mode for
 * the file URI passed.
 *
 * If there is also a .wvc file with the same basename in the same directory
 * it will plug a second filesrc and expose a second pad for the correction
 * data.
 *
 * FIXME: GstStream announcements
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstwvfilesrc.h"

#include <gst/gst.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (wvfilesrc_debug);
#define GST_CAT_DEFAULT wvfilesrc_debug

enum
{
  PROP_0,
  PROP_LOCATION,
};

static void gst_wv_file_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_wv_file_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_wv_file_src_change_state (GstElement * element,
    GstStateChange transition);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static void gst_wv_file_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

#define gst_wv_file_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWvFileSrc, gst_wv_file_src, GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER,
        gst_wv_file_src_uri_handler_init));

static void
gst_wv_file_src_class_init (GstWvFileSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_wv_file_src_set_property;
  gobject_class->get_property = gst_wv_file_src_get_property;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "File Location",
          "Location of the file to read", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  element_class->change_state = gst_wv_file_src_change_state;

  gst_element_class_add_static_pad_template (element_class, &srctemplate);

  gst_element_class_set_static_metadata (element_class, "Wavpack File Source",
      "Testing",
      "Implements wvfile:// URI-handler for wavpack correction file testing",
      "Tim-Philipp Müller <tim centricular com>");
}

static void
gst_wv_file_set_location (GstWvFileSrc * src, WvFile * wvfile,
    const gchar * location)
{
  if (wvfile->uri)
    gst_uri_set_path (wvfile->uri, location);
  else
    wvfile->uri = gst_uri_new ("wvfile", NULL, "", 0, location, NULL, NULL);
}

static gchar *
gst_wv_file_get_location (GstWvFileSrc * src, WvFile * wvfile)
{
  if (wvfile->uri == NULL)
    return NULL;

  return gst_uri_get_path (wvfile->uri);
}

static void
gst_wv_file_set_uri (GstWvFileSrc * src, WvFile * wvfile, const gchar * uri)
{
  if (wvfile->uri)
    gst_uri_unref (wvfile->uri);

  wvfile->uri = gst_uri_from_string (uri);
  gst_uri_set_host (wvfile->uri, "");
}

static gchar *
gst_wv_file_get_uri (GstWvFileSrc * src, WvFile * wvfile)
{
  if (wvfile->uri == NULL)
    return NULL;

  return gst_uri_to_string (wvfile->uri);
}

static void
gst_wv_file_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWvFileSrc *src = (GstWvFileSrc *) object;

  switch (prop_id) {
    case PROP_LOCATION:
      gst_wv_file_set_location (src, &src->wv, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_wv_file_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstWvFileSrc *src = (GstWvFileSrc *) object;

  switch (prop_id) {
    case PROP_LOCATION:{
      gchar *location = gst_wv_file_get_location (src, &src->wv);
      g_value_take_string (value, location);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_wv_file_src_init (GstWvFileSrc * src)
{
  src->wvc.ignore_notlinked = FALSE;
}

static void
create_and_post_collection (GstWvFileSrc * src)
{
  GstStreamCollection *collection;
  GstStream *stream;
  GstMessage *msg;
  GstCaps *caps;
  gchar *topstreamid, *streamid;

  /* FIXME : Do we need an upstream id ? We are the creator after all */
  collection = gst_stream_collection_new (NULL);

  /* There is only one top-level stream (with two variants) */
  topstreamid = g_strdup_printf ("%s/audio", src->unique_hash);
  stream = gst_stream_new (topstreamid, NULL, GST_STREAM_TYPE_AUDIO, 0);
  gst_stream_collection_add_stream (collection, stream);

  /* Base variant */
  gst_stream_collection_add_variant (collection, topstreamid,
      gst_object_ref (src->wv.stream));

  /* rich variant */
  caps = gst_caps_from_string ("audio/x-wavpack(meta:GstWVCorrection)");
  streamid = g_strdup_printf ("%s/enriched", src->unique_hash);
  stream = gst_stream_new (streamid, caps, GST_STREAM_TYPE_AUDIO, 0);
  g_free (streamid);
  gst_caps_unref (caps);
  gst_stream_add_component (stream, src->wv.stream);
  gst_stream_add_component (stream, src->wvc.stream);
  gst_stream_collection_add_variant (collection, topstreamid, stream);
  g_free (topstreamid);

  msg = gst_message_new_stream_collection (GST_OBJECT (src), collection);
  gst_element_post_message (GST_ELEMENT (src), msg);

  src->collection = collection;
}

static GstPadProbeReturn
replace_stream_id_cb (GstPad * pad, GstPadProbeInfo * info, WvFile * wvfile)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;

  if (GST_EVENT_TYPE (event) == GST_EVENT_STREAM_START) {
    GstPad *peer;
    /* We are going to replace the event with a new one containing all
     * the proper information */
    gst_event_unref (event);
    event = gst_event_new_stream_start (wvfile->stream_id);
    gst_event_set_stream (event, wvfile->stream);
    gst_event_set_group_id (event, wvfile->group_id);

    peer = gst_pad_get_peer (pad);
    if (peer) {
      gst_pad_send_event (peer, event);
      gst_object_unref (peer);
    } else
      gst_event_unref (event);
    ret = GST_PAD_PROBE_HANDLED;
  }

  return ret;
}

static gboolean
gst_wv_file_add (GstWvFileSrc * src, WvFile * wvfile)
{
  GstStateChangeReturn sret;
  const gchar *name;
  GstPad *pad;

  wvfile->filesrc = gst_element_factory_make ("filesrc", NULL);
  if (wvfile->uri) {
    gchar *uri = gst_wv_file_get_uri (src, wvfile);
    GError *err = NULL;

    GST_LOG_OBJECT (src, "Set URI %s on %" GST_PTR_FORMAT, uri + 2,
        wvfile->filesrc);

    /* skip 'wv' prefix of 'wvfile://' */
    gst_uri_handler_set_uri (GST_URI_HANDLER (wvfile->filesrc), uri + 2, &err);
    g_free (uri);

    if (err != NULL) {
      GST_ERROR_OBJECT (src, "Could not set URI: %s", err->message);
      g_clear_error (&err);
      return FALSE;
    }
  }
  wvfile->typefind = gst_element_factory_make ("typefind", NULL);
  wvfile->queue = gst_element_factory_make ("queue", NULL);
  if (wvfile->ignore_notlinked) {
    wvfile->filter = gst_element_factory_make ("errorignore", NULL);
    g_object_set (wvfile->filter, "ignore-error", FALSE,
        "ignore-notlinked", TRUE, "ignore-notnegotiated", FALSE,
        "convert-to", GST_FLOW_EOS, NULL);
  } else {
    wvfile->filter = gst_element_factory_make ("identity", NULL);
  }
  gst_bin_add_many (GST_BIN (src), wvfile->filesrc, wvfile->typefind,
      wvfile->queue, wvfile->filter, NULL);
  gst_element_link_many (wvfile->filesrc, wvfile->typefind, wvfile->queue,
      wvfile->filter, NULL);
  gst_element_set_state (wvfile->queue, GST_STATE_READY);
  gst_element_set_state (wvfile->filter, GST_STATE_READY);
  sret = gst_element_set_state (wvfile->typefind, GST_STATE_READY);
  if (sret == GST_STATE_CHANGE_FAILURE)
    return FALSE;
  sret = gst_element_set_state (wvfile->filesrc, GST_STATE_READY);
  if (sret == GST_STATE_CHANGE_FAILURE)
    return FALSE;

  pad = gst_element_get_static_pad (wvfile->filter, "src");
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      (GstPadProbeCallback) replace_stream_id_cb, wvfile, NULL);
  name = (wvfile == &src->wv) ? "src_0" : "src_1";
  wvfile->srcpad = gst_ghost_pad_new (name, pad);
  gst_pad_set_active (wvfile->srcpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (src), wvfile->srcpad);
  gst_object_unref (pad);

  return TRUE;
}

static void
gst_wv_file_remove (GstWvFileSrc * src, WvFile * wvfile, gboolean clear_uri)
{
  if (wvfile->queue) {
    gst_element_set_state (wvfile->queue, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (src), wvfile->queue);
    wvfile->queue = NULL;
  }
  if (wvfile->typefind) {
    gst_element_set_state (wvfile->typefind, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (src), wvfile->typefind);
    wvfile->typefind = NULL;
  }
  if (wvfile->filesrc) {
    gst_element_set_state (wvfile->filesrc, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (src), wvfile->filesrc);
    wvfile->filesrc = NULL;
  }
  if (wvfile->srcpad) {
    gst_pad_set_active (wvfile->srcpad, FALSE);
    gst_element_remove_pad (GST_ELEMENT (src), wvfile->srcpad);
    wvfile->srcpad = NULL;
  }
  if (clear_uri && wvfile->uri != NULL) {
    gst_uri_unref (wvfile->uri);
    wvfile->uri = NULL;
  }
}

static gboolean
gst_wv_file_src_start (GstWvFileSrc * src)
{
  gchar *wv_fn, *wvc_fn;
  GChecksum *cs;
  GstCaps *caps;
  GstEvent *event;

  if (!gst_wv_file_add (src, &src->wv))
    return FALSE;

  wv_fn = gst_wv_file_get_location (src, &src->wv);
  g_assert (wv_fn != NULL);

  if (g_str_has_suffix (wv_fn, ".wv") || g_str_has_suffix (wv_fn, ".Wv")) {
    wvc_fn = g_strconcat (wv_fn, "c", NULL);
  } else if (g_str_has_suffix (wv_fn, ".WV")) {
    wvc_fn = g_strconcat (wv_fn, "C", NULL);
  } else {
    GST_WARNING_OBJECT (src, "Not looking for correction file, no .wv file");
    goto done;
  }

  cs = g_checksum_new (G_CHECKSUM_SHA256);
  g_checksum_update (cs, (const guchar *) wv_fn, strlen (wv_fn) - 3);
  src->unique_hash = g_strdup (g_checksum_get_string (cs));
  g_checksum_free (cs);

  g_free (wv_fn);

  if (!g_file_test (wvc_fn, G_FILE_TEST_EXISTS)) {
    GST_WARNING_OBJECT (src, "No correction file '%s' found", wvc_fn);
    g_free (wvc_fn);
    return FALSE;
  }
  GST_INFO_OBJECT (src, "Correction file '%s' exists", wvc_fn);

  /* Create stream objects and id */
  caps = gst_caps_from_string ("audio/x-wavpack");
  src->wv.stream_id = g_strdup_printf ("%s/base", src->unique_hash);
  src->wv.stream =
      gst_stream_new (src->wv.stream_id, caps, GST_STREAM_TYPE_AUDIO, 0);
  gst_caps_unref (caps);
  caps = gst_caps_from_string ("audio/x-wavpack-correction");
  src->wvc.stream_id = g_strdup_printf ("%s/correction", src->unique_hash);
  src->wvc.stream =
      gst_stream_new (src->wvc.stream_id, caps, GST_STREAM_TYPE_AUDIO, 0);
  gst_caps_unref (caps);
  src->wv.group_id = src->wvc.group_id = gst_util_group_id_next ();

  /* FIXME : Create collection with combined stream and check whether
   * downstream can support that stream before adding it */
  create_and_post_collection (src);

  gst_wv_file_set_location (src, &src->wvc, wvc_fn);
  g_free (wvc_fn);

  if (!gst_wv_file_add (src, &src->wvc))
    return FALSE;

  /* Finally send the collection on all pads */
  event = gst_event_new_stream_collection (src->collection);
  GST_DEBUG_OBJECT (src, "Sending collection %p", src->collection);
  gst_pad_push_event (src->wv.srcpad, gst_event_ref (event));
  gst_pad_push_event (src->wvc.srcpad, event);
  GST_DEBUG_OBJECT (src, "Done sending collection");

  /* And the selected streams */
  event = gst_event_new_streams_selected (src->collection);
  gst_event_streams_selected_add (event, src->wv.stream);
  gst_event_streams_selected_add (event, src->wvc.stream);
  GST_DEBUG_OBJECT (src, "Sending STREAMS_SELECTED");
  gst_pad_push_event (src->wv.srcpad, gst_event_ref (event));
  gst_pad_push_event (src->wvc.srcpad, event);
  GST_DEBUG_OBJECT (src, "Done sending STREAMS_SELECTED");

done:
  gst_element_no_more_pads (GST_ELEMENT (src));
  return TRUE;
}

static void
gst_wv_file_src_stop (GstWvFileSrc * src)
{
  gst_wv_file_remove (src, &src->wv, FALSE);
  gst_wv_file_remove (src, &src->wvc, TRUE);
}

static GstStateChangeReturn
gst_wv_file_src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstWvFileSrc *src = (GstWvFileSrc *) element;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_wv_file_src_start (src))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_wv_file_src_stop (src);
      break;
    default:
      break;
  }

  return ret;
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static GstURIType
gst_wv_file_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_wv_file_src_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "wvfile", NULL };

  return protocols;
}

static gchar *
gst_wv_file_src_uri_get_uri (GstURIHandler * handler)
{
  GstWvFileSrc *src = GST_WV_FILE_SRC (handler);

  return gst_wv_file_get_uri (src, &src->wv);
}

static gboolean
gst_wv_file_src_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstWvFileSrc *src = GST_WV_FILE_SRC (handler);

  gst_wv_file_set_uri (src, &src->wv, uri);

  /* FIXME: no error handling, as this is for testing only */
  return TRUE;
}

static void
gst_wv_file_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_wv_file_src_uri_get_type;
  iface->get_protocols = gst_wv_file_src_uri_get_protocols;
  iface->get_uri = gst_wv_file_src_uri_get_uri;
  iface->set_uri = gst_wv_file_src_uri_set_uri;
}

gboolean
gst_wv_file_src_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (wvfilesrc_debug, "wvfilesrc", 0,
      "wvfilesrc element");

  return gst_element_register (plugin, "wvfilesrc", GST_RANK_MARGINAL,
      GST_TYPE_WV_FILE_SRC);
}
