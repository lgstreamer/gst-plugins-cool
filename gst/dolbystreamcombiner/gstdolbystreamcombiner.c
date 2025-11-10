/* GStreamer Dolby Stream Combiner
 * Copyright (C) 2015 LG Electronics, Inc.
 * Author : Seungha Yang <sh.yang@lge.com>
 *          Kyungyong Kim <kyungyong.kim@lge.com>
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

#include <string.h>
#include "gstdolbystreamcombiner.h"

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265; video/x-h264"));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/x-h265, "
        "need-compositor = (boolean) true;"
        "video/x-h264, " "need-compositor = (boolean) true")
    );

GST_DEBUG_CATEGORY_STATIC (gst_dolby_stream_combiner_debug);

#define GST_CAT_DEFAULT gst_dolby_stream_combiner_debug
#define gst_dolby_stream_combiner_parent_class parent_class

#define GST_DOLBY_STREAM_COMBINER_GET_LOCK(stream_combiner) \
	(&GST_DOLBY_STREAM_COMBINER(stream_combiner)->lock)
#define GST_DOLBY_STREAM_COMBINER_LOCK(stream_combiner) \
	(g_mutex_lock(GST_DOLBY_STREAM_COMBINER_GET_LOCK (stream_combiner)))
#define GST_DOLBY_STREAM_COMBINER_UNLOCK(stream_combiner) \
	(g_mutex_unlock(GST_DOLBY_STREAM_COMBINER_GET_LOCK (stream_combiner)))

#define GST_DOLBY_STREAM_COMBINER_MAX_STREAMS	2
/*
 * EL PTS will be inserted at the end of META AU
 * Nal type (2 byte) + [0xff (1byte) + pts (2byte)] + [0xff + pts] + [0xff + pts] + [0xff + pts]
 */
#define GST_DOLBY_STREAM_COMBINER_EL_PTS_SIZE (2 + 4 + sizeof(GstClockTime))

G_DEFINE_TYPE (GstDolbyStreamCombiner, gst_dolby_stream_combiner,
    GST_TYPE_ELEMENT);

static void gst_dolby_stream_combiner_finalize (GObject * object);
static GstPad *gst_dolby_stream_combiner_request_new_pad (GstElement *
    element, GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_dolby_stream_combiner_release_pad (GstElement * element,
    GstPad * pad);
static void gst_dolby_stream_combiner_reset (GstDolbyStreamCombiner *
    stream_combiner, gboolean hard);
static GstStateChangeReturn gst_dolby_stream_combiner_change_state (GstElement *
    element, GstStateChange transition);
static GstFlowReturn gst_dolby_stream_combiner_collected (GstCollectPads * pads,
    GstDolbyStreamCombiner * stream_combiner);
static gboolean gst_dolby_stream_combiner_sink_event (GstCollectPads * pads,
    GstCollectData * c_data, GstEvent * event,
    GstDolbyStreamCombiner * stream_combiner);
static gboolean gst_dolby_stream_combiner_src_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static GstFlowReturn gst_dolby_stream_combiner_prepare (GstDolbyStreamCombiner *
    stream_combiner);
static GstFlowReturn
gst_dolby_stream_combiner_prepare_pad (GstDolbyStreamCombiner * stream_combiner,
    GstDolbyStreamCombinerPad * data, GstPad * pad);
static GstFlowReturn
gst_dolby_stream_combiner_prepare_srcpad (GstDolbyStreamCombiner *
    stream_combiner);
static GstDolbyStreamCombinerPad
    * gst_dolby_stream_combiner_choose_best_pad (GstDolbyStreamCombiner *
    stream_combiner);
static GstFlowReturn
gst_dolby_stream_combiner_process_buffer (GstDolbyStreamCombiner *
    stream_combiner, GstDolbyStreamCombinerPad * combiner_pad, GstBuffer * buf);
static void gst_dolby_stream_combiner_adapter_init (GstDolbyStreamCombiner *
    stream_combiner);
static gboolean gst_dolby_stream_combiner_adapter_push (GstDolbyStreamCombiner *
    stream_combiner, GstBuffer * buf, DolbyStreamType type);
static GstBuffer *gst_dolby_stream_combiner_adapter_pop (GstDolbyStreamCombiner
    * stream_combiner);


static void
gst_dolby_stream_combiner_class_init (GstDolbyStreamCombinerClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;

  gobject_klass->finalize = gst_dolby_stream_combiner_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_dolby_stream_combiner_debug,
      "dolbystreamcombiner", 0, "Dolby Stream Combiner");

  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&sink_template));

  gstelement_klass->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_dolby_stream_combiner_request_new_pad);
  gstelement_klass->release_pad =
      GST_DEBUG_FUNCPTR (gst_dolby_stream_combiner_release_pad);
  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_dolby_stream_combiner_change_state);

  gst_element_class_set_static_metadata (gstelement_klass,
      "dolbystreamcombiner",
      "Codec/Parser/Converter/Video",
      "Combines dual layer encoded Dolby HDR stream into single stream",
      "Seungha Yang <sh.yang@lge.com>,  Kyungyong Kim <kyungyong.kim@lge.com>");
}

