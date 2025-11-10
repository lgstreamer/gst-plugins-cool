/* GStreamer Plugins Cool
 * Copyright (C) 2016 LG Electronics, Inc.
 *    Author : Kanghwan Jang <kanghwan.jang@lge.com>
 *
 * gstcooltracer.c: tracing module
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
/**
 * SECTION:GstCoolTracer
 * @short_description: tracing module
 *
 * A tracing module
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "gstcooltracer.h"
#include "gstpmlog.h"
#include "gsttracepad.h"
#include "gsttraceelement.h"
#include "gsttraceprocstat.h"

GST_DEBUG_CATEGORY (gst_cooltracer_debug);
#define GST_CAT_DEFAULT gst_cooltracer_debug

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_cooltracer_debug, "cooltracer", 0, "cool tracer");
#define gst_cool_tracer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstCoolTracer, gst_cool_tracer, GST_TYPE_TRACER,
    _do_init);



static gboolean need_to_stare (const gchar * klassname);
static gboolean need_to_stare_element (const gchar * elementi_name);
static void add_to_stared_element (GstCoolTracer * self, GstElement * elem);
static gboolean remove_from_stared_element (GstCoolTracer * self,
    GstElement * elem);
static int get_stared_element_index (GstCoolTracer * self, GstElement * elem);
static inline gboolean is_stared_element (GstCoolTracer * self,
    GstElement * elem);
static void gst_cool_tracer_add_trace (GstCoolTracer * self, gpointer trace);
static GList *get_list_splitstr (const gchar * str, const gchar * delimiter);
static void gst_cool_g_list_free_alloc (GList * list);
static void get_trace_event_flag (GstCoolTracer * self);

static gboolean is_thread_alive (pid_t tid);

static void gst_cool_tracer_reset_all_trace (GstCoolTracer * self);

static void gst_cool_tracer_add_tick_source (GstCoolTracer * self);

static void gst_cool_tracer_remove_tick_source (GstCoolTracer * self);

static void gst_cool_tracer_dump_dot (GstCoolTracer * self, const char *fname);
static void gst_cool_tracer_dump_dot_trigger (GstCoolTracer * self,
    const char *fmt, ...);


static GPrivate key_pad_push = G_PRIVATE_INIT (NULL);
static GPrivate key_change_state = G_PRIVATE_INIT (NULL);


static GList *klass_list = NULL;
static GList *element_list = NULL;
static GList *event_list = NULL;

static void
gst_cool_tracer_add_trace (GstCoolTracer * self, gpointer trace)
{
  GST_COOL_TRACER_LOCK (self);
  self->trace_list = g_list_append (self->trace_list, trace);
  GST_COOL_TRACER_UNLOCK (self);

}

static void
print_remained_stared_element (GstCoolTracer * self)
{
  int i;
  int loglevel = GST_LEVEL_INFO;

  if (self->stared_elements_count == 0) {
    GST_PMLOG (loglevel, "--> No satred element is left..");
    return;
  }
  GST_PMLOG (loglevel, "--> %d stared elements are remained",
      self->stared_elements_count);
  for (i = 0; i < self->stared_elements_count; ++i) {
    GstElementClass *klass = NULL;
    const gchar *klassname = NULL;
    gchar *elemname = NULL;
    GstElement *elem = self->stared_elements[i];

    klass = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS (elem));
    if (klass)
      klassname =
          gst_element_class_get_metadata (klass, GST_ELEMENT_METADATA_KLASS);
    elemname = gst_element_get_name (elem);
    if (!elemname || !klass)
      continue;

    GST_PMLOG (loglevel,
        "%dth stared element still exist %p={name:%s, class:%s, type:%s}",
        i, elem, elemname, klassname, G_OBJECT_TYPE_NAME (elem));
    g_free (elemname);
  }
}

static void
gst_cool_tracer_free_traces (GstCoolTracer * self, gboolean force)
{
  GList *old, *old_top;
  GList *elem;

  GST_PMLOG_DEBUG ("free traces +");
  GST_COOL_TRACER_LOCK (self);

  // clear debug list
  for (elem = klass_list; elem != NULL; elem = elem->next) {
    gst_cool_g_list_free_alloc (elem->data);
  }
  g_list_free (klass_list);

  gst_cool_g_list_free_alloc (element_list);
  gst_cool_g_list_free_alloc (event_list);

  klass_list = NULL;
  element_list = NULL;
  event_list = NULL;

  old = old_top = self->trace_list;
  self->trace_list = NULL;

  while (old) {
    GstTraceCommon *trace = old->data;
    if (force || !is_thread_alive (trace->thread))
      g_free (trace);
    else
      self->trace_list = g_list_append (self->trace_list, trace);
    old = old->next;
  }
  g_list_free (old_top);

  GST_PMLOG_INFO ("Is there any stared elements not disposed yet ??");
  print_remained_stared_element (self);

  /**
   * TODO : Need to initialize even if some elements are not finalized
   *        Removing from the stared element array is not enough and should
   *        be finalized. However, since this is a tracer,
   *        it is now only deleted from the array.
   **/
  if (self->stared_elements_count > 0) {
    memset (self->stared_elements, 0,
        sizeof (GstElement *) * self->stared_elements_count + 1);
    self->stared_elements_count = 0;
  }
  GST_COOL_TRACER_UNLOCK (self);
  GST_PMLOG_DEBUG ("free traces -");
}


