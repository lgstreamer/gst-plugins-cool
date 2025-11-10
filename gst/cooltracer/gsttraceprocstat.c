/* GStreamer Plugins Cool
 * Copyright (C) 2018 LG Electronics, Inc.
 *    Author : Heekyoung Seo <heekyoung.seo@lge.com>
 *
 * gsttraceprocstat.c: cpu/memory usages tracing module
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
 * @short_description: cpu/memory checker for gstreamer cooltracer
 *
 * Module for tracing cpu/memory usages
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gstinfo.h>

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "gstpmlog.h"
#include "gsttraceprocstat.h"

#define DEFAULT_TRACE_PROC_STAT 0
#define MAX_COMM_LENGTH 64
#define MAX_MEMORY_TO_NOTI 150000       // kB

GST_DEBUG_CATEGORY_EXTERN (gst_cooltracer_debug);
#define GST_CAT_DEFAULT gst_cooltracer_debug

struct _GstProcRead
{
  gint pid;                     //  1 : d
  char comm[MAX_COMM_LENGTH];   //  2 : s
  char state;                   //  3 : c
  gint ppid;                    //  4 : d
  gint pgrp;                    //  5 : d
  gint session;                 //  6 : d
  gint tty_nr;                  //  7 : d
  gint tpgid;                   //  8 : d
  guint flags;                  //  9 : u
  gulong minflt;                // 10 : lu
  gulong cminflt;               // 11 : lu
  gulong majflt;                // 12 : lu
  gulong cmajflt;               // 13 : lu

  gulong utime;                 // 14 : lu
  gulong stime;                 // 15 : lu
  glong cutime;                 // 16 : ld
  glong cstime;                 // 17 : ld
  glong priority;               // 18 : ld
  glong nice;                   // 19 : ld
  glong num_threads;            // 20 : ld
  glong itrealvalue;            // 21 : ld
  tick_t starttime;             // 22 : llu

  gulong vsize;                 // 23 : lu
  gulong rss;                   // 24 : lu
  gulong rsslim;                // 25 : lu
};

struct _GstTotalProcRead
{
  tick_t user;
  tick_t nice;
  tick_t system;
  tick_t idle;
  tick_t waitio;
  tick_t irq;
  tick_t softirq;
  tick_t steal;
};

typedef struct _GstProcRead GstProcRead;
typedef struct _GstTotalProcRead GstTotalProcRead;

#define RETURN_ZERO_IF_READ_FAIL(fd, ...) G_STMT_START {                        \
  if (fscanf(fd, __VA_ARGS__) == 0){                                            \
    GST_PMLOG_ERROR ("failed to read from /proc/.. %s %d", __func__, __LINE__); \
    fclose (fd);                                                                \
    return 0;                                                                   \
  }                                                                             \
} G_STMT_END

/**
 * read_proc_read
 * read /proc/pid/stat for calculating cpu/memory usages
 *
 *  Returns: %TRUE when reading is successed, returns %TRUE,
 *  %FALUSE otherwise.
 */
