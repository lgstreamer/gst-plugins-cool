// --------------------------------------------------------------------------
//  LG ELECTRONICS INC., SEOUL, KOREA
//  Copyright(c) 2013 by LG Electronics Inc.
//
//  All rights reserved. No part of this work may be reproduced, stored in a
//  retrieval system, or transmitted by any means without prior written
//  permission of LG Electronics Inc.
// --------------------------------------------------------------------------

#ifndef __GST_MPOMUX_H__
#define __GST_MPOMUX_H__

#include <gst/gst.h>

G_BEGIN_DECLS
/* #defines don't like whitespacey bits */
#define GST_TYPE_MPOMUX \
  (gst_mpomux_get_type())
#define GST_MPOMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPOMUX,GstMPOMux))
#define GST_MPOMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPOMUX,GstMPOMuxClass))
#define GST_IS_MPOMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPOMUX))
#define GST_IS_MPOMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPOMUX))
typedef struct _GstMPOMux GstMPOMux;
typedef struct _GstMPOMuxClass GstMPOMuxClass;

typedef struct _GstMPOMux_videopad GstMPOMux_videopad;
struct _GstMPOMux_videopad
{
  gint index;

  GstPad *sinkpad, *srcpad;
  gboolean has_mpotag;
  gboolean configured;

  gint MPIndividualNum;
  gint BaseViewpointNum;
  gint ConvergenceAngle_num, ConvergenceAngle_den;
  gint BaselineLength_num, BaselineLength_den;
};

struct _GstMPOMux
{
  GstElement element;

  GList *videopads;

  GstBuffer *buf;

  gint sinkpad_number;
  gboolean mux_mpo;

  gboolean silent;
  gboolean force_2d;

  gboolean have_group_id;
  guint group_id;
  GstCaps *defaultCaps;
};

struct _GstMPOMuxClass
{
  GstElementClass parent_class;
};

GType gst_mpomux_get_type (void);

G_END_DECLS
#endif /* __GST_MPOMUX_H__ */
// vim:set sw=2 et:
