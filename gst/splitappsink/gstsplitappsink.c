/*
 * GStreamer splitappsink element
 *
 * Copyright 2016 LG Electronics, Inc.
 *  @author: HoonHee Lee <hoonhee.lee@lge.com>
 *
 * gstsplitappsink.c: Convenience bin that split data to appsink from incoming streams
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gstsplitappsink.h"

GST_DEBUG_CATEGORY_STATIC (split_appsink_debug);
#define GST_CAT_DEFAULT split_appsink_debug

#define parent_class gst_split_appsink_parent_class

/* properties */
enum
{
  PROP_0,
  PROP_STREAM_TYPE,
  PROP_APPSINK,
  PROP_MAX_SIZE_BUFFERS,
  PROP_MAX_SIZE_BYTES,
  PROP_MAX_SIZE_TIME,
};

#define DEFAULT_STREAM_TYPE GST_STREAM_TYPE_UNKNOWN
#define DEFAULT_MAX_SIZE_BUFFERS  0
#define DEFAULT_MAX_SIZE_BYTES    0
#define DEFAULT_MAX_SIZE_TIME     0

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_split_appsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_split_appsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_split_appsink_dispose (GObject * self);
static void gst_split_appsink_finalize (GObject * self);
static gboolean gst_split_appsink_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstStateChangeReturn gst_split_appsink_change_state (GstElement *
    element, GstStateChange transition);
static void gst_split_appsink_handle_message (GstBin * bin,
    GstMessage * message);
static gboolean setup_sink (GstSplitAppsink * bin);
static void remove_children (GstSplitAppsink * bin);

G_DEFINE_TYPE (GstSplitAppsink, gst_split_appsink, GST_TYPE_BIN);