static gboolean
read_proc_stat (guint pid, GstProcRead * stat)
{
  char filename[32] = { 0 };
  FILE *fd = NULL;

  if (!stat)
    return FALSE;

  sprintf (filename, "/proc/%d/stat", pid);
  fd = fopen (filename, "r");
  if (fd) {
    char c;
    int comm_index = 0;
    RETURN_ZERO_IF_READ_FAIL (fd, "%d ", &stat->pid);
    do {
      RETURN_ZERO_IF_READ_FAIL (fd, "%c", &c);
      if (comm_index < MAX_COMM_LENGTH)
        stat->comm[comm_index] = c;
      ++comm_index;
    } while (c != ' ');
    do {
      RETURN_ZERO_IF_READ_FAIL (fd, "%c", &stat->state);
    } while (stat->state == ' ');

    RETURN_ZERO_IF_READ_FAIL (fd, "%d %d %d %d %d %u %lu", &stat->ppid,
        &stat->pgrp, &stat->session, &stat->tty_nr, &stat->tpgid, &stat->flags,
        &stat->minflt);
    RETURN_ZERO_IF_READ_FAIL (fd, "%lu %lu %lu %lu %lu %ld %ld", &stat->cminflt,
        &stat->majflt, &stat->cmajflt, &stat->utime, &stat->stime,
        &stat->cutime, &stat->cstime);
    RETURN_ZERO_IF_READ_FAIL (fd, "%ld %ld %ld %ld %llu %lu %ld %lu",
        &stat->priority, &stat->nice, &stat->num_threads, &stat->itrealvalue,
        &stat->starttime, &stat->vsize, &stat->rss, &stat->rsslim);

    fclose (fd);

    GST_PMLOG_DEBUG ("%d %s %c", stat->pid, stat->comm, stat->state);
    GST_PMLOG_DEBUG
        ("ppid:%d pgrp:%d session:%d tty_nr:%d tpgid:%d flags:%u minflt:%lu",
        stat->ppid, stat->pgrp, stat->session, stat->tty_nr, stat->tpgid,
        stat->flags, stat->minflt);
    GST_PMLOG_DEBUG
        ("cminflt:%lu majflt:%lu cmajflt:%lu utime:%lu stime:%lu cutime:%ld cstime:%ld",
        stat->cminflt, stat->majflt, stat->cmajflt, stat->utime, stat->stime,
        stat->cutime, stat->cstime);
    GST_PMLOG_DEBUG
        ("pri:%ld nice:%ld numt_threads:%ld itrealvalue:%ld starttime:%llu vsize:%lu rss:%ld rslim:%lu",
        stat->priority, stat->nice, stat->num_threads, stat->itrealvalue,
        stat->starttime, stat->vsize, stat->rss, stat->rsslim);
  } else {
    GST_PMLOG_INFO ("Can't open %s", filename);
    return FALSE;
  }
  return TRUE;
}

/**
 * read_total_proc_stat
 * read /proc/stat for calculating total cpu usages
 *
 *  Returns: %TRUE when reading is successed, returns %TRUE,
 *  %FALUSE otherwise.
 */
static gboolean
read_total_proc_stat (GstTotalProcRead * stat)
{
  FILE *fd = NULL;

  if (!stat)
    return FALSE;

  fd = fopen ("/proc/stat", "r");
  if (fd) {
    RETURN_ZERO_IF_READ_FAIL (fd, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
        &stat->user, &stat->nice, &stat->system, &stat->idle, &stat->waitio,
        &stat->irq, &stat->softirq, &stat->steal);

    fclose (fd);

    GST_PMLOG_DEBUG
        ("user:%llu, nice:%llu, system:%llu, idle:%llu, waitio:%llu, irq:%llu, softirq:%llu, steal:%llu",
        stat->user, stat->nice, stat->system, stat->idle,
        stat->waitio, stat->irq, stat->softirq, stat->steal);
  } else {
    GST_PMLOG_INFO ("Can't open %s", "/proc/stat");
    return FALSE;
  }
  return TRUE;
}


