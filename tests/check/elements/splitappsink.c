/* GStreamer unit tests for the splitappsink
 *
 * Copyright 2016 LGE Corporation.
 *  @author: Hoonhee Lee <hoonhee.lee@lge.com>
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

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <stdlib.h>

#define NUM_BUFFERS 4
#define NUM_STREAMS 2
static GCond waiting_cond;
static GMutex waiting_mutex;

#if 0
struct TestData
{
  GstElement *splitter;
  GstElement *appsink;
  GstStreamCollection *collection[2];
  GstStream *stream[2];
  GstCaps *caps[2];
  guint sample_count[2];
  gboolean is_stream_change;
  gchar *current_stream_id;
};
#endif

GST_START_TEST (test_simple)
{
  GstElement *splitter = NULL;
  GstElement *tee = NULL;
  GstIterator *it;
  GValue data = { 0, };
  gint nb_srcpads = 0;

  /* Make splitappsink bin */
  splitter = gst_check_setup_element ("splitappsink");
  fail_if (splitter == NULL);

  /* Check child elements. splitter should have one tee at initial */
  it = gst_bin_iterate_elements (GST_BIN (splitter));
  while (gst_iterator_next (it, &data) == GST_ITERATOR_OK) {
    GstElement *element = g_value_get_object (&data);
    gchar *name = gst_element_get_name (element);
    fail_unless (g_strrstr (name, "tee"));
    tee = element;
    g_free (name);
    g_value_reset (&data);
  }
  g_value_unset (&data);
  gst_iterator_free (it);

  /* Check number of srcpads of tee element */
  it = gst_element_iterate_src_pads (tee);
  while (gst_iterator_next (it, &data) == GST_ITERATOR_OK) {
    nb_srcpads++;
  }
  g_value_unset (&data);
  gst_iterator_free (it);

  fail_unless (nb_srcpads == 1);

  gst_object_unref (splitter);
}

GST_END_TEST;

GST_START_TEST (test_setup_appsink)
{
  GstPad *mysrc, *mysink, *sinkpad, *srcpad;
  GstElement *splitter, *appsink;
  gchar *name;
  GstEvent *event;

  splitter = gst_check_setup_element ("splitappsink");

  /* Link mysrc - splitter - mysink */
  mysrc = gst_pad_new ("mysrc", GST_PAD_SRC);
  sinkpad = gst_element_get_static_pad (splitter, "sink");
  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (mysrc, sinkpad)));
  gst_pad_set_active (mysrc, TRUE);
  gst_object_unref (sinkpad);

  mysink = gst_pad_new ("mysink", GST_PAD_SINK);
  srcpad = gst_element_get_static_pad (splitter, "src");
  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (srcpad, mysink)));
  gst_pad_set_active (mysink, TRUE);
  gst_object_unref (srcpad);

  fail_unless (gst_element_set_state (splitter, GST_STATE_PAUSED) ==
      GST_STATE_CHANGE_SUCCESS);

  /* Push stream-start event to configure appsink */
  event = gst_event_new_stream_start ("stream-id1");
  gst_pad_push_event (mysrc, event);

  /* Child appsink should be created */
  g_object_get (G_OBJECT (splitter), "appsink", &appsink, NULL);
  fail_unless (appsink);
  name = gst_element_get_name (appsink);
  fail_unless (g_strrstr (name, "appsink"));

  gst_pad_set_active (mysink, FALSE);
  gst_object_unref (mysink);
  gst_pad_set_active (mysrc, FALSE);
  gst_object_unref (mysrc);

  /* Release splitter */
  fail_unless (gst_element_set_state (splitter, GST_STATE_READY) ==
      GST_STATE_CHANGE_SUCCESS);

  fail_unless (gst_element_set_state (splitter, GST_STATE_NULL) ==
      GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (appsink);
  gst_object_unref (splitter);

  g_free (name);
}

GST_END_TEST;

static void
handoff (GstElement * fakesink, GstBuffer * buf, GstPad * pad, guint * count)
{
  *count = *count + 1;
}

