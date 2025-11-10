/* GStreamer Dolby Stream Combiner
 * Copyright (C) 2015 LG Electronics, Inc.
 * Author : Seungha Yang <sh.yang@lge.com>
 *          Kyungyong Kim <kyungyong.kim@lge.com>
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

#ifndef __GST_DOLBY_STREAMCOMBINER_H__
#define __GST_DOLBY_STREAMCOMBINER_H__

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>
#include <gst/base/gstadapter.h>

#define GST_TYPE_DOLBY_STREAM_COMBINER               (gst_dolby_stream_combiner_get_type())
#define GST_DOLBY_STREAM_COMBINER(obj)               (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DOLBY_STREAM_COMBINER,GstDolbyStreamCombiner))
#define GST_DOLBY_STREAM_COMBINER_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DOLBY_STREAM_COMBINER,GstDolbyStreamCombinerClass))
#define GST_IS_DOLBY_STREAM_COMBINER(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DOLBY_STREAM_COMBINER))
#define GST_IS_DOLBY_STREAM_COMBINER_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DOLBY_STREAM_COMBINER))

typedef struct _GstDolbyStreamCombiner GstDolbyStreamCombiner;
typedef struct _GstDolbyStreamCombinerClass GstDolbyStreamCombinerClass;
typedef enum _GstDolbyStreamCombinerState GstDolbyStreamCombinerState;
typedef struct _GstDolbyStreamCombinerPad GstDolbyStreamCombinerPad;
typedef enum _DolbyStreamType DolbyStreamType;
typedef enum _DolbyStreamCodec DolbyStreamCodec;
typedef struct _GstDolbyStreamCombinerAdapter GstDolbyStreamCombinerAdapter;

enum _GstDolbyStreamCombinerState
{
  GST_DOLBY_SC_STATE_NONE,
  GST_DOLBY_SC_STATE_WAIT_BL,
  GST_DOLBY_SC_STATE_WAIT_EL,
  GST_DOLBY_SC_STATE_EOS
};

enum _DolbyStreamType
{
  DOLBY_STREAM_TYPE_NONE,
  DOLBY_STREAM_TYPE_BASE,
  DOLBY_STREAM_TYPE_ENHANCE
};

enum _DolbyStreamCodec
{
  DOLBY_STREAM_NONE,
  DOLBY_STREAM_H264,
  DOLBY_STREAM_HEVC
};

struct _GstDolbyStreamCombinerPad
{
  GstCollectData collect;

  DolbyStreamType type;
  DolbyStreamCodec codec;

  gulong blocked_id;
};

struct _GstDolbyStreamCombinerAdapter
{
  GstAdapter *adapter;

  GstClockTime pts_bl;
  GstClockTime dts_bl;
  GstClockTime duration_bl;

  GstClockTime pts_el;
  GstClockTime dts_el;
  GstClockTime duration_el;

  gboolean need_flag_discont;
  gboolean need_flag_delta_unit;
};

struct _GstDolbyStreamCombiner
{
  GstElement parent;

  GstPad *srcpad;
  GstCollectPads *collect;

  GstDolbyStreamCombinerState state;

  GstDolbyStreamCombinerAdapter *payload;

  guint8 n_streams;

  gboolean waiting_flush_stop;  /* Used when we receive a flush_start to make
                                   sure to forward the flush_stop only once */
  GMutex lock;

  GCond cond;                   /* Used when the number of requested pad is not matched to that of needed pad */

  gboolean has_pending_segment;
  GstTagList *pending_tags;
  GstSegment *seg_last;
};

struct _GstDolbyStreamCombinerClass
{
  GstElementClass parent;
};

GType gst_dolby_stream_combiner_get_type (void);

GstElement *gst_dolby_stream_combiner_new (gchar * name);

#endif /* __GST_DOLBY_STREAMCOMBINER_H__ */
