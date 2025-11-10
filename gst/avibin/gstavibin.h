// --------------------------------------------------------------------------
//  LG ELECTRONICS INC., SEOUL, KOREA
//  Copyright(c) 2013 by LG Electronics Inc.
//
//  All rights reserved. No part of this work may be reproduced, stored in a
//  retrieval system, or transmitted by any means without prior written
//  permission of LG Electronics Inc.
// --------------------------------------------------------------------------

#ifndef __GST_AVIBIN_H__
#define __GST_AVIBIN_H__

#include <gst/gst.h>

G_BEGIN_DECLS
/* #defines don't like whitespacey bits */
#define GST_TYPE_AVIBIN \
  (gst_avibin_get_type())
#define GST_AVIBIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AVIBIN,GstAviBin))
#define GST_AVIBIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AVIBIN,GstAviBinClass))
#define GST_IS_AVIBIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AVIBIN))
#define GST_IS_AVIBIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AVIBIN))
typedef struct _GstAviBin GstAviBin;
typedef struct _GstAviBinClass GstAviBinClass;

struct _GstAviBin
{
  GstBin bin;

  GstPad *sinkpad;

  GList *video_demux_pads;
  GList *audio_demux_pads;

  GList *video_pads;
  GList *audio_pads;
  GList *subtitle_pads;

  GstElement *demux, *mpomux;

  gboolean fujifilm_3d;
  gboolean silent;
  gboolean thumbnail_mode;
};

struct _GstAviBinClass
{
  GstBinClass parent_class;
};

GType gst_avibin_get_type (void);

G_END_DECLS
#endif /* __GST_AVIBIN_H__ */