static GstFlowReturn
new_sample_cb (GstElement * sink, guint * count)
{
  GstSample *sample = NULL;

  g_signal_emit_by_name (sink, "pull-sample", &sample);

  if (sample != NULL) {
    *count = *count + 1;
    gst_sample_unref (sample);
  }

  if (*count == NUM_BUFFERS) {
    g_mutex_lock (&waiting_mutex);
    g_cond_signal (&waiting_cond);
    g_mutex_unlock (&waiting_mutex);
  }

  return GST_FLOW_OK;
}

static void
splitter_added_cb (GstElement * splitter, GstElement * child, guint * count)
{
  gchar *elem_name = NULL;

  elem_name = gst_element_get_name (child);

  if (g_str_has_prefix (elem_name, "appsink")) {
    g_signal_connect (child, "new-sample", G_CALLBACK (new_sample_cb), count);
  }

  g_free (elem_name);
}

/* construct fakesrc num-buffers=4 ! splitappsink name=split ! queue ! fakesink split.
 * Each fakesink should exactly receive 4 buffers.
 */
GST_START_TEST (test_num_samples)
{
  GstElement *pipeline, *src, *splitter, *queue, *sink;
  guint buffer_count, sample_count;
  GstBus *bus;
  GstMessage *msg;

  g_mutex_init (&waiting_mutex);
  g_cond_init (&waiting_cond);
  buffer_count = sample_count = 0;

  /* Construct pipeline: fakesrc - splitter - queue - fakesink */
  pipeline = gst_pipeline_new ("pipeline");
  src = gst_check_setup_element ("fakesrc");
  g_object_set (src, "num-buffers", NUM_BUFFERS, NULL);
  splitter = gst_check_setup_element ("splitappsink");
  queue = gst_check_setup_element ("queue");
  sink = gst_check_setup_element ("fakesink");

  g_signal_connect (splitter, "element-added", G_CALLBACK (splitter_added_cb),
      &sample_count);

  /* Check count of buffers on fakesink */
  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "handoff", (GCallback) handoff, &buffer_count);

  fail_unless (gst_bin_add (GST_BIN (pipeline), src));
  fail_unless (gst_bin_add (GST_BIN (pipeline), splitter));
  fail_unless (gst_bin_add (GST_BIN (pipeline), queue));
  fail_unless (gst_bin_add (GST_BIN (pipeline), sink));

  fail_unless (gst_element_link_many (src, splitter, queue, sink, NULL));

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  fail_if (bus == NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_EOS);
  gst_message_unref (msg);

  /* Wait until all sample data has been delivered. */
  g_mutex_lock (&waiting_mutex);
  while (sample_count != NUM_BUFFERS)
    g_cond_wait (&waiting_cond, &waiting_mutex);
  g_mutex_unlock (&waiting_mutex);

  /* Check number of buffers */
  fail_unless_equals_int (buffer_count, NUM_BUFFERS);
  fail_unless_equals_int (sample_count, NUM_BUFFERS);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (bus);

  gst_object_unref (pipeline);

  g_mutex_clear (&waiting_mutex);
  g_cond_clear (&waiting_cond);
}

GST_END_TEST;

#if 0
static GstFlowReturn
chain_ok (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  gst_buffer_unref (buffer);
  return GST_FLOW_OK;
}

