/* GStreamer CENC DRM WideVine element
 * Copyright (C) 2019-2022 LG Electronics, Inc.
 *
 * Authors:
 *   Chihyoung Kim <chihyoung2.kim@lge.com>
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

#ifndef  __GST_CENCDRM_WIDEVINE_H__
#define  __GST_CENCDRM_WIDEVINE_H__

#include <stdbool.h>
#include <gst/base/gstbasetransform.h>
#include <gmodule.h>
#include <wv_cdm_wrapper.h>

G_BEGIN_DECLS

#define GST_TYPE_CENCDRM_WIDEVINE \
  (gst_cencdrm_widevine_get_type())
#define GST_CENCDRM_WIDEVINE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_CENCDRM_WIDEVINE, GstCencDrmWidevine))
#define GST_CENCDRM_WIDEVINE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_CENCDRM_WIDEVINE, GstCencDrmWidevineClass))
#define GST_CENCDRM_WIDEVINE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_CENCDRM_WIDEVINE, GstCencDrmWidevineClass))
#define GST_IS_CENCDRM_WIDEVINE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_CENCDRM_WIDEVINE))
#define GST_IS_CENCDRM_WIDEVINE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_CENCDRM_WIDEVINE))
#define GST_CENCDRM_WIDEVINE_CAST(obj) \
  ((GstCencDrmWidevine *) (obj))

typedef struct _GstCencDrmWidevineNotifyInfo GstCencDrmWidevineNotifyInfo;
typedef struct _GstCencDrmWidevine GstCencDrmWidevine;
typedef struct _GstCencDrmWidevineClass GstCencDrmWidevineClass;

enum GstWidevineLicense
{
  WIDEVINE_LICENSE_GRANTED = 0,
  WIDEVINE_LICENSE_REFUSED = 1,
  WIDEVINE_LICENSE_DEFERRED = 2,
  WIDEVINE_MAX_LICENSE = 3
};

enum GstLicenseType
{
  LICENSE_OTA = 0,
  LICENSE_OTT_RMP = 1,
  LICENSE_OTT_APP = 2
};

/**
 * _GstCencDrmWidevineNotifyInfo: Info node for a/344
 */
struct _GstCencDrmWidevineNotifyInfo
{
  gchar *system_id;
  gchar *service;
  gchar *drm_msg_type;
  guint8 *kid;
  guint32 kid_length;
  guint8 *session_id;
  guint32 session_id_length;
  guint8 *drm_data;
  guint32 drm_data_length;
  gint32 request_id;
};

/**
 * _GstCencDrmWidevine: CENC DRM for Widevine.
 * @basedrm: the parent #GstBaseDrm.
 */
struct _GstCencDrmWidevine
{
  GstBaseDrm basedrm;

  /* CencDrmWidevine specific */
  GBytes *iv_bytes;             /* initialization vector */
  GBytes *kid_bytes;            /* key id of the content */
  gchar session_id[256];        /* session id */
  guint32 session_id_length;    /* session id length */
  enum GstWidevineLicense license_state;        /* license state */
  gboolean network_connectivity;        /* network connectivity state */
  gchar *global_service_id;     /* global service id */
  gchar *system_id;
  gchar *generated_request_message;     /* generated request message for license requesting */
  guint32 generated_message_length;     /* generated request message length */
  GstCencDrmWidevineNotifyInfo *notify_info;    /* for saving a/344 notify api structure */
  GMutex notify_transaction_lock;       /* a/344 notify transaction lock */
  GMutex notify_info_lock;      /* a/344 notify info lock */
  GCond notify_info_cond;       /* a/344 notify info cond wait */
  gboolean is_response;         /* a/344 notify response */
  guint8 retry_count;           /* a/344 notify response check count */
  gchar group_license_id[256];  /* group license id for OTA */
  guint32 group_license_id_length;      /* group license id length for OTA */
  gchar *content_id;
  guint8 content_id_size;
  gchar *license_url;
  guint32 license_url_size;
  gboolean has_usable_key;      /* if there is a usable key or not */
  bool is_exist_license;        /*if there is persistent license or not */
  guint8 *init_data;
  guint init_data_size;
  gboolean done_ready_license;
  gboolean is_svp;
  gboolean seamless_mode;
  gboolean seamless_app;
  enum GstLicenseType license_type;
  CDM_WRAPPER_CLASS *cdm_wrapper_ota;
  CDM_WRAPPER_CLASS *cdm_wrapper_ott;
  GThread *thread;
};

struct _GstCencDrmWidevineClass
{
  GstBaseDrmClass parent_class;
};

GType gst_cencdrm_widevine_get_type (void);

G_END_DECLS
#endif /* __GST_CENCDRM_WIDEVINE_H__ */
