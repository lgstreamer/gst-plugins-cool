// --------------------------------------------------------------------------
//  LG ELECTRONICS INC., SEOUL, KOREA
//  Copyright(c) 2013 by LG Electronics Inc.
//
//  All rights reserved. No part of this work may be reproduced, stored in a
//  retrieval system, or transmitted by any means without prior written
//  permission of LG Electronics Inc.
// --------------------------------------------------------------------------

/**
 * SECTION:element-avibin
 *
 * FIXME:Describe avibin here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! avibin ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>

#include <gst/gst.h>

#include "gstavibin.h"

GST_DEBUG_CATEGORY_STATIC (gst_avibin_debug);
#define GST_CAT_DEFAULT gst_avibin_debug

#define ELEMENT_NAME "avibin"
//#define VERSION                "1.0"

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_THUMBNAIL_MODE,
  PROP_SILENT
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-msvideo;")
    );

static GstStaticPadTemplate video_src_factory =
GST_STATIC_PAD_TEMPLATE ("video_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate audio_src_factory =
GST_STATIC_PAD_TEMPLATE ("audio_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("ANY")
    );

static void gst_avibin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_avibin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_avibin_set_caps (GstPad * pad, GstCaps * caps);

static void avibin_pad_linked (GstPad * pad, GstPad * peer, gpointer data);
static GstElementClass *parent_class = NULL;
static void gst_avibin_class_init (GstAviBinClass * klass);
static void gst_avibin_init (GstAviBin * bin, GstAviBinClass * gclass);

GType
gst_avibin_get_type (void)
{
  static GType gst_avibin_type = 0;

  if (!gst_avibin_type) {
    static const GTypeInfo gst_avibin_info = {
      sizeof (GstAviBinClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_avibin_class_init,
      NULL,
      NULL,
      sizeof (GstAviBin),
      0,
      (GInstanceInitFunc) gst_avibin_init,
      NULL
    };

    gst_avibin_type = g_type_register_static (GST_TYPE_BIN,
        "GstAviBin", &gst_avibin_info, 0);
  }

  return gst_avibin_type;
}

static void
gst_avibin_finalize (GObject * object)
{
  GstAviBin *bin = GST_AVIBIN (object);

  if (bin->video_demux_pads)
    g_list_free (bin->video_demux_pads);
  if (bin->video_pads)
    g_list_free (bin->video_pads);
  if (bin->audio_demux_pads)
    g_list_free (bin->audio_demux_pads);
  if (bin->audio_pads)
    g_list_free (bin->audio_pads);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* initialize the avibin's class */