static gboolean
query_ok (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ACCEPT_CAPS:
    {
      gst_query_set_accept_caps_result (query, TRUE);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

  return ret;
}

static gboolean
check_nth_stream_is_valid (struct TestData *td, guint nth_stream,
    GstSample * sample)
{
  GstStream *current_stream = NULL;
  GstCaps *caps, *sample_caps;
  GstSegment *sample_segment = NULL;
  gboolean ret = TRUE;
  guint i = 0;

  /* Get GstStream */
  for (i = 0; i < gst_stream_collection_get_size (td->collection[nth_stream]);
      i++) {
    GstStream *stream =
        gst_stream_collection_get_stream (td->collection[nth_stream], i);
    if (!g_strcmp0 (gst_stream_get_stream_id (stream), td->current_stream_id)) {
      current_stream = stream;
      break;
    }
  }

  sample_caps = gst_sample_get_caps (sample);
  sample_segment = gst_sample_get_segment (sample);
  caps = gst_stream_get_caps (current_stream);

  /* Check caps and segment with sample */
  fail_unless (gst_caps_is_equal (caps, sample_caps));
  fail_unless (sample_segment->start == (nth_stream + 1) * GST_SECOND);

  gst_caps_unref (caps);

  return ret;
}

static GstFlowReturn
new_sample_cb_2 (GstElement * sink, struct TestData *td)
{
  GstSample *sample = NULL;

  g_signal_emit_by_name (sink, "pull-sample", &sample);

  if (sample != NULL) {
    GstPad *sinkpad = NULL;
    gchar *stream_id = NULL;

    sinkpad = gst_element_get_static_pad (sink, "sink");
    stream_id = gst_pad_get_stream_id (sinkpad);

    if (td->current_stream_id == NULL) {
      td->current_stream_id = g_strdup (stream_id);
    } else if (g_strcmp0 (td->current_stream_id, stream_id)) {
      GST_DEBUG ("Handle stream changes (%s => %s) !", td->current_stream_id,
          stream_id);
      g_free (td->current_stream_id);
      td->current_stream_id = g_strdup (stream_id);
      td->is_stream_change = TRUE;
    }

    /* Check First Stream */
    if (!td->is_stream_change) {
      if (check_nth_stream_is_valid (td, 0, sample))
        td->sample_count[0] = td->sample_count[0] + 1;
      /* Check Second Stream */
    } else {
      if (check_nth_stream_is_valid (td, 1, sample))
        td->sample_count[1] = td->sample_count[1] + 1;
    }

    g_free (stream_id);
    gst_object_unref (sinkpad);
    gst_sample_unref (sample);
  }

  if (td->sample_count[0] == NUM_STREAMS && td->sample_count[1] == NUM_STREAMS) {
    g_mutex_lock (&waiting_mutex);
    g_cond_signal (&waiting_cond);
    g_mutex_unlock (&waiting_mutex);
  }

  return GST_FLOW_OK;
}

static void
sink_setup_3 (GstElement * splitter, GstElement * appsink,
    const gchar * stream_id, struct TestData *td)
{
  GST_LOG ("sink-setup called, sink = %s, stream-id = %s",
      G_OBJECT_TYPE_NAME (appsink), stream_id);

  td->appsink = gst_object_ref (appsink);
  g_signal_connect (td->appsink, "new-sample", G_CALLBACK (new_sample_cb_2),
      td);
}

static void
sink_removed_3 (GstElement * splitter, const gchar * stream_id,
    struct TestData *td)
{
  GST_LOG ("sink removed, stream-id = %s", stream_id);

  g_object_unref (td->appsink);
  td->appsink = NULL;
}

GST_START_TEST (test_stream_change)
{
  GstPad *mysrc, *mysink, *sinkpad, *srcpad;
  GstSegment segment;
  GstBuffer *buffer;
  struct TestData td;
  guint i;

  g_mutex_init (&waiting_mutex);
  g_cond_init (&waiting_cond);
  td.sample_count[0] = td.sample_count[1] = 0;
  td.current_stream_id = NULL;
  td.is_stream_change = FALSE;
  td.appsink = NULL;

  td.splitter = gst_check_setup_element ("splitappsink");
  g_signal_connect (td.splitter, "sink-setup", G_CALLBACK (sink_setup_3), &td);
  g_signal_connect (td.splitter, "sink-removed", G_CALLBACK (sink_removed_3),
      &td);

  /* Create 2 streamcollections */
  for (i = 0; i < NUM_STREAMS; i++) {
    gchar *stream_id, *caps, *collection;
    stream_id = g_strdup_printf ("stream-id%d", i);
    caps = g_strdup_printf ("some/video-caps%d", i);
    collection = g_strdup_printf ("Collection%d", i);

    td.caps[i] = gst_caps_from_string (caps);
    td.stream[i] =
        gst_stream_new (stream_id, td.caps[i], GST_STREAM_TYPE_VIDEO, 0);
    td.collection[i] = gst_stream_collection_new (collection);
    fail_unless (gst_stream_collection_add_stream (td.collection[i],
            td.stream[i]));

    g_free (collection);
    g_free (caps);
    g_free (stream_id);
  }

  /* Link mysrc - splitter - mysink */
  mysrc = gst_pad_new ("mysrc", GST_PAD_SRC);
  sinkpad = gst_element_get_static_pad (td.splitter, "sink");
  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (mysrc, sinkpad)));
  gst_pad_set_active (mysrc, TRUE);
  gst_object_unref (sinkpad);

  mysink = gst_pad_new ("mysink", GST_PAD_SINK);
  srcpad = gst_element_get_static_pad (td.splitter, "src");
  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (srcpad, mysink)));
  gst_pad_set_chain_function (mysink, chain_ok);
  /* Intercept 'accept-caps' query to cope changing caps */
  gst_pad_set_query_function (mysink, query_ok);
  gst_pad_set_active (mysink, TRUE);
  gst_object_unref (srcpad);

  fail_unless (gst_element_set_state (td.splitter, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_SUCCESS);

  GST_DEBUG ("Pushing stream-start, caps, segment and 2 buffers");
  for (i = 0; i < NUM_STREAMS; i++) {
    GstEvent *event = NULL;

    event =
        gst_event_new_stream_start (gst_stream_get_stream_id (td.stream[i]));
    gst_event_set_stream (event, td.stream[i]);
    gst_pad_push_event (mysrc, event);

    gst_pad_set_caps (mysrc, td.caps[i]);

    gst_segment_init (&segment, GST_FORMAT_TIME);
    segment.start = (i + 1) * GST_SECOND;
    gst_pad_push_event (mysrc, gst_event_new_segment (&segment));

    buffer = gst_buffer_new_and_alloc (4);
    fail_unless (gst_pad_push (mysrc, buffer) == GST_FLOW_OK);

    buffer = gst_buffer_new_and_alloc (4);
    fail_unless (gst_pad_push (mysrc, buffer) == GST_FLOW_OK);
  }

  /* Wait until all sample data has been delivered. */
  g_mutex_lock (&waiting_mutex);
  while (!(td.sample_count[0] == NUM_STREAMS
          && td.sample_count[1] == NUM_STREAMS))
    g_cond_wait (&waiting_cond, &waiting_mutex);
  g_mutex_unlock (&waiting_mutex);

  /* Release */
  gst_pad_set_active (mysink, FALSE);
  gst_object_unref (mysink);
  gst_pad_set_active (mysrc, FALSE);
  gst_object_unref (mysrc);

  fail_unless (gst_element_set_state (td.splitter, GST_STATE_NULL) ==
      GST_STATE_CHANGE_SUCCESS);

  for (i = 0; i < NUM_STREAMS; i++) {
    gst_caps_unref (td.caps[i]);
    gst_object_unref (td.collection[i]);
  }
  g_free (td.current_stream_id);
  gst_object_unref (td.splitter);

  g_mutex_clear (&waiting_mutex);
  g_cond_clear (&waiting_cond);
}

