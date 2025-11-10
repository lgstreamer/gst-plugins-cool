/* GStreamer Plugins Cool
 * Copyright (C) 2014 LG Electronics, Inc.
 *    Author : Wonchul Lee <wonchul86.lee@lge.com>
 *             HoonHee Lee <hoonhee.lee@lge.com>
 *             Jeongseok Kim <jeongseok.kim@lge.com>
 *             Myoungsun Lee <mysunny.lee@lge.com>
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

#include "gstdecproxy2.h"

#define DEFAULT_PROPAGATE_STICKY_EVENT TRUE

enum
{
  PROP_0,
  PROP_VDECBUFFER_TS,
  PROP_ADECBUFFER_TS,
  PROP_PROPAGATE_STICKY_EVENT,
};

GST_DEBUG_CATEGORY_STATIC (decproxy_debug);
#define GST_CAT_DEFAULT decproxy_debug

static GstStaticPadTemplate decproxy_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (DECODE_VIDEO_CAPS ";" DECODE_AUDIO_CAPS));

static GstStaticPadTemplate decproxy_src_template =
    GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw;audio/x-media;video/x-raw;video/x-raw(ANY)"));

#define gst_decproxy_parent_class parent_class
G_DEFINE_TYPE (GstDecProxy, gst_decproxy, GST_TYPE_BIN);

static void gst_decproxy_dispose (GObject * object);
static void gst_decproxy_finalize (GObject * object);

static void gst_decproxy_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_decproxy_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_decproxy_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_decproxy_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static gboolean gst_decproxy_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static GstPadProbeReturn dec_buffer_ts_cb (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data);

static GstPadProbeReturn back_bridge_event_probe (GstPad * pad,
    GstPadProbeInfo * info, gpointer user_data);
static GstPadProbeReturn decproxy_buffer_probe (GstPad * pad,
    GstPadProbeInfo * info, gpointer user_data);
static void front_bridge_set_blocked (GstDecProxy * decproxy, gboolean blocked);
static void back_bridge_add_probe (GstDecProxy * decproxy, gboolean blocked);
static GstPadProbeReturn decoder_downstream_probe (GstPad * pad,
    GstPadProbeInfo * info, gpointer user_data);
static GstPadProbeReturn analyze_new_caps (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data);
//static GstPadProbeReturn replace_decoder_stage1_cb (GstPad * pad,
//    GstPadProbeInfo * info, gpointer user_data);
static GstPadProbeReturn idle_reconfigure (GstPad * pad,
    GstPadProbeInfo * info, gpointer user_data);
static void reconfigure_decoder (GstDecProxy * decproxy, GstElement * decoder);

static GstPadProbeReturn droppable_buffer_drop_probe (GstPad * pad,
    GstPadProbeInfo * info, gpointer user_data);

static gint sort_pending_events (GstEvent * e1, GstEvent * e2);
static gboolean event_in_pending_list (GstDecProxy * decproxy,
    GstEvent * event);
static gboolean is_raw_caps (GstCaps * caps);

static void
gst_decproxy_class_init (GstDecProxyClass * klass)
{
  GObjectClass *gobject_klass = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (decproxy_debug, "decproxy", 0, "Decoder Proxy Bin");

  parent_class = g_type_class_peek_parent (klass);

  gobject_klass->dispose = GST_DEBUG_FUNCPTR (gst_decproxy_dispose);
  gobject_klass->finalize = GST_DEBUG_FUNCPTR (gst_decproxy_finalize);

  gobject_klass->set_property = GST_DEBUG_FUNCPTR (gst_decproxy_set_property);
  gobject_klass->get_property = GST_DEBUG_FUNCPTR (gst_decproxy_get_property);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&decproxy_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&decproxy_src_template));

  gst_element_class_set_static_metadata (element_class,
      "Proxy for Decoders", "Codec/Decoder/Bin",
      "Acutal decoder deployment controller by resource permissions",
      "Wonchul Lee <wonchul86.lee@lge.com>, HoonHee Lee <hoonhee.lee@lge.com>");

  g_object_class_install_property (gobject_klass, PROP_VDECBUFFER_TS,
      g_param_spec_uint64 ("vdec-buffer-ts", "Video Decoder Buffer TimeStamp",
          "To Use Buffering Logic, Get timestamp at decproxy", 0,
          G_MAXUINT64, GST_CLOCK_TIME_NONE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_ADECBUFFER_TS,
      g_param_spec_uint64 ("adec-buffer-ts", "Audio Decoder Buffer TimeStamp",
          "To Use Buffering Logic, Get timestamp at decproxy", 0,
          G_MAXUINT64, GST_CLOCK_TIME_NONE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstDecproxy::propatate-sticky-events:
   * If TRUE decproxy will send stream-start and caps event in order to re-order
   * track order and end auto-plugging up in raw data by caps.
   */
  g_object_class_install_property (gobject_klass, PROP_PROPAGATE_STICKY_EVENT,
      g_param_spec_boolean ("propagate-sticky-event",
          "Propagate stream-start and caps event",
          "Propagate stream-start and caps event",
          DEFAULT_PROPAGATE_STICKY_EVENT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
pad_linked (GstPad * pad, GstPad * peer, gpointer user_data)
{
  GstDecProxy *decproxy = GST_DECPROXY (user_data);
  GstStructure *smart_properties = NULL;
  GstElement *top;
  GstPipeline *pipe;

  top = (GstElement *) decproxy;
  while (GST_ELEMENT_PARENT (top))
    top = GST_ELEMENT_PARENT (top);

  pipe = (GstPipeline *) top;

  smart_properties = get_smart_property_from_source ((GstElement *) pipe);
  GST_DEBUG_OBJECT (decproxy, "smart_properties: %p", smart_properties);
  if (smart_properties) {
    gst_structure_get_boolean (smart_properties, "use-stream-collection",
        &decproxy->use_stream_collection);
    GST_DEBUG_OBJECT (decproxy,
        "has 'use-stream-collection' field: %d, returned value: %d",
        gst_structure_has_field (smart_properties, "use-stream-collection"),
        decproxy->use_stream_collection);
    gst_structure_free (smart_properties);
  } else {
    GstSmartPropertiesReturn ret =
        gst_element_get_smart_properties (GST_ELEMENT_CAST (decproxy),
        "use-stream-collection", &decproxy->use_stream_collection, NULL);
    GST_DEBUG_OBJECT (decproxy,
        "response of custom query : [%d], use-stream-collection = [%d]", ret,
        decproxy->use_stream_collection);
  }
}

static void
pad_unlinked (GstPad * pad, GstPad * peer, gpointer user_data)
{
  GstDecProxy *decproxy = GST_DECPROXY (user_data);

  GST_DEBUG_OBJECT (pad, "Clear pending-switch-decoder state");
  decproxy->pending_switch_decoder = FALSE;
}

/* FIXME, Pending all events until we get first buffer. */
static GstPadProbeReturn
back_bridge_event_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstDecProxy *decproxy = GST_DECPROXY (user_data);
  GstEvent *ev = GST_PAD_PROBE_INFO_EVENT (info);
  GstEvent *copy = NULL;

#if 0
  if (!decproxy->decoder && event_in_pending_list (decproxy, ev)) {
    GST_DEBUG_OBJECT (decproxy,
        "Drop immediately if pending list has %s event on pad(%s:%s)",
        GST_EVENT_TYPE_NAME (GST_EVENT_TYPE (ev)), GST_DEBUG_PAD_NAME (pad));
    return GST_PAD_PROBE_DROP;
  }
#endif

  /* FIXME: Store Non-raw CAPS when actual decoder is not activated.
   * Otherwise, store Raw CAPS when actual decoder is activated. */
  if (GST_EVENT_TYPE (ev) == GST_EVENT_CAPS && !decproxy->decoder) {
    GstPad *srcpad = gst_element_get_static_pad (decproxy->front, "src");
    GstEvent *caps_event = gst_pad_get_sticky_event (srcpad, GST_EVENT_CAPS, 0);
    if (caps_event) {
      copy = gst_event_copy (caps_event);
      gst_event_unref (caps_event);
    }
    gst_object_unref (srcpad);
  } else
    copy = gst_event_copy (ev);

  if (copy) {
    GST_WARNING_OBJECT (decproxy,
        "Not Forwarding and Storing event: %" GST_PTR_FORMAT " on pad(%s:%s)",
        copy, GST_DEBUG_PAD_NAME (pad));
#if 0
    decproxy->pending_events =
        g_list_insert_sorted (decproxy->pending_events, copy,
        (GCompareFunc) sort_pending_events);
#endif
    decproxy->pending_events = g_list_append (decproxy->pending_events, copy);

    /* FIXME: Store event and drop it immediately until flushing is finished
     * even if actual decoder is already deployed. If not, deadlock can be happened
     * in MQ's output with GST_PAD_STREAM_LOCK (). */
    if (decproxy->decoder) {
      GstEvent *copy_ev = gst_event_copy (ev);
      GST_WARNING_OBJECT (decproxy,
          "Storing event: %" GST_PTR_FORMAT " on pad(%s:%s) for back bridge",
          copy, GST_DEBUG_PAD_NAME (pad));
      decproxy->back_pending_events =
          g_list_append (decproxy->back_pending_events, copy_ev);
    }
  }

  GST_DEBUG_OBJECT (decproxy, "Dropped on pad(%s:%s)",
      GST_DEBUG_PAD_NAME (pad));

  return GST_PAD_PROBE_DROP;
}

/* FIXME: Probe DOWNSTREAM EVENTS/BUFFER on back bridge's sinkpad.
 * Missing events (i.e. STREAM_START, CAPS, ...) by flushing would be forwarded
 * to downstream elements. */
static GstPadProbeReturn
back_downstream_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstDecProxy *decproxy = GST_DECPROXY (user_data);
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;
  gboolean remove_probe = FALSE;

  if (GST_IS_EVENT (info->data)) {
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
    GST_DEBUG_OBJECT (decproxy,
        "Got a event: %" GST_PTR_FORMAT " on pad(%s:%s)", event,
        GST_DEBUG_PAD_NAME (pad));

    if (GST_EVENT_TYPE (event) == GST_EVENT_GAP)
      remove_probe = TRUE;

    if (decproxy->back_pending_events) {
      GList **pending_events, *l;

      pending_events = &decproxy->back_pending_events;

      for (l = *pending_events; l;) {
        GstEvent *pev = GST_EVENT (l->data);
        GList *tmp;

        if (GST_EVENT_TYPE (pev) < GST_EVENT_TYPE (event)) {
          GstEvent *dec_ev =
              gst_pad_get_sticky_event (pad, GST_EVENT_TYPE (pev), 0);
          if (!dec_ev) {
            GST_WARNING_OBJECT (decproxy,
                "Sending omitted event: %" GST_PTR_FORMAT, pev);
            gst_pad_send_event (pad, pev);
          } else {
            gst_event_unref (dec_ev);
            gst_event_unref (pev);
          }

          tmp = l;
          l = l->next;
          *pending_events = g_list_delete_link (*pending_events, tmp);
        } else {
          l = l->next;
        }
      }
    }
  } else if (GST_IS_BUFFER (info->data)) {
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);
    GST_DEBUG_OBJECT (decproxy,
        "Got a buffer: %" GST_PTR_FORMAT " on pad(%s:%s)", buffer,
        GST_DEBUG_PAD_NAME (pad));
    remove_probe = TRUE;
  }

  if (remove_probe) {
    decproxy->back_downstream_probe_id = 0;
    ret = GST_PAD_PROBE_REMOVE;
  }

  return ret;
}

