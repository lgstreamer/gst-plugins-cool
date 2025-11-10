/* GStreamer Plugins Cool
 * Copyright (C) 2013-2015 LG Electronics, Inc.
 *    Author : Wonchul Lee <wonchul86.lee@lge.com>
 *           Jeongseok Kim <jeongseok.kim@lge.com>
 *           HoonHee Lee <hoonhee.lee@lge.com>
 *           Myoungsun Lee <mysunny.lee@lge.com>
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

#include "gstfakeadec.h"
#include "gstfdcaps.h"

/**
 * GST_AUDIO_DEF_RATE:
 *
 * Standard sampling rate used in consumer audio.
 */
#define GST_AUDIO_DEF_RATE 44100
/**
 * GST_AUDIO_DEF_CHANNELS:
 *
 * Standard number of channels used in consumer audio.
 */
#define GST_AUDIO_DEF_CHANNELS 2

static GstStaticPadTemplate gst_fakeadec_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (FD_AUDIO_CAPS)
    );

static GstStaticPadTemplate gst_fakeadec_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw")
    );

enum
{
  PROP_0,
  PROP_ACTIVE_MODE,
  PROP_SYNC_STREAMS,
  PROP_LAST
};

#define DEFAULT_LOOP_MSTIME 50
#define DEFAULT_SYNC_STREAMS TRUE

#define GST_FAKEADEC_GET_LOCK(d) (&((GstFakeAdec*)(d))->lock)
#define GST_FAKEADEC_GET_COND(d) (&((GstFakeAdec*)(d))->turn)
#define GST_FAKEADEC_LOCK(d) (g_mutex_lock (GST_FAKEADEC_GET_LOCK(d)))
#define GST_FAKEADEC_UNLOCK(d) (g_mutex_unlock (GST_FAKEADEC_GET_LOCK(d)))
#define GST_FAKEADEC_WAIT(d) (g_cond_wait (GST_FAKEADEC_GET_COND(d), \
                        GST_FAKEADEC_GET_LOCK(d)))
#define GST_FAKEADEC_BROADCAST(d) (g_cond_broadcast (GST_FAKEADEC_GET_COND(d)))

GST_DEBUG_CATEGORY_STATIC (fakeadec_debug);
#define GST_CAT_DEFAULT fakeadec_debug

static void gst_fakeadec_finalize (GObject * object);
static gboolean gst_fakeadec_set_caps (GstFakeAdec * fakeadec, GstCaps * caps);
static gboolean gst_fakeadec_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static void gst_fakeadec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_fakeadec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstFlowReturn gst_fakeadec_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static GstStateChangeReturn gst_fakeadec_change_state (GstElement *
    element, GstStateChange transition);
static gboolean gst_fakeadec_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

#define gst_fakeadec_parent_class parent_class
G_DEFINE_TYPE (GstFakeAdec, gst_fakeadec, GST_TYPE_ELEMENT);

static gboolean
gst_fakeadec_set_caps (GstFakeAdec * fakeadec, GstCaps * caps)
{
  gboolean ret;

  GstStructure *s;
  GstCaps *output_caps;
  gint rate, channels;
  const gchar *format;

  GST_DEBUG_OBJECT (fakeadec, "got caps %" GST_PTR_FORMAT, caps);

  output_caps =
      negotiate_default_caps (caps, fakeadec->srcpad,
      GST_COOL_STREAM_TYPE_AUDIO);
  if (!output_caps) {
    s = gst_caps_get_structure (caps, 0);
    output_caps = gst_caps_new_empty_simple ("audio/x-raw");

    /* set format */
    format = gst_structure_get_string (s, "format");

    //FIXME: hardcoded format should be fixed
    if (!format)
      gst_caps_set_simple (output_caps, "format", G_TYPE_STRING,
          DEFAULT_PUPPET_AUDIO_FORMAT, NULL);
    else
      gst_caps_set_simple (output_caps, "format", G_TYPE_STRING, format, NULL);
    /* set layout */
    gst_caps_set_simple (output_caps, "layout", G_TYPE_STRING, "interleaved",
        NULL);
    /* set rate */
    gst_structure_get_int (s, "rate", &rate);
    gst_caps_set_simple (output_caps, "rate", G_TYPE_INT, rate, NULL);
    /* set channels */
    gst_structure_get_int (s, "channels", &channels);
    /* FIXME: omxaudiosink can handle less than two channels */
    if (channels > 2)
      channels = 2;

    gst_caps_set_simple (output_caps, "channels", G_TYPE_INT, channels, NULL);

  }
  GST_DEBUG_OBJECT (fakeadec, "Expected output caps: %" GST_PTR_FORMAT,
      output_caps);
  ret = gst_pad_set_caps (fakeadec->srcpad, output_caps);
  gst_caps_unref (output_caps);

  return ret;
}

