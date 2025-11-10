/*
 * GStreamer dashsink element
 *
 * Copyright 2017 LG Electronics, Inc.
 *  @author: Seoungil Kang <seoungil.kang@lge.com>
 *
 * gstdashsink.h:
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

#ifndef __GST_DASHSINK_H__
#define __GST_DASHSINK_H__

#include <gst/gst.h>
#include "manifestgenerator.hpp"

G_BEGIN_DECLS
#define GST_TYPE_DASHSINK (gst_dashsink_get_type())
#define GST_DASHSINK(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DASHSINK,GstDashSink))
#define GST_DASHSINK_CLASS(obj) (G_TYPE_CHECK_CLASS_CAST((obj),GST_TYPE_DASHSINK,GstDashSinkClass))
#define GST_IS_DASHSINK(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DASHSINK))
#define GST_IS_DASHSINK_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((obj),GST_TYPE_DASHSINK))

typedef struct _GstDashSink GstDashSink;
typedef struct _GstDashSinkClass GstDashSinkClass;

typedef struct _MediaStreamCtx
{
  gint refcount;

  GstDashSink * dashsink;

  gchar * name;
  gchar * suffix;
  gchar * current_file_name;

  guint probe_id;
  guint segment_id;
  gint adaptationset_id;
  guint input_timer_id;
  guint delay_count;

  gboolean is_reference;
  gboolean is_first_fragment;
  gboolean is_first_buffer;
  gboolean remove_file;

  GstClockTime segment_duration;
  GstClockTime next_start_time;
  GstClockTime first_ts;
  GstClockTime running_time;
  GstClockTimeDiff presentation_time_offset;

  GstSegment segment;

  GstPad * g_pad;
  GstElement * muxer;
  GstElement * sink;
} MediaStreamCtx;

struct _GstDashSink
{
  GstBin parent;

  gchar * location_prefix;
  gchar * media_prefix;
  guint segment_duration;
  gboolean use_source_timestamp;

  GMutex lock;

  guint video_id;
  guint audio_id;
  guint subtitle_id;
  GList *contexts;
  gboolean internal_eos;
  GstClockTimeDiff presentation_time_offset;

  ManifestGenerator *manifest_generator;
};

struct _GstDashSinkClass
{
  GstBinClass parent_class;

  /* actions */
  void (*update_metadata) (GstDashSink *dashsink, gpointer id);
};

GType gst_dashsink_get_type (void);

G_END_DECLS
#endif /* __GST_DASHSINK_H__ */