static void
reset_flushing_properties (GstDecProxy * decproxy)
{
  decproxy->eaten_flush_start = FALSE;
  decproxy->eaten_flush_stop = FALSE;
  decproxy->waiting_flush = FALSE;
  decproxy->sending_event = FALSE;
}

/* FIXME, Probe buffer on decproxy's sinkpad. */
static GstPadProbeReturn
decproxy_buffer_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);
  GstDecProxy *decproxy = GST_DECPROXY (user_data);

  GST_DEBUG_OBJECT (decproxy,
      "Got a buffer: %" GST_PTR_FORMAT " on pad(%s:%s)", buffer,
      GST_DEBUG_PAD_NAME (pad));

  GST_DECPROXY_LOCK (decproxy);
  decproxy->seen_buffer = TRUE;
  reset_flushing_properties (decproxy);
  GST_DECPROXY_UNLOCK (decproxy);

  return GST_PAD_PROBE_REMOVE;
}

static void
_internal_pad_push_pending_events (GstDecProxy * decproxy, GList * events)
{
  GList *l;
  GstPad *srcpad = NULL;

  srcpad = gst_element_get_static_pad ((GstElement *) decproxy, "src");

  for (l = g_list_last (events); l; l = l->prev) {
    if (GST_EVENT_TYPE (l->data) == GST_EVENT_CAPS) {
      GstEvent *e =
          gst_pad_get_sticky_event (srcpad, GST_EVENT_STREAM_START, 0);
      /* FIXME: Add DOWNSTREAM EVENT probe to send missing STREAM_START event for
       * the playsink knows what stream it will be getting. */
      if (!e) {
        GstPad *front_srcpad =
            gst_element_get_static_pad (decproxy->front, "src");
        GstEvent *s_ev =
            gst_pad_get_sticky_event (front_srcpad, GST_EVENT_STREAM_START, 0);
        GST_WARNING_OBJECT (decproxy,
            "Storing stream-start: %" GST_PTR_FORMAT " on pad(%s:%s)", s_ev,
            GST_DEBUG_PAD_NAME (srcpad));
        gst_pad_store_sticky_event (srcpad, s_ev);
        gst_event_unref (s_ev);
        gst_object_unref (front_srcpad);
      } else
        gst_event_unref (e);
    }

    GST_DEBUG_OBJECT (decproxy, "Forwarding event: %" GST_PTR_FORMAT,
        GST_EVENT (l->data));
    gst_pad_push_event (srcpad, l->data);
  }
  g_list_free (events);

  gst_object_unref (srcpad);
}

static gint
sort_pending_events (GstEvent * e1, GstEvent * e2)
{
  GST_LOG ("e1: %" GST_PTR_FORMAT, e1);
  GST_LOG ("e2: %" GST_PTR_FORMAT, e2);

  return (gint) GST_EVENT_TYPE (e2) - (gint) GST_EVENT_TYPE (e1);
}

static gboolean
event_in_pending_list (GstDecProxy * decproxy, GstEvent * event)
{
  GST_FIXME_OBJECT (decproxy, "Checking pending events %" GST_PTR_FORMAT,
      event);

  if (!GST_EVENT_IS_STICKY (event)
      || GST_EVENT_TYPE (event) > GST_EVENT_SEGMENT) {
    GST_LOG_OBJECT (decproxy, "Don't consider NON-STICKY event");
    return TRUE;
  }

  if (decproxy->pending_events) {
    GList **pending_events, *l;

    pending_events = &decproxy->pending_events;

    for (l = *pending_events; l;) {
      GstEvent *pev = GST_EVENT (l->data);

      if (GST_EVENT_TYPE (event) == GST_EVENT_TYPE (pev)) {
        if (GST_EVENT_SEQNUM (event) != GST_EVENT_SEQNUM (pev))
          GST_LOG_OBJECT (decproxy,
              "pending list has %s event, but seqmum is different (%u)",
              GST_EVENT_TYPE_NAME (event), (guint) GST_EVENT_SEQNUM (event));
        GST_DEBUG_OBJECT (decproxy, "pending list has %s event",
            GST_EVENT_TYPE_NAME (event));
        return TRUE;
      }
      l = l->next;
    }
  }

  GST_DEBUG_OBJECT (decproxy, "%s event is not in pending list",
      GST_EVENT_TYPE_NAME (event));
  return FALSE;
}

static gboolean
is_raw_caps (GstCaps * caps)
{
  GstCaps *raw_video_caps = gst_caps_from_string ("video/x-raw");
  GstCaps *raw_audio_caps = gst_caps_from_string ("audio/x-raw");
  gboolean is_raw = FALSE;

  is_raw = gst_caps_can_intersect (caps,
      raw_video_caps) ? TRUE : gst_caps_can_intersect (caps,
      raw_audio_caps) ? TRUE : FALSE;

  gst_caps_unref (raw_video_caps);
  gst_caps_unref (raw_audio_caps);

  return is_raw;
}

static gboolean
_internal_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstDecProxy *decproxy = gst_pad_get_element_private (pad);
  GstPad *srcpad = NULL;
  gboolean ret = TRUE;
  gboolean dump_syslog = FALSE;

  GST_DECPROXY_LOCK (decproxy);

  /* FIXME: Don't allow forwarding sticky events to backend until actual decoder is configured */
  if (!decproxy->decoder) {
    gboolean is_raw = FALSE;

    if (GST_EVENT_TYPE (event) == GST_EVENT_CAPS) {
      GstCaps *caps = NULL;

      gst_event_parse_caps (event, &caps);
      is_raw = is_raw_caps (caps);
    }

    if (!is_raw && !event_in_pending_list (decproxy, event)) {
      GstEvent *copy = gst_event_copy (event);

      GST_WARNING_OBJECT (decproxy,
          "Not Forwarding and Storing event: %" GST_PTR_FORMAT " on pad(%s:%s)",
          copy, GST_DEBUG_PAD_NAME (pad));
      decproxy->pending_events =
          g_list_insert_sorted (decproxy->pending_events, copy,
          (GCompareFunc) sort_pending_events);
    } else if (is_raw) {
      GST_WARNING_OBJECT (decproxy,
          "Not Forwarding and Queueing event: %" GST_PTR_FORMAT
          " on pad(%s:%s)", event, GST_DEBUG_PAD_NAME (pad));
      decproxy->internal_pending_events =
          g_list_insert_sorted (decproxy->internal_pending_events, event,
          (GCompareFunc) sort_pending_events);
    }

    GST_DECPROXY_UNLOCK (decproxy);
    return TRUE;
  }

  if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT
      || GST_EVENT_TYPE (event) == GST_EVENT_EOS
      || GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_START
      || GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_STOP
      || GST_EVENT_TYPE (event) == GST_EVENT_GAP) {
    dump_syslog = TRUE;
  }

  if (dump_syslog) {
    GST_SYS_DEBUG_OBJECT (decproxy,
        "Got event: %" GST_PTR_FORMAT " on pad(%s:%s)", event,
        GST_DEBUG_PAD_NAME (pad));
  } else {
    GST_DEBUG_OBJECT (decproxy,
        "Got event: %" GST_PTR_FORMAT " on pad(%s:%s)", event,
        GST_DEBUG_PAD_NAME (pad));
  }

  srcpad = gst_element_get_static_pad ((GstElement *) decproxy, "src");

  if (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_START
      && !decproxy->got_internal_buffer) {
    GST_FIXME_OBJECT (decproxy, "Got flush-start before any buffers");
    goto forward;
  }

  if (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_STOP) {
    GList *l;

    for (l = decproxy->internal_pending_events; l; l = l->next) {
      if (GST_EVENT_TYPE (l->data) == GST_EVENT_SEGMENT ||
          GST_EVENT_TYPE (l->data) == GST_EVENT_EOS) {
        gst_event_unref (l->data);
        decproxy->internal_pending_events =
            g_list_delete_link (decproxy->internal_pending_events, l);
        break;
      }
    }
    if (!decproxy->got_internal_buffer) {
      GST_FIXME_OBJECT (decproxy, "Got flush-stop before any buffers");
      goto forward;
    }
  }

  if (!decproxy->got_internal_buffer && GST_EVENT_TYPE (event) == GST_EVENT_GAP) {
    decproxy->got_internal_buffer = TRUE;
    /* push pending events before GAP */
    if (G_UNLIKELY (decproxy->internal_pending_events)) {
      GList *events = decproxy->internal_pending_events;
      decproxy->internal_pending_events = NULL;
      GST_DECPROXY_UNLOCK (decproxy);
      GST_DEBUG_OBJECT (decproxy, "Pushing all pending events before GAP");
      _internal_pad_push_pending_events (decproxy, events);
      GST_DECPROXY_LOCK (decproxy);
    }
  }

  if (decproxy->got_internal_buffer && (!GST_EVENT_IS_STICKY (event)
          || GST_EVENT_TYPE (event) <= GST_EVENT_CAPS))
    goto forward;

  /* If we get EOS before any buffers, just push all pending events */
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    GList *l;

    for (l = g_list_last (decproxy->internal_pending_events); l; l = l->prev) {
      GST_DEBUG_OBJECT (decproxy, "Forwarding %s event",
          GST_EVENT_TYPE_NAME (l->data));
      GST_DECPROXY_UNLOCK (decproxy);
      gst_pad_push_event (srcpad, l->data);
      GST_DECPROXY_LOCK (decproxy);
    }
    g_list_free (decproxy->internal_pending_events);
    decproxy->internal_pending_events = NULL;
  } else if (!decproxy->got_internal_buffer || !decproxy->got_internal_caps) {
    GST_DEBUG_OBJECT (decproxy,
        "Queueing event: %" GST_PTR_FORMAT " on pad(%s:%s)", event,
        GST_DEBUG_PAD_NAME (pad));
    if (GST_EVENT_IS_SERIALIZED (event) && GST_EVENT_IS_STICKY (event)) {
      decproxy->internal_pending_events =
          g_list_insert_sorted (decproxy->internal_pending_events, event,
          (GCompareFunc) sort_pending_events);
    } else
      decproxy->internal_pending_events =
          g_list_prepend (decproxy->internal_pending_events, event);
    gst_object_unref (srcpad);

    if (GST_EVENT_TYPE (event) == GST_EVENT_CAPS)
      decproxy->got_internal_caps = TRUE;

    GST_DECPROXY_UNLOCK (decproxy);

    return TRUE;
  }