static void
do_push_buffer_pre (GstTracer * obj, guint64 ts, GstPad * pad,
    GstBuffer * buffer)
{
  GstTracePadPush *trace;

  trace = g_private_get (&key_pad_push);
  if (!trace) {
    trace = make_trace_pad_push (ts);
    if (!trace)
      return;
    g_private_set (&key_pad_push, trace);
    gst_cool_tracer_add_trace (GST_COOL_TRACER (obj), trace);
    GST_PMLOG_DEBUG ("init trace for push_buffer:%p stack_size:%d new pad:"
        FMT_PAD_NAME, trace, trace->stack_size, PRINT_PAD_NAME (pad));

  }

  trace_pad_push_pre (trace, ts, pad, buffer);
}

static void
do_push_buffer_post (GstTracer * obj, guint64 ts, GstPad * pad,
    GstFlowReturn res)
{
  GstTracePadPush *trace;

  trace = g_private_get (&key_pad_push);
  if (trace)
    trace_pad_push_post (trace, ts, pad, NULL, res);

}

static void
do_push_event_pre (GstTracer * obj, guint64 ts, GstPad * pad, GstEvent * event)
{
  int loglevel = GST_LEVEL_DEBUG;
  GstCoolTracer *self = GST_COOL_TRACER (obj);
  guint32 event_flag = GST_GET_EVENT_FLAG (GST_EVENT_TYPE (event));

  if (self->trace_event_flag[event_flag]) {
    if (is_stared_element (self, GST_PAD_PARENT (pad))) {
      switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_STREAM_START:
        {
          const gchar *stream_id = NULL;
          gst_event_parse_stream_start (event, &stream_id);
          loglevel = GST_LEVEL_INFO;
          GST_PMLOG_INFO ("event:stream-start: "
              "{stream-id: %s} pad:" FMT_PAD_NAME " -> " FMT_PAD_NAME,
              stream_id, PRINT_PAD_NAME (pad), PRINT_PAD_NAME (pad->peer));
          return;
        }
        case GST_EVENT_FLUSH_STOP:
        {
          gboolean reset_time;

          gst_event_parse_flush_stop (event, &reset_time);
          GST_PMLOG_INFO ("evnt:flush-stop: {%p, reset_time: %d} "
              "pad:" FMT_PAD_NAME " -> " FMT_PAD_NAME,
              event, reset_time,
              PRINT_PAD_NAME (pad), PRINT_PAD_NAME (pad->peer));
          return;
        }
        case GST_EVENT_SEEK:
        {
          gdouble rate;
          GstFormat format;
          GstSeekFlags flags;
          GstSeekType start_type = GST_SEEK_TYPE_NONE, stop_type;
          gint64 start, stop;

          gst_event_parse_seek (event, &rate, &format, &flags,
              &start_type, &start, &stop_type, &stop);

          GST_PMLOG_INFO ("event:seek: {format %s, rate %f, "
              "start type %d at %" GST_TIME_FORMAT ", end type %d at %"
              GST_TIME_FORMAT "} pad:" FMT_PAD_NAME " -> " FMT_PAD_NAME,
              gst_format_get_name (format), rate,
              start_type, GST_TIME_ARGS (start), stop_type,
              GST_TIME_ARGS (stop), PRINT_PAD_NAME (pad),
              PRINT_PAD_NAME (pad->peer));
          return;
        }
        case GST_EVENT_SEGMENT:
        {
          const GstSegment *segment = NULL;

          loglevel = GST_LEVEL_INFO;
          gst_event_parse_segment (event, &segment);
          if (segment) {
            GST_PMLOG_INFO ("event:segment: "
                "{start:%" GST_TIME_FORMAT ",stop:%" GST_TIME_FORMAT
                ", rate:%.1f} "
                "pad:" FMT_PAD_NAME " -> " FMT_PAD_NAME,
                GST_TIME_ARGS (segment->start), GST_TIME_ARGS (segment->stop),
                segment->rate, PRINT_PAD_NAME (pad),
                PRINT_PAD_NAME (pad->peer));
          } else {
            GST_PMLOG_INFO ("event:segment: "
                "pad:" FMT_PAD_NAME " -> " FMT_PAD_NAME,
                PRINT_PAD_NAME (pad), PRINT_PAD_NAME (pad->peer));
          }
          return;
        }
        case GST_EVENT_CAPS:
        {
          GstCaps *caps;
          gst_event_parse_caps (event, &caps);
          if (caps) {
            gchar *caps_str = gst_caps_to_string (caps);
            GST_PMLOG_INFO ("event:caps: {%s} "
                "pad:" FMT_PAD_NAME " -> " FMT_PAD_NAME,
                caps_str, PRINT_PAD_NAME (pad), PRINT_PAD_NAME (pad->peer));
            g_free (caps_str);
          } else {
            GST_PMLOG_INFO ("event:caps: "
                "pad:" FMT_PAD_NAME " -> " FMT_PAD_NAME,
                PRINT_PAD_NAME (pad), PRINT_PAD_NAME (pad->peer));
          }
          return;
        }
        case GST_EVENT_GAP:
        {
          // TODO : It is needed to check why GST_PMLOG cause SIG 11 to print pad names.
          // We use log parameter for temporary.
          GstClockTime timestamp, duration;
          gchar log[1024] = { 0 };
          gst_event_parse_gap (event, &timestamp, &duration);
          snprintf (log, 1024, "event:gap: "
              "{timestamp:%" GST_TIME_FORMAT " ,duration:%" GST_TIME_FORMAT "} "
              "pad:" FMT_PAD_NAME " -> " FMT_PAD_NAME,
              GST_TIME_ARGS (timestamp), GST_TIME_ARGS (duration),
              PRINT_PAD_NAME (pad), PRINT_PAD_NAME (pad->peer));
          GST_PMLOG_INFO ("%s", log);
          return;
        }
        case GST_EVENT_FLUSH_START:
          GST_PMLOG_INFO ("evnt:flush-start: "
              "pad:" FMT_PAD_NAME " -> " FMT_PAD_NAME,
              PRINT_PAD_NAME (pad), PRINT_PAD_NAME (pad->peer));
          return;
        case GST_EVENT_EOS:
          GST_PMLOG_INFO ("event:eos: "
              FMT_PAD_NAME " -> " FMT_PAD_NAME,
              PRINT_PAD_NAME (pad), PRINT_PAD_NAME (pad->peer));
          return;
        case GST_EVENT_TAG:
          GST_PMLOG_INFO ("event:tag: "
              FMT_PAD_NAME " -> " FMT_PAD_NAME,
              PRINT_PAD_NAME (pad), PRINT_PAD_NAME (pad->peer));
          return;
        default:
          break;
      }
    }
  }

  GST_PMLOG (loglevel, "event:%s pad:" FMT_PAD_NAME " -> " FMT_PAD_NAME,
      GST_EVENT_TYPE_NAME (event), PRINT_PAD_NAME (pad),
      PRINT_PAD_NAME (pad->peer));
}