static void
gst_fakeadec_class_init (GstFakeAdecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_fakeadec_finalize);

  gobject_class->set_property = gst_fakeadec_set_property;
  gobject_class->get_property = gst_fakeadec_get_property;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_fakeadec_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_fakeadec_sink_pad_template));

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_fakeadec_change_state);

  gst_element_class_set_static_metadata (element_class, "Fake Audio decoder",
      "Codec/Decoder/Audio",
      "Pass data to backend decoder",
      "Wonchul Lee <wonchul86.lee@lge.com>, Jeongseok Kim <jeongseok.kim@lge.com>");

  g_object_class_install_property (gobject_class, PROP_ACTIVE_MODE,
      g_param_spec_boolean ("active-mode", "Active mode",
          "Set active mode to fakeadec",
          FALSE, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstFakeAdec:sync-streams
   *
   * If set to %TRUE all buffers will be synced to the current clock.
   *
   */
  g_object_class_install_property (gobject_class, PROP_SYNC_STREAMS,
      g_param_spec_boolean ("sync-streams", "Sync Streams",
          "Synchronize between buffer and current clock"
          "stream or to the current clock",
          DEFAULT_SYNC_STREAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  GST_DEBUG_CATEGORY_INIT (fakeadec_debug, "fakeadec", 0, "Fake audio decoder");
}

static void
gst_fakeadec_init (GstFakeAdec * fakeadec)
{
  fakeadec->sinkpad =
      gst_pad_new_from_static_template (&gst_fakeadec_sink_pad_template,
      "sink");
  gst_pad_set_chain_function (fakeadec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_fakeadec_chain));
  gst_pad_set_event_function (fakeadec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_fakeadec_sink_event));
  gst_pad_set_query_function (fakeadec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_fakeadec_sink_query));

  gst_element_add_pad (GST_ELEMENT (fakeadec), fakeadec->sinkpad);

  fakeadec->srcpad =
      gst_pad_new_from_static_template (&gst_fakeadec_src_pad_template, "src");
  gst_element_add_pad (GST_ELEMENT (fakeadec), fakeadec->srcpad);

  fakeadec->active_mode = FALSE;        /* whether the fakeadec is activated pad or not */
  fakeadec->need_gap = FALSE;   /* whether the fakeadec needs to send a gap event or not */

  fakeadec->active_stream_id = NULL;
  fakeadec->stream_change = FALSE;
  fakeadec->curcaps = NULL;

  g_mutex_init (&fakeadec->lock);
  g_cond_init (&fakeadec->turn);
  fakeadec->next_time = GST_CLOCK_TIME_NONE;
  fakeadec->cur_base_time = GST_CLOCK_TIME_NONE;
  fakeadec->need_clock_waiting = TRUE;
  fakeadec->flushing = FALSE;
  fakeadec->eos = FALSE;
  fakeadec->sync_streams = DEFAULT_SYNC_STREAMS;
  fakeadec->exit_loop = FALSE;
}