forward:

  if (dump_syslog) {
    GST_SYS_DEBUG_OBJECT (decproxy,
        "Pushing immediately event: %" GST_PTR_FORMAT, event);
  } else {
    GST_DEBUG_OBJECT (decproxy, "Pushing immediately event: %" GST_PTR_FORMAT,
        event);
  }

  GST_DECPROXY_UNLOCK (decproxy);

  ret = gst_pad_push_event (srcpad, gst_event_ref (event));
  if (dump_syslog) {
    GST_SYS_DEBUG_OBJECT (decproxy, "Pushed event: %" GST_PTR_FORMAT, event);
  } else {
    GST_DEBUG_OBJECT (decproxy, "Pushed event: %" GST_PTR_FORMAT, event);
  }

  gst_object_unref (srcpad);
  gst_event_unref (event);

  return ret;
}

static GstFlowReturn
_internal_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstDecProxy *decproxy = gst_pad_get_element_private (pad);
  GstPad *srcpad = NULL;
  GstFlowReturn ret;

  GST_DECPROXY_LOCK (decproxy);
  GST_DEBUG_OBJECT (decproxy, "Got Buffer: %" GST_PTR_FORMAT " on pad(%s:%s)",
      buffer, GST_DEBUG_PAD_NAME (pad));

  decproxy->got_internal_buffer = TRUE;

  /* push pending events before a buffer */
  if (G_UNLIKELY (decproxy->internal_pending_events)) {
    GList *events = decproxy->internal_pending_events;
    decproxy->internal_pending_events = NULL;
    GST_DECPROXY_UNLOCK (decproxy);
    _internal_pad_push_pending_events (decproxy, events);
    GST_DECPROXY_LOCK (decproxy);
  }

  srcpad = gst_element_get_static_pad ((GstElement *) decproxy, "src");
  GST_DECPROXY_UNLOCK (decproxy);

  ret = gst_pad_push (srcpad, buffer);
  gst_object_unref (srcpad);

  return ret;
}

static gboolean
_internal_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstDecProxy *decproxy = gst_pad_get_element_private (pad);
  GstPad *srcpad = NULL;
  gboolean ret;

  GST_LOG_OBJECT (decproxy, "Got Query: %" GST_PTR_FORMAT " on pad(%s:%s)",
      query, GST_DEBUG_PAD_NAME (pad));
  srcpad = gst_element_get_static_pad ((GstElement *) decproxy, "src");
  ret = gst_pad_peer_query (srcpad, query);
  gst_object_unref (srcpad);

  return ret;
}

static void
gst_decproxy_init (GstDecProxy * decproxy)
{
  GstPad *pad;
  GstPad *gpad;
  GstPadTemplate *pad_tmpl;

  g_rec_mutex_init (&decproxy->lock);
  g_mutex_init (&decproxy->flush_lock);
  g_cond_init (&decproxy->flush_cond);

  decproxy->stream_type = GST_COOL_STREAM_TYPE_UNKNOWN;

  decproxy->decoder = NULL;
  decproxy->puppet = NULL;
  decproxy->secure = NULL;
  decproxy->current_decoder_state = GST_DECPROXY_STATE_UNKNOWN;
  decproxy->next_decoder_state = GST_DECPROXY_STATE_UNKNOWN;
  decproxy->pending_decoder_state = GST_DECPROXY_STATE_UNKNOWN;
  decproxy->pending_switch_decoder = FALSE;
  decproxy->dts_seamless = FALSE;
  decproxy->dual_mono = 0;
  decproxy->vdec_buf_ts = GST_CLOCK_TIME_NONE;
  decproxy->adec_buf_ts = GST_CLOCK_TIME_NONE;
  decproxy->use_stream_collection = FALSE;
  decproxy->propagate_sticky_event = DEFAULT_PROPAGATE_STICKY_EVENT;
  decproxy->eaten_flush_start = FALSE;
  decproxy->eaten_flush_stop = FALSE;
  decproxy->pending_event_probe_id = 0;
  decproxy->dec_downstream_probe_id = 0;
  decproxy->back_downstream_probe_id = 0;
  decproxy->seen_buffer = FALSE;
  decproxy->waiting_flush = FALSE;
  decproxy->sending_event = FALSE;
  decproxy->changing_decoder = FALSE;
  decproxy->got_internal_caps = FALSE;
  decproxy->got_internal_buffer = FALSE;
  decproxy->has_secure_area = NULL;
  decproxy->use_external_dec = FALSE;

  decproxy->front = gst_element_factory_make ("decbridge", NULL);
  decproxy->back = gst_element_factory_make ("decbridge", NULL);

  if (!gst_bin_add (GST_BIN (decproxy), decproxy->front)) {
    g_warning ("Could not add front identity element, decproxy will not work");
    gst_object_unref (decproxy->front);
    decproxy->front = NULL;
  }

  if (!gst_bin_add (GST_BIN (decproxy), decproxy->back)) {
    g_warning ("Could not add back identity element, decproxy will not work");
    gst_object_unref (decproxy->back);
    decproxy->back = NULL;
  }

  pad = gst_element_get_static_pad (decproxy->front, "sink");
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER,
      dec_buffer_ts_cb, decproxy, NULL);
  pad_tmpl = gst_static_pad_template_get (&decproxy_sink_template);

  GST_DEBUG_OBJECT (pad, "Trying to connect front identity sink pad");

  gpad = gst_ghost_pad_new_from_template ("sink", pad, pad_tmpl);
  gst_pad_set_active (gpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (decproxy), gpad);

  gst_pad_set_event_function (GST_PAD_CAST (gpad),
      GST_DEBUG_FUNCPTR (gst_decproxy_sink_event));

  g_signal_connect (G_OBJECT (gpad), "linked", (GCallback) pad_linked,
      decproxy);
  g_signal_connect (G_OBJECT (gpad), "unlinked", (GCallback) pad_unlinked,
      decproxy);

  gst_pad_add_probe (GST_PAD_CAST (gpad), GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) decproxy_buffer_probe, decproxy, NULL);

  gst_object_unref (pad_tmpl);
  gst_object_unref (pad);

  pad = gst_element_get_static_pad (decproxy->front, "src");
  decproxy->blocked_id =
      gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
      analyze_new_caps, decproxy, NULL);

  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER,
      droppable_buffer_drop_probe, decproxy, NULL);
  gst_object_unref (pad);

  pad = gst_element_get_static_pad (decproxy->back, "src");

  decproxy->internal_pad = gst_pad_new ("internal", GST_PAD_SINK);
  gst_object_set_parent (GST_OBJECT_CAST (decproxy->internal_pad),
      GST_OBJECT_CAST (decproxy));
  GST_OBJECT_FLAG_SET (decproxy->internal_pad, GST_PAD_FLAG_NEED_PARENT);
  gst_pad_set_element_private (decproxy->internal_pad, decproxy);
  gst_pad_set_active (decproxy->internal_pad, TRUE);
  gst_pad_set_chain_function (decproxy->internal_pad, _internal_chain);
  gst_pad_set_event_function (decproxy->internal_pad, _internal_event);
  gst_pad_set_query_function (decproxy->internal_pad, _internal_query);

  GST_DEBUG_OBJECT (pad, "Trying to connect back identity and internal pad");
  if (gst_pad_link_full (pad, decproxy->internal_pad,
          GST_PAD_LINK_CHECK_NOTHING) != GST_PAD_LINK_OK) {
    GST_ERROR_OBJECT (decproxy, "Failed to link internal pad");
  }

  GST_DEBUG_OBJECT (pad, "Trying to active ghost srcpad");
  pad_tmpl = gst_static_pad_template_get (&decproxy_src_template);
  gpad = gst_ghost_pad_new_no_target_from_template ("src", pad_tmpl);
  gst_pad_set_active (gpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (decproxy), gpad);

  gst_pad_set_query_function (GST_PAD_CAST (gpad),
      GST_DEBUG_FUNCPTR (gst_decproxy_src_query));

  gst_pad_set_event_function (GST_PAD_CAST (gpad),
      GST_DEBUG_FUNCPTR (gst_decproxy_src_event));

  gst_object_unref (pad_tmpl);
  gst_object_unref (pad);
}