static void
gst_dolby_stream_combiner_finalize (GObject * object)
{
  GstDolbyStreamCombiner *stream_combiner = (GstDolbyStreamCombiner *) object;

  if (stream_combiner->payload) {
    if (stream_combiner->payload->adapter != NULL) {
      gst_adapter_clear (stream_combiner->payload->adapter);
      g_object_unref (stream_combiner->payload->adapter);
      stream_combiner->payload->adapter = NULL;
    }
    g_free (stream_combiner->payload);
    stream_combiner->payload = NULL;
  }

  if (stream_combiner->seg_last) {
    gst_segment_free (stream_combiner->seg_last);
    stream_combiner->seg_last = NULL;
  }

  if (stream_combiner->pending_tags) {
    gst_tag_list_unref (stream_combiner->pending_tags);
    stream_combiner->pending_tags = NULL;
  }

  if (stream_combiner->collect) {
    g_object_unref (stream_combiner->collect);
    stream_combiner->collect = NULL;
  }

  /*FIXME in order to prevent deadlock */
  g_cond_signal (&stream_combiner->cond);

  g_mutex_clear (&stream_combiner->lock);
  g_cond_clear (&stream_combiner->cond);

  G_OBJECT_CLASS (gst_dolby_stream_combiner_parent_class)->finalize (object);
}

static void
gst_dolby_stream_combiner_init (GstDolbyStreamCombiner * stream_combiner)
{
  stream_combiner->srcpad =
      gst_pad_new_from_static_template (&src_template, "src");

  stream_combiner->collect = gst_collect_pads_new ();
  stream_combiner->seg_last = gst_segment_new ();
  stream_combiner->pending_tags = NULL;
  stream_combiner->payload = NULL;

  gst_collect_pads_set_event_function (stream_combiner->collect,
      (GstCollectPadsEventFunction)
      GST_DEBUG_FUNCPTR (gst_dolby_stream_combiner_sink_event),
      stream_combiner);
  gst_pad_set_event_function (stream_combiner->srcpad,
      GST_DEBUG_FUNCPTR (gst_dolby_stream_combiner_src_event));

  gst_element_add_pad (GST_ELEMENT (stream_combiner), stream_combiner->srcpad);


  gst_collect_pads_set_function (stream_combiner->collect,
      (GstCollectPadsFunction)
      GST_DEBUG_FUNCPTR (gst_dolby_stream_combiner_collected), stream_combiner);

  g_mutex_init (&stream_combiner->lock);
  g_cond_init (&stream_combiner->cond);
  gst_dolby_stream_combiner_reset (stream_combiner, TRUE);

}

static void
gst_dolby_stream_combiner_reset (GstDolbyStreamCombiner * stream_combiner,
    gboolean hard)
{
  if (hard) {
    gst_dolby_stream_combiner_adapter_init (stream_combiner);
    stream_combiner->n_streams = 0;
    stream_combiner->state = GST_DOLBY_SC_STATE_NONE;
  }

  stream_combiner->waiting_flush_stop = FALSE;
  gst_segment_init (stream_combiner->seg_last, GST_FORMAT_TIME);
  if (stream_combiner->pending_tags) {
    gst_tag_list_unref (stream_combiner->pending_tags);
    stream_combiner->pending_tags = NULL;
  }

}

static void
gst_dolby_stream_combiner_adapter_init (GstDolbyStreamCombiner *
    stream_combiner)
{
  if (stream_combiner->payload) {
    if (stream_combiner->payload->adapter) {
      gst_adapter_clear (stream_combiner->payload->adapter);
    } else {
      stream_combiner->payload->adapter = gst_adapter_new ();
    }
  } else {
    stream_combiner->payload = g_new0 (GstDolbyStreamCombinerAdapter, 1);
    stream_combiner->payload->adapter = gst_adapter_new ();
  }

  stream_combiner->payload->pts_bl = GST_CLOCK_TIME_NONE;
  stream_combiner->payload->dts_bl = GST_CLOCK_TIME_NONE;
  stream_combiner->payload->duration_bl = GST_CLOCK_TIME_NONE;

  stream_combiner->payload->pts_el = GST_CLOCK_TIME_NONE;
  stream_combiner->payload->dts_el = GST_CLOCK_TIME_NONE;
  stream_combiner->payload->duration_el = GST_CLOCK_TIME_NONE;

  stream_combiner->payload->need_flag_discont = FALSE;
  stream_combiner->payload->need_flag_delta_unit = FALSE;

}

