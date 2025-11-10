/*
 * GStreamer splitappsink element
 *
 * Copyright 2016 LG Electronics, Inc.
 *  @author: HoonHee Lee <hoonhee.lee@lge.com>
 *
 * gstsplitappsink.h: Convenience bin that split data to appsink from incoming streams
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

#ifndef __GST_SPLIT_APPSINK_H__
#define __GST_SPLIT_APPSINK_H__

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_SPLIT_APPSINK (gst_split_appsink_get_type())
#define GST_SPLIT_APPSINK(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SPLIT_APPSINK,GstSplitAppsink))
#define GST_SPLIT_APPSINK_CLASS(obj) (G_TYPE_CHECK_CLASS_CAST((obj),GST_TYPE_SPLIT_APPSINK,GstSplitAppsinkClass))
#define GST_IS_SPLIT_APPSINK(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SPLIT_APPSINK))
#define GST_IS_SPLIT_APPSINK_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((obj),GST_TYPE_SPLIT_APPSINK))
#define GST_SPLIT_APPSINK_GET_LOCK(bin) (&((GstSplitAppsink*)(bin))->lock)
#define GST_SPLIT_APPSINK_LOCK(bin) (g_rec_mutex_lock (GST_SPLIT_APPSINK_GET_LOCK(bin)))
#define GST_SPLIT_APPSINK_UNLOCK(bin) (g_rec_mutex_unlock (GST_SPLIT_APPSINK_GET_LOCK(bin)))

typedef struct _GstSplitAppsink GstSplitAppsink;
typedef struct _GstSplitAppsinkClass GstSplitAppsinkClass;

struct _GstSplitAppsink
{
  GstBin parent;

  GRecMutex lock;

  GstPad *sinkpad;
  GstPad *srcpad;

  GstElement *tee;
  GstElement *queue;
  GstElement *appsink;

  GstStream *active_stream;
  gchar *stream_id;

  guint max_size_bytes;
  guint max_size_buffers;
  guint64 max_size_time;

  guint stream_type;
};

struct _GstSplitAppsinkClass
{
  GstBinClass parent_class;

  /* signals */
  void (*sink_setup) (GstSplitAppsink *splitter, GstElement *sink, const gchar *stream_id);
  void (*sink_removed) (GstSplitAppsink *splitter, const gchar *stream_id);
};

GType gst_split_appsink_get_type (void);

G_END_DECLS
#endif /* __GST_SPLIT_APPSINK_H__ */