static void
do_element_change_state_pre (GstTracer * obj, guint64 ts, GstElement * elem,
    GstStateChange change)
{
  GstTraceChangeState *trace;
  GstCoolTracer *self = GST_COOL_TRACER (obj);

  trace = g_private_get (&key_change_state);
  if (!trace) {
    trace = make_trace_change_state (ts);
    if (!trace)
      return;
    g_private_set (&key_change_state, trace);
    gst_cool_tracer_add_trace (GST_COOL_TRACER (obj), trace);
    GST_PMLOG_DEBUG ("init trace for change_state:%p stack_size:%d",
        trace, trace->stack_size);
  }

  if (self->pipeline == elem) {
    GST_PMLOG_INFO ("pipeline state changeing     : %s->%s",
        state_name (GST_STATE_TRANSITION_CURRENT (change)),
        state_name (GST_STATE_TRANSITION_NEXT (change)));

    if (self->enable_pstrace) {
      GstProcStat *procstat = (GstProcStat *) self->procstat;
      gst_trace_proc_stat_log (state_name (GST_STATE_TRANSITION_NEXT (change)),
          procstat);
      if (self->enable_post_psinfo)
        /**
         * When do-post is set, report proc/stat tracing data
         * every time the state is changed.
        **/
        gst_trace_proc_stat_post ((GstObject *) obj, elem, procstat);
    }
  }
  trace_change_state_pre (trace, ts, elem, change);
}

static void
do_element_change_state_post (GstTracer * obj, guint64 ts, GstElement * elem,
    GstStateChange change, GstStateChangeReturn res)
{
  GstTraceChangeState *trace;
  GstCoolTracer *self = GST_COOL_TRACER (obj);

  trace = g_private_get (&key_change_state);
  if (trace)
    trace_change_state_post (trace, ts, elem, change, res);

  if (self->pipeline == elem) {
    self->pipeline_state = GST_STATE_TRANSITION_NEXT (change);
    GST_PMLOG_INFO ("pipeline state changed ret:%d : %s->%s", res,
        state_name (GST_STATE_TRANSITION_CURRENT (change)),
        state_name (GST_STATE_TRANSITION_NEXT (change)));
  }
}


