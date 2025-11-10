/* GStreamer
 *
 * dynappsrc-playbin3.c: example for using dynappsrc in playbin3.
 *
 * Copyright (C) 2016 HoonHee Lee <hoonhee.lee@lge.com>
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

#include <gst/gst.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

GST_DEBUG_CATEGORY (dynappsrc_playbin3_debug);
#define GST_CAT_DEFAULT dynappsrc_playbin3_debug

/*
 * an example application of using appsrc in streaming push mode. We simply push
 * buffers into appsrc. The size of the buffers we push can be any size we
 * choose.
 *
 * This example is very close to how one would deal with a streaming webserver
 * that does not support range requests or does not report the total file size.
 *
 * Some optimisations are done so that we don't push too much data. We connect
 * to the need-data and enough-data signals to start/stop sending buffers.
 *
 * Appsrc in streaming mode (the default) does not support seeking so we don't
 * have to handle any seek callbacks.
 *
 * Some formats are able to estimate the duration of the media file based on the
 * file length (mp3, mpeg,..), others report an unknown length (ogg,..).
 */
typedef struct _App App;

struct _App
{
  GstElement *pipeline;

  GstElement *appsrc;
  guint sourceid;

  GMappedFile *file;
  guint8 *data;
  gsize length;
  guint64 offset;

  GstStreamCollection *collection;
  guint notify_id;
};

App v_app;
App a_app;

#define CHUNK_SIZE  4096

/* This method is called by the idle GSource in the mainloop. We feed CHUNK_SIZE
 * bytes into appsrc.
 * The ide handler is added to the mainloop when appsrc requests us to start
 * sending data (need-data signal) and is removed when appsrc has enough data
 * (enough-data signal).
 */
static gboolean
read_data (App * app)
{
  GstBuffer *buffer;
  guint len;
  GstFlowReturn ret;

  if (app->offset >= app->length) {
    /* we are EOS, send end-of-stream and remove the source */
    g_signal_emit_by_name (app->appsrc, "end-of-stream", &ret);
    return FALSE;
  }

  /* read the next chunk */
  buffer = gst_buffer_new ();

  len = CHUNK_SIZE;
  if (app->offset + len > app->length)
    len = app->length - app->offset;

  gst_buffer_append_memory (buffer,
      gst_memory_new_wrapped (GST_MEMORY_FLAG_READONLY,
          app->data, app->length, app->offset, len, NULL, NULL));

  GST_DEBUG ("feed buffer %p, offset %" G_GUINT64_FORMAT "-%u", buffer,
      app->offset, len);
  g_signal_emit_by_name (app->appsrc, "push-buffer", buffer, &ret);

  //GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (app->pipeline),
  //    GST_DEBUG_GRAPH_SHOW_ALL, "feed_buffer");

  gst_buffer_unref (buffer);
  if (ret != GST_FLOW_OK) {
    /* some error, stop sending data */
    return FALSE;
  }

  app->offset += len;
  return TRUE;
}

/* This signal callback is called when appsrc needs data, we add an idle handler
 * to the mainloop to start pushing data into the appsrc */
static void
start_feed (GstElement * playbin, guint size, App * app)
{
  if (app->sourceid == 0) {
    GST_DEBUG ("start feeding");
    app->sourceid = g_idle_add ((GSourceFunc) read_data, app);
  }
}

/* This callback is called when appsrc has enough data and we can stop sending.
 * We remove the idle handler from the mainloop */
static void
stop_feed (GstElement * playbin, App * app)
{
  if (app->sourceid != 0) {
    GST_DEBUG ("stop feeding");
    g_source_remove (app->sourceid);
    app->sourceid = 0;
  }
}

/* this callback is called when playbin has constructed a source object to read
 * from. Since we provided the dynappsrc:// uri to playbin, this will be the
 * dynappsrc that we must handle. We set up some signals to start and stop pushing
 * data into appsrc */
static void
found_source (GObject * object, GObject * orig, GParamSpec * pspec,
    gpointer not_used)
{
  GstElement *dynappsrc = NULL;
  App *video_app = &v_app;
  App *audio_app = &a_app;
  GstStructure *s = NULL;

  /* get a handle to the dynappsrc */
  g_object_get (orig, pspec->name, &dynappsrc, NULL);

  GST_DEBUG ("got dynappsrc %p", dynappsrc);

  /* Enable to use stream-collection */
  s = gst_structure_new ("smart-properties", "use-stream-collection",
      G_TYPE_BOOLEAN, TRUE, NULL);
  g_object_set (dynappsrc, "smart-properties", s, NULL);

  /* create a appsrc element */
  g_signal_emit_by_name (dynappsrc, "new-appsrc", "video", &video_app->appsrc);
  g_signal_emit_by_name (dynappsrc, "new-appsrc", "audio", &audio_app->appsrc);

  gst_object_ref (video_app->appsrc);
  gst_object_ref (audio_app->appsrc);

  /* we can set the length in appsrc. This allows some elements to estimate the
   * total duration of the stream. It's a good idea to set the property when you
   * can but it's not required. */
  g_object_set (video_app->appsrc, "size", (gint64) video_app->length, NULL);
  g_object_set (audio_app->appsrc, "size", (gint64) audio_app->length, NULL);

  /* configure the appsrc, we will push data into the appsrc from the
   * mainloop. */
  g_signal_connect (video_app->appsrc, "need-data", G_CALLBACK (start_feed),
      video_app);
  g_signal_connect (video_app->appsrc, "enough-data", G_CALLBACK (stop_feed),
      video_app);

  g_signal_connect (audio_app->appsrc, "need-data", G_CALLBACK (start_feed),
      audio_app);
  g_signal_connect (audio_app->appsrc, "enough-data", G_CALLBACK (stop_feed),
      audio_app);
}

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
    GstCaps *caps = gst_stream_get_caps (stream);
    gchar *caps_str = gst_caps_to_string (caps);
    g_print (" New caps: %s\n", caps_str);
    g_free (caps_str);
    gst_caps_unref (caps);
  }
}