static void
gst_decproxy_dispose (GObject * object)
{
  GstDecProxy *decproxy = GST_DECPROXY (object);

  if (decproxy->back != NULL) {
    GST_DEBUG_OBJECT (decproxy->back, "Trying to remove back identity");
    gst_element_set_state (decproxy->back, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (decproxy), decproxy->back);
    decproxy->back = NULL;
  }

  if (decproxy->current_decoder_state == GST_DECPROXY_STATE_PUPPET) {
    if (decproxy->puppet != NULL) {
      GST_DEBUG_OBJECT (decproxy->back, "Trying to remove puppet");
      gst_element_set_state (decproxy->puppet, GST_STATE_NULL);
      gst_bin_remove (GST_BIN_CAST (decproxy), decproxy->puppet);
      decproxy->puppet = NULL;
    }
  } else {
    if (decproxy->decoder != NULL) {
      GST_DEBUG_OBJECT (decproxy->back, "Trying to remove decoder");
      gst_element_set_state (decproxy->decoder, GST_STATE_NULL);
      gst_bin_remove (GST_BIN_CAST (decproxy), decproxy->decoder);
      decproxy->decoder = NULL;
    }
  }

  if (decproxy->secure != NULL) {
    GST_DEBUG_OBJECT (decproxy->secure, "Trying to remove secure");
    gst_element_set_state (decproxy->secure, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (decproxy), decproxy->secure);
    decproxy->secure = NULL;
  }

  if (decproxy->front != NULL) {
    GST_DEBUG_OBJECT (decproxy->back, "Trying to remove front identity");
    gst_element_set_state (decproxy->front, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (decproxy), decproxy->front);
    decproxy->front = NULL;
  }

  if (decproxy->internal_pad) {
    gst_object_unparent (GST_OBJECT_CAST (decproxy->internal_pad));
    decproxy->internal_pad = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_decproxy_finalize (GObject * object)
{
  GstDecProxy *decproxy = GST_DECPROXY (object);

  g_rec_mutex_clear (&decproxy->lock);
  g_mutex_clear (&decproxy->flush_lock);
  g_cond_clear (&decproxy->flush_cond);

  g_list_free_full (decproxy->pending_events, (GDestroyNotify) gst_event_unref);
  decproxy->pending_events = NULL;

  g_list_free_full (decproxy->back_pending_events,
      (GDestroyNotify) gst_event_unref);
  decproxy->back_pending_events = NULL;

  g_list_free_full (decproxy->internal_pending_events,
      (GDestroyNotify) gst_event_unref);
  decproxy->internal_pending_events = NULL;

  if (decproxy->resource_info) {
    gst_structure_free (decproxy->resource_info);
    decproxy->resource_info = NULL;
  }

  g_free (decproxy->has_secure_area);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_decproxy_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean ret = FALSE;
  gchar *buffer_ts = NULL;
  GstDecProxy *decproxy = GST_DECPROXY (parent);

  GST_LOG_OBJECT (decproxy, "got query : %" GST_PTR_FORMAT, query);

  if (GST_QUERY_TYPE (query) == GST_QUERY_CUSTOM) {
    GstStructure *s;
    s = (GstStructure *) gst_query_get_structure (query);

    buffer_ts =
        g_strdup_printf ("%s-buffer-ts",
        (decproxy->stream_type ==
            GST_COOL_STREAM_TYPE_VIDEO ? "vdec" : "adec"));

    if (gst_structure_has_name (s, buffer_ts)) {
      GValue value = { 0, };

      guint64 ts =
          (decproxy->stream_type ==
          GST_COOL_STREAM_TYPE_VIDEO ? decproxy->
          vdec_buf_ts : decproxy->adec_buf_ts);

      g_value_init (&value, G_TYPE_UINT64);
      g_value_set_uint64 (&value, ts);

      gst_structure_set_value (s, buffer_ts, &value);

      g_value_unset (&value);

      ret = TRUE;
      goto done;
    }

    if (gst_structure_has_name (s, "external-sink")
        && decproxy->use_external_dec
        && decproxy->stream_type == GST_COOL_STREAM_TYPE_VIDEO) {
      GValue value = { 0, };
      gchar *fac_name = NULL;
      GST_DEBUG_OBJECT (decproxy, "Seeing external-sink query");
      g_value_init (&value, G_TYPE_STRING);
      if (!(fac_name = gst_cool_get_external_video_sink ())) {
        GST_DEBUG_OBJECT (decproxy, "fail to get external video sink");
        ret = FALSE;
        goto done;
      }
      GST_DEBUG_OBJECT (decproxy, "external sink name : %s", fac_name);
      g_value_set_string (&value, fac_name);
      gst_structure_set_value (s, "external-sink", &value);
      g_value_unset (&value);
      g_free (fac_name);
      ret = TRUE;
      goto done;
    }
  }
  ret = gst_pad_peer_query (decproxy->internal_pad, query);

done:
  if (buffer_ts)
    g_free (buffer_ts);
  return ret;

}

static void
gst_decproxy_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDecProxy *decproxy = GST_DECPROXY (object);

  switch (prop_id) {
    case PROP_PROPAGATE_STICKY_EVENT:
      GST_DECPROXY_LOCK (decproxy);
      decproxy->propagate_sticky_event = g_value_get_boolean (value);
      GST_DECPROXY_UNLOCK (decproxy);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_decproxy_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDecProxy *decproxy = GST_DECPROXY (object);
  switch (prop_id) {
    case PROP_VDECBUFFER_TS:
      g_value_set_uint64 (value, decproxy->vdec_buf_ts);
      break;
    case PROP_ADECBUFFER_TS:
      g_value_set_uint64 (value, decproxy->adec_buf_ts);
      break;
    case PROP_PROPAGATE_STICKY_EVENT:
      GST_DECPROXY_LOCK (decproxy);
      g_value_set_boolean (value, decproxy->propagate_sticky_event);
      GST_DECPROXY_UNLOCK (decproxy);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_decproxy_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstDecProxy *decproxy = GST_DECPROXY (parent);
  gboolean ret = TRUE;
  gboolean forward = TRUE;

  GST_DEBUG_OBJECT (pad, "got event %" GST_PTR_FORMAT, event);

  GST_DECPROXY_LOCK (decproxy);
  if (!decproxy->seen_buffer) {
    g_mutex_lock (&decproxy->flush_lock);
    if (GST_EVENT_IS_SERIALIZED (event) && GST_EVENT_IS_STICKY (event)) {
      decproxy->sending_event = TRUE;
    }
    g_mutex_unlock (&decproxy->flush_lock);
  }
  GST_DECPROXY_UNLOCK (decproxy);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;
      gst_event_parse_caps (event, &caps);

      GST_DEBUG_OBJECT (decproxy, "recieved caps %" GST_PTR_FORMAT, caps);
      if (!decproxy->use_stream_collection &&
          decproxy->current_decoder_state != GST_DECPROXY_STATE_DECODER) {
        GstMessage *message = NULL;
        GstStructure *media_info = NULL;
        gchar *stream_id;
        const gchar *mime_type;

        GST_DECPROXY_LOCK (decproxy);

        stream_id = gst_pad_get_stream_id (pad);
        mime_type = gst_structure_get_name (gst_caps_get_structure (caps, 0));
        media_info = gst_cool_caps_to_info (caps, stream_id, mime_type);

        if (media_info) {
          /* store type of media */
          gst_structure_get_int (media_info, "type", &decproxy->stream_type);

          /* post media-info */
          message =
              gst_message_new_custom (GST_MESSAGE_APPLICATION,
              GST_OBJECT (decproxy), media_info);
          gst_element_post_message (GST_ELEMENT_CAST (decproxy), message);
        }

        GST_DECPROXY_UNLOCK (decproxy);

        g_free (stream_id);
      }

      if (caps) {
        GstStructure *s = gst_caps_get_structure (caps, 0);
        if (decproxy->has_secure_area) {
          g_free (decproxy->has_secure_area);
          decproxy->has_secure_area = NULL;
        }
        decproxy->has_secure_area =
            g_strdup (gst_structure_get_string (s, "secure_area"));
      }
      break;
    }
    case GST_EVENT_TAG:
    {
      if (!decproxy->use_stream_collection) {
        GstTagList *tags;
        GstMessage *message = NULL;
        GstCaps *caps = NULL;
        GstStructure *media_info = NULL;
        gchar *stream_id;
        const gchar *mime_type;

        gst_event_parse_tag (event, &tags);
        GST_DEBUG_OBJECT (pad, "got taglist %" GST_PTR_FORMAT, tags);

        GST_DECPROXY_LOCK (decproxy);
        caps = gst_pad_get_current_caps (pad);
        mime_type = gst_structure_get_name (gst_caps_get_structure (caps, 0));

        stream_id = gst_pad_get_stream_id (pad);
        media_info = gst_cool_taglist_to_info (tags, stream_id, mime_type);

        if (media_info) {
          /* store type of media */
          gst_structure_get_int (media_info, "type", &decproxy->stream_type);

          /* post media-info */
          message =
              gst_message_new_custom (GST_MESSAGE_APPLICATION,
              GST_OBJECT (decproxy), media_info);
          gst_element_post_message (GST_ELEMENT_CAST (decproxy), message);
        }

        GST_DECPROXY_UNLOCK (decproxy);

        gst_caps_unref (caps);
        g_free (stream_id);
      }
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      if (decproxy->puppet) {
        GstSegment segment;
        gboolean active_mode = FALSE;

        GST_OBJECT_LOCK (decproxy);

        gst_event_copy_segment (event, &segment);

        /* turn on active mode for dropping buffers, if the rate is not 1.0 */
        if (segment.rate < 1.0 || segment.rate > 1.0)
          active_mode = TRUE;

        GST_DEBUG_OBJECT (decproxy, "Set active-mode to %s for trick play",
            (active_mode ? "TRUE" : "FALSE"));
        g_object_set (decproxy->puppet, "active-mode", active_mode, NULL);

        GST_OBJECT_UNLOCK (decproxy);
      }
      break;
    }
    case GST_EVENT_GAP:
    {
      GST_DECPROXY_LOCK (decproxy);
      if (!decproxy->seen_buffer) {
        decproxy->seen_buffer = TRUE;
        reset_flushing_properties (decproxy);
      }
      GST_DECPROXY_UNLOCK (decproxy);
      break;
    }
    case GST_EVENT_FLUSH_START:
    {
      GST_DECPROXY_LOCK (decproxy);
      if (!decproxy->seen_buffer) {
        GST_DEBUG_OBJECT (decproxy, "sending_event(%d), changing_decoder(%d)",
            decproxy->sending_event, decproxy->changing_decoder);
        while (decproxy->sending_event || decproxy->changing_decoder) {
          back_bridge_add_probe (decproxy, TRUE);
          front_bridge_set_blocked (decproxy, FALSE);

          GST_INFO_OBJECT (decproxy, "Waiting FLUSH-START ...");
          g_mutex_lock (&decproxy->flush_lock);
          decproxy->waiting_flush = TRUE;
          GST_DECPROXY_UNLOCK (decproxy);
          g_cond_wait (&decproxy->flush_cond, &decproxy->flush_lock);
          GST_DECPROXY_LOCK (decproxy);
          decproxy->waiting_flush = FALSE;
          g_mutex_unlock (&decproxy->flush_lock);
        }

        decproxy->eaten_flush_stop = FALSE;
        decproxy->eaten_flush_start = TRUE;


        GST_INFO_OBJECT (decproxy, "Eaten FLUSH-START %" GST_PTR_FORMAT, event);
        gst_event_unref (event);
        forward = FALSE;
        ret = TRUE;
      }
      GST_DECPROXY_UNLOCK (decproxy);
      break;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      GST_DECPROXY_LOCK (decproxy);
      if (!decproxy->seen_buffer || decproxy->eaten_flush_start) {
        decproxy->eaten_flush_start = FALSE;
        decproxy->eaten_flush_stop = TRUE;
        if (!decproxy->decoder)
          front_bridge_set_blocked (decproxy, TRUE);
        /* FIXME: Probe back bridge and store incoming events and drop it immediately
         * in order to prevent deadlock problem while flushing before actual decoder
         * receives first buffer. The stored events would be forwarded as soon as
         * flushing is finished. */
        else if (decproxy->decoder && decproxy->back_downstream_probe_id == 0) {
          GstPad *sinkpad = gst_element_get_static_pad (decproxy->back, "sink");
          decproxy->back_downstream_probe_id =
              gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM,
              (GstPadProbeCallback) back_downstream_probe, decproxy, NULL);
          gst_object_unref (sinkpad);
        }

        back_bridge_add_probe (decproxy, FALSE);
        GST_INFO_OBJECT (decproxy, "Eaten FLUSH-STOP %" GST_PTR_FORMAT, event);
        gst_event_unref (event);
        forward = FALSE;
        ret = TRUE;
      }
      GST_DECPROXY_UNLOCK (decproxy);
      break;
    }
    default:
      break;
  }

  GST_DECPROXY_LOCK (decproxy);
  if (forward) {
    GST_INFO_OBJECT (decproxy, "Sending event: %" GST_PTR_FORMAT, event);
    GST_DECPROXY_UNLOCK (decproxy);
    ret = gst_pad_event_default (pad, parent, event);

    GST_DECPROXY_LOCK (decproxy);
    GST_INFO_OBJECT (decproxy, "Sent event: %" GST_PTR_FORMAT, event);
    if (!decproxy->seen_buffer) {
      g_mutex_lock (&decproxy->flush_lock);
      decproxy->sending_event = FALSE;
      g_mutex_unlock (&decproxy->flush_lock);
    }

    if (decproxy->waiting_flush) {
      GST_INFO_OBJECT (decproxy, "Wake Up FLUSH-START");
      g_mutex_lock (&decproxy->flush_lock);
      g_cond_signal (&decproxy->flush_cond);
      g_mutex_unlock (&decproxy->flush_lock);
    }
  }
  GST_DECPROXY_UNLOCK (decproxy);

  return ret;
}