static void
do_post_message_pre (GstTracer * obj, guint64 ts, GstElement * elem,
    GstMessage * msg)
{
  int loglevel = GST_LEVEL_DEBUG;
  GError *err = NULL;
  gchar *dbg = NULL;
  gboolean dumpdotgraph = FALSE;
  GstCoolTracer *self = GST_COOL_TRACER (obj);

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:
      dumpdotgraph = TRUE;
      loglevel = GST_LEVEL_ERROR;
      gst_message_parse_error (msg, &err, &dbg);
      break;
    case GST_MESSAGE_WARNING:
      dumpdotgraph = TRUE;
      loglevel = GST_LEVEL_INFO;
      gst_message_parse_warning (msg, &err, &dbg);
      break;
    case GST_MESSAGE_INFO:
      dumpdotgraph = TRUE;
      loglevel = GST_LEVEL_INFO;
      gst_message_parse_info (msg, &err, &dbg);
      break;
    case GST_MESSAGE_EOS:
    case GST_MESSAGE_ASYNC_DONE:
      loglevel = GST_LEVEL_INFO;
      if (self->pipeline == elem)
        dumpdotgraph = TRUE;
      break;
    case GST_MESSAGE_STREAM_START:
    case GST_MESSAGE_STREAM_COLLECTION:
    case GST_MESSAGE_STREAMS_SELECTED:
      if (self->pipeline == elem) {
        dumpdotgraph = TRUE;
        loglevel = GST_LEVEL_INFO;
      }
      break;
    default:
      break;
  }

  if (err) {
    GST_PMLOG_INFO ("message:%s:%s element:%s debug:%s",
        GST_MESSAGE_TYPE_NAME (msg), err->message,
        GST_ELEMENT_NAME (elem), dbg);
    g_error_free (err);
  } else {
    GST_PMLOG (loglevel, "message:%s element:%s",
        GST_MESSAGE_TYPE_NAME (msg), GST_ELEMENT_NAME (elem));
  }

  if (self->is_dump_dot_dir && dumpdotgraph)
    gst_cool_tracer_dump_dot_trigger (GST_COOL_TRACER (obj), "message---%s--%s",
        GST_ELEMENT_NAME (elem), GST_MESSAGE_TYPE_NAME (msg));

  g_free (dbg);
}

static void
object_weak_cb (gpointer data, GObject * object)
{
  int loglevel = GST_LEVEL_DEBUG;
  GstElement *elem = GST_ELEMENT (object);

  if (elem == NULL)
    return;

  if (remove_from_stared_element (GST_COOL_TRACER (data), elem))
    loglevel = GST_LEVEL_INFO;

  GST_PMLOG (loglevel, "--> removed by weak_ref callback: %p", elem);
}

static void
do_element_new (GstTracer * obj, guint64 ts, GstElement * elem)
{
  GstElementClass *klass;
  const gchar *klassname;
  gchar *elemname;
  GstCoolTracer *self = GST_COOL_TRACER (obj);
  int loglevel = GST_LEVEL_DEBUG;

  klass = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS (elem));
  klassname =
      gst_element_class_get_metadata (klass, GST_ELEMENT_METADATA_KLASS);
  elemname = gst_element_get_name (elem);

  if (GST_IS_PIPELINE (elem)) {
    loglevel = GST_LEVEL_INFO;
    if (self->pipeline) {
      // cleanup old trace data from the previous pipeline
      gst_cool_tracer_reset_all_trace (self);
    }
    self->pipeline = elem;

    if (self->enable_pstrace)
      gst_trace_proc_stat_reset ((GstProcStat *) self->procstat);

    add_to_stared_element (self, elem);
    g_object_weak_ref ((GObject *) elem, object_weak_cb, self);
  } else if ((klassname && need_to_stare (klassname)) ||
      (elemname && need_to_stare_element (elemname))) {
    loglevel = GST_LEVEL_INFO;
    add_to_stared_element (self, elem);
    g_object_weak_ref ((GObject *) elem, object_weak_cb, self);
  } else {
    loglevel = GST_LEVEL_DEBUG;
  }

  GST_PMLOG (loglevel, "new element %p={name:%s, class:%s, type:%s}",
      elem, GST_ELEMENT_NAME (elem), klassname, G_OBJECT_TYPE_NAME (elem));
  g_free (elemname);
}

