/* GStreamer Lightweight Playback Plugins
 *
 * Copyright (C) 2013-2014 LG Electronics, Inc.
 *	Author : Wonchul Lee <wonchul86.lee@lge.com>
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

#include <string.h>
#include "gsttsinkbin.h"

GST_DEBUG_CATEGORY_STATIC (gst_tsink_bin_debug);
#define GST_CAT_DEFAULT gst_tsink_bin_debug

static GstStaticPadTemplate text_template =
GST_STATIC_PAD_TEMPLATE ("text_sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

/* props */
enum
{
  PROP_0,
  PROP_LAST
};

#define DEFAULT_THUMBNAIL_MODE FALSE

static void gst_tsink_bin_finalize (GObject * object);
static void gst_tsink_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_tsink_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);

static GstPad *gst_tsink_bin_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_tsink_bin_release_request_pad (GstElement * element,
    GstPad * pad);

static gboolean activate_chain (GstElement * sink, GstTextGroup * tgoup,
    gboolean activate);
static GstFlowReturn gst_tsink_bin_new_sample (GstElement * sink);
static gboolean gst_tsink_bin_query (GstElement * element, GstQuery * query);
static GstPadProbeReturn gst_tsink_appsink_event_probe_cb (GstPad * pad,
    GstPadProbeInfo * info, gpointer user_data);
static gboolean gst_tsink_bin_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

G_DEFINE_TYPE (GstTSinkBin, gst_tsink_bin, GST_TYPE_BIN);

#define parent_class gst_tsink_bin_parent_class

static void
gst_tsink_bin_class_init (GstTSinkBinClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;

  gobject_klass->finalize = gst_tsink_bin_finalize;
  gobject_klass->set_property = gst_tsink_bin_set_property;
  gobject_klass->get_property = gst_tsink_bin_get_property;

  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&text_template));

  gst_element_class_set_static_metadata (gstelement_klass,
      "Text Sink Bin", "/Bin/TextSinkBin",
      "Convenience sink for multiple text streams in a restricted system",
      "Wonchul Lee <wonchul86.lee@lge.com>");

  gstelement_klass->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_tsink_bin_request_new_pad);
  gstelement_klass->release_pad =
      GST_DEBUG_FUNCPTR (gst_tsink_bin_release_request_pad);
  gstelement_klass->query = GST_DEBUG_FUNCPTR (gst_tsink_bin_query);
}

static void
gst_tsink_bin_init (GstTSinkBin * tsinkbin)
{
  GST_DEBUG_CATEGORY_INIT (gst_tsink_bin_debug, "tsinkbin", 0, "Text Sink Bin");
  g_rec_mutex_init (&tsinkbin->lock);

  tsinkbin->nb_stream = 0;
}

