/* GStreamer Plugins Cool
 * Copyright (C) 2018 LG Electronics, Inc.
 *    Author : Heekyoung Seo <heekyoung.seo@lge.com>
 *
 * gsttraceprocstat.h: cpu/memory usages tracing module
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


#ifndef __gst_trace_proc_stat_H__
#define __gst_trace_proc_stat_H__

#include <gst/gst.h>
#include <sys/types.h>

typedef unsigned long long tick_t;

typedef struct
{
  guint64 last_uptime;
  guint64 last_total_time;

  guint min;
  guint max;
  guint avg;
} GstProcCpuUsageStruct;

typedef struct
{
  guint min;
  guint max;
  guint cur;
  guint limit;
} GstProcMemStruct;

typedef struct
{
  tick_t usages;
  tick_t idle;
  tick_t user;
  tick_t nice;
  tick_t system;

  guint percent;
  guint us;
  guint sy;
  guint ni;
} GstCpuTotal;

typedef struct
{
  long hertz;
  gfloat page_size;
  pid_t pid;
  guint cpu_num;
  guint source_id;
  gfloat uptime;
  tick_t previous_cpu_time;

  GstProcCpuUsageStruct cpu;
  GstProcMemStruct memory;
  GstCpuTotal cpu_total;

  GMutex lock;
} GstProcStat;

void gst_trace_proc_stat_init (GstProcStat * procstat);
void gst_trace_proc_stat_reset (GstProcStat * procstat);
void gst_trace_proc_stat_post (GstObject * obj, GstElement * elem, GstProcStat * procstat);
void gst_trace_proc_stat_log (const gchar* state, GstProcStat * procstat);
void gst_trace_proc_stat_deinit (GstProcStat * procstat);

#endif
