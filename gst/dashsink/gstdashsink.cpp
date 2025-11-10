/*
 * GStreamer dashsink element
 *
 * Copyright 2017 LG Electronics, Inc.
 *  @author: Seoungil Kang <seoungil.kang@lge.com>
 *
 * gstdashsink.c:
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

#include <glib/gstdio.h>

#include <string.h>
#include <stdlib.h>
#include <gst/tag/tag.h>
#include "gstdashsink.hpp"

GST_DEBUG_CATEGORY (dashsink_debug);
#define GST_CAT_DEFAULT dashsink_debug

#define GST_DASHSINK_GET_LOCK(bin) (&((GstDashSink*)(bin))->lock)
#define GST_DASHSINK_LOCK(bin) (g_mutex_lock (GST_DASHSINK_GET_LOCK(bin)))
#define GST_DASHSINK_UNLOCK(bin) (g_mutex_unlock (GST_DASHSINK_GET_LOCK(bin)))
#define GST_DASHSINK_INTERVAL 90
#define GST_DASHSINK_INPUT_TIMER_INTERVAL 200

/* properties */
enum
{
  PROP_0,
  PROP_LOCATION_PREFIX,
  PROP_SEGMENT_DURATION,
  PROP_PERIOD_ID,
  PROP_USE_SOURCE_TIMESTAMP,
};

/* signals */
enum
{
  SIGNAL_UPDATE_METADATA,
  SIGNAL_LAST
};

static guint gst_dashsink_signals [SIGNAL_LAST] = { 0 };

#define DEFAULT_SEGMENT_DURATION 90

