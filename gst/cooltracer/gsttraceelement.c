/* GStreamer Plugins Cool
 * Copyright (C) 2016 LG Electronics, Inc.
 *    Author : Kanghwan Jang <kanghwan.jang@lge.com>
 *
 * gsttraceelement.c: trace state operations
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
#  include "config.h"
#endif

#include "gstcooltracer.h"
#include "gstpmlog.h"
#include "gsttraceelement.h"

GST_DEBUG_CATEGORY_EXTERN (gst_cooltracer_debug);
#define GST_CAT_DEFAULT gst_cooltracer_debug


static void
get_element_names (GstTraceChangeState * trace, gchar * buf, int len)
{
  int i;
  gchar *ptr = buf;

  ptr[0] = 0;
  if (trace->pos < trace->stack_size) {
    for (i = 0; i <= trace->pos; ++i) {
      ptr += g_snprintf (ptr, len - (ptr - buf), "%s%s",
          (i == 0) ? "" : " -> ", GST_ELEMENT_NAME (trace->stack[i].element));
    }
  }
}


static gboolean
check_trace (GstTracer * obj, gpointer data, GstClockTime current)
{
  gchar names[1024];
  GstClockTimeDiff elapsed;
  GstTraceChangeState *trace = data;

  if (!trace->common.pre)
    return TRUE;

  elapsed = GST_CLOCK_DIFF (trace->common.last_updated, current);
  if (elapsed > DURATION_MAX_BLOCKING) {
    trace->common.error_log_count += 1;
    if (trace->common.error_log_count <= MAX_ERROR_LOG_COUNT) {
      get_element_names (trace, names, sizeof (names));
      GST_PMLOG_INFO
          ("gst_element_change_state is pending for %lldms(log count:%d) : %s",
          elapsed / GST_MSECOND, trace->common.error_log_count, names);
    }
    return FALSE;
  }

  return TRUE;
}

GstTraceChangeState *
make_trace_change_state (GstClockTime ts)
{
  const gsize size = 512;

  GstTraceChangeState *trace = (GstTraceChangeState *) g_malloc0 (size);
  if (!trace) {
    GST_PMLOG_INFO ("make_trace_change_state: g_malloc0 (%d) failed", size);
    return NULL;
  }

  trace->common.thread = GETTID ();
  trace->common.pre = FALSE;
  trace->common.start_time = gst_util_get_timestamp () - ts;
  trace->common.check_trace = check_trace;

  trace->stack_size =
      (size - sizeof (GstTraceChangeState)) / sizeof (GstChangeState);
  trace->pos = -1;
  return trace;
}

inline const gchar *
state_name (GstState state)
{
  const gchar *names[] = {
    "VOID_PENDING", "NULL", "READY", "PAUSED", "PLAYING"
  };

  if (state < sizeof (names) / sizeof (*names))
    return names[state];
  return "unknown";
}


void
trace_change_state_pre (GstTraceChangeState * trace, GstClockTime ts,
    GstElement * elem, GstStateChange change)
{
  int newpos;

  newpos = trace->pos + 1;
  trace->common.pre = TRUE;
  trace->common.last_updated = ts + trace->common.start_time;
  trace->common.error_log_count = 0;
  if (newpos < trace->stack_size) {     //record only when there's enough space
    GstChangeState *t = &trace->stack[newpos];
    t->ts = trace->common.last_updated;
    t->element = elem;
    t->change = change;
  }
  trace->pos = newpos;
  GST_PMLOG_VERBOSE ("state:%s->%s pos:%d element:%s",
      state_name (GST_STATE_TRANSITION_CURRENT (change)),
      state_name (GST_STATE_TRANSITION_NEXT (change)),
      trace->pos, GST_ELEMENT_NAME (elem));

}

void
trace_change_state_post (GstTraceChangeState * trace, GstClockTime ts,
    GstElement * elem, GstStateChange change, GstStateChangeReturn res)
{
  // TODO: generate error log if push failed.

  if (trace->pos < trace->stack_size) {
    gchar names[1024];

    if (trace->stack[trace->pos].element != elem)
      GST_PMLOG_ERROR
          ("state  element mismatch elem:%p != expected:%p",
          elem, trace->stack[trace->pos].element);

    if (trace->common.pre) {    //print names on the first post hook only
      get_element_names (trace, names, sizeof (names));
      GST_PMLOG_DEBUG ("state:%s->%s element:%s",
          state_name (GST_STATE_TRANSITION_CURRENT (change)),
          state_name (GST_STATE_TRANSITION_NEXT (change)), names);
    }

    if (trace->common.error_log_count > 0) {
      GstClockTime current;
      GstClockTimeDiff elapsed;
      current = gst_util_get_timestamp ();
      elapsed = GST_CLOCK_DIFF (trace->common.last_updated, current);
      if (!trace->common.pre)
        get_element_names (trace, names, sizeof (names));
      GST_PMLOG_INFO ("gst_element_change_state took %lldms : %s",
          (elapsed / GST_MSECOND), names);
    }
  }
  trace->pos -= 1;
  trace->common.pre = FALSE;
  trace->common.last_updated = ts + trace->common.start_time;
  trace->common.error_log_count = 0;

  GST_PMLOG_VERBOSE
      ("state:%s->%s pos:%d element:%s res:%d",
      state_name (GST_STATE_TRANSITION_CURRENT (change)),
      state_name (GST_STATE_TRANSITION_NEXT (change)), trace->pos,
      GST_ELEMENT_NAME (elem), res);
}