static gboolean
gst_dolby_stream_combiner_adapter_push (GstDolbyStreamCombiner *
    stream_combiner, GstBuffer * buf, DolbyStreamType type)
{
  GstDolbyStreamCombinerAdapter *combiner_adapter = stream_combiner->payload;

  if (type == DOLBY_STREAM_TYPE_BASE) {
    combiner_adapter->pts_bl = GST_BUFFER_PTS (buf);
    combiner_adapter->dts_bl = GST_BUFFER_DTS (buf);
    combiner_adapter->duration_bl = GST_BUFFER_DURATION (buf);

    if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT))
      combiner_adapter->need_flag_discont = TRUE;
    if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT))
      combiner_adapter->need_flag_delta_unit = TRUE;

  } else if (type == DOLBY_STREAM_TYPE_ENHANCE) {
    combiner_adapter->pts_el = GST_BUFFER_PTS (buf);
    combiner_adapter->dts_el = GST_BUFFER_DTS (buf);
    combiner_adapter->duration_el = GST_BUFFER_DURATION (buf);
  } else {
    GST_DEBUG_OBJECT (stream_combiner,
        "Invalid Stream type for process buffer");
    return FALSE;
  }

  gst_adapter_push (combiner_adapter->adapter, buf);

  return TRUE;
}

static GstBuffer *
gst_dolby_stream_combiner_adapter_pop (GstDolbyStreamCombiner * stream_combiner)
{
  GstBuffer *buf = NULL;
  GstDolbyStreamCombinerAdapter *combiner_adapter = stream_combiner->payload;

  buf = gst_adapter_take_buffer (combiner_adapter->adapter,
      gst_adapter_available (combiner_adapter->adapter));

  GST_BUFFER_PTS (buf) = combiner_adapter->pts_bl;
  GST_BUFFER_DTS (buf) = combiner_adapter->dts_bl;
  GST_BUFFER_DURATION (buf) = combiner_adapter->duration_bl;

  if (combiner_adapter->need_flag_discont)
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);

  if (combiner_adapter->need_flag_delta_unit)
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);

  return buf;
}

static GstFlowReturn
gst_dolby_stream_combiner_prepare_pad (GstDolbyStreamCombiner * stream_combiner,
    GstDolbyStreamCombinerPad * data, GstPad * pad)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstCaps *caps;
  GstStructure *s;
  const gchar *str = NULL;

  caps = gst_pad_get_current_caps (pad);
  if (caps == NULL) {
    GST_DEBUG_OBJECT (pad, "Sink pad caps were not set before pushing");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  s = gst_caps_get_structure (caps, 0);
  g_return_val_if_fail (s != NULL, FALSE);

  GST_LOG_OBJECT (pad, "Pad caps: %" GST_PTR_FORMAT, caps);

  if (gst_structure_has_name (s, "video/x-h265")) {
    data->codec = DOLBY_STREAM_HEVC;
    if (!gst_structure_has_field (s, "format")) {
      GST_DEBUG_OBJECT (pad, "Stream has no format field");
      ret = GST_FLOW_ERROR;
      goto beach;
    }

    if ((str = gst_structure_get_string (s, "format"))) {
      GST_LOG_OBJECT (pad, "format = %s", str);
      if (!g_strcmp0 (str, "dvhe") || !g_strcmp0 (str, "dvh1"))
        data->type = DOLBY_STREAM_TYPE_ENHANCE;
      else
        data->type = DOLBY_STREAM_TYPE_BASE;
    } else {
      GST_DEBUG_OBJECT (pad, "Stream has no format field");
      ret = GST_FLOW_ERROR;
      goto beach;
    }
  } else if (gst_structure_has_name (s, "video/x-h264")) {
    data->codec = DOLBY_STREAM_H264;
    if (!gst_structure_has_field (s, "format")) {
      GST_DEBUG_OBJECT (pad, "Stream has no format field");
      ret = GST_FLOW_ERROR;
      goto beach;
    }

    if ((str = gst_structure_get_string (s, "format"))) {
      GST_LOG_OBJECT (pad, "format = %s", str);
      if (!g_strcmp0 (str, "dvav"))
        data->type = DOLBY_STREAM_TYPE_ENHANCE;
      else
        data->type = DOLBY_STREAM_TYPE_BASE;
    } else {
      GST_DEBUG_OBJECT (pad, "Stream has no format field");
      ret = GST_FLOW_ERROR;
      goto beach;
    }
  } else {
    GST_ELEMENT_ERROR (pad, STREAM, FORMAT,
        ("Invalid stream format for dolbymux"),
        ("Only supports video/x-h265 or video/x-264"));
    ret = GST_FLOW_ERROR;
    goto beach;
  }

beach:
  if (caps)
    gst_caps_unref (caps);
  return ret;
}