static void
front_bridge_set_blocked (GstDecProxy * decproxy, gboolean blocked)
{
  if (blocked && decproxy->blocked_id == 0) {
    GstPad *front_srcpad = gst_element_get_static_pad (decproxy->front, "src");

    GST_DEBUG_OBJECT (decproxy,
        "Again Blocking pad(%s:%s)", GST_DEBUG_PAD_NAME (front_srcpad));
    decproxy->blocked_id =
        gst_pad_add_probe (front_srcpad,
        GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, analyze_new_caps, decproxy, NULL);

    gst_object_unref (front_srcpad);
  } else if (!blocked && decproxy->blocked_id) {
    GstPad *front_srcpad = gst_element_get_static_pad (decproxy->front, "src");

    GST_DEBUG_OBJECT (decproxy, "Un-blocking pad(%s:%s)",
        GST_DEBUG_PAD_NAME (front_srcpad));
    gst_pad_remove_probe (front_srcpad, decproxy->blocked_id);

    gst_object_unref (front_srcpad);
    decproxy->blocked_id = 0;
  }
}

static void
back_bridge_add_probe (GstDecProxy * decproxy, gboolean added)
{
  if (added && decproxy->pending_event_probe_id == 0) {
    GstPad *back_sinkpad = gst_element_get_static_pad (decproxy->back, "sink");
    GST_DEBUG_OBJECT (decproxy, "Adding DOWNSTREAM EVENT probe on pad(%s:%s)",
        GST_DEBUG_PAD_NAME (back_sinkpad));
    decproxy->pending_event_probe_id =
        gst_pad_add_probe (back_sinkpad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
        (GstPadProbeCallback) back_bridge_event_probe, decproxy, NULL);
    gst_object_unref (back_sinkpad);
  } else if (!added && decproxy->pending_event_probe_id) {
    GstPad *back_sinkpad = gst_element_get_static_pad (decproxy->back, "sink");
    GST_DEBUG_OBJECT (decproxy,
        "Removing DOWNSTREAM EVENT probe on pad(%s:%s)",
        GST_DEBUG_PAD_NAME (back_sinkpad));
    gst_pad_remove_probe (back_sinkpad, decproxy->pending_event_probe_id);
    decproxy->pending_event_probe_id = 0;
    gst_object_unref (back_sinkpad);
  }
}

static void
gst_decproxy_switch_decoder (GstDecProxy * decproxy, GstDecProxyState state)
{
  GstPad *front_sinkpad = NULL;
  GstPad *front_srcpad = NULL;
  gulong probe_front = 0;

  GST_DECPROXY_LOCK (decproxy);
  /* FIXME : To prevent event loss on front element */
  front_sinkpad = gst_element_get_static_pad (decproxy->front, "sink");
  probe_front =
      gst_pad_add_probe (front_sinkpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
      NULL, NULL, NULL);

  back_bridge_add_probe (decproxy, TRUE);
  front_bridge_set_blocked (decproxy, FALSE);

  /* decproxy is now on changing state, pending it */
  if (decproxy->current_decoder_state != decproxy->next_decoder_state) {
    GST_INFO_OBJECT (decproxy, "pending to change state to (%s)",
        ((state == GST_DECPROXY_STATE_PUPPET) ? "PUPPET" : "DECODER"));
    decproxy->pending_decoder_state = state;
    GST_DECPROXY_UNLOCK (decproxy);
    goto skip_switch;
  }

  /* skip to change equal state */
  if (decproxy->current_decoder_state == decproxy->next_decoder_state &&
      state == decproxy->next_decoder_state) {
    GST_INFO_OBJECT (decproxy, "skip to change equal state of decproxy. (%s)",
        ((state == GST_DECPROXY_STATE_PUPPET) ? "PUPPET" : "DECODER"));
    GST_DECPROXY_UNLOCK (decproxy);
    goto skip_switch;
  }

  decproxy->next_decoder_state = state;

  GST_DEBUG_OBJECT (decproxy,
      "Add IDLE probe for replacing decoder, creating (%s)",
      ((state == GST_DECPROXY_STATE_PUPPET) ? "PUPPET" : "DECODER"));

  front_srcpad = gst_element_get_static_pad (decproxy->front, "src");
  //gst_pad_add_probe (front_srcpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
  //    replace_decoder_stage1_cb, decproxy, NULL);
  /* Add IDLE probe to switch decoder */
  GST_DECPROXY_UNLOCK (decproxy);
  gst_pad_add_probe (front_srcpad, GST_PAD_PROBE_TYPE_IDLE,
      (GstPadProbeCallback) idle_reconfigure, decproxy, NULL);

  gst_object_unref (front_srcpad);

skip_switch:
  /* FIXME : To prevent event loss on front element */
  gst_pad_remove_probe (front_sinkpad, probe_front);
  back_bridge_add_probe (decproxy, FALSE);

  gst_object_unref (front_sinkpad);
}

static gboolean
gst_decproxy_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstDecProxy *decproxy = GST_DECPROXY (parent);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (decproxy, "event : %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_UPSTREAM:
    {
      const GstStructure *s;
      gboolean active = FALSE;
      GstDecProxyState state;

      s = gst_event_get_structure (event);

      /* dual-mono output mode set on audio decoder
       * L+R: 0 (L: main, R: sub)
       * L+L: 1
       * R+R: 2
       * MIX: 3 (L: main+sub, R: main+sub)
       */
      if (gst_event_has_name (event, "set-dual-mono")
          && gst_structure_get_uint (s, "dual-mono", &decproxy->dual_mono)) {
        GST_DEBUG_OBJECT (event, "Set dual-mono property");
        if (decproxy->decoder
            &&
            g_object_class_find_property (G_OBJECT_GET_CLASS
                (decproxy->decoder), "dual-mono")) {
          g_object_set (decproxy->decoder, "dual-mono", decproxy->dual_mono,
              NULL);
          GST_DEBUG_OBJECT (decproxy, "set dual-mono %d to decoder",
              decproxy->dual_mono);
        }
        break;
      }

      if (gst_event_has_name (event, "set-dts-seamless")) {
        GST_DEBUG_OBJECT (event, "Set dts-seamless property");
        decproxy->dts_seamless = TRUE;
        break;
      }

      if (!gst_event_has_name (event, "acquired-resource")) {
        GST_DEBUG_OBJECT (event, "Unknown custom event");
        ret = gst_pad_push_event (decproxy->internal_pad, event);
        break;
      }

      gst_structure_get_boolean (s, "active", &active);

      /* store resource info for set on decoder */
      if (!decproxy->resource_info)
        decproxy->resource_info = gst_structure_copy (s);
      else
        gst_structure_set (decproxy->resource_info, "active", G_TYPE_BOOLEAN,
            active, NULL);

      GST_SYS_INFO_OBJECT (decproxy, "got resource-info : %" GST_PTR_FORMAT,
          decproxy->resource_info);

      if (!g_strcmp0 (gst_structure_get_string (s, "core-type"),
              "external-decoder"))
        decproxy->use_external_dec = TRUE;

      GST_DECPROXY_LOCK (decproxy);

      /* pending to create fake decoder before doing analyze_new_caps */
      if (decproxy->current_decoder_state == GST_DECPROXY_STATE_UNKNOWN) {
        decproxy->pending_switch_decoder = TRUE;
        GST_DECPROXY_UNLOCK (decproxy);

        GST_INFO_OBJECT (decproxy, "pending switching decoder");
        break;
      }

      GST_DECPROXY_UNLOCK (decproxy);

      state = active ? GST_DECPROXY_STATE_DECODER : GST_DECPROXY_STATE_PUPPET;
      gst_decproxy_switch_decoder (decproxy, state);

      /* some of audio decoder should receive acqurired-resource event for
       * switching audio track.
       */
      ret = gst_pad_push_event (decproxy->internal_pad, event);
      break;
    }
    default:
      ret = gst_pad_push_event (decproxy->internal_pad, event);
      break;
  }

  return ret;
}