static void
gst_split_appsink_class_init (GstSplitAppsinkClass * klass)
{
  GObjectClass *gobject_class;
  GstBinClass *gstbin_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstbin_class = GST_BIN_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_split_appsink_set_property;
  gobject_class->get_property = gst_split_appsink_get_property;
  gobject_class->dispose = gst_split_appsink_dispose;
  gobject_class->finalize = gst_split_appsink_finalize;

  gstbin_class->handle_message =
      GST_DEBUG_FUNCPTR (gst_split_appsink_handle_message);

  g_object_class_install_property (gobject_class, PROP_STREAM_TYPE,
      g_param_spec_uint ("stream-type", "Stream Type", "Stream Type", 0,
          G_MAXUINT, DEFAULT_STREAM_TYPE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSplitAppsink:appsink
   *
   * Get the current appsink.
   */
  g_object_class_install_property (gobject_class, PROP_APPSINK,
      g_param_spec_object ("appsink", "Appsink",
          "Current appsink",
          GST_TYPE_ELEMENT, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_SIZE_BYTES,
      g_param_spec_uint ("max-size-bytes", "Max. size (kB)",
          "Max. amount of data in the queue (bytes, 0=disable)",
          0, G_MAXUINT, DEFAULT_MAX_SIZE_BYTES,
          G_PARAM_WRITABLE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MAX_SIZE_BUFFERS,
      g_param_spec_uint ("max-size-buffers", "Max. size (buffers)",
          "Max. number of buffers in the queue (0=disable)", 0, G_MAXUINT,
          DEFAULT_MAX_SIZE_BUFFERS,
          G_PARAM_WRITABLE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MAX_SIZE_TIME,
      g_param_spec_uint64 ("max-size-time", "Max. size (ns)",
          "Max. amount of data in the queue (in ns, 0=disable)", 0, G_MAXUINT64,
          DEFAULT_MAX_SIZE_TIME,
          G_PARAM_WRITABLE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_split_appsink_change_state);

  gst_element_class_set_static_metadata (gstelement_class,
      "Split Appsink Bin", "Generic/Bin",
      "Convenience bin that split data to appsink from incoming streams",
      "HoonHee Lee <hoonhee.lee@lge.com>");

  GST_DEBUG_CATEGORY_INIT (split_appsink_debug, "splitappsink", 0,
      "Split Appsink");
}

static void
gst_split_appsink_init (GstSplitAppsink * bin)
{
  GstPad *sinkpad, *srcpad;
  GstPadTemplate *pad_tmpl;

  g_rec_mutex_init (&bin->lock);

  bin->tee = gst_element_factory_make ("tee", NULL);
  gst_bin_add (GST_BIN (bin), bin->tee);

  /* Setup tee and link */
  pad_tmpl = gst_static_pad_template_get (&sink_template);
  sinkpad = gst_element_get_static_pad (bin->tee, "sink");
  bin->sinkpad = gst_ghost_pad_new_from_template ("sink", sinkpad, pad_tmpl);
  gst_pad_set_active (bin->sinkpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (bin), bin->sinkpad);
  gst_object_unref (pad_tmpl);
  gst_object_unref (sinkpad);

  pad_tmpl = gst_static_pad_template_get (&src_template);
  srcpad = gst_element_get_request_pad (bin->tee, "src_%u");
  bin->srcpad = gst_ghost_pad_new_from_template ("src", srcpad, pad_tmpl);
  gst_pad_set_active (bin->srcpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (bin), bin->srcpad);
  gst_object_unref (pad_tmpl);
  gst_object_unref (srcpad);

  gst_pad_set_event_function (GST_PAD_CAST (bin->sinkpad),
      GST_DEBUG_FUNCPTR (gst_split_appsink_sink_event));

  bin->active_stream = NULL;

  bin->stream_type = DEFAULT_STREAM_TYPE;
  bin->max_size_buffers = DEFAULT_MAX_SIZE_BUFFERS;
  bin->max_size_bytes = DEFAULT_MAX_SIZE_BYTES;
  bin->max_size_time = DEFAULT_MAX_SIZE_TIME;
}

static void
gst_split_appsink_dispose (GObject * object)
{
  GstSplitAppsink *bin = GST_SPLIT_APPSINK (object);

  remove_children (bin);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_split_appsink_finalize (GObject * object)
{
  GstSplitAppsink *bin = GST_SPLIT_APPSINK (object);

  g_rec_mutex_clear (&bin->lock);
  if (bin->active_stream) {
    gst_object_unref (bin->active_stream);
    bin->active_stream = NULL;
  }

  if (bin->stream_id) {
    g_free (bin->stream_id);
    bin->stream_id = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_split_appsink_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstSplitAppsink *bin = GST_SPLIT_APPSINK (parent);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (pad, "got event %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:
    {
      GstStream *stream = NULL;
      const gchar *stream_id;

      gst_event_parse_stream (event, &stream);
      if (stream != NULL) {
        if (bin->active_stream == NULL) {
          bin->active_stream = stream;
        } else if (bin->active_stream != stream) {
          GST_FIXME_OBJECT (pad, "Handle stream changes (%s => %s) !",
              gst_stream_get_stream_id (bin->active_stream),
              gst_stream_get_stream_id (stream));
          gst_object_unref (bin->active_stream);
          bin->active_stream = stream;
        } else
          gst_object_unref (stream);
      }

      /* Store stream-id */
      gst_event_parse_stream_start (event, &stream_id);
      if (bin->stream_id)
        g_free (bin->stream_id);
      bin->stream_id = g_strdup (stream_id);

#if 0
      /* Setup Appsink */
      if (!setup_sink (bin)) {
        GST_ERROR_OBJECT (bin, "Fail to configure appsink");
      }
#endif

      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

static gboolean
setup_sink (GstSplitAppsink * bin)
{
  GstPad *sinkpad, *srcpad;

  GST_DEBUG_OBJECT (bin, "setup sink");

  if (bin->appsink) {
    GST_LOG_OBJECT (bin, "appink is already configured!!");
    return TRUE;
  }

  GST_SPLIT_APPSINK_LOCK (bin);

  bin->queue = gst_element_factory_make ("queue", NULL);
  bin->appsink = gst_element_factory_make ("appsink", NULL);

  /* No limits */
  g_object_set (bin->queue,
      "max-size-bytes", (guint) bin->max_size_bytes,
      "max-size-buffers", (guint) bin->max_size_buffers, "max-size-time",
      (guint64) bin->max_size_time, NULL);

  GST_OBJECT_FLAG_UNSET (bin->appsink, GST_ELEMENT_FLAG_SINK);
  g_object_set (bin->appsink, "emit-signals", TRUE, "sync", FALSE,
      "wait-on-eos", FALSE, NULL);
  /* disable async enable */
  g_object_set (bin->appsink, "async", FALSE, NULL);

  gst_bin_add (GST_BIN (bin), bin->queue);
  gst_bin_add (GST_BIN (bin), bin->appsink);

  srcpad = gst_element_get_request_pad (bin->tee, "src_%u");
  sinkpad = gst_element_get_static_pad (bin->queue, "sink");
  gst_pad_link (srcpad, sinkpad);
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);

  gst_element_link (bin->queue, bin->appsink);

  gst_element_sync_state_with_parent (bin->queue);
  gst_element_sync_state_with_parent (bin->appsink);

  GST_SPLIT_APPSINK_UNLOCK (bin);

  GST_DEBUG_OBJECT (bin, "Complete to configure appsink");

  return TRUE;
}

static void
remove_children (GstSplitAppsink * bin)
{
  if (bin->tee) {
    GstIterator *it;
    GValue data = { 0, };
    /* unlink tee */
    GST_DEBUG_OBJECT (bin, "unlink tee");
    it = gst_element_iterate_src_pads (bin->tee);
    while (gst_iterator_next (it, &data) == GST_ITERATOR_OK) {
      GstPad *pad = g_value_get_object (&data);
      gst_element_release_request_pad (bin->tee, pad);
      //gst_object_unref (pad);
      g_value_reset (&data);
    }
    g_value_unset (&data);
    gst_iterator_free (it);
  }

  if (bin->appsink) {
    GST_DEBUG_OBJECT (bin, "release appsink element");
    gst_element_set_state (bin->appsink, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (bin), bin->appsink);
    bin->appsink = NULL;
  }

  if (bin->queue) {
    GST_DEBUG_OBJECT (bin, "release queue element");
    gst_element_set_state (bin->queue, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (bin), bin->queue);
    bin->queue = NULL;
  }

  if (bin->tee) {
    GST_DEBUG_OBJECT (bin, "release tee element");
    gst_element_set_state (bin->tee, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (bin), bin->tee);
    bin->tee = NULL;
  }
}

static void
gst_split_appsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSplitAppsink *bin = GST_SPLIT_APPSINK (object);

  switch (prop_id) {
    case PROP_STREAM_TYPE:
      bin->stream_type = g_value_get_uint (value);
      break;
    case PROP_MAX_SIZE_BYTES:
      bin->max_size_bytes = g_value_get_uint (value);
      break;
    case PROP_MAX_SIZE_BUFFERS:
      bin->max_size_buffers = g_value_get_uint (value);
      break;
    case PROP_MAX_SIZE_TIME:
      bin->max_size_time = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_split_appsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSplitAppsink *bin = GST_SPLIT_APPSINK (object);

  switch (prop_id) {
    case PROP_STREAM_TYPE:
      g_value_set_uint (value, bin->stream_type);
      break;
    case PROP_APPSINK:
      g_value_set_object (value, bin->appsink);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_split_appsink_handle_message (GstBin * bin, GstMessage * message)
{
  GstSplitAppsink *appsink_bin = GST_SPLIT_APPSINK (bin);
  if (G_UNLIKELY (message == NULL))
    return;

  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_STREAM_START) {
    GST_INFO_OBJECT (appsink_bin, "Don't post stream-start from (%s)",
        GST_OBJECT_NAME (GST_MESSAGE_SRC (message)));
    gst_message_unref (message);
    return;
  }

  GST_LOG_OBJECT (appsink_bin, "Posting message: %" GST_PTR_FORMAT, message);
  GST_BIN_CLASS (parent_class)->handle_message (bin, message);
}

static GstStateChangeReturn
gst_split_appsink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstSplitAppsink *bin;

  bin = GST_SPLIT_APPSINK (element);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_DEBUG ("ready to paused");
      /* Setup Appsink */
      if (!setup_sink (bin)) {
        GST_ERROR_OBJECT (bin, "Fail to configure appsink");
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG ("paused to ready");
      /* release to child elements */
      remove_children (bin);
      break;
    default:
      break;
  }
  return ret;
}
