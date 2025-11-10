/* GStreamer Plugins Cool
 * Copyright (C) 2014 LG Electronics, Inc.
 *	Author : Jeongseok Kim <jeongseok.kim@lge.com>
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

#include <gst/audio/audio.h>

#include "gstcool.h"
#include "gstcoolutil.h"

enum
{
  STREAM_AUDIO = 0,
  STREAM_VIDEO,
  STREAM_TEXT,
  STREAM_LAST
};

/* To convert taglist to mediainfo without converting to string */
typedef struct _GstTagListImpl
{
  GstTagList taglist;

  GstStructure *structure;
  GstTagScope scope;
} GstTagListImpl;

#define GST_TAG_LIST_STRUCTURE(taglist)  ((GstTagListImpl*)(taglist))->structure

/* blacklisted field for media-info, we don't need to add it to media-info message */
static const gchar *blacklisted_tag_fields[] =
    { GST_TAG_BITRATE, GST_TAG_MINIMUM_BITRATE, GST_TAG_MAXIMUM_BITRATE,
  NULL
};

static gboolean
append_media_field (GQuark field_id, const GValue * value, gpointer user_data)
{
  GstStructure *media = user_data;
  gchar *value_str = gst_value_serialize (value);

  GST_DEBUG_OBJECT (media, "field [%s:%s]", g_quark_to_string (field_id),
      value_str);

  gst_structure_id_set_value (media, field_id, value);

  g_free (value_str);

  return TRUE;
}

/* Return whether stream type is video, audio or subtitle.
  * To add media-info structure.
  * Returns : (GstCoolStreamType) type or GST_COOL_STREAM_TYPE_UNKNOWN if mime_type is NULL.
  */
GstCoolStreamType
gst_cool_find_type (const gchar * mime_type)
{
  g_return_val_if_fail (mime_type != NULL, STREAM_LAST);

  guint type;
  if (g_strrstr (mime_type, "video") || g_strrstr (mime_type, "image"))
    type = STREAM_VIDEO;
  else if (g_strrstr (mime_type, "audio"))
    type = STREAM_AUDIO;
  else
    type = STREAM_TEXT;

  GST_LOG ("return type = %u", type);
  return type;
}

/* Return media-info structure that is converted from caps information.
  * Returns : (GstStructure *) structure or NULL if caps is NULL.
  */
GstStructure *
gst_cool_caps_to_info (GstCaps * caps, char *stream_id, const char *mime_type)
{
  g_return_val_if_fail (caps != NULL, NULL);

  GstStructure *s = gst_caps_get_structure (caps, 0);
  GstStructure *media_info;
  GstCoolStreamType type = gst_cool_find_type (mime_type);

  GST_DEBUG ("getting caps information: %" GST_PTR_FORMAT, caps);

  media_info = gst_structure_new ("media-info",
      "stream-id", G_TYPE_STRING, stream_id,
      "type", G_TYPE_INT, type, "mime-type", G_TYPE_STRING, mime_type, NULL);

  /* append media information from caps's structure to media-info */
  gst_structure_foreach (s, append_media_field, media_info);
  GST_DEBUG ("Complete: structure information = [%" GST_PTR_FORMAT "]",
      media_info);

  return media_info;
}

/* Return media-info structure that is converted from taglist.
  * Returns : (GstStructure *) structure or NULL if taglist is NULL.
  */