static gboolean
get_cpu_usages (GstProcRead * stat, GstProcStat * procstat)
{
  guint64 total_time = 0;
  guint64 current_pipeline_cpu_time = 0;
  guint64 partial_total_time = 0;
  gfloat uptime = 0;
  gfloat seconds = 0;
  guint cpu_usage = 0;
  FILE *fd = NULL;
  guint loglevel = GST_LEVEL_DEBUG;

  if (!procstat || procstat->hertz == 0)
    return FALSE;

  fd = fopen ("/proc/uptime", "r");
  if (fd) {
    RETURN_ZERO_IF_READ_FAIL (fd, "%f", &uptime);
    fclose (fd);
    GST_PMLOG_DEBUG ("uptime : %.1f", uptime);
  } else {
    GST_PMLOG_ERROR
        ("Can't calculate cpu usages because /proc/uptime can't be opened.");
    return FALSE;
  }

  total_time = stat->utime + stat->stime;
  total_time += stat->cutime + stat->cstime;

  if (procstat->previous_cpu_time > 0)
    current_pipeline_cpu_time = total_time - procstat->previous_cpu_time;
  else
    current_pipeline_cpu_time = total_time;

  GST_PMLOG (loglevel,
      "uptime : %.1f, utime %lu, stime : %lu, cutime : %lu, cstime : %lu",
      uptime, stat->utime, stat->stime, stat->cutime, stat->cstime);
  GST_PMLOG (loglevel,
      "start time : %llu, s-uptime ; %.1f, p-total time : %lld, hertz : %d",
      stat->starttime, procstat->uptime, procstat->previous_cpu_time,
      procstat->hertz);

  // Calculate average cpu usage
  if (procstat->uptime)
    seconds = uptime - procstat->uptime;
  else
    seconds = uptime - (stat->starttime / procstat->hertz);

  GST_PMLOG (loglevel,
      "calculate >>> total time : %lld = (%llu - %lld), seconds : %.1f",
      current_pipeline_cpu_time, total_time, procstat->previous_cpu_time,
      seconds);

  if (seconds != 0)             // prevent devided by zero
    cpu_usage = (100 * current_pipeline_cpu_time / procstat->hertz) / seconds;
  else
    cpu_usage = (100 * current_pipeline_cpu_time) / procstat->hertz;

  cpu_usage /= procstat->cpu_num;
  procstat->cpu.avg = cpu_usage;
  GST_PMLOG (loglevel, "average cpu usages : %u %", cpu_usage);

  // Calculate partial cpu usage
  if (procstat->cpu.last_uptime) {
    seconds = uptime - procstat->cpu.last_uptime;
    partial_total_time = total_time - procstat->cpu.last_total_time;
    cpu_usage = (partial_total_time * 100) / procstat->hertz / seconds;
    cpu_usage /= procstat->cpu_num;
    if (procstat->cpu.min > cpu_usage) {
      GST_PMLOG_INFO
          ("update minimum cpu usages : %3d%, total : %3d% (u:%3d,s:%3d,n:%3d)",
          cpu_usage, procstat->cpu_total.percent, procstat->cpu_total.us,
          procstat->cpu_total.sy, procstat->cpu_total.ni);
      procstat->cpu.min = cpu_usage;
    }
    if (procstat->cpu.max < cpu_usage) {
      GST_PMLOG_INFO
          ("update maximum cpu usages : %3d%, total : %3d% (u:%3d,s:%3d,n:%3d)",
          cpu_usage, procstat->cpu_total.percent, procstat->cpu_total.us,
          procstat->cpu_total.sy, procstat->cpu_total.ni);
      procstat->cpu.max = cpu_usage;
    }
  } else {
    // Initial value
    procstat->cpu.min = procstat->cpu.max = cpu_usage;
    GST_PMLOG_INFO ("cpu usages : %3d%, total : %3d% (u:%3d,s:%3d,n:%3d)",
        cpu_usage, procstat->cpu_total.percent, procstat->cpu_total.us,
        procstat->cpu_total.sy, procstat->cpu_total.ni);
  }
  procstat->cpu.last_uptime = uptime;
  procstat->cpu.last_total_time = total_time;

  return TRUE;
}

static void
get_memory_usages (GstProcRead * stat, GstProcStat * procstat)
{
  if (procstat->page_size == 0.0)
    return;

  if (procstat->memory.cur) {
    if (procstat->memory.min > stat->rss) {
      long last = procstat->memory.min;
      procstat->memory.min = stat->rss;
      GST_PMLOG_INFO
          ("update minimum memory usages : %.0f kB >> %.0f kB",
          last * procstat->page_size,
          procstat->memory.min * procstat->page_size);
    }
    if (procstat->memory.max < stat->rss) {
      long last = procstat->memory.max;
      procstat->memory.max = stat->rss;
      GST_PMLOG_INFO
          ("update maximum memory usages : %.0f kB >> %.0f kB (%4.0f kB up)",
          last * procstat->page_size,
          procstat->memory.max * procstat->page_size,
          (procstat->memory.max - last) * procstat->page_size);
    }
    procstat->memory.cur = stat->rss;
  } else {
    // Initial value
    procstat->memory.cur = procstat->memory.min = procstat->memory.max =
        stat->rss;
  }
}

