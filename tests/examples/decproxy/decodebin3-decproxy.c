/* sample application for testing decodebin3
 *
 * Copyright 2015 LGE Corporation
 *  @author: HoonHee Lee <hoonhee.lee@lge.com>
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

#include <glib.h>
#include <glib-object.h>
#include <glib/gprintf.h>
#include <gst/gst.h>

/* Global structure */

typedef struct _MyDataStruct
{
  GMainLoop *mainloop;
  GstElement *pipeline;
  GstBus *demux_bus;

  GstElement *decodebin;

  GstElement *src;
  GList *other_src;
  GstElement *playsink;

  /* Current collection */
  GstStreamCollection *collection;
  guint notify_id;

  guint current_audio;
  guint current_video;
  guint current_text;

  glong timeout_id;

  gboolean resource_acquisition;
} MyDataStruct;

static void
print_tag_foreach (const GstTagList * tags, const gchar * tag,
    gpointer user_data)
{
  GValue val = { 0, };
  gchar *str;
  gint depth = GPOINTER_TO_INT (user_data);

  if (!gst_tag_list_copy_value (&val, tags, tag))
    return;

  if (G_VALUE_HOLDS_STRING (&val))
    str = g_value_dup_string (&val);
  else
    str = gst_value_serialize (&val);

  g_print ("%*s%s: %s\n", 2 * depth, " ", gst_tag_get_nick (tag), str);
  g_free (str);

  g_value_unset (&val);
}

static void
dump_collection (GstStreamCollection * collection)
{
  guint i;
  const GstTagList *tags;
  const GstCaps *caps;

  for (i = 0; i < gst_stream_collection_get_size (collection); i++) {
    GstStream *stream = gst_stream_collection_get_stream (collection, i);
    g_print (" Stream %u type %s flags 0x%x\n", i,
        gst_stream_type_get_name (gst_stream_get_stream_type (stream)),
        gst_stream_get_stream_flags (stream));
    g_print ("  ID: %s\n", gst_stream_get_stream_id (stream));

    caps = gst_stream_get_caps (stream);
    if (caps) {
      gchar *caps_str = gst_caps_to_string (caps);
      g_print ("  caps: %s\n", caps_str);
      g_free (caps_str);
    }

    tags = gst_stream_get_tags (stream);
    if (tags) {
      g_print ("  tags:\n");
      gst_tag_list_foreach (tags, print_tag_foreach, GUINT_TO_POINTER (3));
    }
  }
}

static gboolean
switch_streams (MyDataStruct * data)
{
  guint i, nb_streams;
  gint nb_video = 0, nb_audio = 0, nb_text = 0;
  GstStream *videos[256], *audios[256], *texts[256];
  GList *streams = NULL;
  GstEvent *ev;

  g_print ("Switching Streams...\n");

  /* Calculate the number of streams of each type */
  nb_streams = gst_stream_collection_get_size (data->collection);
  for (i = 0; i < nb_streams; i++) {
    GstStream *stream = gst_stream_collection_get_stream (data->collection, i);
    GstStreamType stype = gst_stream_get_stream_type (stream);
    if (stype == GST_STREAM_TYPE_VIDEO) {
      videos[nb_video] = stream;
      nb_video += 1;
    } else if (stype == GST_STREAM_TYPE_AUDIO) {
      audios[nb_audio] = stream;
      nb_audio += 1;
    } else if (stype == GST_STREAM_TYPE_TEXT) {
      texts[nb_text] = stream;
      nb_text += 1;
    }
  }

  if (nb_video) {
    data->current_video = (data->current_video + 1) % nb_video;
    streams =
        g_list_append (streams,
        (gchar *) gst_stream_get_stream_id (videos[data->current_video]));
    g_print ("  Selecting video channel #%d : %s\n", data->current_video,
        gst_stream_get_stream_id (videos[data->current_video]));
  }
  if (nb_audio) {
    data->current_audio = (data->current_audio + 1) % nb_audio;
    streams =
        g_list_append (streams,
        (gchar *) gst_stream_get_stream_id (audios[data->current_audio]));
    g_print ("  Selecting audio channel #%d : %s\n", data->current_audio,
        gst_stream_get_stream_id (audios[data->current_audio]));
  }
  if (nb_text) {
    data->current_text = (data->current_text + 1) % nb_text;
    streams =
        g_list_append (streams,
        (gchar *) gst_stream_get_stream_id (texts[data->current_text]));
    g_print ("  Selecting text channel #%d : %s\n", data->current_text,
        gst_stream_get_stream_id (texts[data->current_text]));
  }

  ev = gst_event_new_select_streams (streams);
  gst_element_send_event (data->pipeline, ev);

  return G_SOURCE_CONTINUE;
}