static GstElementFactory *
gst_decproxy_update_factories_list (GstDecProxy * decproxy)
{
  GstCaps *caps = NULL;
  GstPad *pad = NULL;
  GList *decoders = NULL;
  GList *filtered = NULL;
  GstElementFactory *factory = NULL;

  decoders =
      gst_element_factory_list_get_elements
      (GST_ELEMENT_FACTORY_TYPE_DECODER, GST_RANK_MARGINAL);

  if (decoders == NULL) {
    GST_WARNING_OBJECT (decproxy, "Cannot find any of decoders");
    goto fail;
  }

  GST_DEBUG_OBJECT (decproxy, "got factory list %p \n", decoders);
  gst_plugin_feature_list_debug (decoders);

  decoders = g_list_sort (decoders, gst_plugin_feature_rank_compare_func);

  pad = gst_element_get_static_pad (decproxy->front, "src");
  caps = gst_pad_get_current_caps (pad);

  if (caps == NULL)
    caps = gst_pad_query_caps (pad, NULL);

  if (!(filtered =
          gst_element_factory_list_filter (decoders, caps, GST_PAD_SINK,
              FALSE))) {
    gchar *tmp = gst_caps_to_string (caps);
    GST_WARNING_OBJECT (decproxy, "Cannot find any decoder for caps %s", tmp);
    g_free (tmp);

    goto not_found;
  }

  GST_DEBUG_OBJECT (filtered, "got filtered list %p", filtered);

  gst_plugin_feature_list_debug (filtered);

  factory = GST_ELEMENT_FACTORY_CAST (g_list_nth_data (filtered, 0));

  /* Note that decproxy can not be a child element in decproxy bin */
  if (factory == gst_element_get_factory (GST_ELEMENT_CAST (decproxy))) {
    if (g_list_length (filtered) > 1)
      factory = GST_ELEMENT_FACTORY_CAST (g_list_nth_data (filtered, 1));
    else
      factory = NULL;
  }

  if (factory == NULL)
    GST_DEBUG_OBJECT (filtered, "factory is null");

not_found:
  gst_caps_unref (caps);
  gst_object_unref (pad);

fail:
  if (decoders)
    gst_plugin_feature_list_free (decoders);
  if (filtered)
    gst_plugin_feature_list_free (filtered);

  return factory;
}

static GstElement *
find_and_create_decoder (GstDecProxy * decproxy)
{
  GstElement *decoder = NULL;
  GstElementFactory *factory;

  if (decproxy->next_decoder_state == GST_DECPROXY_STATE_PUPPET) {
    gchar *elem_name = NULL;

    if (decproxy->stream_type == GST_COOL_STREAM_TYPE_AUDIO)
      elem_name = "fakeadec";
    else if (decproxy->stream_type == GST_COOL_STREAM_TYPE_VIDEO)
      elem_name = "fakevdec";

    GST_DEBUG_OBJECT (decproxy, "%s will be deployed", elem_name);
    decoder = gst_element_factory_make (elem_name, NULL);

    return decoder;
  }

  if (decproxy->stream_type == GST_COOL_STREAM_TYPE_VIDEO
      && decproxy->use_external_dec) {
    gchar *fac_name = gst_cool_get_external_video_decoder ();
    if (!fac_name) {
      GST_WARNING_OBJECT (decproxy, "external decoder is not exist");
      decproxy->use_external_dec = FALSE;
      goto find_decoder;
    }

    GST_DEBUG_OBJECT (decproxy, "external decoder name : %s", fac_name);
    if (!(decoder = gst_element_factory_make (fac_name, NULL))) {
      GST_WARNING_OBJECT (decproxy,
          "Could not create external(%s) decoder element", fac_name);
      g_free (fac_name);
      decproxy->use_external_dec = FALSE;
      goto find_decoder;
    }
    g_free (fac_name);
    goto configure_decoder;
  }

find_decoder:
  GST_DEBUG_OBJECT (decproxy, "Actual Decoder will be deployed");
  if (!(factory = gst_decproxy_update_factories_list (decproxy))) {
    GST_WARNING_OBJECT (decproxy, "Could not find actual decoder element");
    return NULL;
  }

  if (!(decoder = gst_element_factory_create (factory, NULL))) {
    GST_WARNING_OBJECT (decproxy, "Could not create actual decoder element");
    return NULL;
  }

configure_decoder:
  // FIXME: Do not access configuration directly
  if (g_object_class_find_property (G_OBJECT_GET_CLASS (decoder),
          "input-buffers")) {
    GError *err = NULL;
    GKeyFile *config = gst_cool_get_configuration ();

    gint in_size = g_key_file_get_integer (config, "decode", "in_size", &err);

    if (err) {
      GST_WARNING_OBJECT (decproxy, "Unable to read in_size: %s", err->message);
      g_error_free (err);
      err = NULL;
    } else {
      g_object_set (decoder, "input-buffers", in_size, NULL);
      GST_DEBUG_OBJECT (decoder, "decoder in-buffers changed: %d", in_size);

      // TODO: How about output-buffers?
    }
  }

  if (decproxy->resource_info
      && g_object_class_find_property (G_OBJECT_GET_CLASS (decoder),
          "resource-info")) {
    g_object_set (decoder, "resource-info", decproxy->resource_info, NULL);
    GST_DEBUG_OBJECT (decoder,
        "set resource info to decoder, %" GST_PTR_FORMAT,
        decproxy->resource_info);
  }

  if (decproxy->dts_seamless
      && g_object_class_find_property (G_OBJECT_GET_CLASS (decoder),
          "dts-seamless")) {
    g_object_set (decoder, "dts-seamless", decproxy->dts_seamless, NULL);
    GST_DEBUG_OBJECT (decoder, "set dts-seamless TRUE to decoder");
  }

  return decoder;
}

#if 0
static GstPadProbeReturn
replace_decoder_stage2_cb (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstElement *decoder;

  GstDecProxy *decproxy = GST_DECPROXY (user_data);

#ifdef REPLACEMENT_EVENT
  if (!(GST_EVENT_TYPE (GST_EVENT_CAST (info->data)) ==
          GST_EVENT_CUSTOM_DOWNSTREAM_OOB
          && gst_event_has_name (GST_EVENT_CAST (info->data),
              "request-dispose"))) {
    return GST_PAD_PROBE_PASS;
  }
#else
  if (GST_EVENT_TYPE (GST_PAD_PROBE_INFO_DATA (info)) != GST_EVENT_EOS)
    return GST_PAD_PROBE_PASS;
#endif

  GST_DEBUG_OBJECT (decproxy, "pad(%s:%s) blocked", GST_DEBUG_PAD_NAME (pad));

  gst_pad_remove_probe (pad, GST_PAD_PROBE_INFO_ID (info));

  if (decproxy->next_decoder_state == GST_DECPROXY_STATE_PUPPET)
    decoder = decproxy->decoder;
  else
    decoder = decproxy->puppet;

  GST_DEBUG_OBJECT (decproxy, "removing %" GST_PTR_FORMAT, decoder);
  gst_element_set_state (decoder, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (user_data), decoder);
  if (decproxy->next_decoder_state == GST_DECPROXY_STATE_PUPPET)
    decproxy->decoder = NULL;
  else
    decproxy->puppet = NULL;

  if (!(decoder = find_and_create_decoder (decproxy))) {
    GST_INFO_OBJECT (decproxy, "Failed to find proper decoder");
    if (decproxy->stream_type == GST_COOL_STREAM_TYPE_AUDIO) {
      decoder = gst_element_factory_make ("fakeadec", NULL);
      g_object_set (decoder, "active-mode", TRUE, NULL);
      GST_ELEMENT_WARNING (GST_ELEMENT_CAST (decproxy), STREAM, CODEC_NOT_FOUND,
          ("This audio is not supported by decoder"),
          ("This audio is not supported by decoder"));
    } else {
      GST_ELEMENT_ERROR (decproxy, CORE, MISSING_PLUGIN, (NULL),
          ("Cannot find proper video decoder plugins"));
      return GST_PAD_PROBE_REMOVE;
    }
  }

  gst_bin_add (GST_BIN_CAST (decproxy), decoder);
  if (!gst_element_link_many (decproxy->front, decoder, decproxy->back, NULL)) {
    GST_ERROR_OBJECT (decproxy, "Cannot make link for all internal elements");
  }

  if (!gst_element_sync_state_with_parent (decoder)) {
    GST_WARNING_OBJECT (decproxy, "Couldn't sync state with parent");
  }

  if (decproxy->next_decoder_state == GST_DECPROXY_STATE_PUPPET)
    decproxy->puppet = decoder;
  else
    decproxy->decoder = decoder;

  decproxy->current_decoder_state = decproxy->next_decoder_state;

  /* ???: is it okay to recursive function call in probe callback? */
  if (decproxy->pending_decoder_state != GST_DECPROXY_STATE_UNKNOWN) {
    GstDecProxyState state = decproxy->pending_decoder_state;

    decproxy->pending_decoder_state = GST_DECPROXY_STATE_UNKNOWN;
    GST_DEBUG_OBJECT (decproxy, "set pending state, (%s)",
        ((state == GST_DECPROXY_STATE_PUPPET) ? "PUPPET" : "DECODER"));

    gst_decproxy_switch_decoder (decproxy, state);
  }

  return GST_PAD_PROBE_DROP;
}
#endif

