/* GStreamer Plugins Cool
 * Copyright (C) 2016 LG Electronics, Inc.
 *    Author : Kanghwan Jang <kanghwan.jang@lge.com>
 *
 * gsttraceelement.h: trace element operations
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

#ifndef __GST_TRACE_ELEMENT_H__
#define __GST_TRACE_ELEMENT_H__

#include <gst/gst.h>
#include "gsttracecommon.h"

typedef struct
{
  GstClockTime ts;
  GstElement *element;
  GstStateChange change;
} GstChangeState;


typedef struct
{
  GstTraceCommon common;

  int pos;
  int stack_size;
  GstChangeState stack[];
} GstTraceChangeState;

GstTraceChangeState *make_trace_change_state (GstClockTime ts);

const gchar *state_name (GstState state);

void
trace_change_state_pre (GstTraceChangeState * trace, GstClockTime ts,
    GstElement * elem, GstStateChange change);
void
trace_change_state_post (GstTraceChangeState * trace, GstClockTime ts,
    GstElement * elem, GstStateChange change, GstStateChangeReturn res);

#endif /* __GST_TRACE_ELEMENT_H__ */
