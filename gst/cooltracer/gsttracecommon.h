/* GStreamer Plugins Cool
 * Copyright (C) 2016 LG Electronics, Inc.
 *    Author : Kanghwan Jang <kanghwan.jang@lge.com>
 *
 * gsttracecommon.h: tracing module
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

#ifndef __GST_TRACE_COMMON_H__
#define __GST_TRACE_COMMON_H__

#include <gst/gst.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * GstTraceCallbackCheckTrace
 * @obj: a #GstTracer
 * @data: trace data
 * @current: current time which is not normalized by _priv_gst_start_time
 *
 * check if the trace data is good.
 *
 * Returns: FALSE if something bad.
 */
typedef gboolean (*GstTraceCallbackCheckTrace) (GstTracer * obj, gpointer data,
    GstClockTime current);

typedef struct
{
  GstClockTime last_updated;
  gint error_log_count;
  pid_t thread;
  GstClockTime start_time;      // real time = start_time + last_updated
  gboolean pre;                 // TRUE if pre

  GstTraceCallbackCheckTrace check_trace; // check trace and report error
} GstTraceCommon;


#define DURATION_MAX_BLOCKING     (5*GST_SECOND)
#define MAX_ERROR_LOG_COUNT       3


#define FMT_PAD_NAME    "%s:%s"
#define FMT_PAD_NAME_W  "%20s:%-10s"
#define PRINT_PAD_NAME(pad) \
  pad ? \
      GST_PAD_PARENT(pad) ? GST_ELEMENT_NAME(GST_PAD_PARENT(pad)):"(null)" \
      :"(null)", \
  pad ? GST_PAD_NAME(pad) : "(null)"


#define GETTID()  (syscall(SYS_gettid))


#endif /* __GST_TRACE_COMMON_H__ */
