/*
 * GStreamer textbin element
 *
 * Copyright 2016 LG Electronics, Inc.
 *  @author: Seungha Yang <sh.yang@lge.com>
 *
 * gstdvcombbin.h: Interleaving element for dolby vision dual track
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
] *
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

#include <string.h>

#include "gstdvcombbin.h"

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


GST_DEBUG_CATEGORY_STATIC (gst_dvcomb_bin_debug);
#define GST_CAT_DEFAULT gst_dvcomb_bin_debug

#define parent_class gst_dvcomb_bin_parent_class

static void gst_dvcomb_bin_dispose (GObject * object);
static void gst_dvcomb_bin_finalize (GObject * object);

static GstPad *gst_dvcomb_bin_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);

static GstPadProbeReturn
gst_dvcomb_bin_src_probe (GstPad * pad, GstPadProbeInfo * info,
    GstDvcombBin * bin);

static GstPadProbeReturn
gst_dvcomb_bin_queue_src_probe (GstPad * pad, GstPadProbeInfo * info,
    GstDvcombGroup * group);


G_DEFINE_TYPE (GstDvcombBin, gst_dvcomb_bin, GST_TYPE_BIN);

static void
gst_dvcomb_bin_class_init (GstDvcombBinClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_dvcomb_bin_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_dvcomb_bin_finalize);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_dvcomb_bin_request_new_pad);
#if 0
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_dvcomb_bin_release_pad);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_dvcomb_bin_change_state);
#endif

  gst_element_class_set_static_metadata (gstelement_class,
      "Dolby Combiner Bin", "Bin/Converter/Video/Codec/Parser",
      "Interleaving element for dolby vision dual track",
      "Seungha Yang <sh.yang@lge.com>");

  GST_DEBUG_CATEGORY_INIT (gst_dvcomb_bin_debug, "dvcombbin", 0,
      "Dolby Combiner Bin");
}

static void
gst_dvcomb_bin_init (GstDvcombBin * bin)
{
  g_rec_mutex_init (&bin->lock);
  g_mutex_init (&bin->seek_lock);

  bin->combiner = NULL;
  bin->seeking = FALSE;
  bin->group_list = g_list_alloc ();
  bin->queue_list = g_list_alloc ();
  bin->seqnum_last_seek = 0;
}

static void
gst_dvcomb_bin_dispose (GObject * object)
{
  GstDvcombBin *bin = GST_DVCOMB_BIN (object);

  if (bin->queue_list) {
    GST_DEBUG_OBJECT (bin, "Trying to remove queue");
    g_list_free (bin->queue_list);
    bin->queue_list = NULL;
  }

  if (bin->group_list) {
    GST_DEBUG_OBJECT (bin, "Trying to group list");
    g_list_free_full (bin->group_list, g_free);
    bin->group_list = NULL;
  }

  if (bin->combiner) {
    GST_DEBUG_OBJECT (bin->combiner, "Trying to remove combiner");
    gst_element_set_state (bin->combiner, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (bin), bin->combiner);
    bin->combiner = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_dvcomb_bin_finalize (GObject * object)
{
  GstDvcombBin *bin = GST_DVCOMB_BIN (object);

  g_rec_mutex_clear (&bin->lock);
  g_mutex_clear (&bin->seek_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstPad *
gst_dvcomb_bin_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  GstDvcombBin *bin = (GstDvcombBin *) element;
  GstPad *sinkpad, *srcpad;
  GstPad *pad;
  GstPadTemplate *pad_tmpl;
  GstDvcombGroup *group;
  gchar *element_name = NULL;
  const gchar *final_name = NULL;
  GstElement *queue = NULL;
  guint n_streams;

  GST_DEBUG_OBJECT (bin, "templ:%p, name:%s", templ, name);
  GST_DVCOMB_BIN_LOCK (bin);
  n_streams = g_list_length (bin->group_list) - 1;      /* Initially length = 1 */

  /* 1. Make queue element */
  element_name = g_strdup_printf ("dvcomb_queue%u", n_streams);
  queue = gst_element_factory_make ("queue", element_name);
  g_free (element_name);
  element_name = NULL;

  if (!queue) {
    GST_WARNING_OBJECT (bin, "fail to create queue element");
    goto fail;
  } else {
    g_object_set (G_OBJECT (queue), "silent", TRUE, NULL);
    g_object_set (G_OBJECT (queue), "max-size-time", 5 * GST_SECOND, NULL);
    gst_bin_add (GST_BIN_CAST (bin), queue);
  }

  /* Setup and event probe for group */

  group = g_new0 (GstDvcombGroup, 1);
  group->bin = bin;
  group->queue = queue;
  group->seeking = FALSE;

  pad = gst_element_get_static_pad (group->queue, "src");
  group->src_pad = pad;
  gst_object_unref (pad);

  pad = gst_element_get_static_pad (group->queue, "sink");
  gst_pad_set_active (pad, TRUE);
  group->sink_pad = pad;
  gst_object_unref (pad);

  group->probe_id = gst_pad_add_probe (group->src_pad,
      GST_PAD_PROBE_TYPE_EVENT_UPSTREAM | GST_PAD_PROBE_TYPE_EVENT_FLUSH |
      GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) gst_dvcomb_bin_queue_src_probe, group, NULL);

  /* 2. Make dolby stream combiner */
  if (!bin->combiner) {
    bin->combiner = gst_element_factory_make ("dolbystreamcombiner", NULL);
    if (!bin->combiner) {
      GST_WARNING_OBJECT (bin, "fail to create combiner element");
      goto fail;
    } else {
      gst_bin_add (GST_BIN_CAST (bin), bin->combiner);
    }
  } else {
    GST_DEBUG_OBJECT (bin, "combiner already exist");
  }

  /* 3. link queue & combiner */
  gst_element_link (queue, bin->combiner);

  if (name == NULL) {
    element_name = g_strdup_printf ("sink_%u", n_streams);
    final_name = element_name;
  } else {
    final_name = name;
  }

  /* 4. link front ghost pad */
  sinkpad =
      gst_ghost_pad_new_from_template (final_name, group->sink_pad, templ);

  if (element_name) {
    g_free (element_name);
    element_name = NULL;
  }

  gst_pad_set_active (sinkpad, TRUE);
  gst_element_add_pad (element, sinkpad);
  GST_DEBUG_OBJECT (sinkpad, "link front ghost pad");

  /* 5. link rear ghost pad if needed */
  if (!n_streams) {
    pad = gst_element_get_static_pad (bin->combiner, "src");
    pad_tmpl = gst_static_pad_template_get (&src_template);
    srcpad = gst_ghost_pad_new_from_template ("src", pad, pad_tmpl);
    gst_pad_set_active (srcpad, TRUE);
    gst_element_add_pad (element, srcpad);
    GST_DEBUG_OBJECT (srcpad, "link rear ghost pad");

    bin->probe_id = gst_pad_add_probe (srcpad,
        GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
        (GstPadProbeCallback) gst_dvcomb_bin_src_probe, bin, NULL);

    gst_object_unref (pad);
    gst_object_unref (pad_tmpl);
  }

  bin->group_list = g_list_append (bin->group_list, group);
  bin->queue_list = g_list_append (bin->queue_list, queue);

  GST_DVCOMB_BIN_UNLOCK (bin);

  GST_DEBUG_OBJECT (element, "Returning pad %p", sinkpad);

  return sinkpad;