static void
get_total_cpu (const GstTotalProcRead * stat, GstProcStat * procstat)
{
  tick_t cur_usages = 0;
  tick_t cur_idle = 0;
  tick_t usages = 0;
  long long idle = 0;
  tick_t total = 0;
  guint loglevel = GST_LEVEL_DEBUG;

  cur_usages =
      stat->user + stat->nice + stat->system + stat->irq + stat->softirq +
      stat->steal;
  cur_idle = stat->idle + stat->waitio;

  usages = cur_usages - procstat->cpu_total.usages;
  idle = cur_idle - procstat->cpu_total.idle;
  if (idle < 0)
    idle = 0;

  total = usages + idle;
  if (total) {
    int percent = (usages * 100) / total;
    if (percent == 0)
      return;
    else if (percent > 90)
      loglevel = GST_LEVEL_INFO;
    procstat->cpu_total.percent = percent;
    procstat->cpu_total.us =
        ((stat->user - procstat->cpu_total.user) * 100) / total;
    procstat->cpu_total.ni =
        ((stat->nice - procstat->cpu_total.nice) * 100) / total;
    procstat->cpu_total.sy =
        ((stat->system - procstat->cpu_total.system) * 100) / total;
  }

  GST_PMLOG (loglevel, "Total cpu us:%3d%, sy:%3d%, ni:%3d% [%3d %]",
      procstat->cpu_total.us, procstat->cpu_total.sy, procstat->cpu_total.ni,
      procstat->cpu_total.percent);

  procstat->cpu_total.usages = cur_usages;
  procstat->cpu_total.idle = cur_idle;

  procstat->cpu_total.user = stat->user;
  procstat->cpu_total.nice = stat->nice;
  procstat->cpu_total.system = stat->system;
}

static void
calculate_proc_stat (GstProcStat * procstat)
{
  GstProcRead proc_read = { 0 };
  GstTotalProcRead total_proc = { 0 };

  if (!procstat)
    return;

  g_mutex_lock (&procstat->lock);
  if (read_proc_stat (procstat->pid, &proc_read)) {
    if (read_total_proc_stat (&total_proc)) {
      get_total_cpu (&total_proc, procstat);
    }
    get_cpu_usages (&proc_read, procstat);
    get_memory_usages (&proc_read, procstat);
  }
  g_mutex_unlock (&procstat->lock);
}

void
gst_trace_proc_stat_post (GstObject * obj, GstElement * elem,
    GstProcStat * procstat)
{
  GstMessage *message = NULL;
  GstStructure *trace_info = NULL;
  GstStructure *cpu_info = NULL;
  GstStructure *mem_info = NULL;
  gboolean result = FALSE;

  if (!procstat)
    return;

  g_mutex_lock (&procstat->lock);
  do {
    cpu_info = gst_structure_new ("cpu",
        "avg", G_TYPE_UINT, procstat->cpu.avg,
        "min", G_TYPE_UINT, procstat->cpu.min,
        "max", G_TYPE_UINT, procstat->cpu.max, NULL);
    if (!cpu_info)
      break;
    mem_info = gst_structure_new ("mem",
        "cur", G_TYPE_UINT,
        (guint) (procstat->memory.cur * procstat->page_size), "min",
        G_TYPE_UINT, (guint) (procstat->memory.min * procstat->page_size),
        "max", G_TYPE_UINT,
        (guint) (procstat->memory.max * procstat->page_size), NULL);
    if (!mem_info)
      break;
    trace_info = gst_structure_new ("tracing_info",
        "cpu-info", GST_TYPE_STRUCTURE, cpu_info,
        "mem-info", GST_TYPE_STRUCTURE, mem_info, NULL);
    if (!trace_info)
      break;
    result = TRUE;
  } while (0);
  g_mutex_unlock (&procstat->lock);

  if (cpu_info)
    gst_structure_free (cpu_info);
  if (mem_info)
    gst_structure_free (mem_info);

  if (result) {
    // Post message of tracing data with GST_MESSAGE_APPLICSTION.
    message = gst_message_new_custom (GST_MESSAGE_APPLICATION, obj, trace_info);
    if (!message) {
      gst_structure_free (trace_info);
      GST_PMLOG_ERROR ("Failed to create message for posting");
    } else {
      GST_PMLOG_DEBUG ("post message: %s",
          gst_structure_to_string (trace_info));
      gst_element_post_message (elem, message);
    }
  } else {
    GST_PMLOG_ERROR ("Failed to create structure for posting message");
  }
}

static gboolean
trace_proc_stat_thread_func (gpointer data)
{
  GstProcStat *procstat = (GstProcStat *) data;;

  calculate_proc_stat (procstat);

  return TRUE;
}

