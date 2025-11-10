/* GStreamer Plugins Cool
 * Copyright (C) 2017 LG Electronics, Inc.
 *   Author : HoonHee Lee <hoonhee.lee@lge.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include "gstdecbridge.h"

static GstStaticPadTemplate gst_dec_bridge_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_dec_bridge_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

#define GST_DEC_BRIDGE_GET_LOCK(d) (&((GstDecBridge*)(d))->lock)
#define GST_DEC_BRIDGE_LOCK(d) (g_mutex_lock (GST_DEC_BRIDGE_GET_LOCK(d)))
#define GST_DEC_BRIDGE_UNLOCK(d) (g_mutex_unlock (GST_DEC_BRIDGE_GET_LOCK(d)))

GST_DEBUG_CATEGORY_STATIC (dec_bridge_debug);
#define GST_CAT_DEFAULT dec_bridge_debug

static void gst_dec_bridge_finalize (GObject * object);
static gboolean gst_dec_bridge_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstFlowReturn gst_dec_bridge_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static gboolean gst_dec_bridge_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

#define gst_dec_bridge_parent_class parent_class
G_DEFINE_TYPE (GstDecBridge, gst_dec_bridge, GST_TYPE_ELEMENT);


static void
gst_dec_bridge_class_init (GstDecBridgeClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_dec_bridge_finalize);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_dec_bridge_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_dec_bridge_sink_pad_template));

  gst_element_class_set_static_metadata (element_class,
      "Bridge element between decoder", "General",
      "Pass data between decoder", "HoonHee Lee <hoonhee.lee@lge.com>");

  GST_DEBUG_CATEGORY_INIT (dec_bridge_debug, "decbridge", 0,
      "Pass data between decoder");
}

static void
srcpad_unlinked (GstPad * pad, GstPad * peer, gpointer user_data)
{
  GstDecBridge *bridge = GST_DEC_BRIDGE (user_data);

  if (bridge->curcaps) {
    GST_DEBUG_OBJECT (pad, "Clear current Caps: %" GST_PTR_FORMAT,
        bridge->curcaps);
    gst_caps_unref (bridge->curcaps);
    bridge->curcaps = NULL;
  }
}

static void
gst_dec_bridge_init (GstDecBridge * bridge)
{
  bridge->sinkpad =
      gst_pad_new_from_static_template (&gst_dec_bridge_sink_pad_template,
      "sink");
  gst_pad_set_event_function (bridge->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dec_bridge_sink_event));
  gst_pad_set_chain_function (bridge->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dec_bridge_chain));
  gst_pad_set_query_function (bridge->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dec_bridge_query));
  gst_element_add_pad (GST_ELEMENT (bridge), bridge->sinkpad);

  bridge->srcpad =
      gst_pad_new_from_static_template (&gst_dec_bridge_src_pad_template,
      "src");
  gst_pad_set_query_function (bridge->srcpad,
      GST_DEBUG_FUNCPTR (gst_dec_bridge_query));
  gst_element_add_pad (GST_ELEMENT (bridge), bridge->srcpad);
  g_signal_connect (G_OBJECT (bridge->srcpad), "unlinked",
      (GCallback) srcpad_unlinked, bridge);

  g_mutex_init (&bridge->lock);
  bridge->active_stream_id = NULL;
  bridge->stream_change = FALSE;
  bridge->curcaps = NULL;
}

static void
gst_dec_bridge_finalize (GObject * object)
{
  GstDecBridge *bridge = GST_DEC_BRIDGE (object);

  if (bridge->active_stream_id) {
    g_free (bridge->active_stream_id);
    bridge->active_stream_id = NULL;
  }

  if (bridge->curcaps) {
    gst_caps_unref (bridge->curcaps);
    bridge->curcaps = NULL;
  }

  g_mutex_clear (&bridge->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_dec_bridge_valid_caps (GstDecBridge * bridge, GstPad * pad,
    GstCaps * incaps)
{
  GstStructure *structure = NULL;
  const gchar *s;
  GstVideoFormat vformat = GST_VIDEO_FORMAT_UNKNOWN;
  GstAudioFormat aformat = GST_AUDIO_FORMAT_UNKNOWN;
  gint width = 0, height = 0;
  gint rate, channels;
  guint64 channel_mask;
  gint i;
  GstAudioChannelPosition position[64];
  GstAudioFlags flags;

  GST_DEBUG_OBJECT (pad, "have new caps %p %" GST_PTR_FORMAT, incaps, incaps);

  structure = gst_caps_get_structure (incaps, 0);

  if (gst_structure_has_name (structure, "video/x-raw")) {
    if (!(s = gst_structure_get_string (structure, "format")))
      goto no_format;

    vformat = gst_video_format_from_string (s);
    if (vformat == GST_VIDEO_FORMAT_UNKNOWN)
      goto unknown_format;

    /* width and height are mandatory, except for non-raw-formats */
    if (!gst_structure_get_int (structure, "width", &width) &&
        vformat != GST_VIDEO_FORMAT_ENCODED)
      goto no_width;
    if (!gst_structure_get_int (structure, "height", &height) &&
        vformat != GST_VIDEO_FORMAT_ENCODED)
      goto no_height;
  } else if (gst_structure_has_name (structure, "audio/x-raw")) {
    flags = 0;
    if (!(s = gst_structure_get_string (structure, "format")))
      goto no_format;

    aformat = gst_audio_format_from_string (s);
    if (aformat == GST_AUDIO_FORMAT_UNKNOWN)
      goto unknown_format;

    if (!(s = gst_structure_get_string (structure, "layout")))
      goto no_layout;
    if (!g_str_equal (s, "interleaved") && !g_str_equal (s, "non-interleaved"))
      goto unknown_layout;

    if (!gst_structure_get_int (structure, "rate", &rate))
      goto no_rate;
    if (!gst_structure_get_int (structure, "channels", &channels))
      goto no_channels;

    if (!gst_structure_get (structure, "channel-mask", GST_TYPE_BITMASK,
            &channel_mask, NULL) || (channel_mask == 0 && channels == 1)) {
      if (channels == 1) {
        position[0] = GST_AUDIO_CHANNEL_POSITION_MONO;
      } else if (channels == 2) {
        position[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        position[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      } else {
        goto no_channel_mask;
      }
    } else if (channel_mask == 0) {
      flags |= GST_AUDIO_FLAG_UNPOSITIONED;
      for (i = 0; i < MIN (64, channels); i++)
        position[i] = GST_AUDIO_CHANNEL_POSITION_NONE;
    } else {
      if (!gst_audio_channel_positions_from_mask (channels, channel_mask,
              position))
        goto invalid_channel_mask;
    }
  }

  return TRUE;

no_format:
  {
    GST_DEBUG_OBJECT (pad, "no format given");
    return FALSE;
  }
unknown_format:
  {
    GST_DEBUG_OBJECT (pad, "unknown format '%s' given", s);
    return FALSE;
  }
no_width:
  {
    GST_DEBUG_OBJECT (pad, "no width property given");
    return FALSE;
  }
no_height:
  {
    GST_DEBUG_OBJECT (pad, "no height property given");
    return FALSE;
  }
no_layout:
  {
    GST_DEBUG_OBJECT (pad, "no layout given");
    return FALSE;
  }
unknown_layout:
  {
    GST_DEBUG_OBJECT (pad, "unknown layout given");
    return FALSE;
  }
no_rate:
  {
    GST_DEBUG_OBJECT (pad, "no rate property given");
    return FALSE;
  }
no_channels:
  {
    GST_DEBUG_OBJECT (pad, "no channels property given");
    return FALSE;
  }
no_channel_mask:
  {
    GST_DEBUG_OBJECT (pad, "no channel-mask property given");
    return FALSE;
  }
invalid_channel_mask:
  {
    GST_DEBUG_OBJECT (pad, "Invalid channel mask 0x%016" G_GINT64_MODIFIER
        "x for %d channels", channel_mask, channels);
    return FALSE;
  }
}

static gboolean
gst_dec_bridge_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstDecBridge *bridge;
  gboolean res = TRUE;
  gboolean forward = TRUE;

  bridge = GST_DEC_BRIDGE (parent);

  GST_DEBUG_OBJECT (pad, "Got event : %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:
    {
      const gchar *stream_id;

      GST_DEC_BRIDGE_LOCK (bridge);
      gst_event_parse_stream_start (event, &stream_id);
      if (bridge->active_stream_id != NULL) {
        if (!g_str_equal (bridge->active_stream_id, stream_id)) {
          GST_DEBUG_OBJECT (pad, "Handle stream changes (%s => %s) !",
              bridge->active_stream_id, stream_id);
          bridge->stream_change = TRUE;
        } else {
          GST_DEBUG_OBJECT (pad, "Repeat stream-id (%s)", stream_id);
        }
      }

      if (bridge->active_stream_id) {
        g_free (bridge->active_stream_id);
        bridge->active_stream_id = NULL;
      }
      bridge->active_stream_id = g_strdup (stream_id);
      GST_DEC_BRIDGE_UNLOCK (bridge);
      break;
    }
    case GST_EVENT_CAPS:
    {
      gboolean update_caps = FALSE;
      GstCaps *caps;

      GST_DEC_BRIDGE_LOCK (bridge);
      gst_event_parse_caps (event, &caps);

      /* clear any pending reconfigure flag */
      gst_pad_check_reconfigure (bridge->srcpad);
      if (!gst_dec_bridge_valid_caps (bridge, pad, caps)) {
        GST_WARNING_OBJECT (pad,
            "Could not send caps: %" GST_PTR_FORMAT " to pad(%s:%s)", caps,
            GST_DEBUG_PAD_NAME (bridge->srcpad));
        GST_DEC_BRIDGE_UNLOCK (bridge);
        goto done;
      }

      if (bridge->stream_change) {
        update_caps = TRUE;
        bridge->stream_change = FALSE;
      }

      if (bridge->curcaps != NULL) {
        if (!gst_caps_is_equal (bridge->curcaps, caps)) {
          GST_DEBUG_OBJECT (pad,
              "Caps changes from %" GST_PTR_FORMAT " to %" GST_PTR_FORMAT,
              bridge->curcaps, caps);
          update_caps = TRUE;
        } else {
          GST_DEBUG_OBJECT (pad, "Repeat caps %" GST_PTR_FORMAT, caps);
        }
      } else {
        update_caps = TRUE;
      }

      if (update_caps) {
        GstCaps *outcaps = NULL;
        if (bridge->curcaps) {
          gst_caps_unref (bridge->curcaps);
          bridge->curcaps = NULL;
        }
        bridge->curcaps = gst_caps_ref (caps);
        outcaps = gst_caps_copy (caps);
        GST_DEBUG_OBJECT (pad, "Sending caps %" GST_PTR_FORMAT " to pad(%s:%s)",
            caps, GST_DEBUG_PAD_NAME (bridge->srcpad));
        GST_DEC_BRIDGE_UNLOCK (bridge);
        gst_pad_set_caps (bridge->srcpad, outcaps);
        gst_caps_unref (outcaps);
      } else
        GST_DEC_BRIDGE_UNLOCK (bridge);

    done:
      gst_event_unref (event);
      res = TRUE;
      forward = FALSE;
    }
      break;
    default:
      break;
  }

  if (forward)
    res = gst_pad_event_default (pad, parent, event);

  return res;
}

