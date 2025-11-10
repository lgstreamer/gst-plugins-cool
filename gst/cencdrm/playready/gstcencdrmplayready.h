/* GStreamer CENC DRM PlayReady element
 * Copyright (C) 2016-2019 LG Electronics, Inc.
 *
 * Authors:
 *   Chihyoung Kim <chihyoung2.kim@lge.com>
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

#ifndef  __GST_CENCDRM_PLAYREADY_H__
#define  __GST_CENCDRM_PLAYREADY_H__

#include <gst/base/gstbasetransform.h>
#include <gmodule.h>

G_BEGIN_DECLS
#define GST_TYPE_CENCDRM_PLAYREADY \
  (gst_cencdrm_playready_get_type())
#define GST_CENCDRM_PLAYREADY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_CENCDRM_PLAYREADY, GstCencDrmPlayready))
#define GST_CENCDRM_PLAYREADY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_CENCDRM_PLAYREADY, GstCencDrmPlayreadyClass))
#define GST_CENCDRM_PLAYREADY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_CENCDRM_PLAYREADY, GstCencDrmPlayreadyClass))
#define GST_IS_CENCDRM_PLAYREADY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_CENCDRM_PLAYREADY))
#define GST_IS_CENCDRM_PLAYREADY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_CENCDRM_PLAYREADY))
#define GST_CENCDRM_PLAYREADY_CAST(obj) \
  ((GstCencDrmPlayready *) (obj))
typedef struct _GstCencDrmPlayready GstCencDrmPlayready;
typedef struct _GstCencDrmPlayreadyClass GstCencDrmPlayreadyClass;

enum GstPlayreadyLicense
{
  PLAYREADY_LICENSE_GRANTED = 0,
  PLAYREADY_LICENSE_REFUSED = 1,
  PLAYREADY_MAX_LICENSE = 2
};

/**
 * _GstCencDrmPlayready: CENC DRM for PlayReady.
 * @basedrm: the parent #GstBaseDrm.
 */
struct _GstCencDrmPlayready
{
  GstBaseDrm basedrm;

  /* CencDrmPlayready specific */
  GModule *module_drmcontroller;
  void *app_context;
  void *opaque_buffer;
  void *revocation_buffer;
  void *decrypt_context;
  GBytes *iv_bytes;             /* initialization vector */
  GBytes *kid_bytes;            /* key id of the content */
  GBytes *kid_history;
  GArray *hds_path;
  enum GstPlayreadyLicense license;     /* license state */
};

struct _GstCencDrmPlayreadyClass
{
  GstBaseDrmClass parent_class;
};

GType gst_cencdrm_playready_get_type (void);

G_END_DECLS
#endif /* __GST_CENCDRM_PLAYREADY_H__ */