static void
stream_notify_cb (GstStreamCollection * collection, GstStream * stream,
    GParamSpec * pspec, guint * val)
{
  g_print ("Got stream-notify from stream %s for %s (collection %p)\n",
      stream->stream_id, pspec->name, collection);
  if (g_str_equal (pspec->name, "caps")) {
    GstCaps *caps = gst_stream_get_caps (stream);
    gchar *caps_str = gst_caps_to_string (caps);
    g_print (" New caps: %s\n", caps_str);
    g_free (caps_str);
    gst_caps_unref (caps);
  }
}

static GstBusSyncReply
_on_bus_message (GstBus * bus, GstMessage * message, MyDataStruct * data)
{
  GstObject *src = GST_MESSAGE_SRC (message);
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (message));
      gst_message_parse_error (message, &err, NULL);

      g_printerr ("ERROR: from element %s: %s\n", name, err->message);
      g_error_free (err);
      g_free (name);

      g_printf ("Stopping\n");
      g_main_loop_quit (data->mainloop);
      break;
    }
    case GST_MESSAGE_EOS:
      g_printf ("EOS ! Stopping \n");
      g_main_loop_quit (data->mainloop);
      break;
    case GST_MESSAGE_STREAM_COLLECTION:
    {
      GstStreamCollection *collection = NULL;
      gst_message_parse_stream_collection (message, &collection);
      if (collection) {
        g_printf ("Got a collection from %s:\n",
            src ? GST_OBJECT_NAME (src) : "Unknown");
        dump_collection (collection);
        if (data->collection && data->notify_id) {
          g_signal_handler_disconnect (data->collection, data->notify_id);
          data->notify_id = 0;
        }
        gst_object_replace ((GstObject **) & data->collection,
            (GstObject *) collection);
        if (data->collection) {
          data->notify_id =
              g_signal_connect (data->collection, "stream-notify",
              (GCallback) stream_notify_cb, data);
        }
        if (data->timeout_id == 0)
          /* In 5s try to change streams */
          data->timeout_id =
              g_timeout_add_seconds (5, (GSourceFunc) switch_streams, data);
      }
      break;
    }
    case GST_MESSAGE_STREAMS_SELECTED:
    {
      GstStreamCollection *collection = NULL;
      gst_message_parse_streams_selected (message, &collection);
      if (collection) {
        guint i, len;
        GST_DEBUG ("Got a STREAMS_SELECTED message from %s (seqnum:%"
            G_GUINT32_FORMAT "):\n", src ? GST_OBJECT_NAME (src) : "unknown",
            GST_MESSAGE_SEQNUM (message));
        len = gst_message_streams_selected_get_size (message);
        for (i = 0; i < len; i++) {
          GstStream *stream =
              gst_message_streams_selected_get_stream (message, i);
          GST_DEBUG ("  Stream #%d : %s\n", i,
              gst_stream_get_stream_id (stream));
          gst_object_unref (stream);
        }
        gst_object_unref (collection);
      }
      break;
    }
    case GST_MESSAGE_APPLICATION:
    {
      const GstStructure *msg_structure = gst_message_get_structure (message);

      if (!g_strrstr (gst_structure_get_name (msg_structure),
              "request-resource"))
        break;

      if (!data->resource_acquisition) {
        GstStructure *s;
        GstEvent *event;

        data->resource_acquisition = TRUE;
        s = gst_structure_new ("acquired-resource",
            "core-type", G_TYPE_STRING, "VDEC",
            "video-port", G_TYPE_INT, 0,
            "audio-port", G_TYPE_INT, 0, "mixer-port", G_TYPE_INT, -1,
            "active", G_TYPE_BOOLEAN, TRUE, NULL);

        event = gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM, s);
        gst_element_send_event (data->pipeline, event);
      }
      break;
    }
    default:
      break;
  }

  return GST_BUS_PASS;
}

static void
decodebin_pad_added_cb (GstElement * dbin, GstPad * pad, MyDataStruct * data)
{
  gchar *pad_name = gst_pad_get_name (pad);
  const gchar *sink_pad = NULL;

  GST_DEBUG_OBJECT (pad, "New pad ! Link to playsink !");
  if (!g_ascii_strncasecmp (pad_name, "video_", 6))
    sink_pad = "video_sink";
  else if (!g_ascii_strncasecmp (pad_name, "audio_", 6))
    sink_pad = "audio_sink";
  else if (!g_ascii_strncasecmp (pad_name, "text_", 5))
    sink_pad = "text_sink";
  else
    GST_WARNING_OBJECT (pad, "non audio/video/text pad");

  g_free (pad_name);

  if (sink_pad) {
    GstPad *playsink_pad;

    playsink_pad = gst_element_get_request_pad (data->playsink, sink_pad);
    if (playsink_pad)
      gst_pad_link (pad, playsink_pad);
  }
}

