/* GStreamer Plugins Cool
 * Copyright (C) 2016 LG Electronics, Inc.
 *    Author : Kanghwan Jang <kanghwan.jang@lge.com>
 *
 * gstcooltracer.h: tracing module
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

#ifndef __GST_COOL_TRACER_H__
#define __GST_COOL_TRACER_H__

#include <gst/gst.h>
#include <gst/gsttracer.h>

G_BEGIN_DECLS
#define GST_TYPE_COOL_TRACER \
  (gst_cool_tracer_get_type())
#define GST_COOL_TRACER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_COOL_TRACER,GstCoolTracer))
#define GST_COOL_TRACER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_COOL_TRACER,GstCoolTracerClass))
#define GST_IS_COOL_TRACER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_COOL_TRACER))
#define GST_IS_COOL_TRACER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_COOL_TRACER))
#define GST_COOL_TRACER_CAST(obj) \
  ((GstCoolTracer *)(obj))
#define GST_COOL_TRACER_GET_LOCK(obj) \
  (&((GstCoolTracer*)(obj))->lock)
#define GST_COOL_TRACER_LOCK(obj) \
  (g_mutex_lock (GST_COOL_TRACER_GET_LOCK(obj)))
#define GST_COOL_TRACER_UNLOCK(obj) \
  (g_mutex_unlock (GST_COOL_TRACER_GET_LOCK(obj)))
typedef struct _GstCoolTracer GstCoolTracer;
typedef struct _GstCoolTracerClass GstCoolTracerClass;

#define GST_GET_EVENT_FLAG(e) (e >> GST_EVENT_NUM_SHIFT)

#define MAX_STARE_COUNT     32
/**
 * GstCoolTracer:
 *
 * Opaque #GstCoolTracer data structure
 */
struct _GstCoolTracer
{
  GstTracer parent;

  /*< private > */
  GMutex lock;
  GList *trace_list;
  gchar trace_event_flag[GST_GET_EVENT_FLAG(GST_EVENT_CUSTOM_BOTH_OOB)];

  GMainContext *context;
  GSource *tick_source;

  GstElement *pipeline;
  GstState pipeline_state;

  GstElement *stared_elements[MAX_STARE_COUNT];
  guint stared_elements_count;


  gboolean error_detected;
  gint error_detected_count;

  GObject *procstat;
  gboolean is_psonly_mode;
  gboolean enable_pstrace;
  gboolean enable_post_psinfo;

  /* dump dot graph from context */
  gchar filename[128];
  gboolean dump_dot_graph;
  gboolean is_dump_dot_dir;
};

struct _GstCoolTracerClass
{
  GstTracerClass parent_class;

  /* signals */
};

G_GNUC_INTERNAL GType gst_cool_tracer_get_type (void);

G_END_DECLS
#endif /* __GST_COOL_TRACER_H__ */