static void
remove_fields (GstCaps * caps)
{
  guint i, n;

  g_return_if_fail (caps != NULL);

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);
    if (gst_structure_has_field (s, "need-compositor"))
      gst_structure_remove_field (s, "need-compositor");
  }

}

static GstFlowReturn
gst_dolby_stream_combiner_prepare_srcpad (GstDolbyStreamCombiner *
    stream_combiner)
{
  GstCaps *sink_caps = NULL;
  GstCaps *src_caps = NULL;
  DolbyStreamType type;

  gchar s_id[32];

  GSList *walk = stream_combiner->collect->data;

  while (walk) {
    GstCollectData *data = (GstCollectData *) walk->data;
    GstDolbyStreamCombinerPad *pad = (GstDolbyStreamCombinerPad *) walk->data;

    type = pad->type;

    GST_DEBUG_OBJECT (stream_combiner, "Pad %s, Stream Type : %d",
        GST_PAD_NAME (pad->collect.pad), type);

    if (type == DOLBY_STREAM_TYPE_BASE) {
      sink_caps = gst_pad_get_current_caps (data->pad);
    } else {
      GST_DEBUG_OBJECT (data->pad,
          "Not a base layer. Search another collect pad");
    }

    walk = g_slist_next (walk);
  }

  if (!sink_caps) {
    GST_DEBUG_OBJECT (stream_combiner, "Fail to search sink caps");
    return GST_FLOW_ERROR;
  }

  /* stream-start (FIXME: create id based on input ids) */
  g_snprintf (s_id, sizeof (s_id), "composite-%08x", g_random_int ());
  gst_pad_push_event (stream_combiner->srcpad,
      gst_event_new_stream_start (s_id));


  src_caps = gst_caps_copy (sink_caps);

  gst_caps_unref (sink_caps);

  /* Modify src caps to be single-track dolby HDR stream format */
  remove_fields (src_caps);

  /* Set caps on src pad and push new segment */
  gst_pad_push_event (stream_combiner->srcpad, gst_event_new_caps (src_caps));
  gst_caps_unref (src_caps);

  return GST_FLOW_OK;
}


static GstFlowReturn
gst_dolby_stream_combiner_prepare (GstDolbyStreamCombiner * stream_combiner)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GSList *walk = stream_combiner->collect->data;

  if (G_UNLIKELY (stream_combiner->n_streams !=
          GST_DOLBY_STREAM_COMBINER_MAX_STREAMS)) {
    GST_DEBUG_OBJECT (stream_combiner,
        "Invalid stream number %d (should be %d)", stream_combiner->n_streams,
        GST_DOLBY_STREAM_COMBINER_MAX_STREAMS);
    return GST_FLOW_ERROR;
  }

  while (walk) {
    GstCollectData *data = (GstCollectData *) walk->data;
    GstDolbyStreamCombinerPad *combiner_pad =
        (GstDolbyStreamCombinerPad *) walk->data;

    walk = g_slist_next (walk);

    if (combiner_pad->type == DOLBY_STREAM_TYPE_NONE) {
      ret =
          gst_dolby_stream_combiner_prepare_pad (stream_combiner, combiner_pad,
          data->pad);

      if (ret != GST_FLOW_OK)
        goto no_stream;
    }
  }

  ret = gst_dolby_stream_combiner_prepare_srcpad (stream_combiner);

  if (ret != GST_FLOW_OK)
    GST_DEBUG_OBJECT (stream_combiner, "Fail to prepare srcpad");

  return ret;

no_stream:
  GST_DEBUG_OBJECT (stream_combiner, "Fail to set stream type");
  return ret;
}

static GstDolbyStreamCombinerPad *
gst_dolby_stream_combiner_choose_best_pad (GstDolbyStreamCombiner *
    stream_combiner)
{
  GstDolbyStreamCombinerPad *best_pad = NULL;
  GstBuffer *buf = NULL;
  GSList *walk;

  GST_DEBUG_OBJECT (stream_combiner, "Choose best pad");


  walk = stream_combiner->collect->data;


  while (walk) {
    GstDolbyStreamCombinerPad *pad = NULL;
    GstCollectData *data = NULL;

    GST_LOG_OBJECT (stream_combiner, "Combiner State = %d",
        stream_combiner->state);
    data = (GstCollectData *) walk->data;
    pad = (GstDolbyStreamCombinerPad *) data;

    walk = g_slist_next (walk);

    if (stream_combiner->state == GST_DOLBY_SC_STATE_WAIT_BL) {
      if (pad->type == DOLBY_STREAM_TYPE_BASE) {
        best_pad = pad;
        /* FIXME: STATE CHANGE HERE??? */
        stream_combiner->state = GST_DOLBY_SC_STATE_WAIT_EL;
        break;
      } else {
        continue;
      }
    } else if (stream_combiner->state == GST_DOLBY_SC_STATE_WAIT_EL) {
      if (pad->type == DOLBY_STREAM_TYPE_ENHANCE) {
        /* FIXME: STATE CHANGE HERE??? */
        stream_combiner->state = GST_DOLBY_SC_STATE_WAIT_BL;
        best_pad = pad;
        break;
      } else {
        continue;
      }
    }
  }

  return best_pad;
}

