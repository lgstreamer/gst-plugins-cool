/* GStreamer Plugins Cool
 * Copyright (C) 2016 LG Electronics, Inc.
 *    Author : Kanghwan Jang <kanghwan.jang@lge.com>
 *
 * gsttracepad.c: trace pad operations
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
#include "gsttracepad.h"

GST_DEBUG_CATEGORY_EXTERN (gst_cooltracer_debug);
#define GST_CAT_DEFAULT gst_cooltracer_debug


static void
get_pad_names (GstTracePadPush * trace, gchar * buf, int len)
{
  int i;
  gchar *ptr = buf;

  ptr[0] = 0;
  if (trace->pos < trace->stack_size) {
    for (i = 0; i <= trace->pos; ++i) {
      ptr += g_snprintf (ptr, len - (ptr - buf), "%s" FMT_PAD_NAME,
          (i == 0) ? "" : " -> ", GST_PAD_PARENT (trace->stack[i].pad) ?
          GST_ELEMENT_NAME (GST_PAD_PARENT (trace->stack[i].pad)) : "(null)",
          GST_PAD_NAME (trace->stack[i].pad));
    }
    if (trace->pos >= 0)
      ptr += g_snprintf (ptr, len - (ptr - buf), " -> " FMT_PAD_NAME,
          PRINT_PAD_NAME (trace->stack[trace->pos].pad->peer));
  }
}

static gboolean
check_trace (GstTracer * obj, gpointer data, GstClockTime current)
{
  gchar names[1024];
  GstClockTimeDiff elapsed;
  GstTracePadPush *trace = data;
  GstCoolTracer *self = GST_COOL_TRACER (obj);

  if (self->pipeline_state != GST_STATE_PLAYING)
    return TRUE;

  if (!trace->common.pre)
    return TRUE;

  elapsed = GST_CLOCK_DIFF (trace->common.last_updated, current);
  if (elapsed > DURATION_MAX_BLOCKING) {
    trace->common.error_log_count += 1;
    if (trace->common.error_log_count <= MAX_ERROR_LOG_COUNT) {
      get_pad_names (trace, names, sizeof (names));
      GST_PMLOG_INFO ("gst_pad_push is pending for %lldms(log count:%d) : %s",
          (elapsed / GST_MSECOND), trace->common.error_log_count, names);
    }
    return FALSE;
  }

  return TRUE;
}

GstTracePadPush *
make_trace_pad_push (GstClockTime ts)
{
  const gsize size = 512;

  GstTracePadPush *trace = (GstTracePadPush *) g_malloc0 (size);
  if (!trace) {
    GST_PMLOG_INFO ("make_trace_pad_push: g_malloc0 (%d) failed", size);
    return NULL;
  }

  trace->common.thread = GETTID ();
  trace->common.pre = FALSE;
  trace->common.start_time = gst_util_get_timestamp () - ts;
  trace->common.check_trace = check_trace;

  trace->stack_size = (size - sizeof (GstTracePadPush)) / sizeof (GstPadPush);
  trace->pos = -1;
  return trace;
}

void
trace_pad_push_pre (GstTracePadPush * trace, GstClockTime ts,
    GstPad * pad, GstBuffer * buffer)
{
  int newpos;

  newpos = trace->pos + 1;
  trace->common.pre = TRUE;
  trace->common.last_updated = ts + trace->common.start_time;
  trace->common.error_log_count = 0;
  if (newpos < trace->stack_size) {     //record only when there's enough space
    GstPadPush *t = &trace->stack[newpos];
    t->ts = trace->common.last_updated;
    t->pad = pad;
    t->buffer = buffer;
  }
  trace->pos = newpos;
  GST_PMLOG_VERBOSE ("buffer push pos:%d", newpos);
}

void
trace_pad_push_post (GstTracePadPush * trace, GstClockTime ts,
    GstPad * pad, GstBuffer * buffer, GstFlowReturn res)
{
  // TODO: generate error log if push failed.

  if (trace->pos < trace->stack_size) {
    gchar names[1024];
    if (trace->stack[trace->pos].pad != pad)
      GST_PMLOG_ERROR
          ("buffer pad mismatch pad:%p != expected:%p",
          pad, trace->stack[trace->pos].pad);

    if (trace->common.pre) {    //print names on the first post hook only
      get_pad_names (trace, names, sizeof (names));
      GST_PMLOG_DEBUG ("buffer push pads:%s", names);
    }
    if (trace->common.error_log_count > 0) {
      GstClockTime current;
      GstClockTimeDiff elapsed;
      current = gst_util_get_timestamp ();
      elapsed = GST_CLOCK_DIFF (trace->common.last_updated, current);
      if (!trace->common.pre)
        get_pad_names (trace, names, sizeof (names));
      GST_PMLOG_INFO ("gst_pad_push took %lldms : %s",
          (elapsed / GST_MSECOND), names);
    }
  }
  trace->pos -= 1;
  trace->common.pre = FALSE;
  trace->common.last_updated = ts + trace->common.start_time;
  trace->common.error_log_count = 0;
  GST_PMLOG_VERBOSE ("buffer push pos:%d res:%d", trace->pos, res);
}