GST_END_TEST;
#endif

#if 0
/* Fake parser/decoder for parser_negotiation test */
static GType gst_fake_h264_parser_get_type (void);
static GType gst_fake_h264_decoder_get_type (void);

#undef parent_class
#define parent_class fake_h264_parser_parent_class
typedef struct _GstFakeH264Parser GstFakeH264Parser;
typedef GstElementClass GstFakeH264ParserClass;

struct _GstFakeH264Parser
{
  GstElement parent;
};

G_DEFINE_TYPE (GstFakeH264Parser, gst_fake_h264_parser, GST_TYPE_ELEMENT);

static void
gst_fake_h264_parser_class_init (GstFakeH264ParserClass * klass)
{
  static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-h264"));
  static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-h264, "
          "stream-format=(string) { avc, byte-stream }"));
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_templ));
  gst_element_class_set_metadata (element_class,
      "FakeH264Parser", "Codec/Parser/Converter/Video", "yep", "me");
}

static gboolean
gst_fake_h264_parser_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstElement *self = GST_ELEMENT (parent);
  GstPad *otherpad = gst_element_get_static_pad (self, "src");
  GstCaps *accepted_caps;
  GstStructure *s;
  const gchar *stream_format;
  gboolean ret = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
      accepted_caps = gst_pad_get_allowed_caps (otherpad);
      accepted_caps = gst_caps_truncate (accepted_caps);

      s = gst_caps_get_structure (accepted_caps, 0);
      stream_format = gst_structure_get_string (s, "stream-format");
      if (!stream_format)
        gst_structure_set (s, "stream-format", G_TYPE_STRING, "avc", NULL);

      gst_pad_set_caps (otherpad, accepted_caps);
      gst_caps_unref (accepted_caps);
      gst_event_unref (event);
      event = NULL;
      break;
    default:
      break;
  }

  if (event)
    ret = gst_pad_push_event (otherpad, event);
  gst_object_unref (otherpad);

  return ret;
}