static void
do_bin_add_pre (GstTracer * obj, guint64 ts, GstBin * bin, GstElement * elem)
{
  int loglevel = GST_LEVEL_DEBUG;
  GstCoolTracer *self = GST_COOL_TRACER (obj);

  if (is_stared_element (self, elem))
    loglevel = GST_LEVEL_INFO;

  GST_PMLOG (loglevel, "bin:%s add element:%s %p",
      GST_ELEMENT_NAME (bin) ? GST_ELEMENT_NAME (bin) : "(null)",
      GST_ELEMENT_NAME (elem), elem);
}


static void
do_bin_remove_pre (GstTracer * obj, guint64 ts, GstBin * bin, GstElement * elem)
{
  int loglevel = GST_LEVEL_DEBUG;
  GstCoolTracer *self = GST_COOL_TRACER (obj);

  if (is_stared_element (self, elem))
    loglevel = GST_LEVEL_INFO;

  GST_PMLOG (loglevel, "bin:%s remove element:%s %p",
      GST_ELEMENT_NAME (bin) ? GST_ELEMENT_NAME (bin) : "(null)",
      GST_ELEMENT_NAME (elem), elem);
}

/* tracer class */

static void
gst_cool_tracer_finalize (GObject * obj)
{
  GstCoolTracer *self = GST_COOL_TRACER (obj);
  GST_PMLOG_INFO ("cooltracer finalize, self:%p", self);

  gst_cool_tracer_remove_tick_source (self);

  if (self->enable_pstrace) {
    gst_trace_proc_stat_deinit ((GstProcStat *) self->procstat);
    if (self->procstat)
      g_free (self->procstat);
  }

  gst_cool_tracer_free_traces (self, TRUE);

  if (self->context)
    g_main_context_unref (self->context);
  self->context = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}


static gboolean
check_make_dot_graph ()
{
  FILE *dummy = NULL;
  gchar *fname[1024] = { 0 };
  const gchar *dump_dot_dir = g_getenv ("GST_DEBUG_DUMP_DOT_DIR");

  if (dump_dot_dir == NULL)
    return FALSE;

  g_snprintf (fname, sizeof (fname) / sizeof (*fname), "%s/cooltracer.dot",
      dump_dot_dir);
  dummy = fopen (fname, "rb");

  if (dummy == NULL) {
    GST_PMLOG_INFO ("%s does not exist. diable dot graph", fname);
    return FALSE;
  }

  fclose (dummy);
  GST_PMLOG_INFO ("%s exist. enable dot graph", fname);
  return TRUE;
}

static void
gst_cool_tracer_constructed (GObject * object)
{
  GstCoolTracer *self = GST_COOL_TRACER (object);
  GstTracer *tracer = GST_TRACER (self);
  gchar *params;
  GST_PMLOG_INFO ("cooltracer constructed, self:%p", self);

  /**
   * Options :
   *   default - disable to trace proc stat
   *   cooltracer(pstrace) - enable tracing proc stat
   *   cooltracer(pstrace=only) - tracing proc stat only
   *   cooltracer(pstrace,do-post) - tracing proc stat &
   *            post tracing_info over application message
   **/
  g_object_get (self, "params", &params, NULL);
  if (params) {
    if (g_strrstr (params, "pstrace") != 0) {
      self->procstat = (GObject *) g_malloc0 (sizeof (GstProcStat));
      self->enable_pstrace = TRUE;

      /* mode for monitoring proc/stat only */
      if (g_strrstr (params, "only") != 0)
        self->is_psonly_mode = TRUE;

      /* enable posting message of tracing data */
      if (g_strrstr (params, "do-post") != 0)
        self->enable_post_psinfo = TRUE;
    } else {
      self->enable_pstrace = FALSE;
      self->procstat = NULL;
    }

    if (g_strrstr (params, "dot") != 0)
      self->is_dump_dot_dir = TRUE;
    else
      self->is_dump_dot_dir = FALSE;
  }

  if (!self->is_dump_dot_dir && !self->is_psonly_mode &&
      check_make_dot_graph ())
    self->is_dump_dot_dir = TRUE;

  GST_PMLOG_INFO
      ("ps trace ? %d [proc/stat check only mode ? %d, do-post ? %d]",
      self->enable_pstrace, self->is_psonly_mode, self->enable_post_psinfo);

  GST_PMLOG_INFO ("dot graph ? %d", self->is_dump_dot_dir);

  if (self->enable_pstrace) {
    gst_trace_proc_stat_init ((GstProcStat *) self->procstat);
  }

  if (!self->is_psonly_mode) {
    self->context = g_main_context_ref_thread_default ();

    gst_cool_tracer_add_tick_source (self);

    g_mutex_init (&self->lock);
  }

  self->trace_list = NULL;
  self->stared_elements_count = 0;
  self->pipeline = NULL;
  self->pipeline_state = GST_STATE_VOID_PENDING;
  self->error_detected = FALSE;
  self->error_detected_count = 0;
  self->dump_dot_graph = FALSE;


  if (!self->is_psonly_mode) {
    if (klass_list == NULL) {
      const gchar *pmlog = g_getenv ("PMLOG_DEBUG_KLASS");
      if (pmlog) {
        gchar **split = g_strsplit (pmlog, ",", 0);
        gchar **walk;
        for (walk = split; *walk; walk++) {
          GList *list = get_list_splitstr (*walk, "&");
          if (list) {
            klass_list = g_list_append (klass_list, list);
            GST_PMLOG_INFO ("enable klass pmlog : %s", *walk);
          }
        }
        g_strfreev (split);
      }
    }
    if (element_list == NULL) {
      const gchar *pmlog = g_getenv ("PMLOG_DEBUG_ELEMENT");
      if (pmlog) {
        GST_PMLOG_INFO ("enable element pmlog : %s", pmlog);
        element_list = get_list_splitstr (pmlog, ",");
      }
    }
    if (event_list == NULL) {
      const gchar *pmlog = g_getenv ("PMLOG_DEBUG_EVENT");
      if (pmlog)
        event_list = get_list_splitstr (pmlog, ",");
    }

    get_trace_event_flag (self);
  }

  gst_tracing_register_hook (tracer, "element-new",
      G_CALLBACK (do_element_new));
  gst_tracing_register_hook (tracer, "element-change-state-pre",
      G_CALLBACK (do_element_change_state_pre));

  if (!self->is_psonly_mode) {
    gst_tracing_register_hook (tracer, "pad-push-pre",
        G_CALLBACK (do_push_buffer_pre));
    gst_tracing_register_hook (tracer, "pad-push-post",
        G_CALLBACK (do_push_buffer_post));
    gst_tracing_register_hook (tracer, "pad-push-event-pre",
        G_CALLBACK (do_push_event_pre));
    gst_tracing_register_hook (tracer, "element-post-message-pre",
        G_CALLBACK (do_post_message_pre));
    gst_tracing_register_hook (tracer, "element-change-state-post",
        G_CALLBACK (do_element_change_state_post));
    gst_tracing_register_hook (tracer, "bin-add-pre",
        G_CALLBACK (do_bin_add_pre));
    gst_tracing_register_hook (tracer, "bin-remove-pre",
        G_CALLBACK (do_bin_remove_pre));
  }
  ((GObjectClass *) gst_cool_tracer_parent_class)->constructed (object);
}