static void
gst_avibin_class_init (GstAviBinClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;
  //GstBinClass *gstbin_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;
  //gstbin_klass = (GstBinClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_klass->set_property = gst_avibin_set_property;
  gobject_klass->get_property = gst_avibin_get_property;

  gobject_klass->finalize = gst_avibin_finalize;

  gst_element_class_set_details_simple (gstelement_klass,
      "AviBin",
      "Demux",
      "supporting bin containing avi demux",
      "Hyunwoo Park <<hyonwoo.park@lge.com>>");

  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&video_src_factory));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&audio_src_factory));
  g_object_class_install_property (gobject_klass, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_klass, PROP_THUMBNAIL_MODE,
      g_param_spec_boolean ("thumbnail-mode", "Thumbnail mode",
          "Thumbnail mode", FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  GST_DEBUG ("avibin class init done");
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_avibin_init (GstAviBin * bin, GstAviBinClass * gclass)
{
  GST_DEBUG_CATEGORY_INIT (gst_avibin_debug, ELEMENT_NAME,
      0, "Template avibin");

  GstPadTemplate *templ;

  templ = gst_static_pad_template_get (&sink_factory);
  bin->sinkpad = gst_ghost_pad_new_no_target_from_template ("sink", templ);
  gst_object_unref (templ);

  /*for gst1.0 */
  /* TODO */
  //gst_pad_set_setcaps_function (bin->sinkpad,
  //                             GST_DEBUG_FUNCPTR(gst_avibin_set_caps));

  g_signal_connect (bin->sinkpad, "linked", (GCallback) avibin_pad_linked, bin);

  gst_element_add_pad (GST_ELEMENT (bin), bin->sinkpad);
  bin->fujifilm_3d = FALSE;
  bin->silent = FALSE;
  bin->thumbnail_mode = FALSE;
}

static void
gst_avibin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAviBin *bin = GST_AVIBIN (object);

  switch (prop_id) {
    case PROP_SILENT:
      bin->silent = g_value_get_boolean (value);
      break;
    case PROP_THUMBNAIL_MODE:
      bin->thumbnail_mode = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_avibin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAviBin *bin = GST_AVIBIN (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, bin->silent);
      break;
    case PROP_THUMBNAIL_MODE:
      g_value_set_boolean (value, bin->thumbnail_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

struct element_filter_info
{
  gchar *type;
  GstCaps *caps;
  GstPadDirection direction;
};

static gboolean
element_filter (GstPluginFeature * feature, struct element_filter_info *info)
{
  GstElementFactory *fac;

  if (G_UNLIKELY (!GST_IS_ELEMENT_FACTORY (feature)))
    return FALSE;

  fac = GST_ELEMENT_FACTORY_CAST (feature);

  if (info->type != NULL) {
    const gchar *klass;

    klass = gst_element_factory_get_klass (fac);
    if (!strstr (klass, info->type))
      return FALSE;
  }

  if (info->caps != NULL) {
    const GList *templates;
    GList *walk;

    //GST_DEBUG ("factory name %s, rank %d",
    //              gst_plugin_feature_get_name (feature),
    //              feature->rank);
    if (!strcmp (gst_plugin_feature_get_name (feature), ELEMENT_NAME)) {
      GST_DEBUG ("it's mine. skip.");
      return FALSE;
    }

    templates = gst_element_factory_get_static_pad_templates (fac);
    for (walk = (GList *) templates; walk; walk = g_list_next (walk)) {
      GstStaticPadTemplate *templ = (GstStaticPadTemplate *) walk->data;

      if (templ->direction == info->direction) {
        GstCaps *templ_caps;

        templ_caps = gst_static_caps_get (&templ->static_caps);

        if (gst_caps_is_subset (info->caps, templ_caps)) {
          gst_caps_unref (templ_caps);
          GST_DEBUG ("true for %s", gst_element_factory_get_longname (fac));
          return TRUE;
        }

        gst_caps_unref (templ_caps);
      }
    }
  }

  return FALSE;
}

#if 0
static gint
rank_compare_func (gconstpointer p1, gconstpointer p2)
{
  GstPluginFeature *f1, *f2;
  gint diff = 0;

  f1 = (GstPluginFeature *) p1;
  f2 = (GstPluginFeature *) p2;

#if 0
  // force to use ffdemux_avi
  if (!strcmp (gst_plugin_feature_get_name (f1), "ffdemux_avi"))
    return -1;
  if (!strcmp (gst_plugin_feature_get_name (f2), "ffdemux_avi"))
    return 1;
#endif

  diff = f2->rank - f1->rank;
  if (diff != 0)
    return diff;

  diff = strcmp (f2->name, f1->name);

  return diff;
}
#endif

static GstElement *
make_proper_element (char *type, GstCaps * caps, char *name)
{
  struct element_filter_info info;
  GList *factories, *factory;
  GstElementFactory *fac;
  GstElement *element;

  info.type = type;
  info.caps = caps;
  info.direction = GST_PAD_SINK;
  //factories = gst_default_registry_feature_filter (
  //              (GstPluginFeatureFilter)element_filter, FALSE, &info);
  //factories = g_list_sort (factories, rank_compare_func);
  //
  /*For Gst1.0 */
  factories = gst_registry_feature_filter (gst_registry_get (),
      (GstPluginFeatureFilter) element_filter, FALSE, &info);

  factories = g_list_sort (factories, gst_plugin_feature_rank_compare_func);

  factory = g_list_first (factories);
  if (factory == NULL) {
    GST_ERROR ("no factory found for %s", type);
    return NULL;
  }

  fac = (GstElementFactory *) factory->data;

  gst_plugin_feature_list_free (factories);

  GST_DEBUG ("selected factory for %s: %s(caps=%" GST_PTR_FORMAT ")",
      type, gst_element_factory_get_longname (fac), caps);
  element = gst_element_factory_create (fac, name);
  if (element == NULL)
    GST_ERROR ("cannot make %s", name);

  return element;
}

static void
avibin_mpomux_padadded (GstElement * element, GstPad * pad, gpointer data)
{
  GstAviBin *bin = GST_AVIBIN (data);
  GstPad *gpad;
  gchar *element_name;
  GstPadTemplate *templ;

  element_name = gst_element_get_name (element);
  GST_DEBUG ("we got a pad from %s, %s", element_name, GST_PAD_NAME (pad));
  g_free (element_name);

  if (GST_PAD_DIRECTION (pad) != GST_PAD_SRC) {
    GST_DEBUG ("its not src pad.");
    return;
  }

  templ = gst_static_pad_template_get (&video_src_factory);
  gpad = gst_ghost_pad_new_from_template (GST_PAD_NAME (pad), pad, templ);
  gst_object_unref (templ);

  if (gpad != NULL) {
    GstCaps *caps = NULL;
    bin->video_pads = g_list_append (bin->video_pads, gpad);

    caps = gst_pad_get_current_caps (pad);
    if (caps == NULL)
      caps = gst_pad_query_caps (pad, NULL);
    gst_pad_set_caps (gpad, caps);
    gst_caps_unref (caps);

    gst_pad_set_active (gpad, TRUE);
    gst_element_add_pad (GST_ELEMENT (bin), gpad);
  } else
    GST_ERROR ("Oops");
}

static void
avibin_demux_padadded_video (GstPad * pad, GstAviBin * bin)
{
  GstPad *srcpad = NULL;

  GstCaps *caps;
  GstStructure *s;

  /* bypass the video data to the ghost src */
  srcpad = pad;

  /* if its MJPEG, append "mpomux" element. */
  /*for gst1.0 */
  caps = gst_pad_get_current_caps (pad);

  if (caps != NULL && (s = gst_caps_get_structure (caps, 0)) != NULL) {
    const gchar *name;

    name = gst_structure_get_name (s);

    gst_element_get_smart_properties (GST_ELEMENT_CAST (bin),
        "thumbnail-mode", &bin->thumbnail_mode, NULL);

    GST_INFO_OBJECT (bin, "thumbnail-mode = %d", bin->thumbnail_mode);

    if (!strcmp (name, "image/jpeg") && !bin->thumbnail_mode) {
      GST_DEBUG ("its MJPEG. make mpomux for 3D MJPEG");

      /* make "mpomux" element */
      if (bin->mpomux == NULL) {
        bin->mpomux = gst_element_factory_make ("mpomux", NULL);
        if (bin->mpomux != NULL) {
          GST_DEBUG ("we got a new mpomux element");

          /* add to bin */
          gst_bin_add (GST_BIN (bin), bin->mpomux);
          g_signal_connect (bin->mpomux, "pad-added",
              (GCallback) avibin_mpomux_padadded, bin);
          gst_element_sync_state_with_parent (bin->mpomux);
        }
      }

      /* link with demux */
      if (bin->mpomux != NULL) {
        GstPad *vpad;

        vpad = gst_element_get_request_pad (bin->mpomux, "video_sink_%u");

        if (gst_pad_link (pad, vpad) == GST_PAD_LINK_OK) {
          srcpad = NULL;
          GST_DEBUG ("successfully linked");
        } else {
          GST_WARNING ("Oops?");
        }
      }
    }
  }

  /* link with ghost pad */
  if (srcpad != NULL) {
    GstPad *gpad;

    GstPadTemplate *templ;
    templ = gst_static_pad_template_get (&video_src_factory);
    gpad = gst_ghost_pad_new_from_template (GST_PAD_NAME (pad), srcpad, templ);
    gst_object_unref (templ);

    if (gpad != NULL) {
      bin->video_demux_pads = g_list_append (bin->video_demux_pads, pad);
      bin->video_pads = g_list_append (bin->video_pads, gpad);

      gst_pad_set_active (gpad, TRUE);
      gst_element_add_pad (GST_ELEMENT (bin), gpad);
    } else
      GST_ERROR ("Oops");
  }
}

static void
avibin_demux_padadded_audio (GstPad * pad, GstAviBin * bin)
{
  GstPad *srcpad = NULL;

  srcpad = pad;

  /* link with ghost pad */
  if (srcpad != NULL) {
    GstPad *gpad;

    GstPadTemplate *templ;
    templ = gst_static_pad_template_get (&audio_src_factory);
    gpad = gst_ghost_pad_new_from_template (GST_PAD_NAME (pad), srcpad, templ);
    gst_object_unref (templ);

    if (gpad != NULL) {
      bin->audio_demux_pads = g_list_append (bin->audio_demux_pads, pad);
      bin->audio_pads = g_list_append (bin->audio_pads, gpad);

      gst_pad_set_active (gpad, TRUE);
      gst_element_add_pad (GST_ELEMENT (bin), gpad);
    } else
      GST_ERROR ("Oops");
  }
}

static void
avibin_demux_padadded (GstElement * element, GstPad * pad, gpointer data)
{
  GstAviBin *bin = GST_AVIBIN (data);
  gchar *padname;
  gboolean thumbnail = FALSE;

  padname = gst_pad_get_name (pad);
  GST_DEBUG ("demux has a pad, %s, with %p" GST_PTR_FORMAT,
      padname, gst_pad_get_current_caps (pad));

  if (padname != NULL) {
    if (!strncmp (padname, "video_", 6))
      avibin_demux_padadded_video (pad, bin);
    else if (!strncmp (padname, "audio_", 6))
      avibin_demux_padadded_audio (pad, bin);
    g_free (padname);
  }
}

static void
avibin_demux_no_more_pads (GstElement * element, gpointer data)
{
  GstAviBin *bin = GST_AVIBIN (data);
  gchar *name;

  name = gst_element_get_name (element);

  GST_DEBUG ("got no-more-pads from %s", name);
  g_free (name);

  gst_element_no_more_pads (GST_ELEMENT (bin));
}

static int
avibin_configure_pipeline (GstAviBin * bin, GstCaps * caps)
{
  GstElement *demux;
  GstPad *sink;

  if (bin->demux != NULL) {
    GST_DEBUG ("already configured");
    return 0;
  }

  GST_DEBUG ("configure the pipeline with %p" GST_PTR_FORMAT, caps);
  demux = make_proper_element ("Demux", caps, NULL);
  if (demux == NULL) {
    GST_ELEMENT_ERROR (bin, RESOURCE, NOT_FOUND,
        ("cannot find a demux"),
        ("cannot find suitable demux for %p" GST_PTR_FORMAT, caps));
    return -1;
  }

  bin->demux = demux;
  gst_bin_add (GST_BIN (bin), demux);
  sink = gst_element_get_static_pad (demux, "sink");
  gst_ghost_pad_set_target (GST_GHOST_PAD (bin->sinkpad), sink);

  g_signal_connect (demux, "pad-added", (GCallback) avibin_demux_padadded, bin);
  g_signal_connect (demux, "no-more-pads",
      (GCallback) avibin_demux_no_more_pads, bin);
  gst_element_sync_state_with_parent (demux);
  gst_object_unref (sink);

  return 0;
}

static void
avibin_pad_linked (GstPad * pad, GstPad * peer, gpointer data)
{
  GstAviBin *bin = GST_AVIBIN (data);
  GstCaps *caps;

  GST_DEBUG ("pad linked, %s, %s", GST_PAD_NAME (pad), GST_PAD_NAME (peer));

  /*for gst1.0 */
  caps = gst_pad_get_current_caps (pad);

  if (caps != NULL) {
    GST_DEBUG ("caps on my pad");
    avibin_configure_pipeline (bin, caps);
    return;
  }

  /*for gst1.0 */
  caps = gst_pad_get_current_caps (peer);

  if (caps != NULL) {
    GST_DEBUG ("caps on peer pad");
    avibin_configure_pipeline (bin, caps);
  }
}

/* this function handles the link with other elements */
/* TODO
static gboolean
gst_avibin_set_caps (GstPad * pad, GstCaps * caps)
{
  GstAviBin *bin;

  bin = GST_AVIBIN (gst_pad_get_parent (pad));

  GST_DEBUG ("got caps, configure pipeline");
  avibin_configure_pipeline (bin, caps);

  gst_object_unref (bin);

  return TRUE;
}
*/

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
plugin_init (GstPlugin * avibin)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template avibin' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_avibin_debug, ELEMENT_NAME,
      0, "Template avibin");

  return gst_element_register (avibin, ELEMENT_NAME, GST_RANK_PRIMARY + 1,
      GST_TYPE_AVIBIN);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstavibin"
#endif

/* gstreamer looks for this structure to register avibins
 *
 * exchange the string 'Template avibin' with your avibin description
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    avibin,
    "Template avibin",
    plugin_init, PACKAGE_VERSION, "Proprietary", PACKAGE_NAME, PACKAGE_VERSION)