static GstFlowReturn
gst_fake_h264_parser_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstElement *self = GST_ELEMENT (parent);
  GstPad *otherpad = gst_element_get_static_pad (self, "src");
  GstFlowReturn ret = GST_FLOW_OK;

  buf = gst_buffer_make_writable (buf);

  ret = gst_pad_push (otherpad, buf);

  gst_object_unref (otherpad);

  return ret;
}

static void
gst_fake_h264_parser_init (GstFakeH264Parser * self)
{
  GstPad *pad;

  pad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_GET_CLASS (self), "sink"), "sink");
  gst_pad_set_event_function (pad, gst_fake_h264_parser_sink_event);
  gst_pad_set_chain_function (pad, gst_fake_h264_parser_sink_chain);
  gst_element_add_pad (GST_ELEMENT (self), pad);

  pad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_GET_CLASS (self), "src"), "src");
  gst_element_add_pad (GST_ELEMENT (self), pad);
}

#undef parent_class
#define parent_class fake_h264_decoder_parent_class
typedef struct _GstFakeH264Decoder GstFakeH264Decoder;
typedef GstElementClass GstFakeH264DecoderClass;

struct _GstFakeH264Decoder
{
  GstElement parent;
};

G_DEFINE_TYPE (GstFakeH264Decoder, gst_fake_h264_decoder, GST_TYPE_ELEMENT);

static void
gst_fake_h264_decoder_class_init (GstFakeH264DecoderClass * klass)
{
  static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-h264, " "stream-format=(string) byte-stream"));
  static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-raw"));
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_templ));
  gst_element_class_set_metadata (element_class,
      "FakeH264Decoder", "Codec/Decoder/Video", "yep", "me");
}

static gboolean
gst_fake_h264_decoder_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstElement *self = GST_ELEMENT (parent);
  GstPad *otherpad = gst_element_get_static_pad (self, "src");
  GstCaps *caps;
  gboolean ret = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
      caps = gst_caps_new_empty_simple ("video/x-raw");
      gst_pad_set_caps (otherpad, caps);
      gst_caps_unref (caps);
      gst_event_unref (event);
      event = NULL;
      break;
    default:
      break;
  }

  if (event)
    ret = gst_pad_push_event (otherpad, event);
  gst_object_unref (otherpad);

  return ret;
}

static GstFlowReturn
gst_fake_h264_decoder_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstElement *self = GST_ELEMENT (parent);
  GstPad *otherpad = gst_element_get_static_pad (self, "src");
  GstFlowReturn ret = GST_FLOW_OK;

  buf = gst_buffer_make_writable (buf);

  ret = gst_pad_push (otherpad, buf);

  gst_object_unref (otherpad);

  return ret;
}

static void
gst_fake_h264_decoder_init (GstFakeH264Decoder * self)
{
  GstPad *pad;

  pad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_GET_CLASS (self), "sink"), "sink");
  gst_pad_set_event_function (pad, gst_fake_h264_decoder_sink_event);
  gst_pad_set_chain_function (pad, gst_fake_h264_decoder_sink_chain);
  gst_element_add_pad (GST_ELEMENT (self), pad);

  pad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_GET_CLASS (self), "src"), "src");
  gst_element_add_pad (GST_ELEMENT (self), pad);
}

static void
parser_negotiation_pad_added_cb (GstElement * dec, GstPad * pad,
    gpointer user_data)
{
  GstBin *pipe = user_data;
  GstElement *sink;
  GstPad *sinkpad;

  sink = gst_element_factory_make ("fakesink", NULL);
  gst_bin_add (pipe, sink);
  gst_element_sync_state_with_parent (sink);
  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_link (pad, sinkpad);
  gst_object_unref (sinkpad);
}