static void
gst_fakeadec_finalize (GObject * object)
{
  GstFakeAdec *fakeadec = GST_FAKEADEC (object);

  if (fakeadec->active_stream_id) {
    g_free (fakeadec->active_stream_id);
    fakeadec->active_stream_id = NULL;
  }

  if (fakeadec->curcaps) {
    gst_caps_unref (fakeadec->curcaps);
    fakeadec->curcaps = NULL;
  }

  g_mutex_clear (&fakeadec->lock);
  g_cond_clear (&fakeadec->turn);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_fakeadec_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstFakeAdec *fakeadec;
  gboolean res = TRUE;
  gboolean forward = TRUE;

  fakeadec = GST_FAKEADEC (parent);

  GST_FAKEADEC_LOCK (fakeadec);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:
    {
      const gchar *stream_id;

      gst_event_parse_stream_start (event, &stream_id);
      if (fakeadec->active_stream_id != NULL) {
        if (!g_str_equal (fakeadec->active_stream_id, stream_id)) {
          GST_DEBUG_OBJECT (pad, "Handle stream changes (%s => %s) !",
              fakeadec->active_stream_id, stream_id);
          fakeadec->stream_change = TRUE;
        } else {
          GST_DEBUG_OBJECT (pad, "Repeat stream-id (%s)", stream_id);
        }
      }

      if (fakeadec->active_stream_id) {
        g_free (fakeadec->active_stream_id);
        fakeadec->active_stream_id = NULL;
      }
      fakeadec->active_stream_id = g_strdup (stream_id);

      break;
    }
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;
      gst_event_parse_caps (event, &caps);

      if (fakeadec->curcaps) {
        gst_caps_unref (fakeadec->curcaps);
        fakeadec->curcaps = NULL;
      }
      fakeadec->curcaps = gst_caps_ref (caps);
      GST_DEBUG_OBJECT (pad, "Current Caps: %" GST_PTR_FORMAT, caps);

      if (!gst_pad_has_current_caps (fakeadec->srcpad))
        gst_fakeadec_set_caps (fakeadec, caps);

      if (fakeadec->stream_change)
        fakeadec->stream_change = FALSE;

      gst_event_unref (event);
      forward = FALSE;
      res = TRUE;
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      gst_event_copy_segment (event, &fakeadec->segment);
      GST_DEBUG_OBJECT (pad, "configured SEGMENT %" GST_SEGMENT_FORMAT,
          &fakeadec->segment);
      fakeadec->need_gap = TRUE;
      if (!fakeadec->need_clock_waiting)
        fakeadec->need_clock_waiting = TRUE;

      break;
    }
    case GST_EVENT_GAP:
    {
      GstClockTime ts, dur;

      GST_DEBUG_OBJECT (pad, "Received gap event: %" GST_PTR_FORMAT, event);

      gst_event_parse_gap (event, &ts, &dur);
      if (GST_CLOCK_TIME_IS_VALID (ts)) {
        if (GST_CLOCK_TIME_IS_VALID (dur))
          ts += dur;

        /* update the segment position */
        GST_OBJECT_LOCK (pad);
        fakeadec->segment.position = ts;
        GST_OBJECT_UNLOCK (pad);
      }

      if (fakeadec->sync_streams)
        GST_FAKEADEC_BROADCAST (fakeadec);

      break;
    }
    case GST_EVENT_FLUSH_START:
    {
      GST_DEBUG_OBJECT (fakeadec, "Received flush-start");
      GST_OBJECT_LOCK (pad);
      fakeadec->flushing = TRUE;
      fakeadec->eos = FALSE;
      GST_OBJECT_UNLOCK (pad);

      if (fakeadec->sync_streams)
        GST_FAKEADEC_BROADCAST (fakeadec);

      break;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      GST_DEBUG_OBJECT (fakeadec, "Received flush-stop");
      GST_OBJECT_LOCK (pad);
      fakeadec->flushing = FALSE;
      fakeadec->eos = FALSE;
      gst_segment_init (&fakeadec->segment, GST_FORMAT_UNDEFINED);
      GST_OBJECT_UNLOCK (pad);

      break;
    }
    case GST_EVENT_EOS:
    {
      GST_DEBUG_OBJECT (fakeadec, "Received EOS");
      GST_OBJECT_LOCK (pad);
      fakeadec->eos = TRUE;
      GST_OBJECT_UNLOCK (pad);

      if (fakeadec->sync_streams)
        GST_FAKEADEC_BROADCAST (fakeadec);

      break;
    }
    default:
      break;
  }

  GST_FAKEADEC_UNLOCK (fakeadec);

  if (forward) {
    GST_DEBUG_OBJECT (pad, "forwarding event");
    res = gst_pad_push_event (fakeadec->srcpad, event);
  }

  return res;
}