static GstFlowReturn
gst_dec_bridge_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstDecBridge *bridge;
  GstFlowReturn ret;

  bridge = GST_DEC_BRIDGE (parent);

  GST_LOG_OBJECT (pad, "got buffer %" GST_PTR_FORMAT, buffer);
  ret = gst_pad_push (bridge->srcpad, buffer);

  return ret;
}

static gboolean
gst_dec_bridge_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstDecBridge *bridge;
  gboolean ret = FALSE;
  GstPadDirection direction;
  GstPad *otherpad;

  bridge = GST_DEC_BRIDGE (parent);
  direction = GST_PAD_DIRECTION (pad);

  if (direction == GST_PAD_SRC)
    otherpad = bridge->sinkpad;
  else
    otherpad = bridge->srcpad;

  if (GST_QUERY_TYPE (query) == GST_QUERY_CAPS) {
    if (direction == GST_PAD_SRC) {
      GST_DEBUG_OBJECT (pad, "Got caps query, our caps are %" GST_PTR_FORMAT,
          bridge->curcaps);

      if (bridge->curcaps) {
        gst_query_set_caps_result (query, bridge->curcaps);
        return TRUE;
      }
    }
  }

  GST_DEBUG_OBJECT (pad,
      "Sending query: %" GST_PTR_FORMAT " to peerpad(%s:%s)", query,
      GST_DEBUG_PAD_NAME (otherpad));
  ret = gst_pad_peer_query (otherpad, query);

  return ret;
}