static void
pipeline_deep_element_added_cb (GstBin * bin, GstBin * sub_bin,
    GstElement * child, gboolean * have_splitter)
{
  gchar *element_name = gst_element_get_name (child);
  gchar *subbin_name = gst_element_get_name (GST_ELEMENT_CAST (sub_bin));

  /* Detect SplitAppsink */
  if (g_str_has_prefix (element_name, "splitappsink") &&
      g_str_has_prefix (subbin_name, "parsebin")) {
    g_print ("%s is added in %s \n", element_name, subbin_name);
    *have_splitter = TRUE;
  }

  g_free (element_name);
  g_free (subbin_name);
}

static GstBusSyncReply
_on_bus_message (GstBus * bus, GstMessage * message, GMainLoop * loop)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:{
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_APPLICATION:
    {
      const GstStructure *s = gst_message_get_structure (message);

      if (g_strrstr (gst_structure_get_name (s), "es-sample")) {
        GstSample *sample = NULL;
        const GValue *v_sample;

        v_sample = gst_structure_get_value (s, "sample");
        sample = gst_value_get_sample (v_sample);

        gst_sample_unref (sample);
      }

      break;
    }
    default:
      break;
  }

  return GST_BUS_PASS;
}

/*
 * Fakesrc - capsfilter - decodebin3 - fakesink
 */
GST_START_TEST (test_negotiation_in_decodebin)
{
  GstStateChangeReturn sret;
  GstCaps *caps;
  GstElement *pipe, *src, *filter, *dec;
  gboolean have_splitter = FALSE;
  GstStructure *s;
  GstBus *bus;
  GMainLoop *mainloop;

  gst_element_register (NULL, "fakeh264parse", GST_RANK_PRIMARY + 101,
      gst_fake_h264_parser_get_type ());
  gst_element_register (NULL, "fakeh264dec", GST_RANK_PRIMARY + 100,
      gst_fake_h264_decoder_get_type ());

  pipe = gst_pipeline_new (NULL);

  src = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (src != NULL);
  g_object_set (G_OBJECT (src), "num-buffers", 5, "sizetype", 2, "filltype", 2,
      "can-activate-pull", FALSE, NULL);

  /* Set up fallback element using smart-properties */
  s = gst_structure_new ("smart-properties", "fallback-element", G_TYPE_STRING,
      "splitappsink", NULL);
  g_object_set (G_OBJECT (src), "smart-properties", s, NULL);

  filter = gst_element_factory_make ("capsfilter", NULL);
  fail_unless (filter != NULL);
  caps = gst_caps_from_string ("video/x-h264");
  g_object_set (G_OBJECT (filter), "caps", caps, NULL);
  gst_caps_unref (caps);

  dec = gst_element_factory_make ("decodebin3", NULL);
  fail_unless (dec != NULL);

  /* Add signal to get splitappsink */
  g_signal_connect (dec, "deep-element-added",
      G_CALLBACK (pipeline_deep_element_added_cb), &have_splitter);

  g_signal_connect (dec, "pad-added",
      G_CALLBACK (parser_negotiation_pad_added_cb), pipe);

  gst_bin_add_many (GST_BIN (pipe), src, filter, dec, NULL);
  gst_element_link_many (src, filter, dec, NULL);

  mainloop = g_main_loop_new (NULL, FALSE);

  /* Put a bus handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipe));
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) _on_bus_message, mainloop,
      NULL);

  sret = gst_element_set_state (pipe, GST_STATE_PLAYING);
  fail_unless_equals_int (sret, GST_STATE_CHANGE_ASYNC);
  g_main_loop_run (mainloop);

  /* Check splitter is configured */
  fail_unless (have_splitter);

  //GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipe), GST_DEBUG_GRAPH_SHOW_ALL,
  //    "negotiation_in_decodebin3");

  gst_element_set_state (pipe, GST_STATE_NULL);

  gst_object_unref (pipe);
  gst_object_unref (bus);
}

GST_END_TEST;
#endif

static Suite *
splitappsink_suite (void)
{
  Suite *s = suite_create ("splitappsink");
  TCase *tc_chain;

  tc_chain = tcase_create ("splitappsink simple");
  tcase_add_test (tc_chain, test_simple);
  tcase_add_test (tc_chain, test_setup_appsink);
  tcase_add_test (tc_chain, test_num_samples);
  //tcase_add_test (tc_chain, test_stream_change);
  //tcase_add_test (tc_chain, test_negotiation_in_decodebin);

  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (splitappsink);
