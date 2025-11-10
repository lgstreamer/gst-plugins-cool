/* sample application for testing decproxy with playbin3
 *
 * Copyright 2016 LGE Corporation
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
#include <gst/cool/gstcool.h>
#include <stdlib.h>

typedef enum
{
  GST_PLAY_FLAG_VIDEO = (1 << 0),
  GST_PLAY_FLAG_AUDIO = (1 << 1),
  GST_PLAY_FLAG_TEXT = (1 << 2),
  GST_PLAY_FLAG_VIS = (1 << 3),
  GST_PLAY_FLAG_SOFT_VOLUME = (1 << 4),
  GST_PLAY_FLAG_NATIVE_AUDIO = (1 << 5),
  GST_PLAY_FLAG_NATIVE_VIDEO = (1 << 6),
  GST_PLAY_FLAG_DOWNLOAD = (1 << 7),
  GST_PLAY_FLAG_BUFFERING = (1 << 8),
  GST_PLAY_FLAG_DEINTERLACE = (1 << 9),
  GST_PLAY_FLAG_SOFT_COLORBALANCE = (1 << 10),
  GST_PLAY_FLAG_FORCE_FILTERS = (1 << 11),
  GST_PLAY_FLAG_NATIVE_TEXT = (1 << 12),
  GST_PLAY_FLAG_FORCE_AUDIO_CONVERT = (1 << 13)
} GstPlayFlags;

/* Global structure */

typedef struct _MyDataStruct
{
  GMainLoop *mainloop;
  GstElement *pipeline;
  GstBus *bus;

  /* Current collection */
  GstStreamCollection *collection;
  guint notify_id;

  gboolean resource_acquisition;

  GMutex sample_lock;

  gulong element_added_id;
  gulong element_removed_id;

  GList *my_splitters;

} MyDataStruct;

typedef struct _MySplitter
{
  GstElement *splitter;
  GstElement *appsink;

  gulong child_added_id;
  gulong child_removed_id;

  gulong sample_handler_id;
} MySplitter;

static void
dump_collection (GstStreamCollection * collection)
{
  guint i;
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
  }
}

static void
stream_notify_cb (GstStreamCollection * collection, GstStream * stream,
    GParamSpec * pspec, guint * val)
{
  if (g_str_equal (pspec->name, "caps")) {
    g_print ("\n Got stream-notify from stream %s (collection %p)\n",
        stream->stream_id, collection);
    GstCaps *caps = gst_stream_get_caps (stream);
    gchar *caps_str = gst_caps_to_string (caps);
    g_print (" New caps: %s\n", caps_str);
    g_free (caps_str);
    gst_caps_unref (caps);
  }
}

static GstFlowReturn
new_sample_cb (GstElement * sink, MyDataStruct * data)
{
  GstSample *sample = NULL;
  GstBuffer *buffer, *copy_buffer;
  gchar *stream_id;
  GstPad *pad;
  guint i;
  GstCaps *caps, *copy_caps;
  gboolean found_stream = NULL;

  g_mutex_lock (&data->sample_lock);

  g_signal_emit_by_name (sink, "pull-sample", &sample);
  buffer = gst_sample_get_buffer (sample);
  caps = gst_sample_get_caps (sample);

  /* make a copy and unref sample */
  copy_buffer = gst_buffer_copy (buffer);
  copy_caps = gst_caps_ref (caps);

  gst_sample_unref (sample);

  g_mutex_unlock (&data->sample_lock);

  pad = gst_element_get_static_pad (sink, "sink");
  stream_id = gst_pad_get_stream_id (pad);

  /* To do something for recording */
  for (i = 0; i < gst_stream_collection_get_size (data->collection); i++) {
    GstStream *stream = gst_stream_collection_get_stream (data->collection, i);
    if (!g_strcmp0 (gst_stream_get_stream_id (stream), stream_id)) {
      found_stream = TRUE;
    }
  }

  g_free (stream_id);
  gst_object_unref (pad);

  if (!found_stream)
    g_print
        ("Unexpected sample, stream-id (%s) is not in current collection \n",
        stream_id);
  GST_DEBUG ("Got ES Sample, stream_id (%s), caps: %" GST_PTR_FORMAT
      ", buffer: %" GST_PTR_FORMAT, stream_id, copy_caps, copy_buffer);

  gst_buffer_unref (copy_buffer);
  gst_caps_unref (copy_caps);

  return GST_FLOW_OK;
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
    case GST_MESSAGE_ASYNC_DONE:
    {
      g_printf ("Async-done\n");
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (data->pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, "async-done");
      break;
    }
    default:
      break;
  }

  return GST_BUS_PASS;
}