void
gst_trace_proc_stat_log (const gchar * state, GstProcStat * procstat)
{
  guint loglevel = GST_LEVEL_INFO;
  if (!procstat)
    return;

  g_mutex_lock (&procstat->lock);
  if (procstat->cpu.avg > 100 || procstat->memory.max > procstat->memory.limit)
    loglevel = GST_LEVEL_ERROR;

  GST_PMLOG (loglevel,
      "[%s] cpu usages : %u % (%u ~ %u), memory spent : %.0f kB (%.0f ~ %.0f)",
      state, procstat->cpu.avg, procstat->cpu.min, procstat->cpu.max,
      procstat->memory.cur * procstat->page_size,
      procstat->memory.min * procstat->page_size,
      procstat->memory.max * procstat->page_size);
  g_mutex_unlock (&procstat->lock);
}

void
gst_trace_proc_stat_reset (GstProcStat * procstat)
{
  FILE *fd = NULL;

  if (!procstat)
    return;

  GST_PMLOG_INFO ("procstat reset, self:%p", procstat);

  if (procstat->source_id != 0)
    g_source_remove (procstat->source_id);

  /* Clear procstat except the values that has to be saved. */
  memset (procstat, 0, sizeof (GstProcStat));

  procstat->pid = getpid ();
  procstat->page_size = sysconf (_SC_PAGESIZE);
  procstat->hertz = sysconf (_SC_CLK_TCK);
  procstat->cpu_num = sysconf (_SC_NPROCESSORS_ONLN);
  if (procstat->hertz == 0 || procstat->cpu_num == 0
      || procstat->page_size == 0.0) {
    GST_PMLOG_ERROR
        ("Can't check worklaod because sysconf doesn't work properly.");
    return;
  }
  procstat->page_size /= 1024;
  if (procstat->page_size == 0.0) {
    GST_PMLOG_ERROR
        ("Can't check memory because sysconf(_SC_PAGESIZE) doesn't work properly. %.1f",
        procstat->page_size);
    return;
  }

  procstat->memory.limit = MAX_MEMORY_TO_NOTI / procstat->page_size;

  GST_PMLOG_DEBUG ("pid : %d, clock hertz : %ld", procstat->pid,
      procstat->hertz);
  g_mutex_init (&procstat->lock);

  /* Create new thread to compute the cpu usage periodically */
  if (procstat->source_id == 0)
    procstat->source_id =
        g_timeout_add_seconds (1, trace_proc_stat_thread_func,
        (gpointer) procstat);

  /* TODO : We do not apply cpu number for checking cpu usages at this moment. */
  procstat->cpu_num = 1;

  calculate_proc_stat (procstat);
  procstat->previous_cpu_time = procstat->cpu.last_total_time;
  GST_PMLOG_DEBUG ("previous total time of init state: %llu",
      procstat->previous_cpu_time);

  fd = fopen ("/proc/uptime", "r");
  if (fd) {
    if (fscanf (fd, "%f", &procstat->uptime) == 0)
      GST_PMLOG_ERROR ("failed to read from /proc/uptime");
    fclose (fd);
    GST_PMLOG_DEBUG ("uptime of init state: %.1f", procstat->uptime);
  } else {
    GST_PMLOG_ERROR
        ("Can't calculate cpu usages because /proc/uptime can't be opened.");
  }
}

void
gst_trace_proc_stat_init (GstProcStat * procstat)
{
  GST_PMLOG_INFO ("procstat init, self:%p", procstat);
  if (!procstat)
    return;

  memset (procstat, 0, sizeof (GstProcStat));
}

void
gst_trace_proc_stat_deinit (GstProcStat * procstat)
{
  if (!procstat)
    return;

  if (procstat->source_id) {
    g_source_remove (procstat->source_id);
    procstat->source_id = 0;
  }

  if (procstat->hertz && procstat->page_size) {
    GST_PMLOG_INFO ("average cpu usage : %u, min : %u, max : %u",
        procstat->cpu.avg, procstat->cpu.min, procstat->cpu.max);
    GST_PMLOG_INFO ("memory usage : %.0f min : %.0f, max : %.0f",
        procstat->memory.cur * procstat->page_size,
        procstat->memory.min * procstat->page_size,
        procstat->memory.max * procstat->page_size);
  } else {
    GST_PMLOG_ERROR ("Can't measure cpu/memory usages.");
  }
  return;
}