static void
gst_cool_tracer_class_init (GstCoolTracerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructed = gst_cool_tracer_constructed;
  gobject_class->finalize = gst_cool_tracer_finalize;
  GST_ERROR ("gst_cool_tracer_class_init");

}

static void
gst_cool_tracer_init (GstCoolTracer * self)
{
  GST_PMLOG_INFO ("cooltracer init, self:%p", self);
}

static gboolean
gst_cool_tracer_tick_cb (gpointer user_data)
{
  GList *i;
  GstTraceCommon *trace;
  GstClockTime current;
  gboolean error_detected = FALSE;
  GstCoolTracer *self = GST_COOL_TRACER (user_data);

  current = gst_util_get_timestamp ();

  for (i = self->trace_list; i; i = i->next) {
    trace = i->data;
    if (!trace->check_trace (GST_TRACER (self), trace, current))
      error_detected = TRUE;
  }

  if (error_detected && !self->error_detected) {
    self->error_detected = error_detected;
    if (self->is_dump_dot_dir)
      gst_cool_tracer_dump_dot (self, "pending-detected");
  }
  self->error_detected = error_detected;
  return TRUE;
}

static void
gst_cool_tracer_add_tick_source (GstCoolTracer * self)
{
  if (self->tick_source)
    return;

  self->tick_source = g_timeout_source_new_seconds (1);
  g_source_set_callback (self->tick_source,
      (GSourceFunc) gst_cool_tracer_tick_cb, self, NULL);
  g_source_attach (self->tick_source, self->context);
}

static void
gst_cool_tracer_remove_tick_source (GstCoolTracer * self)
{
  if (!self->tick_source)
    return;

  g_source_destroy (self->tick_source);
  g_source_unref (self->tick_source);
  self->tick_source = NULL;
}


static gboolean
gst_cool_tracer_dump_dot_cb (gpointer user_data)
{
  GstCoolTracer *self = GST_COOL_TRACER (user_data);

  if (self->dump_dot_graph) {
    self->dump_dot_graph = FALSE;
    gst_cool_tracer_dump_dot (self, self->filename);
  }
  return FALSE;                 /* FALSE because it's a oneshot callback */
}