fail:
  GST_WARNING_OBJECT (bin, "fail creating child element");
  GST_DVCOMB_BIN_UNLOCK (bin);

  return NULL;
}

static gboolean
gst_dvcomb_bin_check_and_update_seek (GstDvcombBin * bin, gboolean reset)
{
  gboolean ret = TRUE;
  GList *walk = bin->group_list->next;

  GST_LOG_OBJECT (bin, "Check and update seek, length :%d",
      g_list_length (walk));
  while (walk) {
    GstDvcombGroup *group = (GstDvcombGroup *) walk->data;
    if (reset) {
      group->seeking = FALSE;
    } else {
      GST_LOG_OBJECT (bin, "seeking : %d", group->seeking);
      ret = group->seeking & ret;
    }
    walk = g_list_next (walk);
  }

  return ret;
}

static GstPadProbeReturn
gst_dvcomb_bin_queue_src_probe (GstPad * pad, GstPadProbeInfo * info,
    GstDvcombGroup * group)
{
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;
  GstDvcombBin *bin = group->bin;

  if (GST_IS_EVENT (GST_PAD_PROBE_INFO_DATA (info))) {
    GstEvent *ev = GST_PAD_PROBE_INFO_EVENT (info);
    GST_LOG_OBJECT (pad, "Got event %p %s", ev, GST_EVENT_TYPE_NAME (ev));
    switch (GST_EVENT_TYPE (ev)) {
      case GST_EVENT_SEEK:
      {
        GstSeekFlags flags;
        gboolean forward = FALSE;
        GST_DVCOMB_BIN_SEEK_LOCK (bin);
        gst_event_parse_seek (ev, NULL, NULL, &flags, NULL, NULL, NULL, NULL);
        if (flags & GST_SEEK_FLAG_FLUSH) {
          group->seeking = TRUE;
          forward = gst_dvcomb_bin_check_and_update_seek (bin, FALSE);
        } else {
          forward = TRUE;
        }

        if (!forward) {
          ret = GST_PAD_PROBE_DROP;
          GST_LOG_OBJECT (pad, "Eating seek event");
        } else {
          GST_LOG_OBJECT (pad, "Forwarding seek event");
        }
        GST_DVCOMB_BIN_SEEK_UNLOCK (bin);
      }
        break;
      case GST_EVENT_FLUSH_STOP:
        GST_DVCOMB_BIN_SEEK_LOCK (bin);
        GST_LOG_OBJECT (pad, "Clear seeking flag");
        group->seeking = FALSE;
        bin->seeking = FALSE;
        GST_DVCOMB_BIN_SEEK_UNLOCK (bin);
        break;
      case GST_EVENT_EOS:
        GST_DVCOMB_BIN_SEEK_LOCK (bin);
        if (bin->seeking || group->seeking) {
          GST_LOG_OBJECT (pad, "Eating EOS during seeking state");
          ret = GST_PAD_PROBE_DROP;
        }
        GST_DVCOMB_BIN_SEEK_UNLOCK (bin);
        break;
      default:
        break;
    }
  } else if (GST_IS_BUFFER (GST_PAD_PROBE_INFO_DATA (info))) {
    /* Drop buffer when seeking state */
    GST_DVCOMB_BIN_SEEK_LOCK (bin);
    if (bin->seeking || group->seeking) {
      GST_LOG_OBJECT (pad, "Drop %" GST_PTR_FORMAT " on seeking pad",
          GST_BUFFER (GST_PAD_PROBE_INFO_DATA (info)));
      ret = GST_PAD_PROBE_DROP;
    }
    GST_DVCOMB_BIN_SEEK_UNLOCK (bin);
  }

  return ret;
}