static void
reconfigure_decoder (GstDecProxy * decproxy, GstElement * decoder)
{
  gchar *dec_name = NULL;

  GST_DEBUG_OBJECT (decproxy, "Unlinking front, decoder and back");
  gst_element_unlink_many (decproxy->front, decoder, decproxy->back, NULL);

  back_bridge_add_probe (decproxy, FALSE);

  dec_name = gst_element_get_name (decoder);
  GST_DEBUG_OBJECT (decproxy, "Removing old decoder(%s)", dec_name);
  g_free (dec_name);
  gst_element_set_locked_state (decoder, TRUE);
  gst_element_set_state (decoder, GST_STATE_NULL);

  gst_bin_remove ((GstBin *) decproxy, decoder);

  GST_DEBUG_OBJECT (decproxy, "Removed old decoder");

  if (decproxy->next_decoder_state == GST_DECPROXY_STATE_PUPPET)
    decproxy->decoder = NULL;
  else
    decproxy->puppet = NULL;

  if (!(decoder = find_and_create_decoder (decproxy))) {
    GST_INFO_OBJECT (decproxy, "Failed to find proper decoder");
    if (decproxy->stream_type == GST_COOL_STREAM_TYPE_AUDIO) {
      decoder = gst_element_factory_make ("fakeadec", NULL);
      g_object_set (decoder, "active-mode", TRUE, NULL);
      GST_DECPROXY_UNLOCK (decproxy);
      GST_ELEMENT_WARNING (GST_ELEMENT_CAST (decproxy), STREAM, CODEC_NOT_FOUND,
          ("This audio is not supported by decoder"),
          ("This audio is not supported by decoder"));
      GST_DECPROXY_LOCK (decproxy);
    } else {
      GST_DECPROXY_UNLOCK (decproxy);
      GST_ELEMENT_ERROR (decproxy, CORE, MISSING_PLUGIN, (NULL),
          ("Cannot find proper video decoder plugins"));
      GST_DECPROXY_LOCK (decproxy);
      return;
    }
  }

  dec_name = gst_element_get_name (decoder);
  GST_DEBUG_OBJECT (decproxy, "Found Decoder(%s)", dec_name);
  if (!gst_bin_add ((GstBin *) decproxy, decoder)) {
    GST_ERROR_OBJECT (decproxy, "Could not add decoder(%s)", dec_name);
    g_free (dec_name);
    return;
  }

  GST_DEBUG_OBJECT (decproxy, "Added Decoder(%s)", dec_name);

  gst_element_sync_state_with_parent (decoder);
  GST_DEBUG_OBJECT (decproxy, "Sync decoder(%s) state with parent", dec_name);

  if (decproxy->next_decoder_state == GST_DECPROXY_STATE_DECODER
      && decproxy->has_secure_area) {
    if (decproxy->use_external_dec) {
      GST_DEBUG_OBJECT (decproxy, "make dtcp2usb in decproxy");
      g_free (decproxy->has_secure_area);
      decproxy->has_secure_area = g_strdup ("dtcp2usb");
    }

    decproxy->secure =
        gst_element_factory_make (decproxy->has_secure_area, NULL);
    gst_bin_add ((GstBin *) decproxy, decproxy->secure);
    gst_element_sync_state_with_parent (decproxy->secure);

    if (!g_strcmp0 (decproxy->has_secure_area, "svp")) {
      guint vdec_handle = 0;
      GstPad *pad = NULL;
      GstCaps *caps = NULL;
      pad = gst_element_get_static_pad (decproxy->front, "src");
      caps = gst_pad_get_current_caps (pad);
      if (caps == NULL)
        caps = gst_pad_query_caps (pad, NULL);
      g_signal_emit_by_name (decoder, "acquire-vdec-handle", TRUE, caps,
          &vdec_handle);
      gst_object_unref (pad);
      gst_caps_unref (caps);

      if (vdec_handle) {
        GST_DEBUG_OBJECT (decproxy->secure, "deliver vdec-handle");
        g_object_set (decproxy->secure, "vdec-handle", vdec_handle, NULL);
      } else
        GST_WARNING_OBJECT (decoder, "Failed to get vdec-handle");
    }

    GST_DEBUG_OBJECT (decproxy, "Link %s and decoder",
        decproxy->has_secure_area);
    gst_element_link_many (decproxy->front, decproxy->secure, decoder,
        decproxy->back, NULL);
  } else {
    if (!gst_element_link_many (decproxy->front, decoder, decproxy->back, NULL)) {
      GST_ERROR_OBJECT (decproxy, "Cannot make link for all internal elements");
      g_free (dec_name);
      return;
    }
  }

  GST_DEBUG_OBJECT (decproxy, "Link to success with decoder(%s)", dec_name);

  if (decproxy->next_decoder_state == GST_DECPROXY_STATE_PUPPET)
    decproxy->puppet = decoder;
  else
    decproxy->decoder = decoder;

  if (decproxy->decoder && decproxy->dec_downstream_probe_id == 0) {
    GstPad *sinkpad = gst_element_get_static_pad (decproxy->decoder, "sink");
    decproxy->dec_downstream_probe_id =
        gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM,
        (GstPadProbeCallback) decoder_downstream_probe, decproxy, NULL);
    gst_object_unref (sinkpad);
  }

  decproxy->current_decoder_state = decproxy->next_decoder_state;

  GST_SYS_INFO_OBJECT (decproxy, "Success to switch decoder(%s)", dec_name);
  g_free (dec_name);

  front_bridge_set_blocked (decproxy, FALSE);
}

static GstPadProbeReturn
idle_reconfigure (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstDecProxy *decproxy = GST_DECPROXY (user_data);
  GstElement *decoder = NULL;

  GST_DEBUG_OBJECT (decproxy, "pad(%s:%s) in IDLE status",
      GST_DEBUG_PAD_NAME (pad));

  GST_DECPROXY_LOCK (decproxy);
  /* skip to change equal state */
  if (decproxy->current_decoder_state == decproxy->next_decoder_state) {
    GST_DEBUG_OBJECT (decproxy, "skip to change equal state of decproxy. (%s)",
        ((decproxy->current_decoder_state ==
                GST_DECPROXY_STATE_PUPPET) ? "PUPPET" : "DECODER"));
    GST_DECPROXY_UNLOCK (decproxy);
    return GST_PAD_PROBE_REMOVE;
  }

  decoder =
      (decproxy->next_decoder_state ==
      GST_DECPROXY_STATE_PUPPET) ? decproxy->decoder : decproxy->puppet;
  if (!decoder) {
    GST_DEBUG_OBJECT (decproxy, "current decoder(%s) is NULL",
        ((decproxy->current_decoder_state ==
                GST_DECPROXY_STATE_PUPPET) ? "PUPPET" : "DECODER"));
    GST_DECPROXY_UNLOCK (decproxy);
    return GST_PAD_PROBE_PASS;
  }

  g_mutex_lock (&decproxy->flush_lock);
  decproxy->changing_decoder = TRUE;
  g_mutex_unlock (&decproxy->flush_lock);

  reconfigure_decoder (decproxy, decoder);

  g_mutex_lock (&decproxy->flush_lock);
  decproxy->changing_decoder = FALSE;
  GST_INFO_OBJECT (decproxy, "Wake Up FLUSH-START");
  g_cond_signal (&decproxy->flush_cond);
  g_mutex_unlock (&decproxy->flush_lock);
  GST_DECPROXY_UNLOCK (decproxy);

  return GST_PAD_PROBE_REMOVE;
}

#if 0
static GstPadProbeReturn
replace_decoder_stage1_cb (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstPad *target_pad;
  GstElement *decoder;
  GstDecProxy *decproxy = GST_DECPROXY (user_data);

  GST_DEBUG_OBJECT (decproxy, "pad(%s:%s) blocked", GST_DEBUG_PAD_NAME (pad));

  if (GST_IS_BUFFER (info->data))
    GST_LOG_OBJECT (pad, "got buffer : %" GST_PTR_FORMAT,
        GST_BUFFER_CAST (info->data));
  else if (GST_IS_EVENT (info->data))
    GST_LOG_OBJECT (pad, "got event : %" GST_PTR_FORMAT,
        GST_EVENT_CAST (info->data));

  gst_pad_remove_probe (pad, GST_PAD_PROBE_INFO_ID (info));

  if (decproxy->next_decoder_state == GST_DECPROXY_STATE_PUPPET)
    decoder = decproxy->decoder;
  else
    decoder = decproxy->puppet;

  target_pad = gst_element_get_static_pad (decoder, "src");
  GST_DEBUG_OBJECT (decproxy, "Registered pad(%s:%s) block to remove decoder",
      GST_DEBUG_PAD_NAME (target_pad));

  gst_pad_add_probe (target_pad,
      GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      replace_decoder_stage2_cb, user_data, NULL);
  gst_object_unref (target_pad);

  target_pad = gst_element_get_static_pad (decoder, "sink");

  GST_DEBUG_OBJECT (decproxy,
      "Sent EOS to pad(%s:%s) for removing decoder element",
      GST_DEBUG_PAD_NAME (target_pad));

#ifdef REPLACEMENT_EVENT
  gst_pad_send_event (target_pad,
      gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM_OOB,
          gst_structure_new_empty ("request-dispose")));
#else
  gst_pad_send_event (target_pad, gst_event_new_eos ());
#endif

  gst_object_unref (target_pad);

  return GST_PAD_PROBE_OK;
}
#endif