static GstFlowReturn
gst_dolby_stream_combiner_process_buffer (GstDolbyStreamCombiner *
    stream_combiner, GstDolbyStreamCombinerPad * combiner_pad, GstBuffer * buf)
{
  GstAdapter *adapter_el;
  GstBuffer *buf_el, *temp_buf, *buf_el_pts;
  GstMapInfo map, map_el, map_el_pts;
  gint offset, size, es_size = 0;
  gboolean skip_indicator;

  GstClockTime el_pts;
  gint i, j;

  if (combiner_pad->type == DOLBY_STREAM_TYPE_BASE) {
    GST_LOG_OBJECT (stream_combiner, "adapter push for BL");
    if (!gst_dolby_stream_combiner_adapter_push (stream_combiner,
            buf, combiner_pad->type)) {
      GST_DEBUG_OBJECT (stream_combiner, "Adapter push fail for BL");
      return GST_FLOW_ERROR;
    }
  }
  /* We add a indicator (0x7E01) after the start code for EL */
  if (combiner_pad->type == DOLBY_STREAM_TYPE_ENHANCE) {
    adapter_el = gst_adapter_new ();

    gst_buffer_map (buf, &map, GST_MAP_READ);
    size = map.size;

    for (offset = 0; offset < size; offset += es_size + 4) {
      skip_indicator = FALSE;
      es_size = GST_READ_UINT32_BE (map.data + offset);
      GST_DEBUG_OBJECT (stream_combiner, "EL: es size %d", es_size);

#if 0
      // If nal_type is AU, drop the AU.
      if (combiner_pad->codec == DOLBY_STREAM_HEVC) {
        if (*(map.data + offset + 4) == 0x46) {
          GST_DEBUG_OBJECT (stream_combiner, "drop a AU of HEVC EL");
          continue;
        }
      } else if (combiner_pad->codec == DOLBY_STREAM_H264) {
        guint8 temp = *(map.data + offset + 4);
        temp = temp & 0x0F;
        if (temp == 0x09) {
          GST_DEBUG_OBJECT (stream_combiner, "drop a AU of H264 EL");
          continue;
        }
      }
#endif
      // If first byte of data is 7C, write buf without 0x7E01.
      if (*(map.data + offset + 4) == 0x7C) {
        GST_DEBUG_OBJECT (stream_combiner, "skip indicator");
        skip_indicator = TRUE;
      }

      temp_buf = gst_buffer_new_and_alloc (es_size + (skip_indicator ? 4 : 6));

      gst_buffer_map (temp_buf, &map_el, GST_MAP_WRITE);

      // size (4 bytes), (0x7E01 (2 bytes)), ES data (es_size)
      if (skip_indicator) {
        memcpy (map_el.data, map.data + offset, 4);
      } else {
        GST_WRITE_UINT32_BE (map_el.data, es_size + 2);
        *(map_el.data + 4) = 0x7E;
        *(map_el.data + 5) = 0x01;
      }

      memcpy (map_el.data + (skip_indicator ? 4 : 6), map.data + offset + 4,
          es_size);

      gst_buffer_unmap (temp_buf, &map_el);

      gst_adapter_push (adapter_el, temp_buf);
    }

    /* MAKE CUSTOM NAL FOR EL PTS */
    /* Nal type (2 byte) + [0xff (1byte) + pts (2byte)] + [0xff + pts] + [0xff + pts] + [0xff + pts] */
    el_pts = GST_BUFFER_PTS (buf);
    buf_el_pts = gst_buffer_new_and_alloc (4 + GST_DOLBY_STREAM_COMBINER_EL_PTS_SIZE);  //nal size (4) + 0x7A01 + el pts
    gst_buffer_map (buf_el_pts, &map_el_pts, GST_MAP_WRITE);

    // FIXME: Write nal size (Assume that input type is not BYTE_FORMAT)
    GST_WRITE_UINT32_BE (map_el_pts.data,
        GST_DOLBY_STREAM_COMBINER_EL_PTS_SIZE);

    // Write nal type code
    *(map_el_pts.data + 4) = 0x7A;
    *(map_el_pts.data + 5) = 0x01;

    for (i = 6, j = 48; i < 4 + GST_DOLBY_STREAM_COMBINER_EL_PTS_SIZE;
        i += 3, j -= 16) {
      guint16 pts_frag = (el_pts >> j) & 0xffff;
      *(map_el_pts.data + i) = 0xff;
      GST_WRITE_UINT16_BE (map_el_pts.data + i + 1, pts_frag);
    }

    // Write pts of EL
    gst_adapter_push (adapter_el, buf_el_pts);
    GST_LOG_OBJECT (stream_combiner, "INSERT PTS FOR EL pts %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_PTS (buf)));

    gst_buffer_unmap (buf_el_pts, &map_el_pts);
    gst_buffer_unmap (buf, &map);
    gst_buffer_unref (buf);

    GST_DEBUG_OBJECT (stream_combiner, "EL: el size %" G_GSIZE_FORMAT,
        gst_adapter_available (adapter_el));

    buf_el = gst_adapter_take_buffer (adapter_el,
        gst_adapter_available (adapter_el));

    GST_LOG_OBJECT (stream_combiner, "adapter push for EL");
    if (!gst_dolby_stream_combiner_adapter_push (stream_combiner,
            buf_el, combiner_pad->type)) {
      GST_DEBUG_OBJECT (stream_combiner, "Adapter push fail for EL");
      return GST_FLOW_ERROR;
    }

    g_object_unref (adapter_el);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_dolby_stream_combiner_collected (GstCollectPads * pads,
    GstDolbyStreamCombiner * stream_combiner)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstDolbyStreamCombinerPad *best = NULL;
  GstBuffer *buf = NULL;
  gint i;

  GST_LOG_OBJECT (stream_combiner, "Start collect function");

  if (G_UNLIKELY (stream_combiner->state == GST_DOLBY_SC_STATE_NONE)) {
    ret = gst_dolby_stream_combiner_prepare (stream_combiner);

    if (G_UNLIKELY (ret != GST_FLOW_OK)) {
      GST_DEBUG_OBJECT (stream_combiner, "Combiner prepare fail");
      return ret;
    }

    stream_combiner->state = GST_DOLBY_SC_STATE_WAIT_BL;
  }

  GST_DOLBY_STREAM_COMBINER_LOCK (stream_combiner);

  /* 0. Forwarding pending segment event if has */
  if (stream_combiner->has_pending_segment) {
    GST_LOG_OBJECT (stream_combiner, "Pushing pending segment event");
    gst_pad_push_event (stream_combiner->srcpad,
        gst_event_new_segment (stream_combiner->seg_last));

    stream_combiner->has_pending_segment = FALSE;
  }

  /* 0. Forwarding pending tag event if has */
  if (G_UNLIKELY (stream_combiner->pending_tags)) {
    GST_LOG_OBJECT (stream_combiner, "Pushing pending tag event");
    gst_pad_push_event (stream_combiner->srcpad,
        gst_event_new_tag (stream_combiner->pending_tags));
    stream_combiner->pending_tags = NULL;
  }

  GST_DOLBY_STREAM_COMBINER_UNLOCK (stream_combiner);

  /* 1. Init adapter to process buffers */
  gst_dolby_stream_combiner_adapter_init (stream_combiner);

  for (i = 0; i < stream_combiner->n_streams; i++) {
    best = gst_dolby_stream_combiner_choose_best_pad (stream_combiner);

    if (best != NULL) {
      guint64 dts, pts;

      buf = gst_collect_pads_pop (pads, &best->collect);
      if (buf == NULL) {
        GST_DEBUG_OBJECT (stream_combiner, "Fail to peaking buffer");
        goto eos;
      }
      dts = GST_BUFFER_DTS (buf);
      pts = GST_BUFFER_PTS (buf);

      GST_LOG_OBJECT (stream_combiner,
          "Peeking buffer with dts %" GST_TIME_FORMAT ", pts %" GST_TIME_FORMAT
          ", on pad %s", GST_TIME_ARGS (dts), GST_TIME_ARGS (pts),
          GST_PAD_NAME (best->collect.pad));

      ret =
          gst_dolby_stream_combiner_process_buffer (stream_combiner, best, buf);
      if (ret != GST_FLOW_OK) {
        GST_DEBUG_OBJECT (stream_combiner, "Fail to process buffer, EOS");
        goto eos;
      }
    } else {
      goto eos;
    }
  }

  buf = gst_dolby_stream_combiner_adapter_pop (stream_combiner);

  {
    guint64 dts, pts;

    dts = GST_BUFFER_DTS (buf);
    pts = GST_BUFFER_PTS (buf);

    GST_LOG_OBJECT (stream_combiner,
        "Pushing buffer with dts %" GST_TIME_FORMAT ", pts %" GST_TIME_FORMAT
        ", on pad %s", GST_TIME_ARGS (dts), GST_TIME_ARGS (pts),
        GST_PAD_NAME (stream_combiner->srcpad));

  }

  ret = gst_pad_push (stream_combiner->srcpad, buf);

  return ret;

eos:
  GST_DEBUG_OBJECT (stream_combiner, "No best collect pad, EOS");
  gst_pad_push_event (stream_combiner->srcpad, gst_event_new_eos ());
  return GST_FLOW_EOS;
}

static gboolean
gst_dolby_stream_combiner_sink_event (GstCollectPads * pads,
    GstCollectData * c_data, GstEvent * event,
    GstDolbyStreamCombiner * stream_combiner)
{
  GstDolbyStreamCombinerPad *combiner_pad =
      (GstDolbyStreamCombinerPad *) c_data;
  gboolean ret = TRUE;
  gboolean discard = FALSE;

  GST_DEBUG_OBJECT (combiner_pad->collect.pad, "Got %s event on pad %s:%s",
      GST_EVENT_TYPE_NAME (event),
      GST_DEBUG_PAD_NAME (combiner_pad->collect.pad));


  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *in_caps;
      GstCaps *out_caps;
      gst_event_parse_caps (event, &in_caps);

      GST_LOG_OBJECT (combiner_pad->collect.pad,
          "caps event with caps: %" GST_PTR_FORMAT, in_caps);

      /* FIXME: NEED TO CLEAN UP */
      if (combiner_pad->type == DOLBY_STREAM_TYPE_BASE
          && stream_combiner->state != GST_DOLBY_SC_STATE_NONE) {
        GST_LOG_OBJECT (combiner_pad->collect.pad,
            "forwarding caps event for base layer");
        out_caps = gst_caps_copy (in_caps);
        remove_fields (out_caps);
        GST_LOG_OBJECT (combiner_pad->collect.pad,
            "out caps is %" GST_PTR_FORMAT, out_caps);

        ret = gst_pad_set_caps (stream_combiner->srcpad, out_caps);
        gst_caps_unref (out_caps);
        gst_event_unref (event);
        event = NULL;
      } else {
        discard = TRUE;
        GST_LOG_OBJECT (combiner_pad->collect.pad, "eating caps event");
      }

      break;
    }
    case GST_EVENT_SEGMENT:
    {
      /*
       * Default collect event function does not forward SEGMENT to src pad.
       * DolbyStreamCombiner forward SEGMENT event only which was from
       * Base layer pad.
       */

      const GstSegment *in_seg;
      gst_event_parse_segment (event, &in_seg);

      /* Segment event will be forwarded after stream-start.
       * Just copy segment in here, and forwarded in collected function. */
      if (combiner_pad->type == DOLBY_STREAM_TYPE_BASE
          || stream_combiner->state == GST_DOLBY_SC_STATE_NONE) {
        GST_LOG_OBJECT (combiner_pad->collect.pad,
            "Get segment and store to be pushed");
        stream_combiner->has_pending_segment = TRUE;
        gst_segment_copy_into (in_seg, stream_combiner->seg_last);
      } else {
        GST_LOG_OBJECT (combiner_pad->collect.pad,
            "Get segment from EL sink pad. Discard it");
      }

      ret = gst_collect_pads_event_default (pads, c_data, event, discard);

      if (!ret)
        GST_DEBUG_OBJECT (combiner_pad->collect.pad, "segment event fail");
      event = NULL;
      break;
    }
    case GST_EVENT_TAG:
    {
      GstTagList *tags;
      GST_LOG_OBJECT (combiner_pad->collect.pad, "eating tag event");
      gst_event_parse_tag (event, &tags);
      tags =
          gst_tag_list_merge (stream_combiner->pending_tags, tags,
          GST_TAG_MERGE_APPEND);
      if (stream_combiner->pending_tags)
        gst_tag_list_unref (stream_combiner->pending_tags);
      stream_combiner->pending_tags = tags;
      gst_event_unref (event);
      event = NULL;
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      /* reset state to WAIT_BL to ensure the BL/EL ordering */
      stream_combiner->state = GST_DOLBY_SC_STATE_WAIT_BL;
      break;
    default:
      break;
  }

  if (event != NULL)
    ret = gst_collect_pads_event_default (pads, c_data, event, discard);

  return ret;
}

static gboolean
gst_dolby_stream_combiner_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstDolbyStreamCombiner *stream_combiner = (GstDolbyStreamCombiner *) parent;
  gboolean ret = TRUE;

  ret = gst_collect_pads_src_event_default (stream_combiner->collect,
      pad, event);

  return ret;
}