static void
gst_cool_tracer_dump_dot_trigger (GstCoolTracer * self, const char *fmt, ...)
{
  va_list arg;

  va_start (arg, fmt);
  g_vsnprintf (self->filename, sizeof (self->filename), fmt, arg);
  va_end (arg);

  if (!self->dump_dot_graph) {
    self->dump_dot_graph = TRUE;
    gst_cool_tracer_dump_dot_cb (self);
  }
}

static void
gst_cool_tracer_dump_dot (GstCoolTracer * self, const char *fname)
{
  va_list arg;
  gchar filename[256];
  gchar *bufpos = filename;
  gchar *bufend = bufpos + sizeof (filename) / sizeof (*filename);

  bufpos +=
      g_snprintf (bufpos, bufend - bufpos, "cooltracer-pid%d-%s", getpid (),
      fname);

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (self->pipeline),
      GST_DEBUG_GRAPH_SHOW_VERBOSE, filename);
}

static void
gst_cool_tracer_reset_all_trace (GstCoolTracer * self)
{
  GST_PMLOG_INFO ("cooltracer reset all trace, self:%p, pipeline:%p",
      self, self->pipeline);
  self->pipeline = NULL;
  gst_cool_tracer_free_traces (self, FALSE);
}

static gboolean
is_thread_alive (pid_t tid)
{
#if defined(SYS_tgkill)
  return syscall (SYS_tgkill, getpid (), tid, 0) == 0;
#elif defined(SYS_tkill)
  return syscall (SYS_tkill, tid, 0) == 0;
#else
  return kill (tid, 0) == 0;
#endif
}

static gboolean
need_to_stare (const gchar * klassname)
{
  gboolean is_stare = FALSE;

  if (klass_list) {
    GList *klass = NULL;
    for (klass = klass_list; klass != NULL; klass = klass->next) {
      gboolean partial_result = TRUE;
      GList *sub = NULL;
      for (sub = klass->data; sub != NULL; sub = sub->next)
        partial_result = partial_result
            && strstr (klassname, (gchar *) sub->data);
      is_stare |= partial_result;
    }
  }

  is_stare |= (strstr (klassname, "Codec") != NULL)
      && (strstr (klassname, "Decoder") != NULL);
  is_stare |= (strstr (klassname, "Sink") != NULL)
      && ((strstr (klassname, "Audio") != NULL)
      || (strstr (klassname, "Video") != NULL));

  return is_stare;
}

static gboolean
need_to_stare_element (const gchar * element_name)
{
  gboolean is_stare = FALSE;

  if (element_list) {
    GList *elem = NULL;
    for (elem = element_list; elem != NULL; elem = elem->next)
      is_stare = is_stare || (strstr (element_name, (gchar *) elem->data));
  }

  return is_stare;
}


static inline gint
compare_pointer (gconstpointer pa, gconstpointer pb)
{
  const unsigned long a = (const unsigned long) pa;
  const unsigned long b = (const unsigned long) pb;
  return (a > b) ? 1 : (a == b) ? 0 : -1;
}

static void
add_to_stared_element (GstCoolTracer * self, GstElement * elem)
{
  int index = self->stared_elements_count;
  GstElement *t = NULL;

  GST_COOL_TRACER_LOCK (self);

  if (self->stared_elements_count < MAX_STARE_COUNT - 1) {
    self->stared_elements[self->stared_elements_count] = elem;
    self->stared_elements_count += 1;

    // make it sorted
    for (index = self->stared_elements_count - 1; index > 0; --index) {
      if (compare_pointer (self->stared_elements[index - 1],
              self->stared_elements[index]) < 0)
        break;

      // swap
      t = self->stared_elements[index - 1];
      self->stared_elements[index - 1] = self->stared_elements[index];
      self->stared_elements[index] = t;
    }
  }
  GST_COOL_TRACER_UNLOCK (self);
  GST_PMLOG_DEBUG ("add %p, index : %d, stared count : %d",
      elem, index, self->stared_elements_count);
}

static gboolean
remove_from_stared_element (GstCoolTracer * self, GstElement * elem)
{
  GstElementClass *klass = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS (elem));
  const gchar *klassname =
      gst_element_class_get_metadata (klass, GST_ELEMENT_METADATA_KLASS);
  gchar *elemname = gst_element_get_name (elem);
  int loglevel = GST_LEVEL_INFO;
  gboolean res = TRUE;
  int index = get_stared_element_index (self, elem);

  do {
    if (index == -1 || index >= self->stared_elements_count) {
      res = FALSE;
      break;
    }

    GST_COOL_TRACER_LOCK (self);

    memcpy (self->stared_elements + index, self->stared_elements + index + 1,
        sizeof (GstElement *) * (self->stared_elements_count - index));
    self->stared_elements[self->stared_elements_count] = NULL;
    --self->stared_elements_count;

    GST_COOL_TRACER_UNLOCK (self);
  } while (0);

  GST_PMLOG (loglevel,
      "removed element %p={index: %d, name:%s, class:%s, type:%s, remained: %d",
      elem, index, elemname, klassname, G_OBJECT_TYPE_NAME (elem),
      self->stared_elements_count);
  g_free (elemname);
  return res;
}

