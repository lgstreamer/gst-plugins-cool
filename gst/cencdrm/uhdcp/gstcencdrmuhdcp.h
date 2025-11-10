/* GStreamer CENC DRM UHDCP element
 * Copyright (C) 2016 LG Electronics, Inc.
 *
 * Authors:
 *   Yujin Lee <yujin.lee@lge.com>
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

#ifndef  __GST_CENCDRM_UHDCP_H__
#define  __GST_CENCDRM_UHDCP_H__

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_CENCDRM_UHDCP \
  (gst_cencdrm_uhdcp_get_type())
#define GST_CENCDRM_UHDCP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_CENCDRM_UHDCP, GstCencDrmUhdcp))
#define GST_CENCDRM_UHDCP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_CENCDRM_UHDCP, GstCencDrmUhdcpClass))
#define GST_CENCDRM_UHDCP_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_CENCDRM_UHDCP, GstCencDrmUhdcpClass))
#define GST_IS_CENCDRM_UHDCP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_CENCDRM_UHDCP))
#define GST_IS_CENCDRM_UHDCP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_CENCDRM_UHDCP))
#define GST_CENCDRM_UHDCP_CAST(obj) \
  ((GstCencDrmUhdcp *) (obj))

typedef struct _GstCencDrmUhdcp GstCencDrmUhdcp;
typedef struct _GstCencDrmUhdcpClass GstCencDrmUhdcpClass;

enum GstUhdcpLicense
{
  UHDCP_LICENSE_GRANTED = 0,
  UHDCP_LICENSE_REFUSED = 1,
  UHDCP_MAX_LICENSE = 2
};

/**
 * _GstCencDrmUhdcp: CENC DRM for UHDCP.
 * @basedrm: the parent #GstBaseDrm.
 */
struct _GstCencDrmUhdcp
{
  GstBaseDrm basedrm;

  /* CencDrmUhdcp specific */
  GBytes *iv_bytes;                  /* initialization vector */
  GBytes *kid_bytes;                 /* key id of the content */
  gboolean init_done;
  enum GstUhdcpLicense license;      /* license state */
};

struct _GstCencDrmUhdcpClass
{
  GstBaseDrmClass parent_class;
};

GType gst_cencdrm_uhdcp_get_type (void);

G_END_DECLS
#endif /* __GST_CENCDRM_UHDCP_H__ */