static GstPadProbeReturn
gst_dvcomb_bin_src_probe (GstPad * pad, GstPadProbeInfo * info,
    GstDvcombBin * bin)
{
  GstPadProbeReturn ret = GST_PAD_PROBE_OK;

  if (GST_IS_EVENT (GST_PAD_PROBE_INFO_DATA (info))) {
    GstEvent *ev = GST_PAD_PROBE_INFO_EVENT (info);
    GST_LOG_OBJECT (pad, "Got event %p %s", ev, GST_EVENT_TYPE_NAME (ev));
    switch (GST_EVENT_TYPE (ev)) {
      case GST_EVENT_SEEK:
      {
        GstSeekFlags flags;
        gboolean forward = FALSE;
        GST_DVCOMB_BIN_SEEK_LOCK (bin);
        gst_event_parse_seek (ev, NULL, NULL, &flags, NULL, NULL, NULL, NULL);
        if (flags & GST_SEEK_FLAG_FLUSH) {
          guint32 seqnum = gst_event_get_seqnum (ev);
          if (bin->seqnum_last_seek == seqnum) {
            forward = FALSE;
          } else {
            bin->seqnum_last_seek = seqnum;
            forward = TRUE;
            bin->seeking = TRUE;
          }
        } else {
          forward = TRUE;
        }
        if (!forward) {
          ret = GST_PAD_PROBE_DROP;
          GST_LOG_OBJECT (pad, "Eating seek event");
        } else {
          GST_LOG_OBJECT (pad, "Forwarding seek event");
        }
        GST_DVCOMB_BIN_SEEK_UNLOCK (bin);
      }
        break;
      default:
        break;
    }
  }

  return ret;
}