static GstPadProbeReturn
dec_buffer_ts_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);
  GstDecProxy *decproxy = GST_DECPROXY (user_data);

  if (decproxy->stream_type == GST_COOL_STREAM_TYPE_VIDEO) {
    decproxy->vdec_buf_ts = GST_BUFFER_PTS (buffer);
  } else if (decproxy->stream_type == GST_COOL_STREAM_TYPE_AUDIO) {
    decproxy->adec_buf_ts = GST_BUFFER_PTS (buffer);
  }

  return GST_PAD_PROBE_OK;

}

/* FIXME, Probe DOWNSTREAM EVENTS/BUFFER on decoder's sinkpad */
static GstPadProbeReturn
decoder_downstream_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstDecProxy *decproxy = GST_DECPROXY (user_data);
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;
  gboolean remove_probe = FALSE;

  if (GST_IS_EVENT (info->data)) {
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);

    if (GST_EVENT_TYPE (event) != GST_EVENT_TAG)
      GST_SYS_DEBUG_OBJECT (decproxy,
          "Got a event: %" GST_PTR_FORMAT " on pad(%s:%s)", event,
          GST_DEBUG_PAD_NAME (pad));

    if (GST_EVENT_TYPE (event) == GST_EVENT_GAP)
      remove_probe = TRUE;

    if (decproxy->pending_events) {
      GList **pending_events, *l;

      pending_events = &decproxy->pending_events;

      for (l = *pending_events; l;) {
        GstEvent *pev = GST_EVENT (l->data);
        GList *tmp;

        if (GST_EVENT_TYPE (pev) < GST_EVENT_TYPE (event)) {
          GstEvent *dec_ev =
              gst_pad_get_sticky_event (pad, GST_EVENT_TYPE (pev), 0);
          if (!dec_ev) {
            GST_SYS_INFO_OBJECT (decproxy,
                "Sending Pending event: %" GST_PTR_FORMAT, pev);
            gst_pad_send_event (pad, pev);
          } else {
            gst_event_unref (dec_ev);
            gst_event_unref (pev);
          }

          tmp = l;
          l = l->next;
          *pending_events = g_list_delete_link (*pending_events, tmp);
        } else {
          l = l->next;
        }
      }
    }
  } else if (GST_IS_BUFFER (info->data)) {
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);
    GST_SYS_DEBUG_OBJECT (decproxy,
        "Got a buffer: %" GST_PTR_FORMAT " on pad(%s:%s)", buffer,
        GST_DEBUG_PAD_NAME (pad));
    remove_probe = TRUE;
  }

  if (remove_probe) {
    decproxy->dec_downstream_probe_id = 0;
    ret = GST_PAD_PROBE_REMOVE;
  }

  return ret;
}

static GstPadProbeReturn
analyze_new_caps (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstPad *target_pad = NULL;
  GstCaps *caps = NULL;
  GstStructure *s = NULL;
  GstEvent *stream_start;

  GstDecProxy *decproxy = GST_DECPROXY (user_data);

  GST_DEBUG_OBJECT (decproxy, "got probe %" GST_PTR_FORMAT " on pad(%s:%s)",
      GST_PAD_PROBE_INFO_EVENT (info), GST_DEBUG_PAD_NAME (pad));

  GST_DECPROXY_LOCK (decproxy);

  if (decproxy->current_decoder_state != GST_DECPROXY_STATE_UNKNOWN
      && decproxy->eaten_flush_stop) {
    GST_DEBUG_OBJECT (decproxy, "Blocked again by FLUSH");
    decproxy->eaten_flush_stop = TRUE;
    GST_DECPROXY_UNLOCK (decproxy);
    return GST_PAD_PROBE_OK;
  }

  if (GST_EVENT_TYPE (GST_PAD_PROBE_INFO_EVENT (info)) ==
      GST_EVENT_STREAM_START) {
    if (decproxy->puppet != NULL) {
      GST_DEBUG_OBJECT (decproxy,
          "Dropping duplicated stream-start event on pad(%s:%s)",
          GST_DEBUG_PAD_NAME (pad));
      GST_DECPROXY_UNLOCK (decproxy);
      return GST_PAD_PROBE_DROP;
    } else {
      GST_DEBUG_OBJECT (decproxy, "Passing stream-start event on pad(%s:%s)",
          GST_DEBUG_PAD_NAME (pad));
      GST_DECPROXY_UNLOCK (decproxy);
      return GST_PAD_PROBE_PASS;
    }
  }

  if (GST_IS_BUFFER (GST_PAD_PROBE_INFO_DATA (info))) {
    GST_DEBUG_OBJECT (decproxy, "Block buffer on STATE_PUPPET");
    GST_DECPROXY_UNLOCK (decproxy);
    return GST_PAD_PROBE_OK;
  }

  GST_DEBUG_OBJECT (decproxy,
      "Trying to make pipeline for decproxy on pad(%s:%s)",
      GST_DEBUG_PAD_NAME (pad));

  caps = gst_pad_get_current_caps (pad);

  if (caps == NULL)
    caps = gst_pad_query_caps (pad, NULL);

  s = gst_caps_get_structure (caps, 0);
  decproxy->stream_type = gst_cool_find_type (gst_structure_get_name (s));
  gst_caps_unref (caps);

  GST_DEBUG_OBJECT (decproxy, "current state %d",
      decproxy->current_decoder_state);

  /* If we already have a fake decoder, this means the incoming caps
   * is a duplicate */
  if (decproxy->puppet != NULL) {
    GST_DEBUG_OBJECT (decproxy, "Dropping duplicate caps event");
    GST_DECPROXY_UNLOCK (decproxy);
    return GST_PAD_PROBE_DROP;
  }

  if (decproxy->current_decoder_state != GST_DECPROXY_STATE_UNKNOWN) {
    GST_DEBUG_OBJECT (decproxy,
        "waiting for acquired resource event to unblock");
    GST_DECPROXY_UNLOCK (decproxy);
    return GST_PAD_PROBE_OK;
  }

  decproxy->next_decoder_state = decproxy->current_decoder_state =
      GST_DECPROXY_STATE_PUPPET;

  if (!(decproxy->puppet = find_and_create_decoder (decproxy))) {
    GST_ERROR_OBJECT (decproxy, "Failed to find proper decoder");
    GST_DECPROXY_UNLOCK (decproxy);
    return GST_PAD_PROBE_REMOVE;
  }

  if (!gst_bin_add (GST_BIN (decproxy), decproxy->puppet)) {
    g_warning ("Could not add puppet element, puppet will not work");
    gst_object_unref (decproxy->puppet);
    decproxy->puppet = NULL;
  }

  GST_INFO_OBJECT (decproxy->puppet, "Trying to connect with fake decoder");
  if (!gst_element_link_many (decproxy->front, decproxy->puppet, decproxy->back,
          NULL)) {
    GST_ERROR_OBJECT (decproxy, "Cannot make link for all internal elements");
  }

  if (!gst_element_sync_state_with_parent (decproxy->puppet)) {
    GST_WARNING_OBJECT (decproxy, "Couldn't sync state with parent");
  }

  if (decproxy->propagate_sticky_event) {
    target_pad =
        gst_element_get_static_pad (GST_ELEMENT_CAST (decproxy), "src");
    /* propagate stream-start event downstream to guarantee order of track */
    /* We re-use the *actual* stream-start event because it might contain extra
     * information (such as group-id) which downstream elements (such as
     * streamsynchronizer) might need for proper behaviour */
    GST_DEBUG_OBJECT (pad, "propagating stream-start event");
    stream_start = gst_pad_get_sticky_event (pad, GST_EVENT_STREAM_START, 0);
    GST_DECPROXY_UNLOCK (decproxy);
    if (stream_start)
      gst_pad_push_event (target_pad, stream_start);
    GST_DECPROXY_LOCK (decproxy);

    /* set up the outcaps in order to finish auto-plugging */
    if (decproxy->stream_type == GST_COOL_STREAM_TYPE_VIDEO)
      caps = gst_caps_from_string ("video/x-raw");
    else if (decproxy->stream_type == GST_COOL_STREAM_TYPE_AUDIO)
      caps = gst_caps_from_string ("audio/x-media");

    gst_pad_set_caps (target_pad, caps);

    gst_object_unref (target_pad);
    gst_caps_unref (caps);
  }

  /* received acquired-resource event before called analyze_new_caps */
  if (decproxy->pending_switch_decoder) {
    gboolean active = FALSE;
    GstDecProxyState state;

    decproxy->pending_switch_decoder = FALSE;

    gst_structure_get_boolean (decproxy->resource_info, "active", &active);
    state = active ? GST_DECPROXY_STATE_DECODER : GST_DECPROXY_STATE_PUPPET;

    GST_DECPROXY_UNLOCK (decproxy);
    gst_decproxy_switch_decoder (decproxy, state);
    GST_DECPROXY_LOCK (decproxy);
  }

  GST_DECPROXY_UNLOCK (decproxy);

  /* FIXME: Post a message to player that it is appropriate time to allocate
   * H/W resource for custom pipeline */
  if (!gst_element_post_message (GST_ELEMENT_CAST (decproxy),
          gst_message_new_application (GST_OBJECT_CAST (decproxy),
              gst_structure_new_empty ("configured-decoder"))))
    GST_ERROR_OBJECT (decproxy, "ERROR: Send configured-decoder message");

  GST_INFO_OBJECT (decproxy, "Configured Fake Decoder and Blocked");

  return GST_PAD_PROBE_OK;
}

/* Check "decode only and droppable" flag on the first incoming buffer
 * and drop it if needed */
static GstPadProbeReturn
droppable_buffer_drop_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstDecProxy *decproxy = GST_DECPROXY (user_data);
  GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER (info);
  GstPadProbeReturn ret = GST_PAD_PROBE_REMOVE;

  GST_DEBUG_OBJECT (decproxy,
      "pad(%s:%s) blocked to check droppable", GST_DEBUG_PAD_NAME (pad));

  if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DECODE_ONLY) &&
      GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DROPPABLE)) {
    GST_DEBUG_OBJECT (pad, "Drop droppable buffer");
    ret = GST_PAD_PROBE_DROP;
    gst_pad_remove_probe (pad, GST_PAD_PROBE_INFO_ID (info));
  }

  return ret;
}