static GstStaticPadTemplate video_sink_template =
GST_STATIC_PAD_TEMPLATE ("video_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate audio_sink_template =
GST_STATIC_PAD_TEMPLATE ("audio_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate subtitle_sink_template =
GST_STATIC_PAD_TEMPLATE ("subtitle_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GQuark PAD_CONTEXT;

static gint
gcd (gint num, gint den)
{
  int tmp = 0;
  while (num > 0) { tmp = num; num = den % num; den = tmp; }
  return den;
}

static void
_do_init (void)
{
  PAD_CONTEXT = g_quark_from_static_string ("pad-context");
}

#define gst_dashsink_parent_class parent_class
G_DEFINE_TYPE_EXTENDED (GstDashSink, gst_dashsink, GST_TYPE_BIN, 0,
    _do_init ());

//////////////////////////////////////////////////////////////////////////////
// Gst Dash Sink
static void gst_dashsink_dispose (GObject * self);
static void gst_dashsink_finalize (GObject * self);
static void gst_dashsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dashsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstPad *gst_dashsink_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_dashsink_release_pad (GstElement * element, GstPad * pad);

static GstStateChangeReturn gst_dashsink_change_state (GstElement *
    element, GstStateChange transition);

static void gst_dashsink_bus_handler (GstBin * bin, GstMessage * msg);
static gboolean gst_dashsink_query (GstElement * element, GstQuery * query);
static void gst_dashsink_update_metadata (GstDashSink * dashsink, gpointer id);
static void gst_dashsink_store_manifest (GstDashSink * dashsink);

//////////////////////////////////////////////////////////////////////////////
// Media Stream Context
static MediaStreamCtx *media_stream_ctx_new (GstDashSink * dashsink,
    const gchar * name_template);
static void media_stream_ctx_free (MediaStreamCtx * ctx);
static void media_stream_ctx_unref (MediaStreamCtx * ctx);
static void media_stream_ctx_ref (MediaStreamCtx * ctx);
static void media_stream_ctx_reset_elements (MediaStreamCtx * ctx);
static void media_stream_ctx_reset_variables (MediaStreamCtx * ctx);
static GstElement *media_stream_ctx_create_element (MediaStreamCtx * ctx,
    const gchar * factory, const gchar * name);
static gboolean media_stream_ctx_create_muxer (MediaStreamCtx * ctx);
static gboolean media_stream_ctx_create_sink (MediaStreamCtx * ctx);
static void media_stream_ctx_set_next_filename (MediaStreamCtx * ctx);
static void media_stream_ctx_update_first_ts (MediaStreamCtx * ctx,
    GstClockTime ts);
static GstPadProbeReturn media_stream_ctx_handle_input (GstPad * pad,
    GstPadProbeInfo * info, MediaStreamCtx * ctx);
static void media_stream_ctx_handle_input_destroy (MediaStreamCtx * ctx);
static gboolean media_stream_ctx_resend_sticky (GstPad * pad,
    GstEvent ** event, GstPad * peer);
static void media_stream_ctx_restart_context (MediaStreamCtx * ctx);
static void media_stream_ctx_send_eos (MediaStreamCtx * ctx);
static void media_stream_ctx_start_next_fragment (MediaStreamCtx * ctx);
static void media_stream_ctx_schedule_next_fragment (MediaStreamCtx * ctx);
static gboolean media_stream_ctx_check_pipeline_state (MediaStreamCtx * ctx);
static gboolean media_stream_ctx_check_next_fragment (MediaStreamCtx * ctx);
static void media_stream_ctx_update_running_time (MediaStreamCtx * ctx,
    GstClockTime ts);
static void media_stream_ctx_init_segment_id (MediaStreamCtx * ctx);
static gboolean media_stream_ctx_check_input_delay (gpointer data);
static void media_stream_ctx_check_file_header (GstElement *muxer,
    gpointer data);

static void
gst_dashsink_class_init (GstDashSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class = (GstBinClass *) klass;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_dashsink_set_property;
  gobject_class->get_property = gst_dashsink_get_property;
  gobject_class->dispose = gst_dashsink_dispose;
  gobject_class->finalize = gst_dashsink_finalize;

  gst_element_class_add_static_pad_template (gstelement_class,
      &video_sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &audio_sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &subtitle_sink_template);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_dashsink_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_dashsink_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_dashsink_release_pad);
  gstelement_class->query = GST_DEBUG_FUNCPTR (gst_dashsink_query);

  gstbin_class->handle_message = gst_dashsink_bus_handler;

  gst_element_class_set_static_metadata (gstelement_class,
      "DashSink Bin", "Generic/Bin",
      "It's DashSink", "Seoungil Kang <seoungil.kang@lge.com>");

  g_object_class_install_property (gobject_class, PROP_LOCATION_PREFIX,
      g_param_spec_string ("location-prefix", "File Output Pattern",
          "Format string pattern for prefix of the location of the files to "
          "write (e.g. prefix%05d)\n"
          "                        "
          "Suffix will be automatically added to the context.", NULL,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SEGMENT_DURATION,
      g_param_spec_uint ("segment-duration", "Duration of a segment (sec)",
          "Duration of a segment in sec.", 0, G_MAXUINT,
          DEFAULT_SEGMENT_DURATION,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_PERIOD_ID,
      g_param_spec_uint ("period-id", "(RO) id of current period",
          "(RO) id of current period", 0, G_MAXUINT, 0,
          (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_USE_SOURCE_TIMESTAMP,
      g_param_spec_boolean ("use-source-timestamp",
          "select the method for processing timestamp",
          "select the method for processing timestamp.\n"
          "                        "
          "A boolean property indicates that: \n"
          "                        "
          "1. (false : default) Use timestamp incresing to 0 based on "
          "the starting value of current segment.\n"
          "                        "
          "2. (true) The container and manifest use timestamp from source's "
          "one \n"
          "                        "
          "   and the attributes for adjusting timestamp at playback time "
          "are as below:\n"
          "                        "
          "   - \"presentationTimeOffset\" on Segment element of MPD.\n"
          "                        "
          "   - \"earliest_presentation_time\" on sidx box of ISOBMFF.\n"
          "                        "
          "   - \"baseMediaDecodeTime\" on tfdt box of ISOBMFF.",
          FALSE,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_dashsink_signals [SIGNAL_UPDATE_METADATA] =
      g_signal_new ("update-metadata", G_TYPE_FROM_CLASS (klass),
      (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
      G_STRUCT_OFFSET (GstDashSinkClass, update_metadata),
      NULL, NULL, g_cclosure_marshal_VOID__POINTER,
      G_TYPE_NONE, 1, G_TYPE_POINTER);

  klass->update_metadata = GST_DEBUG_FUNCPTR(gst_dashsink_update_metadata);

  GST_DEBUG_CATEGORY_INIT (dashsink_debug, "dashsink", 0, "DashSink");
}

static void
gst_dashsink_init (GstDashSink * dashsink)
{
  dashsink->location_prefix = NULL;
  dashsink->media_prefix = NULL;
  dashsink->segment_duration = DEFAULT_SEGMENT_DURATION;
  dashsink->use_source_timestamp = FALSE;
  g_mutex_init (&dashsink->lock);

  dashsink->video_id = 0;
  dashsink->audio_id = 0;
  dashsink->subtitle_id = 0;
  dashsink->contexts = NULL;
  dashsink->internal_eos = FALSE;
  dashsink->presentation_time_offset = GST_CLOCK_STIME_NONE;

  dashsink->manifest_generator = new ManifestGenerator();
}

static void
gst_dashsink_reset (GstDashSink * dashsink)
{
}

static void
gst_dashsink_store_manifest (GstDashSink * dashsink)
{
  GList *walk;
  gboolean collect_all_pad = TRUE;

  for (walk = dashsink->contexts; walk != NULL; walk = g_list_next (walk)) {
    MediaStreamCtx * current_ctx = (MediaStreamCtx *) walk->data;
    if (current_ctx->is_first_buffer) {
      collect_all_pad = FALSE;
      break;
    }
  }

  if (collect_all_pad)
    dashsink->manifest_generator->storeManifest ();
}

static void
gst_dashsink_dispose (GObject * object)
{
  GstDashSink *dashsink = GST_DASHSINK (object);
  gst_dashsink_store_manifest (dashsink);
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_dashsink_finalize (GObject * object)
{
  GstDashSink *dashsink = GST_DASHSINK (object);

  g_mutex_clear (&dashsink->lock);

  g_free (dashsink->location_prefix);
  g_free (dashsink->media_prefix);

  gst_dashsink_store_manifest (dashsink);
  delete dashsink->manifest_generator;

  g_list_foreach (dashsink->contexts, (GFunc) media_stream_ctx_unref, NULL);
  g_list_free (dashsink->contexts);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_dashsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDashSink *dashsink = GST_DASHSINK (object);

  switch (prop_id) {
    case PROP_LOCATION_PREFIX:{
      GST_OBJECT_LOCK (dashsink);
      g_free (dashsink->location_prefix);
      dashsink->location_prefix = g_value_dup_string (value);
      {
        gchar * bname = g_path_get_basename (dashsink->location_prefix);
        gchar * dname = g_path_get_dirname (dashsink->location_prefix);
        gchar * recid = g_strndup (bname, 8);
        gchar * mpdname = g_strdup_printf ("%sMPD", recid);
        gchar * mpdpath = g_build_filename (dname, mpdname, NULL);
        g_free (dashsink->media_prefix);
        dashsink->media_prefix = g_strdup_printf ("%s$Number%%08d$", recid);
        if (dashsink->manifest_generator->loadManifest (mpdpath))
          dashsink->manifest_generator->addPeriod ();
        g_free (bname);
        g_free (dname);
        g_free (recid);
        g_free (mpdname);
        g_free (mpdpath);
      }
      GST_OBJECT_UNLOCK (dashsink);
      break;
    }
    case PROP_SEGMENT_DURATION:{
      GST_OBJECT_LOCK (dashsink);
      dashsink->segment_duration = g_value_get_uint (value);
      GST_OBJECT_UNLOCK (dashsink);
      break;
    }
    case PROP_USE_SOURCE_TIMESTAMP:{
      GST_OBJECT_LOCK (dashsink);
      dashsink->use_source_timestamp = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (dashsink);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dashsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDashSink *dashsink = GST_DASHSINK (object);

  switch (prop_id) {
    case PROP_LOCATION_PREFIX:{
      GST_OBJECT_LOCK (dashsink);
      g_value_set_string (value, dashsink->location_prefix);
      GST_OBJECT_UNLOCK (dashsink);
      break;
    }
    case PROP_SEGMENT_DURATION:{
      GST_OBJECT_LOCK (dashsink);
      g_value_set_uint (value, dashsink->segment_duration);
      GST_OBJECT_UNLOCK (dashsink);
      break;
    }
    case PROP_PERIOD_ID:{
      GST_OBJECT_LOCK (dashsink);
      g_value_set_uint (value,
          dashsink->manifest_generator->getCurrentPeriodId());
      GST_OBJECT_UNLOCK (dashsink);
      break;
    }
    case PROP_USE_SOURCE_TIMESTAMP:{
      GST_OBJECT_LOCK (dashsink);
      g_value_set_boolean (value, dashsink->use_source_timestamp);
      GST_OBJECT_UNLOCK (dashsink);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstPad *
gst_dashsink_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  GstDashSink *dashsink = GST_DASHSINK (element);
  GstPad *res = NULL;
  MediaStreamCtx *ctx = NULL;
  GstPad *mux_sink = NULL;
  GstPadTemplate *mux_template = NULL;

  GST_INFO_OBJECT (dashsink, "templ:%s, name:%s", templ->name_template, name);
  if (!templ->name_template)
    goto fail;

  GST_DASHSINK_LOCK (dashsink);

  if ((ctx = media_stream_ctx_new (dashsink, templ->name_template)) == NULL)
    goto fail;

  if (!media_stream_ctx_create_muxer (ctx))
    goto fail;

  if (!media_stream_ctx_create_sink (ctx))
    goto fail;

  mux_template = gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS
      (ctx->muxer), templ->name_template);
  mux_sink = gst_element_request_pad (ctx->muxer, mux_template, name, caps);
  if (!mux_sink)
    goto fail;

  res = gst_ghost_pad_new (ctx->name, mux_sink);
  g_object_set_qdata ((GObject *) (res), PAD_CONTEXT, ctx);

  media_stream_ctx_ref (ctx);
  ctx->probe_id =
      gst_pad_add_probe (res, GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM,
      (GstPadProbeCallback) media_stream_ctx_handle_input, ctx,
      (GDestroyNotify) media_stream_ctx_handle_input_destroy);

  gst_object_unref (mux_sink);

  dashsink->contexts = g_list_prepend (dashsink->contexts, ctx);

  gst_pad_set_active (res, TRUE);
  gst_element_add_pad (element, res);
  ctx->g_pad = res;
  media_stream_ctx_set_next_filename (ctx);
  GST_INFO_OBJECT (dashsink, "OK : %" GST_PTR_FORMAT, res);

  GST_DASHSINK_UNLOCK (dashsink);

  return res;
fail:
  if (ctx)
    media_stream_ctx_unref (ctx);
  GST_DASHSINK_UNLOCK (dashsink);
  return NULL;
}

static void
gst_dashsink_release_pad (GstElement * element, GstPad * pad)
{
  GstDashSink *dashsink = (GstDashSink *) element;
  GstPad *mux_sink = NULL;
  GstObject *muxer = NULL;
  MediaStreamCtx *ctx =
      (MediaStreamCtx *) (g_object_get_qdata ((GObject *) (pad), PAD_CONTEXT));

  GST_DASHSINK_LOCK (dashsink);
  GST_INFO_OBJECT (dashsink, "Attempting to remove the pad %" GST_PTR_FORMAT,
      pad);

  mux_sink = gst_ghost_pad_get_target (GST_GHOST_PAD (pad));

  dashsink->contexts = g_list_remove (dashsink->contexts, ctx);

  if (ctx->probe_id)
    gst_pad_remove_probe (pad, ctx->probe_id);

  media_stream_ctx_unref (ctx);

  muxer = gst_pad_get_parent (mux_sink);

  gst_element_release_request_pad (GST_ELEMENT (muxer), mux_sink);
  gst_object_unref (mux_sink);
  gst_object_unref (muxer);

  gst_element_remove_pad (element, pad);

  if (dashsink->contexts == NULL)
    gst_dashsink_reset (dashsink);

  GST_INFO_OBJECT (dashsink, "DONE");
  GST_DASHSINK_UNLOCK (dashsink);
}

static GstStateChangeReturn
gst_dashsink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstDashSink *dashsink = GST_DASHSINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{
      GST_INFO_OBJECT (dashsink, "State change -> READY");
      break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:{
      GST_INFO_OBJECT (dashsink, "State change -> PAUSED");
      g_list_foreach (dashsink->contexts,
          (GFunc) media_stream_ctx_reset_variables, NULL);
      break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto beach;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_INFO_OBJECT (dashsink, "State change -> NULL");
      break;
    default:
      break;
  }

beach:

  if (transition == GST_STATE_CHANGE_NULL_TO_READY &&
      ret == GST_STATE_CHANGE_FAILURE) {
    /* Cleanup elements on failed transition out of NULL */
    gst_dashsink_reset (dashsink);
  }
  return ret;
}

static void
gst_dashsink_bus_handler (GstBin * bin, GstMessage * message)
{
  GstDashSink *dashsink = GST_DASHSINK (bin);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:
      /* If the state is draining out the current file, drop this EOS */
      GST_INFO_OBJECT (dashsink, "<%s> EOS",
          GST_OBJECT_NAME (GST_MESSAGE_SRC (message)));
      if (dashsink->internal_eos) {
        GST_INFO_OBJECT (dashsink, "Caught internal EOS, dropping");
        dashsink->internal_eos = FALSE;
        gst_message_unref (message);
        return;
      }
      GST_OBJECT_LOCK (dashsink);
      gst_dashsink_store_manifest (dashsink);
      GST_OBJECT_UNLOCK (dashsink);
      break;
    case GST_MESSAGE_STATE_CHANGED:
      {
        GstState old = GST_STATE_NULL;
        GstState next = GST_STATE_NULL;
        gst_message_parse_state_changed (message, &old, &next, NULL);
        GST_INFO_OBJECT (dashsink, "<%s> %s -> %s",
            GST_OBJECT_NAME (GST_MESSAGE_SRC (message)),
            gst_element_state_get_name (old),
            gst_element_state_get_name (next));
      }
      break;
    default:
      break;
  }

  GST_BIN_CLASS (parent_class)->handle_message (bin, message);
}

static gboolean
gst_dashsink_query (GstElement * element, GstQuery * query)
{
  GstDashSink *dashsink = GST_DASHSINK (element);

  gboolean ret = GST_ELEMENT_CLASS (parent_class)->query (element, query);

  switch (GST_QUERY_TYPE (query))
  {
    case GST_QUERY_POSITION:
      gst_query_set_position (query, GST_FORMAT_TIME,
          dashsink->manifest_generator->getOverallDuration ());
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_dashsink_update_metadata (GstDashSink * dashsink, gpointer id)
{
  *((int *)id) = dashsink->manifest_generator->addEvent ();
}

//////////////////////////////////////////////////////////////////////////////
// Media Stream Context
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
static MediaStreamCtx *
media_stream_ctx_new (GstDashSink * dashsink, const gchar * name_template)
{
  ManifestGenerator::StreamType type = ManifestGenerator::AUDIO;
  MediaStreamCtx *ctx = NULL;

  ctx = g_new0 (MediaStreamCtx, 1);
  g_atomic_int_set (&ctx->refcount, 1);
  ctx->dashsink = dashsink;

  ctx->is_reference = FALSE;
  ctx->is_first_fragment = TRUE;
  ctx->is_first_buffer = TRUE;

  if (g_str_has_prefix (name_template, "video")) {
    if (dashsink->video_id == 0)
      ctx->is_reference = TRUE;
    ctx->suffix = g_strdup_printf ("STRV%02d", dashsink->video_id);
    ctx->name = g_strdup_printf (name_template, dashsink->video_id++);
    type = ManifestGenerator::VIDEO;
  } else if (g_str_has_prefix (name_template, "audio")) {
    ctx->suffix = g_strdup_printf ("STRA%02d", dashsink->audio_id);
    ctx->name = g_strdup_printf (name_template, dashsink->audio_id++);
    type = ManifestGenerator::AUDIO;
  } else if (g_str_has_prefix (name_template, "subtitle")) {
    ctx->suffix = g_strdup_printf ("STRC%02d", dashsink->subtitle_id);
    ctx->name = g_strdup_printf (name_template, dashsink->subtitle_id++);
    type = ManifestGenerator::SUBTITLE;
  }

  ctx->current_file_name = NULL;
  ctx->probe_id = 0;
  ctx->input_timer_id = 0;
  ctx->delay_count = 0;
  ctx->remove_file = FALSE;
  media_stream_ctx_init_segment_id (ctx);
  {
    gchar * media = g_strdup_printf ("%s%s", dashsink->media_prefix,
        ctx->suffix);
    ctx->adaptationset_id = dashsink->manifest_generator->addAdaptationSet (type,
        media, dashsink->segment_duration * 1000, ctx->segment_id);
    g_free (media);
  }

  gst_segment_init (&ctx->segment, GST_FORMAT_UNDEFINED);
  ctx->segment_duration = dashsink->segment_duration * GST_SECOND;
  ctx->next_start_time = 0;
  ctx->first_ts = 0;
  ctx->running_time = 0;
  ctx->presentation_time_offset = 0;

  ctx->g_pad = NULL;
  ctx->muxer = NULL;
  ctx->sink = NULL;

  GST_INFO_OBJECT (dashsink, "create ctx : %s", ctx->name);
  return ctx;
}

static void
media_stream_ctx_free (MediaStreamCtx * ctx)
{
  GST_INFO_OBJECT (ctx->dashsink, "destroy ctx : %s", ctx->name);
  media_stream_ctx_reset_elements (ctx);
  if (ctx->remove_file && remove (ctx->current_file_name) == 0) {
    GST_INFO_OBJECT (ctx->dashsink, "remove invalid file : %s",
        ctx->current_file_name);
    ctx->remove_file = FALSE;
  }
  g_free (ctx->name);
  g_free (ctx->suffix);
  g_free (ctx->current_file_name);
  g_free (ctx);
}

static void
media_stream_ctx_unref (MediaStreamCtx * ctx)
{
  if (g_atomic_int_dec_and_test (&ctx->refcount))
    media_stream_ctx_free (ctx);
}

static void
media_stream_ctx_ref (MediaStreamCtx * ctx)
{
  g_atomic_int_inc (&ctx->refcount);
}

static void
media_stream_ctx_reset_elements (MediaStreamCtx * ctx)
{
  GstDashSink *dashsink = ctx->dashsink;
  if (ctx->muxer)
    gst_bin_remove (GST_BIN (dashsink), ctx->muxer);
  if (ctx->sink)
    gst_bin_remove (GST_BIN (dashsink), ctx->sink);
  ctx->muxer = ctx->sink = NULL;
}

static void
media_stream_ctx_reset_variables (MediaStreamCtx * ctx)
{
}

static GstElement *
media_stream_ctx_create_element (MediaStreamCtx * ctx, const gchar * factory,
    const gchar * name)
{
  GstDashSink *dashsink = ctx->dashsink;
  GstElement *ret = gst_element_factory_make (factory, name);
  if (ret == NULL) {
    g_warning ("Failed to create %s - dashsink will not work", name);
    return NULL;
  }

  if (!gst_bin_add (GST_BIN (dashsink), ret)) {
    g_warning ("Could not add %s element - dashsink will not work", name);
    gst_object_unref (ret);
    return NULL;
  }

  return ret;
}


static gboolean
media_stream_ctx_create_muxer (MediaStreamCtx * ctx)
{
  GstDashSink *dashsink = ctx->dashsink;
  do {
    if (ctx->muxer == NULL) {
      gchar *name = g_strdup_printf ("%s-mux", ctx->name);
      guint movie_timescale = 0;
      guint trak_timescale = 0;
      if ((ctx->muxer =
              media_stream_ctx_create_element (ctx, "mp4dashmux",
                  name)) == NULL) {
        g_free (name);
        break;
      }

      if (g_str_has_prefix (ctx->name, "video")) {
        movie_timescale = 10000;
        trak_timescale = 10000;
      }
      g_object_set (ctx->muxer, "fragment-duration", 1000,
          "segment-duration", dashsink->segment_duration * 1000,
          "dvr-encryption", TRUE,
          "movie-timescale", movie_timescale,
          "trak-timescale", trak_timescale, NULL);
      g_free (name);

      g_signal_connect (ctx->muxer, "send_header",
          G_CALLBACK(media_stream_ctx_check_file_header), ctx);
    }
    return TRUE;
  } while (0);
  return FALSE;
}

static gboolean
media_stream_ctx_create_sink (MediaStreamCtx * ctx)
{
  do {
    /* Create internal elements */
    if (ctx->sink == NULL) {
      gchar *name = g_strdup_printf ("%s-sink", ctx->name);
      if ((ctx->sink =
              media_stream_ctx_create_element (ctx, "filesink",
                  name)) == NULL) {
        g_free (name);
        break;
      }

      g_object_set (ctx->sink, "buffer-mode",
          _IONBF, NULL);
      g_free (name);
    }

    if (!gst_element_link (ctx->muxer, ctx->sink)) {
      g_warning ("Failed to link muxer and sink- dashsink will not work");
      break;
    }

    return TRUE;
  } while (0);
  return FALSE;
}

static void
media_stream_ctx_set_next_filename (MediaStreamCtx * ctx)
{
  gchar *prefix = NULL;
  GstDashSink *dashsink = ctx->dashsink;

  prefix = dashsink->location_prefix ?
      g_strdup_printf (dashsink->location_prefix, ctx->segment_id) : NULL;

  if (prefix) {
    g_free (ctx->current_file_name);
    ctx->current_file_name = g_strdup_printf ("%s%s", prefix, ctx->suffix);
    GST_INFO_OBJECT (dashsink, "<%s> Setting file to %s", ctx->name,
        ctx->current_file_name);
    g_object_set (ctx->sink, "location", ctx->current_file_name, NULL);
    g_free (prefix);
  }
}

static void
media_stream_ctx_update_first_ts (MediaStreamCtx * ctx, GstClockTime ts)
{
  GstDashSink *dashsink = ctx->dashsink;
  if (GST_CLOCK_TIME_IS_VALID (ts)) {
    ctx->first_ts = ts;
    ctx->presentation_time_offset = ctx->first_ts - ctx->segment.start;
    dashsink->manifest_generator->updatePresentationTimeOffset (
        ctx->adaptationset_id, ctx->presentation_time_offset);
    GST_INFO_OBJECT (dashsink, "<%s> presentation time offset %"
        G_GINT64_FORMAT, ctx->name, ctx->presentation_time_offset);
    GST_OBJECT_LOCK (dashsink);
    if ((ctx->presentation_time_offset > 0) && (
        (dashsink->presentation_time_offset == GST_CLOCK_STIME_NONE) ||
        (dashsink->presentation_time_offset > ctx->presentation_time_offset)))
    {
      dashsink->presentation_time_offset = ctx->presentation_time_offset;
      GST_INFO_OBJECT (dashsink, "<%s> the smallest presentation time"
          "offset to : %" G_GINT64_FORMAT, ctx->name,
          dashsink->presentation_time_offset);
    }
    GST_OBJECT_UNLOCK (dashsink);
  }
}

static GstPadProbeReturn
media_stream_ctx_handle_input (GstPad * pad, GstPadProbeInfo * info,
    MediaStreamCtx * ctx)
{
  GstDashSink *dashsink = ctx->dashsink;
  GstBuffer *buf;
  GstClockTime ts;

  if (info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
    GstEvent *event = gst_pad_probe_info_get_event (info);
    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_CAPS:{
        GstCaps * caps = NULL;
        GstStructure * s = NULL;
        gint width = 0;
        gint height = 0;
        gint g = 1;
        gint mpegversion = 0;
        gint channels = 0;

        gst_event_parse_caps (event, &caps);
        if (caps != NULL)
          s = gst_caps_get_structure (caps, 0);

        if ((s != NULL) &&
            (g_str_has_prefix (gst_structure_get_name (s), "video"))) {
          if (gst_structure_has_field (s, "width") &&
              gst_structure_has_field (s, "height")) {
            gst_structure_get_int (s, "width", &width);
            gst_structure_get_int (s, "height", &height);
          }
          if ((width != 0) && (height != 0)) {
            g = gcd (abs (width), abs (height));
            gchar * aspect_ratio = g_strdup_printf ("%d:%d", width/g, height/g);
            GST_INFO_OBJECT (dashsink, "ASPECT RATIO (%s)", aspect_ratio);
            dashsink->manifest_generator->updateAdaptationSet (
                ctx->adaptationset_id, "par", aspect_ratio);
            g_free (aspect_ratio);
          }
        }
        if ((s != NULL) &&
            (g_str_has_prefix (gst_structure_get_name (s), "audio"))) {
          if (gst_structure_has_field (s, "mpegversion") &&
              gst_structure_has_field (s, "channels")) {
            gst_structure_get_int (s, "mpegversion", &mpegversion);
            gst_structure_get_int (s, "channels", &channels);
          }
          if ((mpegversion == 4) && (channels != 0)) {
            gchar * codec = g_strdup_printf("mp4a.40.2");
            gchar * channel = g_strdup_printf ("%d", channels);
            GST_INFO_OBJECT (dashsink, "audio codec (%s) channel (%s)", codec, channel);
            dashsink->manifest_generator->updateRepresentation (
                ctx->adaptationset_id, "codecs", codec);
            dashsink->manifest_generator->updateAudioChannelConfiguration (
                ctx->adaptationset_id, "value", channel);
            g_free (codec);
            g_free (channel);
          }
        }
        break;
      }
      case GST_EVENT_SEGMENT:
        GST_INFO_OBJECT (dashsink, "SEGMENT : %" GST_PTR_FORMAT, event);
        gst_event_copy_segment (event, &ctx->segment);
        if (dashsink->use_source_timestamp)
          return GST_PAD_PROBE_DROP;
        break;
      case GST_EVENT_TAG:{
        GstTagList *list;
        gchar *code;
        gchar *role;

        gst_event_parse_tag (event, &list);
        GST_INFO_OBJECT (dashsink, "TAG %s:%s : %" GST_PTR_FORMAT,
            GST_DEBUG_PAD_NAME (pad), list);

        if (gst_tag_list_get_string (list, GST_TAG_LANGUAGE_CODE, &code)) {
          const char *iso_code = gst_tag_get_language_code_iso_639_2T (code);
          GST_INFO_OBJECT (dashsink, "CODE : %s, ISO_CODE : %s", code, iso_code);
          if (iso_code) {
            dashsink->manifest_generator->updateAdaptationSet (
                ctx->adaptationset_id, "lang", iso_code);
          }
          g_free (code);
        }

        if (gst_tag_list_get_string (list, "role", &role)) {
          GST_INFO_OBJECT (dashsink, "ROLE : %s", role);
          if (g_strcmp0(role, "0")) {
            dashsink->manifest_generator->updateAdaptationSetRole (
                ctx->adaptationset_id, role);
          }
          g_free (role);
        }
        break;
      }
      case GST_EVENT_EOS:{
        if (ctx->input_timer_id) {
          GST_INFO_OBJECT (ctx->dashsink,
              "[%s] Remove timer schedule checking input delay", ctx->name);
          g_source_remove (ctx->input_timer_id);
          ctx->input_timer_id = 0;
        }
        break;
      }
      default:
        break;
    }
    return GST_PAD_PROBE_PASS;
  }

  buf = gst_pad_probe_info_get_buffer (info);
  ctx->delay_count = 0;

  if (GST_BUFFER_PTS_IS_VALID (buf))
    ts = GST_BUFFER_PTS (buf);
  else
    ts = GST_BUFFER_DTS (buf);

  if (ts < ctx->segment.start) {
    GST_INFO_OBJECT (ctx->dashsink,
        "[%s][PTS: %" G_GUINT64_FORMAT "] Drop data earlier than sgment start time",
        ctx->name, ts);
    return GST_PAD_PROBE_DROP;
  }

  if (ctx->is_first_buffer) {
    if (dashsink->use_source_timestamp) {
      ctx->segment.start = 0;
      ctx->segment.position = 0;
      ctx->segment.time = 0;
      gst_pad_store_sticky_event (ctx->g_pad, gst_event_new_segment(&ctx->segment));
      media_stream_ctx_restart_context (ctx);
    }

    ctx->is_first_buffer = FALSE;
    media_stream_ctx_update_first_ts (ctx, ts);
    ctx->input_timer_id = g_timeout_add (GST_DASHSINK_INPUT_TIMER_INTERVAL,
        media_stream_ctx_check_input_delay, ctx);
  }

  if (media_stream_ctx_check_next_fragment (ctx))
    media_stream_ctx_update_running_time (ctx, ts);

  return GST_PAD_PROBE_PASS;
}

static void
media_stream_ctx_handle_input_destroy (MediaStreamCtx * ctx)
{
  ctx->probe_id = 0;
  media_stream_ctx_unref (ctx);
}

static gboolean
media_stream_ctx_resend_sticky (GstPad * pad, GstEvent ** event, GstPad * peer)
{
  GST_INFO_OBJECT (pad, "RESEND %" GST_PTR_FORMAT, *event);
  return gst_pad_send_event (peer, gst_event_ref (*event));
}

static void
media_stream_ctx_restart_context (MediaStreamCtx * ctx)
{
  GstPad *peer = gst_ghost_pad_get_target (GST_GHOST_PAD (ctx->g_pad));

  gst_pad_sticky_events_foreach (ctx->g_pad,
      (GstPadStickyEventsForeachFunction) (media_stream_ctx_resend_sticky),
      peer);

  gst_object_unref (peer);
}

static void
media_stream_ctx_send_eos (MediaStreamCtx * ctx)
{
  GstEvent *eos = NULL;
  GstPad *pad = NULL;
  GstDashSink *dashsink = ctx->dashsink;

  eos = gst_event_new_eos ();
  pad = gst_ghost_pad_get_target (GST_GHOST_PAD (ctx->g_pad));

  dashsink->internal_eos = TRUE;
  gst_pad_send_event (pad, eos);
  gst_object_unref (pad);
  GST_INFO_OBJECT (dashsink, "Sent EOS on %" GST_PTR_FORMAT, pad);
}

static void
media_stream_ctx_start_next_fragment (MediaStreamCtx * ctx)
{
  GstDashSink *dashsink = ctx->dashsink;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  if (ctx->is_first_fragment != TRUE)
    media_stream_ctx_send_eos (ctx);
  else {
    ctx->is_first_fragment = FALSE;
    return;
  }

  /* change to new file */
  ret = gst_element_set_state (ctx->muxer, GST_STATE_NULL);
  GST_INFO_OBJECT (dashsink, "<%s> muxer set_state_return : %u", ctx->name, ret);
  ret = gst_element_set_state (ctx->sink, GST_STATE_NULL);
  GST_INFO_OBJECT (dashsink, "<%s> sink set_state_return : %u", ctx->name, ret);

  media_stream_ctx_set_next_filename (ctx);

  gst_element_sync_state_with_parent (ctx->sink);
  gst_element_sync_state_with_parent (ctx->muxer);

  media_stream_ctx_restart_context (ctx);

  GST_INFO_OBJECT (dashsink, "<%s> Restarting flow for new fragment. ",
      ctx->name);
}

static void
media_stream_ctx_schedule_next_fragment (MediaStreamCtx * ctx)
{
  GstDashSink *dashsink = ctx->dashsink;

  ctx->next_start_time = ((ctx->running_time / GST_SECOND) +
      dashsink->segment_duration) * GST_SECOND;
  ctx->segment_id++;
  GST_INFO_OBJECT (dashsink, "<%s> Next Fragment is scheduled to %"
      GST_TIME_FORMAT, ctx->name, GST_TIME_ARGS (ctx->next_start_time));
}

static gboolean
media_stream_ctx_check_pipeline_state (MediaStreamCtx * ctx)
{
  GstDashSink *dashsink = ctx->dashsink;
  GstState state = GST_STATE_NULL;
  GstStateChangeReturn state_return = GST_STATE_CHANGE_FAILURE;
  gboolean ret = FALSE;

  GST_INFO_OBJECT (dashsink,
      "<%s> Check whether the pipeline is in playing state.", ctx->name);

  state_return = gst_element_get_state (GST_ELEMENT_PARENT (dashsink),
      &state, NULL, GST_CLOCK_TIME_NONE);

  GST_INFO_OBJECT (dashsink, "<%s> state change %s, current state of %s is %s.",
      ctx->name, state_return == GST_STATE_CHANGE_FAILURE ? "failed" : "succeeded",
      GST_ELEMENT_NAME (GST_ELEMENT_PARENT (dashsink)),
      gst_element_state_get_name (state));

  do {
    if (state_return == GST_STATE_CHANGE_FAILURE) {
      GST_INFO_OBJECT (dashsink, "<%s> state change failed, do nothing...",
          ctx->name);
      break;
    }

    if (state != GST_STATE_PLAYING) {
      GST_INFO_OBJECT (dashsink,
          "<%s> The pipeline is not in playing state... retry", ctx->name);
      break;
    }

    ret = TRUE;
  } while (0);

  return ret;
}

static gboolean
media_stream_ctx_check_next_fragment (MediaStreamCtx * ctx)
{
  GstDashSink *dashsink = ctx->dashsink;
  if (ctx->running_time > ctx->next_start_time) {
    GST_DASHSINK_LOCK (dashsink);
    GST_INFO_OBJECT (dashsink, "<%s> NEXT FILE", ctx->name);
    if (!media_stream_ctx_check_pipeline_state (ctx)) {
      GST_DASHSINK_UNLOCK (dashsink);
      return FALSE;
    }
    if (ctx->is_reference)
      gst_dashsink_store_manifest (dashsink);
    media_stream_ctx_start_next_fragment (ctx);
    media_stream_ctx_schedule_next_fragment (ctx);
    GST_DASHSINK_UNLOCK (dashsink);
  }
  return TRUE;
}

static void
media_stream_ctx_update_running_time (MediaStreamCtx * ctx, GstClockTime ts)
{
  GstDashSink *dashsink = ctx->dashsink;

  if (GST_CLOCK_TIME_IS_VALID (ts)) {
    if (ts < ctx->first_ts || ctx->presentation_time_offset < 0)
      media_stream_ctx_update_first_ts (ctx, ts);

    GstClockTime running_time = (ts - ctx->first_ts);
    if (GST_CLOCK_TIME_IS_VALID (running_time) &&
        (ctx->running_time == GST_CLOCK_TIME_NONE ||
            running_time > ctx->running_time)
        )
      ctx->running_time = running_time;
  }

  /* Try to make sure we have a valid running time */
  if (!GST_CLOCK_TIME_IS_VALID (ctx->running_time)) {
    ctx->running_time = ctx->first_ts;
  }

  if ((dashsink->presentation_time_offset != GST_CLOCK_STIME_NONE) &&
      (dashsink->presentation_time_offset >= 0) &&
      (dashsink->presentation_time_offset < ctx->presentation_time_offset))
  {
    GST_INFO_OBJECT (dashsink, "<%s> update context presentation time offset : %"
        G_GINT64_FORMAT " => %" G_GINT64_FORMAT, ctx->name,
        ctx->presentation_time_offset, dashsink->presentation_time_offset);

    dashsink->manifest_generator->updatePresentationTimeOffset (
        ctx->adaptationset_id, dashsink->presentation_time_offset);
    ctx->presentation_time_offset = dashsink->presentation_time_offset;
  }

  GST_LOG_OBJECT (dashsink, "<%s> RUNNING TIME : %" G_GINT64_FORMAT, ctx->name,
      ctx->running_time);

  if (ctx->is_reference) {
    dashsink->manifest_generator->updateCurrentDuration (ctx->running_time);
  }
}

static void
media_stream_ctx_init_segment_id (MediaStreamCtx * ctx)
{
  gchar *path = NULL;
  gchar *name = NULL;
  gint64 idx = -1;
  gint64 val = -1;
  GDir *dir = NULL;

  path = g_path_get_dirname (ctx->dashsink->location_prefix);

  dir = g_dir_open (path, 0, NULL);
  while ((name = (gchar *) g_dir_read_name (dir)) != NULL) {
    if (g_str_has_suffix (name, ctx->suffix) &&
        strlen (name) == strlen ("abcdefgh01234567STR?XX")) {
      val = g_ascii_strtoll (name + 8, NULL, 10);
      if (val > idx) idx = val;
    }
  }

  ctx->segment_id = (guint) (idx + 1);
  GST_INFO_OBJECT (ctx->dashsink, ">>> <%s> Next segment is started with %d",
      ctx->name, ctx->segment_id);
  g_dir_close (dir);
  g_free (path);
}

static gboolean
media_stream_ctx_check_input_delay (gpointer data)
{
  if (data == NULL) return FALSE;

  MediaStreamCtx * ctx = (MediaStreamCtx *) data;
  ctx->delay_count++;
  if (ctx->delay_count >= 20) {
    GST_ELEMENT_ERROR (ctx->dashsink, STREAM, FAILED,
        (("Internal data flow error.")), ("<%s> input data delay over 4s",
          ctx->name));
    return FALSE;
  }

  return TRUE;
}

static void
media_stream_ctx_check_file_header (GstElement * muxer, gpointer data)
{
  if (data == NULL) return;

  // fixed 8byte header info - isobmff 'ftyp' atom box
  const guchar check_header[] = { 0x00, 0x00, 0x00, 0x1C, 0x66, 0x74, 0x79, 0x70 };
  MediaStreamCtx * ctx = (MediaStreamCtx *) data;
  FILE * fp = g_fopen (ctx->current_file_name, "rb");
  gsize size = 0L;
  gchar header[8];

  if (fp == NULL) {
    GST_WARNING_OBJECT (ctx->dashsink, "Failed to open [%s] file",
                    ctx->current_file_name);
    return;
  }

  GST_DEBUG_OBJECT (ctx->dashsink, "<%s> handle send-header signal about [%s]"
      "from muxer[%p]",
      ctx->name, ctx->current_file_name, muxer);

  fseek (fp, 0L, SEEK_END);
  size = ftell (fp);
  fseek (fp, 0L, SEEK_SET);

  memset (header, 0, sizeof(header));
  GST_DEBUG_OBJECT (ctx->dashsink, "check current file size [%lu]", size);
  if (size >= 8 && fread (header, 8, 1, fp) != 1) {
    GST_WARNING_OBJECT (ctx->dashsink, "Failed to read [%s] file",
                    ctx->current_file_name);
    fclose (fp);
    return;
  }

  GST_DEBUG_OBJECT (ctx->dashsink, "check header 8byte[%x %x %x %x %x %x %x %x]",
    header[0], header[1], header[2], header[3], header[4],
    header[5], header[6], header[7]);

  if (memcmp (header, check_header, sizeof (check_header)) != 0) {
    GST_ELEMENT_ERROR (ctx->dashsink, STREAM, FAILED,
        ("invalid file header was written - wrong mp4 header 8byte."),
        GST_ERROR_SYSTEM);
    ctx->remove_file = TRUE;
  }

  fclose (fp);
  return;
}