static gchar *
cmdline_to_uri (const gchar * arg)
{
  if (gst_uri_is_valid (arg))
    return g_strdup (arg);

  return gst_filename_to_uri (arg, NULL);
}

static MySplitter *
find_my_splitter (MyDataStruct * my_data, GstElement * splitter)
{
  GList *tmp;

  for (tmp = my_data->my_splitters; tmp; tmp = tmp->next) {
    MySplitter *my_splitter = (MySplitter *) tmp->data;
    if (my_splitter->splitter == splitter) {
      return my_splitter;
    }
  }
  return NULL;
}

static void
free_my_splitter (MySplitter * my_splitter)
{
  if (my_splitter->sample_handler_id > 0) {
    g_signal_handler_disconnect (my_splitter->appsink,
        my_splitter->sample_handler_id);
    my_splitter->sample_handler_id = 0;
  }
  if (my_splitter->child_added_id > 0) {
    g_signal_handler_disconnect (my_splitter->splitter,
        my_splitter->child_added_id);
    my_splitter->child_added_id = 0;
  }
  if (my_splitter->child_removed_id > 0) {
    g_signal_handler_disconnect (my_splitter->splitter,
        my_splitter->child_removed_id);
    my_splitter->child_removed_id = 0;
  }

  if (my_splitter->appsink) {
    gst_object_unref (my_splitter->appsink);
    my_splitter->appsink = NULL;
  }

  if (my_splitter->splitter) {
    gst_object_unref (my_splitter->splitter);
    my_splitter->splitter = NULL;
  }
}

static void
splitter_added_cb (GstElement * splitter, GstElement * child,
    MyDataStruct * my_data)
{
  gchar *elem_name = NULL;

  elem_name = gst_element_get_name (child);

  if (g_str_has_prefix (elem_name, "appsink")) {
    MySplitter *my_splitter = NULL;

    g_print ("splitter adds child element : %s\n", elem_name);
    my_splitter = find_my_splitter (my_data, splitter);
    if (!my_splitter) {
      g_print ("Could not find splitter!! \n");
      goto done;
    }

    my_splitter->appsink = gst_object_ref (child);
    my_splitter->sample_handler_id =
        g_signal_connect (child, "new-sample", G_CALLBACK (new_sample_cb),
        my_data);
  }

done:
  g_free (elem_name);
}

static void
splitter_removed_cb (GstElement * splitter, GstElement * child,
    MyDataStruct * my_data)
{
  gchar *elem_name = NULL;

  elem_name = gst_element_get_name (child);

  if (g_str_has_prefix (elem_name, "appsink")) {
    MySplitter *my_splitter = NULL;

    g_print ("splitter removes child element : %s\n", elem_name);
    my_splitter = find_my_splitter (my_data, splitter);
    if (!splitter) {
      g_print ("Could not find splitter!! \n");
      goto done;
    }

    if (my_splitter->sample_handler_id > 0) {
      g_signal_handler_disconnect (my_splitter->appsink,
          my_splitter->sample_handler_id);
      my_splitter->sample_handler_id = 0;
    }

    if (my_splitter->appsink) {
      gst_object_unref (my_splitter->appsink);
      my_splitter->appsink = NULL;
    }
  }

done:
  g_free (elem_name);
}

static void
pipeline_element_added_cb (GstBin * pipeline, GstBin * bin,
    GstElement * element, MyDataStruct * my_data)
{
  gchar *elem_name = NULL;

  elem_name = gst_element_get_name (element);

  /* Add smart properties to source element */
  if (g_str_has_prefix (elem_name, "source")) {
    GstStructure *s = NULL;
    g_print
        ("find source element : (%s) and set up splitappsink as fallback element \n",
        elem_name);
    s = gst_structure_new ("smart-properties", "fallback-element",
        G_TYPE_STRING, "splitappsink", NULL);
    g_object_set (element, "smart-properties", s, NULL);
    /* Adjust Q size and add signal handler to splitappsink */
  } else if (g_str_has_prefix (elem_name, "splitappsink")) {
    MySplitter *my_splitter = NULL;
    GstStreamType stream_type;

    g_object_get (element, "stream-type", &stream_type, NULL);
    if (stream_type == GST_STREAM_TYPE_VIDEO) {
      g_print ("Adjust Q size to 30 secs for video stream type \n");
      g_object_set (element, "max-size-time", 30 * GST_SECOND, NULL);
    }

    g_print ("Creating my_splitter for %s \n", elem_name);
    my_splitter = g_new0 (MySplitter, 1);

    my_splitter->splitter = gst_object_ref (element);
    my_splitter->child_added_id =
        g_signal_connect (element, "element-added",
        G_CALLBACK (splitter_added_cb), my_data);
    my_splitter->child_removed_id =
        g_signal_connect (element, "element-removed",
        G_CALLBACK (splitter_removed_cb), my_data);
    my_splitter->appsink = NULL;
    my_splitter->sample_handler_id = 0;

    /* Add to list of current my_splitter */
    my_data->my_splitters = g_list_append (my_data->my_splitters, my_splitter);
  }

  g_free (elem_name);
}