static gboolean
bus_message (GstBus * bus, GstMessage * message, GMainLoop * loop)
{
  GstObject *src = GST_MESSAGE_SRC (message);

  GST_DEBUG ("got message %s",
      gst_message_type_get_name (GST_MESSAGE_TYPE (message)));

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      GError *err = NULL;
      gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (message));
      gst_message_parse_error (message, &err, NULL);

      g_printerr ("ERROR: from element %s: %s\n", name, err->message);
      g_error_free (err);
      g_free (name);

      g_message ("Stopping\n");
      gst_object_unref (v_app.appsrc);
      gst_object_unref (a_app.appsrc);

      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
    {
      g_message ("EOS ! Stopping \n");
      gst_object_unref (v_app.appsrc);
      gst_object_unref (a_app.appsrc);

      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_STREAM_COLLECTION:
    {
      GstStreamCollection *collection = NULL;
      gst_message_parse_stream_collection (message, &collection);
      if (collection) {
        g_message ("Got a collection from %s:\n",
            src ? GST_OBJECT_NAME (src) : "Unknown");
        dump_collection (collection);
        App *data = &v_app;
        if (data->collection && data->notify_id) {
          g_signal_handler_disconnect (data->collection, data->notify_id);
          data->notify_id = 0;
        }
        gst_object_replace ((GstObject **) & data->collection,
            (GstObject *) collection);
        if (data->collection) {
          data->notify_id =
              g_signal_connect (data->collection, "stream-notify",
              (GCallback) stream_notify_cb, NULL);
        }
      }
      break;
    }
    case GST_MESSAGE_ASYNC_DONE:{
      App *video_app = &v_app;
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (video_app->pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, "async-done");
      break;
    }
    default:
      break;
  }
  return GST_BUS_PASS;
}

int
main (int argc, char *argv[])
{
  App *video_app = &v_app;
  App *audio_app = &a_app;
  GError *error = NULL;
  GMainLoop *loop = NULL;
  GstBus *bus;

  gst_init (&argc, &argv);

  GST_DEBUG_CATEGORY_INIT (dynappsrc_playbin3_debug, "dynappsrc-playbin3",
      0, "dynappsrc playbin3 example");

  if (argc < 3) {
    g_print ("usage: %s <video es filename> <auido es filename>\n", argv[0]);
    return -1;
  }

  /* try to open the file as an mmapped file */
  video_app->file = g_mapped_file_new (argv[1], FALSE, &error);
  if (error) {
    g_print ("failed to open file: %s\n", error->message);
    g_error_free (error);
    return -2;
  }
  /* get some vitals, this will be used to read data from the mmapped file and
   * feed it to appsrc. */
  video_app->length = g_mapped_file_get_length (video_app->file);
  video_app->data = (guint8 *) g_mapped_file_get_contents (video_app->file);
  video_app->offset = 0;

  /* try to open the file as an mmapped file */
  audio_app->file = g_mapped_file_new (argv[2], FALSE, &error);
  if (error) {
    g_print ("failed to open file: %s\n", error->message);
    g_error_free (error);
    return -2;
  }
  /* get some vitals, this will be used to read data from the mmapped file and
   * feed it to appsrc. */
  audio_app->length = g_mapped_file_get_length (audio_app->file);
  audio_app->data = (guint8 *) g_mapped_file_get_contents (audio_app->file);
  audio_app->offset = 0;

  /* Create pipeline */
  video_app->pipeline = audio_app->pipeline =
      gst_element_factory_make ("playbin3", NULL);
  g_assert (video_app->pipeline);

  /* set to read from dynappsrc */
  g_object_set (video_app->pipeline, "uri", "dynappsrc://", NULL);

  /* get notification when the source is created so that we get a handle to it
   * and can configure it */
  g_signal_connect (video_app->pipeline, "deep-notify::source",
      (GCallback) found_source, NULL);

  loop = g_main_loop_new (NULL, FALSE);

  /* Put a bus handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (video_app->pipeline));
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_message, loop, NULL);

  /* go to playing and wait in a mainloop. */
  gst_element_set_state (video_app->pipeline, GST_STATE_PLAYING);
  /* this mainloop is stopped when we receive an error or EOS */
  g_main_loop_run (loop);

  GST_DEBUG ("stopping");

  gst_element_set_state (video_app->pipeline, GST_STATE_NULL);

  /* free the file */
  g_mapped_file_unref (audio_app->file);
  g_mapped_file_unref (video_app->file);

  gst_object_unref (bus);
  g_main_loop_unref (loop);

  return 0;
}