GstStructure *
gst_cool_taglist_to_info (GstTagList * taglist, char *stream_id,
    const char *mime_type)
{
  g_return_val_if_fail (taglist != NULL, NULL);

  GstStructure *structure;
  GstStructure *media_info = NULL;
  GstCoolStreamType type = gst_cool_find_type (mime_type);
  gint i = 0;

  GST_DEBUG ("getting taglist information: %" GST_PTR_FORMAT, taglist);

  /* conveting taglist to structure */
  structure = GST_TAG_LIST_STRUCTURE (taglist);

  /* filtered out blacklisted fields */
  for (i = 0; blacklisted_tag_fields[i]; i++) {
    if (gst_structure_has_field (structure, blacklisted_tag_fields[i])) {
      GST_DEBUG ("got blacked field : %s", blacklisted_tag_fields[i]);
      gst_structure_remove_field (structure, blacklisted_tag_fields[i]);
    }
  }

  /* whether to check structure has any fields or not */
  if (gst_structure_n_fields (structure) == 0) {
    GST_DEBUG ("No tag fields");
    media_info = NULL;
    goto done;
  }

  media_info =
      gst_structure_new ("media-info",
      "stream-id", G_TYPE_STRING, stream_id,
      "type", G_TYPE_INT, type, "mime-type", G_TYPE_STRING, mime_type, NULL);

  /* append media information from taglist's structure to media-info */
  gst_structure_foreach (structure, append_media_field, media_info);
  GST_DEBUG ("Complete: structure information = [%" GST_PTR_FORMAT "]",
      media_info);

done:
  return media_info;
}

static GstCaps *
expected_output_tmpl_caps (GstCaps * input_caps)
{
  GList *decoders = NULL;
  GList *filtered = NULL;
  GstElementFactory *factory = NULL;
  const GList *templates;
  GstCaps *tmpl_caps = NULL;

  decoders =
      gst_element_factory_list_get_elements
      (GST_ELEMENT_FACTORY_TYPE_DECODER, GST_RANK_MARGINAL);

  if (decoders == NULL) {
    GST_WARNING ("Cannot find any of decoders");
    goto fail;
  }

  GST_DEBUG ("got factory list %p", decoders);
  gst_plugin_feature_list_debug (decoders);

  decoders = g_list_sort (decoders, gst_plugin_feature_rank_compare_func);

  if (!(filtered =
          gst_element_factory_list_filter (decoders, input_caps, GST_PAD_SINK,
              FALSE))) {
    gchar *tmp = gst_caps_to_string (input_caps);
    GST_WARNING ("Cannot find any decoder for caps %s", tmp);
    g_free (tmp);

    goto fail;
  }

  GST_DEBUG ("got filtered list %p", filtered);

  gst_plugin_feature_list_debug (filtered);

  factory = GST_ELEMENT_FACTORY_CAST (g_list_nth_data (filtered, 0));

  /* Note that decproxy can not be a child element in decproxy bin */
  if (!g_strcmp0 (gst_plugin_feature_get_name (g_list_nth_data (filtered, 0)),
          "decproxy")) {
    if (g_list_length (filtered) > 1)
      factory = GST_ELEMENT_FACTORY_CAST (g_list_nth_data (filtered, 1));
    else
      factory = NULL;
  }

  if (factory == NULL) {
    GST_WARNING ("factory is null");
    goto fail;
  }

  GST_DEBUG ("expected actual decoder: %" GST_PTR_FORMAT, factory);
  templates = gst_element_factory_get_static_pad_templates (factory);
  while (templates) {
    GstStaticPadTemplate *template = (GstStaticPadTemplate *) templates->data;

    if (template->direction == GST_PAD_SRC
        && template->presence == GST_PAD_ALWAYS) {
      tmpl_caps = gst_static_caps_get (&template->static_caps);
    }
    templates = g_list_next (templates);
  }

fail:
  if (decoders)
    gst_plugin_feature_list_free (decoders);
  if (filtered)
    gst_plugin_feature_list_free (filtered);

  return tmpl_caps;
}

static gboolean
is_valid_value (const gchar * prop, gint value, GstStructure * s)
{
  GValue v1 = { 0, };
  const GValue *v2;
  gboolean ret = TRUE;

  g_value_init (&v1, G_TYPE_INT);
  g_value_set_int (&v1, value);
  v2 = gst_structure_get_value (s, prop);

  if (v2 && !gst_value_is_fixed (v2) && !gst_value_is_subset (&v1, v2))
    ret = FALSE;

  g_value_unset (&v1);

  return ret;
}

