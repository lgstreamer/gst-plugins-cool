/* GStreamer unit tests for the streamiddemux
 *
 * Copyright 2017 LGE Corporation.
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
      GST_STATIC_CAPS ("video/x-h264"));
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
      caps =
          gst_caps_from_string
          ("video/x-raw,format=(string)I420,width=20,height=20");
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

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw;video/x-raw(ANY)"));

static guint nb_caps;

static gboolean
event_func (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GST_DEBUG ("Got event: %" GST_PTR_FORMAT, event);
  if (GST_EVENT_TYPE (event) == GST_EVENT_CAPS) {
    nb_caps++;
  }
  gst_event_unref (event);
  return TRUE;
}

static GstFlowReturn
chain_ok (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  gst_buffer_unref (buffer);
  return GST_FLOW_OK;
}

GST_START_TEST (test_simple)
{
  GstElement *testbin, *front_bridge, *back_bridge, *decoder;
  GstPad *mysrc, *mysink, *gpad, *sinkpad, *srcpad;
  GstPadTemplate *pad_tmpl;
  GstEvent *event;
  GstCaps *caps;
  GstSegment segment;
  GstBuffer *buffer;

  gst_element_register (NULL, "fakeh264dec", GST_RANK_PRIMARY + 100,
      gst_fake_h264_decoder_get_type ());

  nb_caps = 0;

  testbin = gst_bin_new ("testbin");

  front_bridge = gst_element_factory_make ("decbridge", NULL);
  fail_unless (front_bridge != NULL);

  back_bridge = gst_element_factory_make ("decbridge", NULL);
  fail_unless (back_bridge != NULL);

  decoder = gst_element_factory_make ("fakeh264dec", NULL);
  fail_unless (decoder != NULL);

  gst_bin_add_many (GST_BIN (testbin), front_bridge, decoder, back_bridge,
      NULL);

  /* Link mysrc - testbin [front bridge - decoder - back bridge] - mysink */
  sinkpad = gst_element_get_static_pad (front_bridge, "sink");
  pad_tmpl = gst_static_pad_template_get (&sink_template);
  gpad = gst_ghost_pad_new_from_template ("sink", sinkpad, pad_tmpl);
  gst_pad_set_active (gpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (testbin), gpad);
  gst_object_unref (pad_tmpl);
  gst_object_unref (sinkpad);

  mysrc = gst_pad_new ("mysrc", GST_PAD_SRC);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (mysrc, gpad)));
  gst_pad_set_active (mysrc, TRUE);

  srcpad = gst_element_get_static_pad (back_bridge, "src");
  pad_tmpl = gst_static_pad_template_get (&src_template);
  gpad = gst_ghost_pad_new_from_template ("src", srcpad, pad_tmpl);
  gst_pad_set_active (gpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (testbin), gpad);
  gst_object_unref (pad_tmpl);
  gst_object_unref (srcpad);

  mysink = gst_pad_new ("mysink", GST_PAD_SINK);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (gpad, mysink)));
  gst_pad_set_event_function (mysink, event_func);
  gst_pad_set_chain_function (mysink, chain_ok);
  gst_pad_set_active (mysink, TRUE);

  gst_element_link_many (front_bridge, decoder, back_bridge, NULL);

  fail_unless (gst_element_set_state (GST_ELEMENT (testbin),
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS);

  //GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (testbin),
  //    GST_DEBUG_GRAPH_SHOW_ALL, "decbridge");

  /* Push stream-start, caps, segment and buffer */
  event = gst_event_new_stream_start ("test0");
  gst_pad_push_event (mysrc, event);

  caps = gst_caps_from_string ("video/x-h264");
  gst_pad_set_caps (mysrc, caps);
  /* Caps event should be passed to mysink pad */
  fail_unless_equals_int (nb_caps, 1);
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  gst_pad_push_event (mysrc, gst_event_new_segment (&segment));

  buffer = gst_buffer_new_and_alloc (4);
  fail_unless (gst_pad_push (mysrc, buffer) == GST_FLOW_OK);

  /* Caps event should not be passed to mysink pad if it is same */
  caps = gst_caps_from_string ("video/x-h264");
  gst_pad_set_caps (mysrc, caps);
  fail_unless_equals_int (nb_caps, 1);
  gst_caps_unref (caps);

  /* Caps event shluld be passed to mysink pad if stream is changed */
  event = gst_event_new_stream_start ("test1");
  gst_pad_push_event (mysrc, event);

  caps = gst_caps_from_string ("video/x-h264");
  gst_pad_set_caps (mysrc, caps);
  fail_unless_equals_int (nb_caps, 2);
  gst_caps_unref (caps);

  fail_unless (gst_element_set_state (GST_ELEMENT (testbin), GST_STATE_NULL) ==
      GST_STATE_CHANGE_SUCCESS);

  gst_pad_set_active (mysink, FALSE);
  gst_pad_set_active (mysrc, FALSE);

  gst_object_unref (mysink);
  gst_object_unref (mysrc);
  gst_object_unref (testbin);
}

GST_END_TEST static Suite *
decbridge_suite (void)
{
  Suite *s = suite_create ("decbridge");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_simple);

  return s;
}

GST_CHECK_MAIN (decbridge);