static void
pipeline_element_removed_cb (GstBin * pipeline, GstBin * bin,
    GstElement * element, MyDataStruct * my_data)
{
  gchar *elem_name = NULL;
  GList *tmp;

  elem_name = gst_element_get_name (element);

  if (g_str_has_prefix (elem_name, "splitappsink")) {
    for (tmp = my_data->my_splitters; tmp; tmp = tmp->next) {
      MySplitter *my_splitter = (MySplitter *) tmp->data;
      g_print ("Remove the %s \n", elem_name);
      my_data->my_splitters =
          g_list_remove (my_data->my_splitters, my_splitter);
      free_my_splitter (my_splitter);
    }
  }

  g_free (elem_name);
}

int
main (int argc, gchar ** argv)
{
  GstBus *bus;
  MyDataStruct *data;
  gchar *uri;
  gint flags;
  GList *tmp;

  gst_cool_init (&argc, &argv);

  data = g_new0 (MyDataStruct, 1);

  uri = cmdline_to_uri (argv[1]);

  if (argc < 2 || uri == NULL) {
    g_print ("Usage: %s URI\n", argv[0]);
    return 1;
  }

  data->pipeline = gst_element_factory_make ("playbin3", NULL);
  if (data->pipeline == NULL) {
    g_printerr ("Failed to create playbin element. Aborting");
    return 1;
  }

  gst_cool_playbin_init (data->pipeline);

  data->element_added_id = 0;
  data->element_removed_id = 0;

  /*
   * Hardware resources are not occupied by following mode.
   * 1) Whether to use decproxy bin or not.
   * 2) Whether to synchronize on fakesink or not.
   */
  gst_cool_playbin_no_resource_mode (data->pipeline, TRUE, TRUE);

  g_mutex_init (&data->sample_lock);

  g_object_set (data->pipeline, "uri", uri, NULL);
  g_free (uri);

  g_object_get (G_OBJECT (data->pipeline), "flags", &flags, NULL);
  flags |= GST_PLAY_FLAG_NATIVE_TEXT;
  flags &= ~(GST_PLAY_FLAG_TEXT);

  g_object_set (G_OBJECT (data->pipeline), "flags", flags, NULL);

  /* Handle other input if specified */
  if (argc > 2) {
    uri = cmdline_to_uri (argv[2]);
    if (uri != NULL) {
      g_object_set (data->pipeline, "suburi", uri, NULL);
      g_free (uri);
    } else {
      g_warning ("Could not parse auxilliary file argument. Ignoring");
    }
  }

  /* Add signal to get children elements */
  data->element_added_id =
      g_signal_connect (data->pipeline, "deep-element-added",
      G_CALLBACK (pipeline_element_added_cb), data);
  data->element_removed_id =
      g_signal_connect (data->pipeline, "deep-element-removed",
      G_CALLBACK (pipeline_element_removed_cb), data);

  data->mainloop = g_main_loop_new (NULL, FALSE);

  /* Put a bus handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (data->pipeline));
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) _on_bus_message, data,
      NULL);

  /* Start pipeline */
  gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
  g_main_loop_run (data->mainloop);

  gst_element_set_state (data->pipeline, GST_STATE_NULL);

  for (tmp = data->my_splitters; tmp; tmp = tmp->next) {
    MySplitter *my_splitter = (MySplitter *) tmp->data;
    data->my_splitters = g_list_remove (data->my_splitters, my_splitter);
    free_my_splitter (my_splitter);
  }

  g_list_free (data->my_splitters);
  data->my_splitters = NULL;

  /* Disconnect Signals */
  g_signal_handler_disconnect (data->pipeline, data->element_added_id);
  g_signal_handler_disconnect (data->pipeline, data->element_removed_id);

  g_list_free (data->my_splitters);
  data->my_splitters = NULL;

  g_mutex_clear (&data->sample_lock);
  gst_object_unref (data->pipeline);
  gst_object_unref (bus);

  return 0;
}
