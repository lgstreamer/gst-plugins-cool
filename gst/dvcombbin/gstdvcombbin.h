/*
 * GStreamer dvcombbin element
 *
 * Copyright 2016 LG Electronics, Inc.
 *  @author: Seungha Yang <sh.yang@lge.com>
 *
 * gstdvcombbin.h: Interleaving element for dolby vision dual track
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

#ifndef __GST_DVCOMB_BIN_H__
#define __GST_DVCOMB_BIN_H__

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_DVCOMB_BIN (gst_dvcomb_bin_get_type())
#define GST_DVCOMB_BIN(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DVCOMB_BIN,GstDvcombBin))
#define GST_DVCOMB_BIN_CLASS(obj) (G_TYPE_CHECK_CLASS_CAST((obj),GST_TYPE_DVCOMB_BIN,GstDvcombBinClass))
#define GST_IS_DVCOMB_BIN(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DVCOMB_BIN))
#define GST_IS_DVCOMB_BIN_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((obj),GST_TYPE_DVCOMB_BIN))

#define GST_DVCOMB_BIN_GET_LOCK(bin) (&((GstDvcombBin*)(bin))->lock)
#define GST_DVCOMB_BIN_LOCK(bin) (g_rec_mutex_lock (GST_DVCOMB_BIN_GET_LOCK(bin)))
#define GST_DVCOMB_BIN_UNLOCK(bin) (g_rec_mutex_unlock (GST_DVCOMB_BIN_GET_LOCK(bin)))

#define GST_DVCOMB_BIN_GET_SEEK_LOCK(bin) (&((GstDvcombBin*)(bin))->seek_lock)
#define GST_DVCOMB_BIN_SEEK_LOCK(bin) (g_mutex_lock (GST_DVCOMB_BIN_GET_SEEK_LOCK(bin)))
#define GST_DVCOMB_BIN_SEEK_UNLOCK(bin) (g_mutex_unlock (GST_DVCOMB_BIN_GET_SEEK_LOCK(bin)))

typedef struct _GstDvcombGroup GstDvcombGroup;
typedef struct _GstDvcombBin GstDvcombBin;
typedef struct _GstDvcombBinClass GstDvcombBinClass;

struct _GstDvcombGroup
{
  GstDvcombBin *bin;  /* dvcombbin */
  GstElement *queue;
  GstPad *sink_pad, *src_pad;
  gboolean seeking;
  gulong probe_id;
};

struct _GstDvcombBin
{
  GstBin parent;

  /* FIXME: RecMutex ?? */
  GRecMutex lock;
  GMutex seek_lock;

  gboolean seeking;
  gulong probe_id;

  guint32 seqnum_last_seek;
  GstElement *combiner;
  GList *group_list;
  GList *queue_list;
};

struct _GstDvcombBinClass
{
  GstBinClass parent_class;
};

GType gst_dvcomb_bin_get_type (void);

G_END_DECLS
#endif /* __GST_DVCOMB_BIN_H__ */