GstCaps *
negotiate_default_caps (GstCaps * input_caps, GstPad * srcpad, gint stream_type)
{
  GstCaps *caps, *templcaps;
  gint i;
  gint channels = 0;
  gint rate;
  guint64 channel_mask = 0;
  gint caps_size;
  GstStructure *structure;

  templcaps = expected_output_tmpl_caps (input_caps);
  GST_DEBUG ("expected templcaps : %" GST_PTR_FORMAT, templcaps);
  caps = gst_pad_peer_query_caps (srcpad, templcaps);
  if (caps)
    gst_caps_unref (templcaps);
  else
    caps = templcaps;
  templcaps = NULL;

  if (!caps || gst_caps_is_empty (caps) || gst_caps_is_any (caps))
    goto caps_error;

  GST_DEBUG ("peer caps  %" GST_PTR_FORMAT, caps);

  /* before fixating, try to use whatever upstream provided */
  caps = gst_caps_make_writable (caps);
  caps_size = gst_caps_get_size (caps);
  if (input_caps) {
    GstCaps *sinkcaps = input_caps;
    GstStructure *structure = gst_caps_get_structure (sinkcaps, 0);
    if (stream_type == STREAM_AUDIO) {
      if (gst_structure_get_int (structure, "rate", &rate)) {
        for (i = 0; i < caps_size; i++) {
          if (is_valid_value ("rate", rate, gst_caps_get_structure (caps, i)))
            gst_structure_set (gst_caps_get_structure (caps, i), "rate",
                G_TYPE_INT, rate, NULL);
        }
      }

      if (gst_structure_get_int (structure, "channels", &channels)) {
        for (i = 0; i < caps_size; i++) {
          if (is_valid_value ("channels", channels,
                  gst_caps_get_structure (caps, i)))
            gst_structure_set (gst_caps_get_structure (caps, i), "channels",
                G_TYPE_INT, channels, NULL);
        }
      }

      if (gst_structure_get (structure, "channel-mask", GST_TYPE_BITMASK,
              &channel_mask, NULL)) {
        for (i = 0; i < caps_size; i++) {
          gst_structure_set (gst_caps_get_structure (caps, i), "channel-mask",
              GST_TYPE_BITMASK, channel_mask, NULL);
        }
      }
    } else if (stream_type == STREAM_VIDEO) {
      gint width, height;
      gint par_n, par_d;
      gint fps_n, fps_d;

      if (gst_structure_get_int (structure, "width", &width)) {
        for (i = 0; i < caps_size; i++) {
          gst_structure_set (gst_caps_get_structure (caps, i), "width",
              G_TYPE_INT, width, NULL);
        }
      }

      if (gst_structure_get_int (structure, "height", &height)) {
        for (i = 0; i < caps_size; i++) {
          gst_structure_set (gst_caps_get_structure (caps, i), "height",
              G_TYPE_INT, height, NULL);
        }
      }

      if (gst_structure_get_fraction (structure, "framerate", &fps_n, &fps_d)) {
        for (i = 0; i < caps_size; i++) {
          gst_structure_set (gst_caps_get_structure (caps, i), "framerate",
              GST_TYPE_FRACTION, fps_n, fps_d, NULL);
        }
      }

      if (gst_structure_get_fraction (structure, "pixel-aspect-ratio", &par_n,
              &par_d)) {
        for (i = 0; i < caps_size; i++) {
          gst_structure_set (gst_caps_get_structure (caps, i),
              "pixel-aspect-ratio", GST_TYPE_FRACTION, par_n, par_d, NULL);
        }
      }
    }
  }

  if (stream_type == STREAM_AUDIO) {
    for (i = 0; i < caps_size; i++) {
      structure = gst_caps_get_structure (caps, i);
      gst_structure_fixate_field_nearest_int (structure,
          "channels", GST_AUDIO_DEF_CHANNELS);
      gst_structure_fixate_field_nearest_int (structure,
          "rate", GST_AUDIO_DEF_RATE);

    }

    caps = gst_caps_fixate (caps);
    structure = gst_caps_get_structure (caps, 0);

    /* Need to add a channel-mask if channels > 2 */
    gst_structure_get_int (structure, "channels", &channels);
    if (channels > 2 && !gst_structure_has_field (structure, "channel-mask")) {
      channel_mask = gst_audio_channel_get_fallback_mask (channels);
      if (channel_mask != 0) {
        gst_structure_set (structure, "channel-mask",
            GST_TYPE_BITMASK, channel_mask, NULL);
      } else {
        GST_WARNING ("No default channel-mask for %d channels", channels);
      }
    }
  } else if (stream_type == STREAM_VIDEO) {
    for (i = 0; i < caps_size; i++) {
      structure = gst_caps_get_structure (caps, i);
      /* Random 1280x720@30 for fixation */
      gst_structure_fixate_field_nearest_int (structure, "width", 1280);
      gst_structure_fixate_field_nearest_int (structure, "height", 720);
      gst_structure_fixate_field_nearest_fraction (structure, "framerate", 30,
          1);
      if (gst_structure_has_field (structure, "pixel-aspect-ratio")) {
        gst_structure_fixate_field_nearest_fraction (structure,
            "pixel-aspect-ratio", 1, 1);
      } else {
        gst_structure_set (structure, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            1, 1, NULL);
      }
    }
  }

  caps = gst_caps_fixate (caps);

  if (!caps)
    goto caps_error;

  GST_INFO ("Chose default caps %" GST_PTR_FORMAT " for initial negotiation",
      caps);

  return caps;

caps_error:
  {
    if (caps)
      gst_caps_unref (caps);
    return NULL;
  }
}