static void
gst_tsink_bin_finalize (GObject * obj)
{
  GstTSinkBin *tsinkbin;

  tsinkbin = GST_TSINK_BIN (obj);

  g_rec_mutex_clear (&tsinkbin->lock);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static GstPad *
gst_tsink_bin_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  GstTSinkBin *tsinkbin;
  GstPad *pad;
  GstTextGroup *t_group;
  GstPad *queue_sinkpad;
  GstPad *appsinkpad = NULL;

  g_return_val_if_fail (templ != NULL, NULL);

  tsinkbin = GST_TSINK_BIN (element);

  GST_DEBUG_OBJECT (element, "name: %s, current number of stream : %d", name,
      tsinkbin->nb_stream);

  t_group = (GstTextGroup *) g_malloc0 (sizeof (GstTextGroup));
  if (!t_group) {
    GST_WARNING_OBJECT (element, "TextGroup creation fail");
    return NULL;
  }

  t_group->queue = gst_element_factory_make ("queue", NULL);
  if (t_group->queue == NULL) {
    GST_WARNING_OBJECT (element, "fail to create queue element");
    goto fail;
  }
  g_object_set (G_OBJECT (t_group->queue), "silent", TRUE, NULL);

  t_group->appsink = gst_element_factory_make ("appsink", NULL);
  if (t_group->appsink == NULL) {
    GST_WARNING_OBJECT (element, "fail to create appsink element");
    if (t_group->queue) {
      gst_bin_remove (GST_BIN (tsinkbin), t_group->queue);
      t_group->queue = NULL;
    }
    goto fail;
  }
  // FIXME: Needs to mutex lock when properties are set in appsink.
  // If not, it may cause crash problem in h15 platform.
  // issue address : [BHV-15451] Crash problem when playing internal subtitle

  GST_TSINK_BIN_LOCK (tsinkbin);
  g_object_set (t_group->appsink, "emit-signals", TRUE, "sync", FALSE,
      "ts-offset", (gint64) 1000 * 1000, NULL);
  /* disable async enable */
  g_object_set (t_group->appsink, "async", FALSE, NULL);
  GST_TSINK_BIN_UNLOCK (tsinkbin);

  g_signal_connect (t_group->appsink, "new-sample",
      G_CALLBACK (gst_tsink_bin_new_sample), NULL);

  /* get the appsink sink pad */
  appsinkpad = gst_element_get_static_pad (t_group->appsink, "sink");

  /* add the probe function to receive the downstream events */
  gst_pad_add_probe (appsinkpad,
      GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      gst_tsink_appsink_event_probe_cb, t_group->appsink, NULL);

  gst_object_unref (appsinkpad);

  gst_element_set_state (t_group->appsink, GST_STATE_READY);

  gst_bin_add (GST_BIN_CAST (tsinkbin), t_group->queue);
  gst_bin_add (GST_BIN_CAST (tsinkbin), t_group->appsink);

  gst_element_link_pads (t_group->queue, "src", t_group->appsink, "sink");

  queue_sinkpad = gst_element_get_static_pad (t_group->queue, "sink");

  if (name) {
    pad = gst_ghost_pad_new (name, queue_sinkpad);
  } else {
    gchar *pad_name = g_strdup_printf ("text_sink%d", tsinkbin->nb_stream);
    pad = gst_ghost_pad_new (pad_name, queue_sinkpad);
    g_free (pad_name);
  }
  gst_object_unref (queue_sinkpad);

  tsinkbin->nb_stream++;

  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_tsink_bin_sink_event));

  gst_pad_set_active (pad, TRUE);
  gst_element_add_pad (element, pad);

  activate_chain (element, t_group, TRUE);

  gst_pad_set_element_private (pad, t_group);

  return pad;

fail:
  GST_WARNING_OBJECT (element, "internal element creation fail");
  g_slice_free (GstTextGroup, t_group);
  return NULL;
}

static void
gst_tsink_bin_release_request_pad (GstElement * element, GstPad * pad)
{
  GstTSinkBin *tsinkbin = GST_TSINK_BIN (element);
  GstTextGroup *t_group;

  GST_DEBUG_OBJECT (tsinkbin, "release pad %" GST_PTR_FORMAT, pad);

  GST_TSINK_BIN_LOCK (tsinkbin);

  t_group = gst_pad_get_element_private (pad);

  if (t_group) {
    GST_DEBUG_OBJECT (tsinkbin, "queue %" GST_PTR_FORMAT, t_group->queue);
    GST_DEBUG_OBJECT (tsinkbin, "appsink %" GST_PTR_FORMAT, t_group->appsink);

    if (t_group->appsink) {
      gst_element_set_locked_state (t_group->appsink, TRUE);
      gst_element_set_state (t_group->appsink, GST_STATE_NULL);
      gst_bin_remove (GST_BIN (tsinkbin), t_group->appsink);
      t_group->appsink = NULL;
    }

    if (t_group->queue) {
      gst_element_set_locked_state (t_group->queue, TRUE);
      gst_element_set_state (t_group->queue, GST_STATE_NULL);
      gst_bin_remove (GST_BIN (tsinkbin), t_group->queue);
      t_group->queue = NULL;
    }

    gst_pad_set_element_private (pad, NULL);
    gst_pad_set_active (pad, FALSE);
    gst_element_remove_pad (GST_ELEMENT_CAST (tsinkbin), pad);

    g_slice_free (GstTextGroup, t_group);
  }

  GST_TSINK_BIN_UNLOCK (tsinkbin);
}

static void
gst_tsink_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec)
{
  //GstTSinkBin *tsinkbin = GST_TSINK_BIN (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, spec);
      break;
  }
}

static void
gst_tsink_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec)
{
  //GstTSinkBin *tsinkbin = GST_TSINK_BIN (object);
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, spec);
      break;
  }
}