static inline int
get_stared_element_index (GstCoolTracer * self, GstElement * elem)
{
  int lo, mi, hi, ret;

  lo = 0;
  hi = self->stared_elements_count;
  while (lo < hi) {
    mi = (lo + hi) / 2;
    ret = compare_pointer (elem, self->stared_elements[mi]);
    if (ret < 0)
      hi = mi;
    else if (ret > 0)
      lo = mi + 1;
    else
      return mi;
  }
  return -1;
}

static inline gboolean
is_stared_element (GstCoolTracer * self, GstElement * elem)
{
  int lo, mi, hi, ret;

  lo = 0;
  hi = self->stared_elements_count;
  while (lo < hi) {
    mi = (lo + hi) / 2;
    ret = compare_pointer (elem, self->stared_elements[mi]);
    if (ret < 0)
      hi = mi;
    else if (ret > 0)
      lo = mi + 1;
    else
      return TRUE;
  }
  return FALSE;
}

static GList *
get_list_splitstr (const gchar * str, const gchar * delimiter)
{
  GList *list = NULL;
  if (strstr (str, delimiter)) {
    gchar **split = g_strsplit (str, delimiter, 0);
    gchar **walk;
    for (walk = split; *walk; walk++) {
      g_strstrip (*walk);
      list = g_list_append (list, *walk);
    }
    g_free (split);

  }
  return list;
}

static void
gst_cool_g_list_free_alloc (GList * list)
{
  while (list != NULL) {
    g_free (list->data);
    list = g_list_delete_link (list, list);
  }
}

typedef struct
{
  const gint type;
  const gchar *name;
} GstCoolEventType;

static GstCoolEventType event_types[] = {
  {GST_EVENT_UNKNOWN, "unknown"},
  {GST_EVENT_FLUSH_START, "flush-start"},
  {GST_EVENT_FLUSH_STOP, "flush-stop"},
  {GST_EVENT_SELECT_STREAMS, "select-streams"},
  {GST_EVENT_STREAM_START, "stream-start"},
  {GST_EVENT_STREAM_COLLECTION, "stream-collection"},
  {GST_EVENT_CAPS, "caps"},
  {GST_EVENT_SEGMENT, "segment"},
  {GST_EVENT_TAG, "tag"},
  {GST_EVENT_TOC, "toc"},
  {GST_EVENT_PROTECTION, "protection"},
  {GST_EVENT_BUFFERSIZE, "buffersize"},
  {GST_EVENT_SINK_MESSAGE, "sink-message"},
  {GST_EVENT_EOS, "eos"},
  {GST_EVENT_SEGMENT_DONE, "segment-done"},
  {GST_EVENT_GAP, "gap"},
  {GST_EVENT_QOS, "qos"},
  {GST_EVENT_SEEK, "seek"},
  {GST_EVENT_NAVIGATION, "navigation"},
  {GST_EVENT_LATENCY, "latency"},
  {GST_EVENT_STEP, "step"},
  {GST_EVENT_RECONFIGURE, "reconfigure"},
  {GST_EVENT_TOC_SELECT, "toc-select"},
  {GST_EVENT_CUSTOM_UPSTREAM, "custom-upstream"},
  {GST_EVENT_CUSTOM_DOWNSTREAM, "custom-downstream"},
  {GST_EVENT_CUSTOM_DOWNSTREAM_OOB, "custom-downstream-oob"},
  {GST_EVENT_CUSTOM_DOWNSTREAM_STICKY, "custom-downstream-sticky"},
  {GST_EVENT_CUSTOM_BOTH, "custom-both"},
  {GST_EVENT_CUSTOM_BOTH_OOB, "custom-both-oob"},
  {GST_EVENT_STREAM_GROUP_DONE, "stream-group-done"},

  {0, NULL}
};

static void
get_trace_event_flag (GstCoolTracer * self)
{
  memset (self->trace_event_flag, FALSE,
      sizeof (gchar) * GST_GET_EVENT_FLAG (GST_EVENT_CUSTOM_BOTH_OOB));

  if (event_list) {
    GList *event;
    for (event = event_list; event; event = event->next) {
      int i;
      for (i = 0; event_types[i].name; ++i) {
        if (strcmp (event_types[i].name, event->data) == 0) {
          GST_PMLOG_INFO ("add to stare event : %s (flag : %d)",
              (gchar *) event->data, GST_GET_EVENT_FLAG (event_types[i].type));
          self->trace_event_flag[GST_GET_EVENT_FLAG (event_types[i].type)]
              = TRUE;
          break;
        }
      }
    }
  }
}