static void
pipeline_deep_element_added_cb (GstBin * bin, GstBin * sub_bin,
    GstElement * child, gpointer user_data)
{
  gchar *element_name = gst_element_get_name (child);
  gchar *subbin_name = gst_element_get_name (GST_ELEMENT_CAST (sub_bin));
  GstElementFactory *factory = gst_element_get_factory (child);

  if (g_str_has_prefix (element_name, "decodebin3")) {
    g_print ("%s is added in %s \n", element_name, subbin_name);
  } else if (g_str_has_prefix (element_name, "multiqueue") &&
      g_str_has_prefix (subbin_name, "decodebin3")) {
    /* Only concern multiqueue in decodebin3, and let's ingnore the other multiqueue
     * (e.g., in textbin) */
    g_print ("%s is added in %s \n", element_name, subbin_name);
  } else if (g_str_has_prefix (element_name, "decproxy")) {
    g_print ("%s is added in %s \n", element_name, subbin_name);
  }

  if (factory) {
    if (gst_element_factory_list_is_type (factory,
            GST_ELEMENT_FACTORY_TYPE_DECODER)
        && g_str_has_prefix (subbin_name, "decproxy")) {
      g_print ("%s is added in %s \n", element_name, subbin_name);
    } else if (gst_element_factory_list_is_type (factory,
            GST_ELEMENT_FACTORY_TYPE_SINK)
        && (g_str_has_prefix (subbin_name, "vbin")
            || g_str_has_prefix (subbin_name, "abin"))) {
      g_print ("%s is added in %s \n", element_name, subbin_name);
    }
  }

  g_free (element_name);
  g_free (subbin_name);
}

int
main (int argc, gchar ** argv)
{
  GError *error = NULL;
  GstBus *bus;
  MyDataStruct *data;
  int i;
  GstPluginFeature *feature;

  gst_init (&argc, &argv);

  feature = gst_registry_find_feature (gst_registry_get (), "decproxy",
      GST_TYPE_ELEMENT_FACTORY);
  gst_plugin_feature_set_rank (feature, 290);

  data = g_new0 (MyDataStruct, 1);

  if (argc < 2) {
    g_print ("Usage: decodebin3 URI\n");
    return 1;
  }

  data->pipeline = gst_pipeline_new ("pipeline");

  g_signal_connect (data->pipeline, "deep-element-added",
      G_CALLBACK (pipeline_deep_element_added_cb), NULL);

  data->decodebin = gst_element_factory_make ("decodebin3", NULL);

  data->src =
      gst_element_make_from_uri (GST_URI_SRC, argv[1], "source", &error);
  if (error) {
    g_printf ("pipeline could not be constructed: %s\n", error->message);
    g_error_free (error);
    return 1;
  }
  data->playsink = gst_element_factory_make ("playsink", NULL);

#if 0
  {
    GstElement *sink = gst_element_factory_make ("fakesink", NULL);
    g_object_set (sink, "sync", FALSE, NULL);
    g_object_set (data->playsink, "video-sink", sink, NULL);

    sink = gst_element_factory_make ("fakesink", NULL);
    g_object_set (sink, "sync", FALSE, NULL);
    g_object_set (data->playsink, "audio-sink", sink, NULL);
  }
#endif

  gst_bin_add_many ((GstBin *) data->pipeline, data->src, data->decodebin,
      data->playsink, NULL);
  if (!gst_element_link (data->src, (GstElement *) data->decodebin)) {
    g_printf ("Could not link source to demuxer\n");
    return 1;
  }

  /* Handle other inputs if specified */
  if (argc > 2) {
    for (i = 2; i < argc; i++) {
      GstElement *new_src =
          gst_element_make_from_uri (GST_URI_SRC, argv[i], NULL, &error);
      GstPad *src_pad, *sink_pad;
      if (error) {
        g_printf ("pipeline could not be constructed: %s\n", error->message);
        g_error_free (error);
        return 1;
      }
      data->other_src = g_list_append (data->other_src, new_src);
      gst_bin_add ((GstBin *) data->pipeline, new_src);
      src_pad = gst_element_get_static_pad (new_src, "src");
      sink_pad = gst_element_get_request_pad (data->decodebin, "sink_%u");
      if (gst_pad_link (src_pad, sink_pad) != GST_PAD_LINK_OK) {
        g_printf ("Could not link new source to decodebin : %s\n", argv[i]);
        return 1;
      }
    }
  }


  g_signal_connect (data->decodebin, "pad-added",
      (GCallback) decodebin_pad_added_cb, data);
  data->mainloop = g_main_loop_new (NULL, FALSE);

  /* Put a bus handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (data->pipeline));
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) _on_bus_message, data,
      NULL);

  /* Start pipeline */
  gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
  g_main_loop_run (data->mainloop);

  gst_element_set_state (data->pipeline, GST_STATE_NULL);

  gst_object_unref (data->pipeline);
  gst_object_unref (bus);

  return 0;
}