static gboolean
activate_chain (GstElement * sink, GstTextGroup * tgroup, gboolean activate)
{
  GstState state;
  GstElement *parent;

  parent = GST_ELEMENT (gst_element_get_parent (sink));

  GST_OBJECT_LOCK (sink);
  state = GST_STATE_TARGET (parent);
  GST_OBJECT_UNLOCK (sink);
  if (activate == FALSE)
    state = GST_STATE_NULL;

  gst_element_set_state (tgroup->queue, state);
  gst_element_set_state (tgroup->appsink, state);

  gst_object_unref (parent);

  return TRUE;
}

static GstFlowReturn
gst_tsink_bin_new_sample (GstElement * sink)
{
  GstSample *sample = NULL;
  GstStructure *structure;

  g_signal_emit_by_name (sink, "pull-sample", &sample);

  if (sample == NULL) {
    GST_WARNING_OBJECT (sink, "pull-sample returns NULL");
    return GST_FLOW_CUSTOM_ERROR;
  }

  structure =
      gst_structure_new ("subtitle_data", "sample", GST_TYPE_SAMPLE,
      sample, NULL);

  gst_element_post_message (sink,
      gst_message_new_application (GST_OBJECT_CAST (sink), structure));

  gst_sample_unref (sample);

  return GST_FLOW_OK;
}


static gboolean
gst_tsink_bin_query (GstElement * element, GstQuery * query)
{
  gboolean ret = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      return FALSE;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->query (element, query);

  return ret;
}

/* Process the appsink downstream events */
static GstPadProbeReturn
gst_tsink_appsink_event_probe_cb (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstElement *self = GST_ELEMENT (user_data);
  GstEvent *event = GST_EVENT (GST_PAD_PROBE_INFO_DATA (info));
  GstStructure *tmp_structure;
  const gchar *type_name;
  GstCaps *caps = NULL;

  GST_DEBUG_OBJECT (self, "event = %s, sticky = %d",
      GST_EVENT_TYPE_NAME (event), GST_EVENT_IS_STICKY (event));

  if (GST_EVENT_TYPE (GST_PAD_PROBE_INFO_EVENT (info)) != GST_EVENT_CAPS) {
    GST_DEBUG_OBJECT (pad, "Passing stream start event");
    return GST_PAD_PROBE_OK;
  }

  /* remove the probe function from appsink sink pad */
  gst_pad_remove_probe (pad, GST_PAD_PROBE_INFO_ID (info));

  gst_event_parse_caps (event, &caps);

  /* Get the first gst structure from the caps */
  g_return_val_if_fail (caps != NULL, GST_PAD_PROBE_OK);
  tmp_structure = gst_caps_get_structure (caps, 0);

  g_return_val_if_fail (tmp_structure != NULL, GST_PAD_PROBE_OK);
  type_name = gst_structure_get_name (tmp_structure);

  GST_DEBUG_OBJECT (pad, "[%s]- app sink configured as Sync %p", type_name,
      self);

  /* check the mime type */
  if (!g_strcmp0 (type_name, "application/x-teletext"))
    g_object_set (self, "sync", TRUE, "async", FALSE, "ts-offset", (gint64) 0,
        NULL);
  else if (!g_strcmp0 (type_name, "subpicture/x-dvb"))
    g_object_set (self, "sync", FALSE, "async", FALSE, "ts-offset", (gint64) 0,
        NULL);

  return GST_PAD_PROBE_OK;
}

static gboolean
gst_tsink_bin_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  switch (event->type) {
    case GST_EVENT_CAPS:
    {
      GstEvent *e;

      e = gst_pad_get_sticky_event (pad, GST_EVENT_STREAM_START, 0);
      if (e) {
        const gchar *stream_id;
        GstCaps *caps, *new_caps;
        GstPad *target_pad;
        gboolean ret;

        gst_event_parse_stream_start (e, &stream_id);

        gst_event_parse_caps (event, &caps);
        new_caps = gst_caps_copy (caps);
        new_caps = gst_caps_make_writable (new_caps);
        gst_caps_set_simple (new_caps,
            "stream-id", G_TYPE_STRING, stream_id, NULL);

        target_pad = gst_ghost_pad_get_target (GST_GHOST_PAD (pad));
        ret = gst_pad_send_event (target_pad, gst_event_new_caps (new_caps));

        gst_object_unref (target_pad);
        gst_caps_unref (new_caps);
        gst_event_unref (e);
        gst_event_unref (event);

        return ret;
      } else {
        GST_WARNING_OBJECT (pad, "Got caps before stream-start");
      }
    }
      break;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}