void
make_unsupported_codec (const GstCaps * input_caps)
{
  GList *decoders = NULL;
  GList *filtered = NULL;
  GstElementFactory *factory = NULL;
  guint i;

  decoders =
      gst_element_factory_list_get_elements
      (GST_ELEMENT_FACTORY_TYPE_DECODER, GST_RANK_MARGINAL);

  if (decoders == NULL) {
    GST_DEBUG ("Cannot find any of decoders");
    return;
  }

  decoders = g_list_sort (decoders, gst_plugin_feature_rank_compare_func);

  if (!(filtered =
          gst_element_factory_list_filter (decoders, input_caps, GST_PAD_SINK,
              FALSE))) {
    gchar *tmp = gst_caps_to_string (input_caps);
    GST_DEBUG ("Cannot find any decoder for caps %s", tmp);
    g_free (tmp);

    return;
  }

  for (i = 0; i < g_list_length (filtered); i++) {
    factory = GST_ELEMENT_FACTORY_CAST (g_list_nth_data (filtered, i));
    const gchar *f_name =
        gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory));
    if (g_strcmp0 (f_name, "decproxy") && g_strcmp0 (f_name, "fakeadec")) {
      GST_DEBUG ("plugin feature(%s) will be rank as 0", f_name);
      gst_plugin_feature_set_rank (GST_PLUGIN_FEATURE (factory), 0);
    }
  }
}

GstStructure *
get_smart_property_from_source (GstElement * parent)
{
  GstIterator *iter;
  gboolean done = FALSE;
  GValue item = { 0, };
  GstStructure *smart_properties = NULL;

  iter = gst_bin_iterate_recurse (GST_BIN (parent));

  while (!done) {
    switch (gst_iterator_next (iter, &item)) {
      case GST_ITERATOR_OK:
      {
        GstElement *element = g_value_get_object (&item);
        GST_DEBUG ("Found element(%s)", GST_ELEMENT_NAME (element));
        if (g_object_class_find_property (G_OBJECT_GET_CLASS (element),
                "smart-properties")) {
          g_object_get (element, "smart-properties", &smart_properties, NULL);
          if (smart_properties) {
            GST_DEBUG ("Found smart-property: %p", smart_properties);
            done = TRUE;
          }
        }
        g_value_reset (&item);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_ERROR ("Could not iterate over source elements");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  g_value_unset (&item);
  gst_iterator_free (iter);

  return smart_properties;
}