static GstPadProbeReturn
block_buffer_push_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GST_DEBUG_OBJECT (pad, "Callback");

  return GST_PAD_PROBE_OK;
}

static void
gst_dolby_stream_combiner_start_task (GstDolbyStreamCombiner * stream_combiner)
{
  GSList *walk = stream_combiner->collect->data;

  gst_collect_pads_start (stream_combiner->collect);

  while (walk) {
    GstDolbyStreamCombinerPad *pad = (GstDolbyStreamCombinerPad *) walk->data;

    gst_pad_remove_probe (pad->collect.pad, pad->blocked_id);
    pad->blocked_id = 0;

    GST_DEBUG_OBJECT (stream_combiner, "Pad %s, remove block",
        GST_PAD_NAME (pad->collect.pad));

    walk = g_slist_next (walk);
  }
}

static GstPad *
gst_dolby_stream_combiner_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps)
{
  GstDolbyStreamCombiner *stream_combiner = (GstDolbyStreamCombiner *) element;
  GstDolbyStreamCombinerPad *combiner_pad;
  GstPad *sinkpad;
  gchar *pad_name = NULL;
  const gchar *final_name = NULL;

  GST_DEBUG_OBJECT (stream_combiner, "templ:%p, name:%s", templ, name);

  GST_DOLBY_STREAM_COMBINER_LOCK (stream_combiner);

  if (stream_combiner->state != GST_DOLBY_SC_STATE_NONE) {
    GST_WARNING_OBJECT (stream_combiner,
        "Not providing request pad after element is at "
        "paused/playing state");
  }

  if (name == NULL) {
    pad_name = g_strdup_printf ("sink_%u", stream_combiner->n_streams);
    final_name = pad_name;
  } else {
    final_name = name;
  }

  sinkpad = gst_pad_new_from_template (templ, final_name);

  combiner_pad = (GstDolbyStreamCombinerPad *)
      gst_collect_pads_add_pad (stream_combiner->collect,
      sinkpad, sizeof (GstDolbyStreamCombinerPad), NULL, TRUE);

  stream_combiner->n_streams += 1;
  gst_pad_set_active (sinkpad, TRUE);
  gst_element_add_pad (element, sinkpad);

  combiner_pad->type = DOLBY_STREAM_TYPE_NONE;
  GST_LOG_OBJECT (sinkpad, "Unknown layer pad added");

  GST_DEBUG_OBJECT (element, "Returning pad %p", sinkpad);

  combiner_pad->blocked_id =
      gst_pad_add_probe (combiner_pad->collect.pad,
      GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BLOCK,
      block_buffer_push_cb, NULL, NULL);

  /* FIXME: Sometimes dolbystreamcombiner is not added to decodebin .. */
  if (GST_OBJECT_PARENT (sinkpad)) {
    if (!GST_OBJECT_PARENT (GST_OBJECT_PARENT (sinkpad)))
      GST_WARNING_OBJECT (stream_combiner,
          "DolbyStreamCombiner is not bin added");
  }
  GST_DOLBY_STREAM_COMBINER_UNLOCK (stream_combiner);

  g_free (pad_name);

  /* Only signalling when the number of requested sink pads is tw  */
  if (stream_combiner->n_streams == GST_DOLBY_STREAM_COMBINER_MAX_STREAMS)
    g_cond_signal (&stream_combiner->cond);

  return sinkpad;
}

