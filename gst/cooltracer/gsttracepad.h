/* GStreamer Plugins Cool
 * Copyright (C) 2016 LG Electronics, Inc.
 *    Author : Kanghwan Jang <kanghwan.jang@lge.com>
 *
 * gsttracepad.h: trace pad operations
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

#ifndef __GST_TRACE_PAD_H__
#define __GST_TRACE_PAD_H__

#include <gst/gst.h>
#include "gsttracecommon.h"

typedef struct
{
  GstClockTime ts;
  GstPad *pad;
  GstBuffer *buffer;
} GstPadPush;

typedef struct
{
  GstTraceCommon common;

  int pos;
  int stack_size;
  GstPadPush stack[];
} GstTracePadPush;


GstTracePadPush *make_trace_pad_push (GstClockTime ts);

void
trace_pad_push_pre (GstTracePadPush * trace, GstClockTime ts,
    GstPad * pad, GstBuffer * buffer);
void
trace_pad_push_post (GstTracePadPush * trace, GstClockTime ts,
    GstPad * pad, GstBuffer * buffer, GstFlowReturn res);

#endif /* __GST_TRACE_PAD_H__ */
