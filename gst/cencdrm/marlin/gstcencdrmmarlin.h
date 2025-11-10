/* GStreamer CENC DRM Marlin element
 * Copyright (C) 2016 LG Electronics, Inc.
 *  Author : Chihyoung Kim <chihyoung2.kim@lge.com>
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

#ifndef  __GST_CENCDRM_MARLIN_H__
#define  __GST_CENCDRM_MARLIN_H__

#include <gst/base/gstbasetransform.h>

/* Begin Declaration */
G_BEGIN_DECLS
#define GST_TYPE_CENCDRM_MARLIN           (gst_cencdrm_marlin_get_type())
#define GST_CENCDRM_MARLIN(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CENCDRM_MARLIN,GstCencDrmMarlin))
#define GST_CENCDRM_MARLIN_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CENCDRM_MARLIN,GstCencDrmMarlinClass))
#define GST_CENCDRM_MARLIN_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_CENCDRM_MARLIN, GstCencDrmMarlinClass))
#define GST_IS_CENCDRM_MARLIN(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CENCDRM_MARLIN))
#define GST_IS_CENCDRM_MARLIN_CLASS(klass)(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CENCDRM_MARLIN))
#define GST_CENCDRM_MARLIN_CAST(obj)      ((GstCencDrmMarlin *) (obj))
typedef struct _GstCencDrmMarlin GstCencDrmMarlin;
typedef struct _GstCencDrmMarlinClass GstCencDrmMarlinClass;

enum GstCencMediaType
{
  MEDIA_TYPE_VIDEO = 0,
  MEDIA_TYPE_AUDIO = 1,
  MEDIA_TYPE_MAX = 2
};

enum GstCencDrmLicense
{
  CENCDRM_LICENSE_GRANTED = 0,
  CENCDRM_LICENSE_REFUSED = 1,
  CENCDRM_MAX_LICENSE = 2
};

/**
 * _GstCencDrmMarlin:  CENC DRM for Marlin
 * @parent: Element parent
 */
struct _GstCencDrmMarlin
{
  GstBaseDrm parent;

  /* CencDrmMarlin specific */
  gchar *ms3_url;                        /* URL for the license challenge */
  guint8 ms3_url_length;                 /* length of the license challenge URL */
  enum GstCencMediaType type;            /* type of the encrypted data */
  enum GstCencDrmLicense license_state;  /* license state */
  GBytes *iv_bytes;                      /* initialization vector */
  GBytes *kid_bytes;                     /* key id of the content */
};

struct _GstCencDrmMarlinClass
{
  GstBaseDrmClass parent_class;
};

GType gst_cencdrm_marlin_get_type (void);

G_END_DECLS
#endif /* __GST_CENCDRM_MARLIN_H__ */
