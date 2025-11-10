/* GStreamer Plugins Cool
 * Copyright (C) 2014 LG Electronics, Inc.
 *	Author : HoonHee Lee <hoonhee.lee@lge.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_DEC_BRIDGE_H__
#define __GST_DEC_BRIDGE_H__

#include <gst/gst.h>
#include <gst/cool/gstcool.h>

G_BEGIN_DECLS
#define GST_TYPE_DEC_BRIDGE \
  (gst_dec_bridge_get_type())
#define GST_DEC_BRIDGE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DEC_BRIDGE,GstDecBridge))
#define GST_DEC_BRIDGE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DEC_BRIDGE,GstDecBridgeClass))
#define GST_IS_DEC_BRIDGE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DEC_BRIDGE))
#define GST_IS_DEC_BRIDGE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DEC_BRIDGE))
typedef struct _GstDecBridge GstDecBridge;
typedef struct _GstDecBridgeClass GstDecBridgeClass;

struct _GstDecBridge
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  GMutex lock;
  gchar *active_stream_id;
  gboolean stream_change;
  GstCaps *curcaps;
};

struct _GstDecBridgeClass
{
  GstElementClass parent_class;
};

GType gst_dec_bridge_get_type (void);

G_END_DECLS
#endif /* __GST_DEC_BRIDGE_H__ */