static void
gst_fakeadec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFakeAdec *fakeadec = GST_FAKEADEC (object);

  switch (prop_id) {
    case PROP_ACTIVE_MODE:
      GST_DEBUG_OBJECT (fakeadec, "Set active mode");
      fakeadec->active_mode = g_value_get_boolean (value);
      fakeadec->need_gap = TRUE;
      break;
    case PROP_SYNC_STREAMS:
      GST_FAKEADEC_LOCK (fakeadec);
      fakeadec->sync_streams = g_value_get_boolean (value);
      GST_FAKEADEC_UNLOCK (fakeadec);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_fakeadec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstFakeAdec *fakeadec = GST_FAKEADEC (object);

  switch (prop_id) {
    case PROP_SYNC_STREAMS:
      GST_FAKEADEC_LOCK (fakeadec);
      g_value_set_boolean (value, fakeadec->sync_streams);
      GST_FAKEADEC_UNLOCK (fakeadec);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstClockTime
get_clipped_running_time (GstSegment * seg, GstBuffer * buf)
{
  GstClockTime running_time;

  running_time = GST_BUFFER_DTS_OR_PTS (buf);
  /* If possible try to get the running time at the end of the buffer */
  if (GST_BUFFER_DURATION_IS_VALID (buf))
    running_time += GST_BUFFER_DURATION (buf);
  /* Only use the segment to convert to running time if the segment is
   * in TIME format, otherwise do our best to try to sync */
  if (GST_CLOCK_TIME_IS_VALID (seg->stop)) {
    if (running_time > seg->stop) {
      running_time = seg->stop;
    }
  }

  GST_DEBUG ("buffer running time(PTS + Duration): %" GST_TIME_FORMAT,
      GST_TIME_ARGS (running_time));

  return gst_segment_to_running_time (seg, GST_FORMAT_TIME, running_time);
}

static gboolean
wake_up_next (gpointer data)
{
  GstFakeAdec *fakeadec = (GstFakeAdec *) data;
  gboolean ret = TRUE;
  GstClock *clock;
  GstClockTime cur_running_time = GST_CLOCK_TIME_NONE;

  GST_DEBUG_OBJECT (fakeadec,
      "Compare current running time of clock and current buffer time");

  if (fakeadec->exit_loop) {
    /* FIXME, Force to exit when waiting lock is unblocked  */
    GST_FIXME_OBJECT (fakeadec, "Exit loop");
    return FALSE;
  }

  clock = gst_element_get_clock (GST_ELEMENT (fakeadec));
  if (clock) {
    GstClockTime base_time;
    cur_running_time = gst_clock_get_time (clock);
    base_time = gst_element_get_base_time (GST_ELEMENT_CAST (fakeadec));
    if (base_time <= cur_running_time)
      cur_running_time -= base_time;
    else
      cur_running_time = 0;

    if (fakeadec->cur_base_time == GST_CLOCK_TIME_NONE
        || fakeadec->cur_base_time != base_time) {
      fakeadec->cur_base_time = base_time;
      fakeadec->need_clock_waiting = FALSE;
    }

    gst_object_unref (clock);
  }

  if (fakeadec->need_clock_waiting) {
    /* FIXME, There is a gap to propagate the clock with updated start and base time */
    GST_FIXME_OBJECT (fakeadec, "Wait until valid clock is obtained");
    return TRUE;
  }

  if (fakeadec->eos || fakeadec->flushing
      || (cur_running_time != GST_CLOCK_TIME_NONE
          && cur_running_time > fakeadec->next_time)) {
    GST_FAKEADEC_LOCK (fakeadec);
    GST_FAKEADEC_BROADCAST (fakeadec);
    GST_FAKEADEC_UNLOCK (fakeadec);
    ret = FALSE;
  }

  GST_DEBUG_OBJECT (fakeadec,
      "%s for synchronize to clock. buffer time: %" GST_TIME_FORMAT
      ", running time: %" GST_TIME_FORMAT, ret ? "Waiting " : "Wake up",
      GST_TIME_ARGS (fakeadec->next_time), GST_TIME_ARGS (cur_running_time));

  return ret;
}

static gboolean
gst_fakeadec_wait_running_time (GstFakeAdec * fakeadec, GstBuffer * buf)
{
  GstSegment *seg;

  GST_FIXME_OBJECT (fakeadec, "entering wait for buffer %" GST_PTR_FORMAT, buf);

  /* If we have no valid timestamp we can't sync this buffer */
  if (!GST_BUFFER_PTS_IS_VALID (buf)) {
    GST_DEBUG_OBJECT (fakeadec, "leaving wait for buffer with "
        "invalid timestamp");
    return FALSE;
  }

  seg = &fakeadec->segment;

  /* Wait until the buffer running time is before the current running time */
  GST_FAKEADEC_LOCK (fakeadec);
  while (TRUE) {
    GstClock *clock;
    GstClockTime cur_running_time;
    GstClockTime buf_running_time = GST_CLOCK_TIME_NONE;

    if (seg->format != GST_FORMAT_TIME) {
      GST_DEBUG_OBJECT (fakeadec,
          "Not waiting because we don't have a TIME segment");
      GST_FAKEADEC_UNLOCK (fakeadec);
      return FALSE;
    }

    buf_running_time = get_clipped_running_time (seg, buf);

    /* If this is outside the segment don't sync */
    if (buf_running_time == -1) {
      GST_DEBUG_OBJECT (fakeadec,
          "Not waiting because buffer is outside segment, running time: %"
          GST_TIME_FORMAT, GST_TIME_ARGS (buf_running_time));
      GST_FAKEADEC_UNLOCK (fakeadec);
      return FALSE;
    }

    if (fakeadec->flushing && buf_running_time != -1) {
      GST_DEBUG_OBJECT (fakeadec,
          "Drop buffer (%" GST_PTR_FORMAT ") immediately while flushing", buf);
      GST_FAKEADEC_UNLOCK (fakeadec);
      return TRUE;
    }

    cur_running_time = GST_CLOCK_TIME_NONE;
    clock = gst_element_get_clock (GST_ELEMENT_CAST (fakeadec));
    if (clock) {
      GstClockTime base_time;

      cur_running_time = gst_clock_get_time (clock);
      base_time = gst_element_get_base_time (GST_ELEMENT_CAST (fakeadec));
      if (base_time <= cur_running_time)
        cur_running_time -= base_time;
      else
        cur_running_time = 0;

      gst_object_unref (clock);
    }

    if (fakeadec->need_clock_waiting
        || (!fakeadec->eos && !fakeadec->flushing
            && (cur_running_time == GST_CLOCK_TIME_NONE
                || buf_running_time >= cur_running_time))) {
      GST_DEBUG_OBJECT (fakeadec,
          "Waiting for synchronize to clock. buffer: %" GST_TIME_FORMAT
          " >= clock: %" GST_TIME_FORMAT, GST_TIME_ARGS (buf_running_time),
          GST_TIME_ARGS (cur_running_time));
      fakeadec->next_time = buf_running_time;
      fakeadec->exit_loop = FALSE;
      g_timeout_add (DEFAULT_LOOP_MSTIME, &wake_up_next, fakeadec);
      GST_FAKEADEC_WAIT (fakeadec);
      fakeadec->exit_loop = TRUE;
    } else {
      GST_DEBUG_OBJECT (fakeadec,
          "Done Sync. buffer: %" GST_TIME_FORMAT
          " >= clock: %" GST_TIME_FORMAT ", Clock: %" GST_PTR_FORMAT,
          GST_TIME_ARGS (buf_running_time), GST_TIME_ARGS (cur_running_time),
          clock);
      GST_FAKEADEC_UNLOCK (fakeadec);
      break;
    }
  }

  /* Return TRUE if the pad is flushing */
  return fakeadec->flushing;
}

static GstFlowReturn
gst_fakeadec_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstFakeAdec *fakeadec;
  GstFlowReturn ret = GST_FLOW_OK;

  fakeadec = GST_FAKEADEC (parent);

  GST_LOG_OBJECT (pad, "got buffer %" GST_PTR_FORMAT, buffer);

  GST_FAKEADEC_LOCK (fakeadec);

  if (fakeadec->flushing) {
    GST_FAKEADEC_UNLOCK (fakeadec);
    goto flushing;
  }

  if (fakeadec->active_mode) {
    if (fakeadec->need_gap) {
      GstEvent *gap =
          gst_event_new_gap (fakeadec->segment.start, fakeadec->segment.stop);
      GST_DEBUG_OBJECT (fakeadec,
          "push gap event with time :%" GST_TIME_FORMAT " duration: %"
          GST_TIME_FORMAT, GST_TIME_ARGS (fakeadec->segment.start),
          GST_TIME_ARGS (fakeadec->segment.stop));
      GST_FAKEADEC_UNLOCK (fakeadec);
      if (gst_pad_push_event (fakeadec->srcpad, gap))
        fakeadec->need_gap = FALSE;
      GST_FAKEADEC_LOCK (fakeadec);
    }

    /* FIXME, Sync between current buffer and running time of clock */
    if (fakeadec->sync_streams) {
      GST_FAKEADEC_UNLOCK (fakeadec);
      if (gst_fakeadec_wait_running_time (fakeadec, buffer))
        goto flushing;
      GST_FAKEADEC_LOCK (fakeadec);
    }

    /* update the segment on the srcpad */
    if (GST_BUFFER_PTS_IS_VALID (buffer)) {
      GstClockTime start_time = GST_BUFFER_PTS (buffer);

      GST_DEBUG_OBJECT (pad, "received start time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (start_time));
      if (GST_BUFFER_DURATION_IS_VALID (buffer))
        GST_DEBUG_OBJECT (pad, "received end time %" GST_TIME_FORMAT,
            GST_TIME_ARGS (start_time + GST_BUFFER_DURATION (buffer)));

      GST_OBJECT_LOCK (pad);
      fakeadec->segment.position = start_time;
      GST_OBJECT_UNLOCK (pad);
    }

    /* unref the buffer */
    gst_buffer_unref (buffer);
  } else {
    buffer = gst_buffer_make_writable (buffer);
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_CORRUPTED);
    GST_DEBUG_OBJECT (fakeadec, "Send packet");
    ret = gst_pad_push (fakeadec->srcpad, buffer);
  }
  GST_FAKEADEC_UNLOCK (fakeadec);

done:
  return ret;

flushing:
  {
    GST_DEBUG_OBJECT (pad, "We are flushing, discard buffer %p", buffer);
    gst_buffer_unref (buffer);
    ret = GST_FLOW_FLUSHING;
    goto done;
  }
}

static gboolean
gst_fakeadec_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean ret = FALSE;
  GstFakeAdec *fakeadec = GST_FAKEADEC (parent);

  GST_DEBUG_OBJECT (fakeadec, "got query : %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *prop = NULL;
      gboolean can_intersect = TRUE;

      gst_query_parse_accept_caps (query, &prop);
      GST_DEBUG_OBJECT (pad, "Intercepting Accept Caps query: %" GST_PTR_FORMAT,
          prop);

      if (fakeadec->curcaps) {
        GST_DEBUG_OBJECT (pad, "Current Caps: %" GST_PTR_FORMAT,
            fakeadec->curcaps);
        can_intersect = gst_caps_can_intersect (prop, fakeadec->curcaps);
      }

      GST_DEBUG_OBJECT (pad, "stream_change(%d), can_intersect(%d)",
          fakeadec->stream_change, can_intersect);
      if (fakeadec->stream_change || !can_intersect) {
        GST_DEBUG_OBJECT (pad, "Force to return false");
        gst_query_set_accept_caps_result (query, FALSE);
        ret = TRUE;
      } else {
        GST_DEBUG_OBJECT (pad, "Pass accept query");
        ret = gst_pad_query_default (pad, parent, query);
      }
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

  return ret;
}

static GstStateChangeReturn
gst_fakeadec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstFakeAdec *fakeadec = GST_FAKEADEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_FAKEADEC_LOCK (fakeadec);
      fakeadec->eos = FALSE;
      fakeadec->flushing = FALSE;
      GST_FAKEADEC_UNLOCK (fakeadec);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* first unlock before we call the parent state change function, which
       * tries to acquire the stream lock when going to ready. */
      GST_FAKEADEC_LOCK (fakeadec);
      fakeadec->eos = TRUE;
      fakeadec->flushing = TRUE;
      if (fakeadec->sync_streams) {
        GST_FAKEADEC_BROADCAST (fakeadec);
      }
      GST_FAKEADEC_UNLOCK (fakeadec);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:{
      if (fakeadec->active_mode) {
        GST_DEBUG_OBJECT (fakeadec,
            "Push gap event for preroll when changing state PLAYING to PAUSED");
        GstEvent *gap =
            gst_event_new_gap (fakeadec->segment.start, fakeadec->segment.stop);
        gst_pad_push_event (fakeadec->srcpad, gap);
      }
      break;
    }
    default:
      break;
  }
  return ret;

}