static void
gst_dolby_stream_combiner_release_pad (GstElement * element, GstPad * pad)
{
  GstDolbyStreamCombiner *stream_combiner = (GstDolbyStreamCombiner *) element;

  GST_DEBUG_OBJECT (stream_combiner, "Pad %" GST_PTR_FORMAT " being released",
      pad);

  GST_DOLBY_STREAM_COMBINER_LOCK (stream_combiner);
  gst_collect_pads_remove_pad (stream_combiner->collect, pad);

  GST_DOLBY_STREAM_COMBINER_UNLOCK (stream_combiner);
  return;
}

static GstStateChangeReturn
gst_dolby_stream_combiner_change_state (GstElement * element,
    GstStateChange transition)
{
  GstDolbyStreamCombiner *stream_combiner;
  GstStateChangeReturn ret;

  stream_combiner = GST_DOLBY_STREAM_COMBINER (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_LOG_OBJECT (stream_combiner, "State Change READY->PAUSED");

      GST_DOLBY_STREAM_COMBINER_LOCK (stream_combiner);

      if (G_UNLIKELY (stream_combiner->n_streams !=
              GST_DOLBY_STREAM_COMBINER_MAX_STREAMS)) {
        GST_DEBUG_OBJECT (stream_combiner, "Waiting stream combiner");
        g_cond_wait (&stream_combiner->cond, &stream_combiner->lock);
      }

      gst_dolby_stream_combiner_start_task (stream_combiner);
      GST_DOLBY_STREAM_COMBINER_UNLOCK (stream_combiner);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      GST_LOG_OBJECT (stream_combiner, "State Change PAUSED->PLAYING");
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_LOG_OBJECT (stream_combiner, "State Change PAUSED->READY");
      gst_collect_pads_stop (stream_combiner->collect);
      stream_combiner->state = GST_DOLBY_SC_STATE_NONE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

done:
  return ret;
}
