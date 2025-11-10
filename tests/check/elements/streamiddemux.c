/* GStreamer unit tests for the streamiddemux
 *
 * Copyright 2013 LGE Corporation.
 *  @author: Hoonhee Lee <hoonhee.lee@lge.com>
 *  @author: Jeongseok Kim <jeongseok.kim@lge.com>
 *  @author: Wonchul Lee <wonchul86.lee@lge.com>
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

#define NUM_SUBSTREAMS 10
#define NUM_BUFFER 100

static GstPad *active_srcpad;

struct TestData
{
  GstElement *demux;
  GstPad *mysrc, *mysink[NUM_SUBSTREAMS];
  GstPad *demuxsink, *demuxsrc[NUM_SUBSTREAMS];
  gint srcpad_cnt;
  GstCaps *mycaps;
};

static void
set_active_srcpad (struct TestData *td)
{
  if (active_srcpad)
    gst_object_unref (active_srcpad);

  g_object_get (td->demux, "active-pad", &active_srcpad, NULL);
}

static void
release_test_objects (struct TestData *td)
{
  fail_unless (gst_element_set_state (td->demux, GST_STATE_NULL) ==
      GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (td->demuxsink);

  gst_caps_unref (td->mycaps);
  gst_object_unref (active_srcpad);

  gst_object_unref (td->demux);
}

static void
src_pad_added_cb (GstElement * demux, GstPad * pad, struct TestData *td)
{
  if (td->srcpad_cnt < NUM_SUBSTREAMS) {
    td->demuxsrc[td->srcpad_cnt] = pad;
    fail_unless (gst_pad_link (pad,
            td->mysink[td->srcpad_cnt++]) == GST_PAD_LINK_OK);
  }
}

static void
setup_test_objects (struct TestData *td)
{
  td->mycaps = gst_caps_new_empty_simple ("test/test");
  td->srcpad_cnt = 0;

  td->demux = gst_element_factory_make ("streamiddemux", NULL);
  fail_unless (td->demux != NULL);
  g_signal_connect (td->demux, "pad-added", G_CALLBACK (src_pad_added_cb), td);
  td->demuxsink = gst_element_get_static_pad (td->demux, "sink");
  fail_unless (td->demuxsink != NULL);

  fail_unless (gst_element_set_state (td->demux, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_SUCCESS);
}

static GstFlowReturn
chain_ok (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstPad *peer_pad = NULL;
  gchar *pad_stream_id, *active_srcpad_stream_id;

  peer_pad = gst_pad_get_peer (active_srcpad);
  pad_stream_id = gst_pad_get_stream_id (pad);
  active_srcpad_stream_id = gst_pad_get_stream_id (active_srcpad);
  fail_unless (pad == peer_pad);
  fail_unless (g_strcmp0 (pad_stream_id, active_srcpad_stream_id) == 0);

  g_free (pad_stream_id);
  g_free (active_srcpad_stream_id);
  gst_object_unref (peer_pad);
  gst_buffer_unref (buffer);

  return GST_FLOW_OK;
}

GST_START_TEST (test_streamiddemux_simple)
{
  struct TestData td;

  setup_test_objects (&td);

  GST_DEBUG ("Creating mysink");
  td.mysink[0] = gst_pad_new ("mysink0", GST_PAD_SINK);
  gst_pad_set_chain_function (td.mysink[0], chain_ok);
  gst_pad_set_active (td.mysink[0], TRUE);

  td.mysink[1] = gst_pad_new ("mysink1", GST_PAD_SINK);
  gst_pad_set_chain_function (td.mysink[1], chain_ok);
  gst_pad_set_active (td.mysink[1], TRUE);

  GST_DEBUG ("Creating mysrc");
  td.mysrc = gst_pad_new ("mysrc", GST_PAD_SRC);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (td.mysrc, td.demuxsink)));
  gst_pad_set_active (td.mysrc, TRUE);

  GST_DEBUG ("Pushing stream-start, caps and segment event");
  gst_check_setup_events_with_stream_id (td.mysrc, td.demux, td.mycaps,
      GST_FORMAT_BYTES, "test0");
  set_active_srcpad (&td);
  fail_unless (gst_pad_push (td.mysrc, gst_buffer_new ()) == GST_FLOW_OK);

  gst_check_setup_events_with_stream_id (td.mysrc, td.demux, td.mycaps,
      GST_FORMAT_BYTES, "test1");
  set_active_srcpad (&td);
  fail_unless (gst_pad_push (td.mysrc, gst_buffer_new ()) == GST_FLOW_OK);

  GST_DEBUG ("Pushing buffer");
  fail_unless (gst_pad_push_event (td.mysrc,
          gst_event_new_stream_start ("test0")));
  set_active_srcpad (&td);
  fail_unless (gst_pad_push (td.mysrc, gst_buffer_new ()) == GST_FLOW_OK);
  fail_unless (gst_pad_push (td.mysrc, gst_buffer_new ()) == GST_FLOW_OK);

  fail_unless (gst_pad_push_event (td.mysrc,
          gst_event_new_stream_start ("test1")));
  set_active_srcpad (&td);
  fail_unless (gst_pad_push (td.mysrc, gst_buffer_new ()) == GST_FLOW_OK);
  fail_unless (gst_pad_push (td.mysrc, gst_buffer_new ()) == GST_FLOW_OK);

  GST_DEBUG ("Releasing mysink and mysrc");
  gst_pad_set_active (td.mysink[0], FALSE);
  gst_pad_set_active (td.mysink[1], FALSE);
  gst_pad_set_active (td.mysrc, FALSE);

  gst_object_unref (td.mysink[0]);
  gst_object_unref (td.mysink[1]);
  gst_object_unref (td.mysrc);

  GST_DEBUG ("Releasing streamiddemux");
  release_test_objects (&td);
}

GST_END_TEST;

GST_START_TEST (test_streamiddemux_num_buffers)
{
  struct TestData td;
  gint buffer_cnt = 0;
  gint stream_cnt = 0;

  setup_test_objects (&td);

  GST_DEBUG ("Creating mysink");
  for (stream_cnt = 0; stream_cnt < NUM_SUBSTREAMS; ++stream_cnt) {
    gchar *name;
    name = g_strdup_printf ("mysink%d", stream_cnt);
    td.mysink[stream_cnt] = gst_pad_new (name, GST_PAD_SINK);
    g_free (name);
    gst_pad_set_chain_function (td.mysink[stream_cnt], chain_ok);
    gst_pad_set_active (td.mysink[stream_cnt], TRUE);
  }

  GST_DEBUG ("Creating mysrc");
  td.mysrc = gst_pad_new ("mysrc", GST_PAD_SRC);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (td.mysrc, td.demuxsink)));
  gst_pad_set_active (td.mysrc, TRUE);

  GST_DEBUG ("Pushing stream-start, caps and segment event");
  for (stream_cnt = 0; stream_cnt < NUM_SUBSTREAMS; ++stream_cnt) {
    gchar *name;
    name = g_strdup_printf ("test%d", stream_cnt);
    gst_check_setup_events_with_stream_id (td.mysrc, td.demux, td.mycaps,
        GST_FORMAT_BYTES, name);
    g_free (name);

    set_active_srcpad (&td);

    fail_unless (gst_pad_push (td.mysrc, gst_buffer_new ()) == GST_FLOW_OK);
  }

  GST_DEBUG ("Pushing buffers to random srcpad");
  for (buffer_cnt = 0; buffer_cnt < NUM_BUFFER; ++buffer_cnt) {
    gchar *name;
    gint active_stream = rand () % NUM_SUBSTREAMS;
    name = g_strdup_printf ("test%d", active_stream);
    gst_check_setup_events_with_stream_id (td.mysrc, td.demux, td.mycaps,
        GST_FORMAT_BYTES, name);
    g_free (name);

    set_active_srcpad (&td);

    fail_unless (gst_pad_push (td.mysrc, gst_buffer_new ()) == GST_FLOW_OK);
  }

  GST_DEBUG ("Releasing mysink and mysrc");
  for (stream_cnt = 0; stream_cnt < NUM_SUBSTREAMS; ++stream_cnt) {
    gst_pad_set_active (td.mysink[stream_cnt], FALSE);
  }
  gst_pad_set_active (td.mysrc, FALSE);

  for (stream_cnt = 0; stream_cnt < NUM_SUBSTREAMS; ++stream_cnt) {
    gst_object_unref (td.mysink[stream_cnt]);
  }
  gst_object_unref (td.mysrc);

  GST_DEBUG ("Releasing streamiddemux");
  release_test_objects (&td);
}

GST_END_TEST;

static GList *output_pads = NULL;

static void
list_srcpads (const GValue * item, GstElement * demux)
{
  GstPad *pad = g_value_get_object (item);
  output_pads = g_list_append (output_pads, pad);
}

static gboolean
stream_in_list (const gchar * sid)
{
  GList *tmp;

  for (tmp = output_pads; tmp; tmp = tmp->next) {
    GstPad *pad = (GstPad *) tmp->data;
    gchar *id = NULL;
    id = gst_pad_get_stream_id (pad);
    if (!g_strcmp0 (sid, id)) {
      g_free (id);
      return TRUE;
    }
    g_free (id);
  }

  return FALSE;
}

GST_START_TEST (test_streamiddemux_change_stream)
{
  GstElement *demux;
  GstPad *demux_sinkpad;
  GstPad *mysrc;
  GstStreamCollection *collection1, *collection2;
  GstStream *stream1, *stream2, *stream3;
  GstCaps *caps1, *caps2, *caps3;
  GstEvent *event;
  GstIterator *it = NULL;
  GstIteratorResult itret = GST_ITERATOR_OK;

  demux = gst_element_factory_make ("streamiddemux", NULL);
  fail_unless (demux != NULL);
  demux_sinkpad = gst_element_get_static_pad (demux, "sink");
  fail_unless (demux_sinkpad != NULL);

  fail_unless (gst_element_set_state (demux, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_SUCCESS);

  GST_DEBUG ("Creating mysrc");
  mysrc = gst_pad_new ("mysrc", GST_PAD_SRC);
  fail_unless (GST_PAD_LINK_SUCCESSFUL (gst_pad_link (mysrc, demux_sinkpad)));
  gst_pad_set_active (mysrc, TRUE);

  GST_DEBUG ("Pushing stream-start, caps and stream-collection");
  caps1 = gst_caps_from_string ("some/caps");
  stream1 = gst_stream_new ("stream-1", caps1, GST_STREAM_TYPE_TEXT, 0);
  collection1 = gst_stream_collection_new ("first collection");
  fail_unless (gst_stream_collection_add_stream (collection1, stream1));

  event = gst_event_new_stream_start ("stream-1");
  gst_event_set_stream (event, stream1);

  fail_unless (gst_pad_push_event (mysrc, event));
  fail_unless (gst_pad_push_event (mysrc, gst_event_new_caps (caps1)));
  fail_unless (gst_pad_push_event (mysrc,
          gst_event_new_stream_collection (collection1)));

  GST_DEBUG ("Pushing stream-start, caps and stream-collection2");
  caps2 = gst_caps_from_string ("some/caps2");
  stream2 = gst_stream_new ("stream-2", caps2, GST_STREAM_TYPE_TEXT, 0);
  caps3 = gst_caps_from_string ("some/caps3");
  stream3 = gst_stream_new ("stream-3", caps3, GST_STREAM_TYPE_TEXT, 0);

  collection2 = gst_stream_collection_new ("second collection");
  fail_unless (gst_stream_collection_add_stream (collection2, stream2));
  fail_unless (gst_stream_collection_add_stream (collection2, stream3));

  event = gst_event_new_stream_start ("stream-2");
  gst_event_set_stream (event, stream2);

  fail_unless (gst_pad_push_event (mysrc, event));
  fail_unless (gst_pad_push_event (mysrc, gst_event_new_caps (caps2)));
  fail_unless (gst_pad_push_event (mysrc,
          gst_event_new_stream_collection (collection2)));

  event = gst_event_new_stream_start ("stream-3");
  gst_event_set_stream (event, stream2);

  fail_unless (gst_pad_push_event (mysrc, event));
  fail_unless (gst_pad_push_event (mysrc, gst_event_new_caps (caps3)));
  fail_unless (gst_pad_push_event (mysrc,
          gst_event_new_stream_collection (collection2)));

  GST_DEBUG ("Checking to whether have valid streams");
  it = gst_element_iterate_src_pads (demux);
  while (itret == GST_ITERATOR_OK || itret == GST_ITERATOR_RESYNC) {
    itret =
        gst_iterator_foreach (it,
        (GstIteratorForeachFunction) list_srcpads, demux);
    if (itret == GST_ITERATOR_RESYNC)
      gst_iterator_resync (it);
  }
  gst_iterator_free (it);

  fail_if (stream_in_list ("stream-1"), "Has deativated stream");
  fail_unless (g_list_length (output_pads) == 2,
      "Invalid number of output pads");
  fail_unless (stream_in_list ("stream-2"), "Failed to get activated stream");
  fail_unless (stream_in_list ("stream-3"), "Failed to get activated stream");

  GST_DEBUG ("Releasing mysrc");
  gst_pad_set_active (mysrc, FALSE);
  gst_object_unref (mysrc);

  GST_DEBUG ("Releasing streamiddemux");
  fail_unless (gst_element_set_state (demux, GST_STATE_NULL) ==
      GST_STATE_CHANGE_SUCCESS);

  g_list_free (output_pads);
  output_pads = NULL;
  gst_object_unref (demux_sinkpad);
  gst_caps_unref (caps1);
  gst_caps_unref (caps2);
  gst_caps_unref (caps3);
  gst_object_unref (demux);
}

GST_END_TEST;

static Suite *
streamiddemux_suite (void)
{
  Suite *s = suite_create ("streamiddemux");
  TCase *tc_chain;

  tc_chain = tcase_create ("streamiddemux simple");
  tcase_add_test (tc_chain, test_streamiddemux_simple);
  tcase_add_test (tc_chain, test_streamiddemux_num_buffers);
  tcase_add_test (tc_chain, test_streamiddemux_change_stream);
  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (streamiddemux);
