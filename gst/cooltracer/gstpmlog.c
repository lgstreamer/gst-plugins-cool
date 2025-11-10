/* GStreamer Plugins Cool
 * Copyright (C) 2016 LG Electronics, Inc.
 *    Author : Heekyoung Seo <heekyoung.seo@lge.com>
 *
 * gstpmlog.c: pmlog for cooltracer
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
 * @short_description: pmlog for gstreamer cooltracer
 *
 * A pmlog for tracing module
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gstinfo.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>

#include "gstpmlog.h"

#ifdef USE_PMLOG
#include <PmLogLib.h>
#else
#include <stdio.h>
#endif

GST_DEBUG_CATEGORY_EXTERN (gst_cooltracer_debug);
#define GST_CAT_DEFAULT gst_cooltracer_debug

#define MAX_LOG_LENGTH  2000
#define GST_COOLTRACER_LOG_CONTEXT  "gst.tracer"


/* it is aligned with definition in gstreamer/gst/gstinfo.c */
#if defined (GLIB_SIZEOF_VOID_P) && GLIB_SIZEOF_VOID_P == 8
#define PTR_FMT "%14p"
#else
#define PTR_FMT "%10p"
#endif
#define PID_FMT "%5d"

#ifdef USE_PMLOG
static PmLogContext pmlog_context = NULL;

const char *
gst_pmlog_get_error_str (PmLogErr err)
{
  switch (err) {
    case kPmLogErr_None:
      return "";
    case kPmLogErr_InvalidContext:
      return "Invalid Context";
    case kPmLogErr_InvalidLevel:
      return "Invalid Level";
    case kPmLogErr_InvalidFormat:
      return "Invalid Format";
    case kPmLogErr_InvalidMsgID:
      return "Invalid MsgID";
    case kPmLogErr_LevelDisabled:
      return "LevelDisabled";
    default:
      return "";
  }
}

void
gst_pmlog_context_init ()
{
  PmLogErr err = kPmLogErr_None;
  err = PmLogGetContext (GST_COOLTRACER_LOG_CONTEXT, &pmlog_context);
  if (err != kPmLogErr_None)
    GST_ERROR ("failed to get pmlog context.. err : %s",
        gst_pmlog_get_error_str (err));
  else
    GST_INFO ("success to get pmlog context.. ");
}

static inline void
gst_pmlog_write_impl (int level, const char *msgid, const char *msg)
{
  PmLogErr err = kPmLogErr_None;
  if (pmlog_context == NULL)
    gst_pmlog_context_init ();
  switch (level) {
    case GST_LEVEL_ERROR:
    case GST_LEVEL_WARNING:
    case GST_LEVEL_FIXME:
      err = PmLogString (pmlog_context, kPmLogLevel_Error, msgid /*msgid */ ,
          NULL /*kvpairs */ , msg);
      break;
    case GST_LEVEL_INFO:
      err = PmLogString (pmlog_context, kPmLogLevel_Info, msgid /*msgid */ ,
          NULL /*kvpairs */ , msg);
      break;
    case GST_LEVEL_DEBUG:
      err = PmLogString (pmlog_context, kPmLogLevel_Debug, NULL /*msgid */ ,
          NULL /*kvpairs */ , msg);
      break;
    default:
#if 0                           // Do not print debug log in pmlog
      err = PmLogString (pmlog_context, kPmLogLevel_Debug, NULL /*msgid */ ,
          NULL /*kvpairs */ , msg);
#endif
      break;
  }
  if (err != kPmLogErr_None && err != kPmLogErr_InvalidLevel
      && err != kPmLogErr_LevelDisabled)
    GST_TRACE ("failed to write pmlog : %s(0x%x) : %s",
        gst_pmlog_get_error_str (err), err, msg);
}

#else


#define print_log(...)  fprintf(stdout, ">> " __VA_ARGS__)
//TODO : If tracer pmlog is needed in gst log instead of stdout, then use below
//       it doesn't include CATEGORY info, so it would print "default".
//#define print_log(...)  GST_TRACE(__VA_ARGS__)
static inline void
gst_pmlog_write_impl (int level, const char *msgid, const char *msg)
{
  switch (level) {
    case GST_LEVEL_ERROR:
    case GST_LEVEL_WARNING:
    case GST_LEVEL_FIXME:
      print_log ("[%s][%s][%s] %s\n", GST_COOLTRACER_LOG_CONTEXT, "ERR  ",
          msgid, msg);
      break;
    case GST_LEVEL_INFO:
      print_log ("[%s][%s][%s] %s\n", GST_COOLTRACER_LOG_CONTEXT, "INFO ",
          msgid, msg);
      break;
    case GST_LEVEL_DEBUG:
      print_log ("[%s][%s][%s] %s\n", GST_COOLTRACER_LOG_CONTEXT, "DEBUG",
          msgid, msg);
      break;
    default:
#if 0                           // Do not print debug log in stdout
      print_log ("[%s][%s][%s] %s\n", GST_COOLTRACER_LOG_CONTEXT, "LOG",
          msgid, msg);
#endif
      break;
  }
}

#endif

void
gst_pmlog_write (const char *tag, int level, const char *fmt, ...)
{
  va_list arg;

  va_start (arg, fmt);
  gst_pmlog_write_valist (tag, level, fmt, arg);
  va_end (arg);
}

void
gst_pmlog_write_valist (const char *tag, int level, const char *fmt,
    va_list arg)
{
  char msg[MAX_LOG_LENGTH] = { 0 };
  char *msgend = msg + MAX_LOG_LENGTH;
  char *msgpos = msg;

  // add pid and thread id in the message
  // getpid and g_thread_self are aligned with gstreamer debug log message in gstinfo.c
  msgpos +=
      snprintf (msgpos, msgend - msgpos, "[" PID_FMT "][" PTR_FMT "] ",
      getpid (), g_thread_self ());
  msgpos += vsnprintf (msgpos, msgend - msgpos, fmt, arg);

  gst_pmlog_write_impl (level, tag, msg);
}
