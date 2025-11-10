/* GStreamer CENC DRM Marlin element
 * Copyright (C) 2016-2019 LG Electronics, Inc.
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

/**
 * SECTION:element-gstcencdrmmarlin
 * Decrypts media that has been encrypted / protected using Marlin DRM.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>

#include <gst/gst.h>
#include <gst/gstelement.h>
#include <gst/base/gstbytereader.h>
#include <gst/base/gstbytewriter.h>
#include <gst/basedrm/gstbasedrm.h>
#include <gmodule.h>
#include <glib.h>

#include "gstcencdrmmarlin.h"

#define GST_CAT_DEFAULT gst_cencdrm_marlin_debug_category
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/* UUID is System ID of Marlin DRM system */
#define MARLINDRM_UUID "5e629af5-38da-4063-8977-97ffbd9902d4"

/* prototypes */
static gboolean gst_cencdrm_marlin_drm_info (GstBaseDrm * basedrm,
    GstDRMSystemInfo * drm_system_info);
static gboolean gst_cencdrm_marlin_start (GstBaseDrm * basedrm);
static gboolean gst_cencdrm_marlin_is_playback_allowed (GstBaseDrm * basedrm);
static gboolean gst_cencdrm_marlin_drm_init (GstBaseDrm * basedrm,
    guint8 * header, guint size);
static gboolean gst_cencdrm_marlin_get_license_challenge (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info);
static gboolean gst_cencdrm_marlin_request_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info);
static gboolean gst_cencdrm_marlin_store_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info);
static gboolean gst_cencdrm_marlin_prepare_decrypt (GstBaseDrm * basedrm);
static gboolean gst_cencdrm_marlin_set_kid (GstBaseDrm * basedrm,
    guint8 * kid_data, gsize kid_size);
static gboolean gst_cencdrm_marlin_set_iv (GstBaseDrm * basedrm,
    guint8 * iv_data, gsize iv_size);
static gboolean gst_cencdrm_marlin_decrypt (GstBaseDrm * basedrm,
    GstDecryptInfo * decryptInfo);
static gboolean gst_cencdrm_marlin_stop (GstBaseDrm * basedrm);
static GstBuffer *gst_cencdrm_marlin_get_key_info (GstBaseDrm * basedrm);

const char cencdrm_marlin_xml_node_name[] = "MarlinContentId";

static GModule *module;
static void *drm_ctrl_handle;

static struct _DRMFunc
{
  void *(*drm_load) (const char *drm_type, const char *drm_client_id,
      const char *uri);
  int (*drm_decrypt) (void *ctrl_handle, unsigned char *buffer,
      unsigned int size, char *opt_data, ...);
  int (*drm_resolve_uri) (void *ctrl_handle, char *uri);
  int (*drm_release) (void *ctrl_handle);
  int (*drm_enabler) (void *ctrl_handle, void **decrypt_handle);
} drm_func;

/* pad templates */
static GstStaticPadTemplate
    gst_cencdrm_marlin_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-cenc, original-media-type=(string)"
        "{ video/x-h264, video/x-h265, " SINK_DECODE_AUDIO_CAPS "}, "
        "protection-system=(string) " MARLINDRM_UUID)
    );

static GstStaticPadTemplate gst_cencdrm_marlin_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("video/x-h264, secure_area = (string) svp, parsed = (boolean) true, drm-type = (string) marlin; "
        "video/x-h265, secure_area = (string) svp, parsed = (boolean) true, drm-type = (string) marlin; "
        SRC_DECODE_AUDIO_CAPS)
    );

/* class initialization */
#define gst_cencdrm_marlin_parent_class parent_class
G_DEFINE_TYPE (GstCencDrmMarlin, gst_cencdrm_marlin, GST_TYPE_BASEDRM);

static void
gst_cencdrm_marlin_class_init (GstCencDrmMarlinClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseDrmClass *basedrm_class = GST_BASEDRM_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_cencdrm_marlin_debug_category,
      "cencdrmmarlin", 0, "Marlin DRM for CENC");

  GST_DEBUG ("cencdrmmarlin class init");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_cencdrm_marlin_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_cencdrm_marlin_src_template));

  gst_element_class_set_static_metadata (element_class,
      "Marlin DRM for CENC",
      GST_ELEMENT_FACTORY_KLASS_DECRYPTOR,
      "Decrypts Marlin DRM protected media in ISOBMFF CENC format",
      "Chihyoung Kim <chihyoung2.kim@lge.com>");

  basedrm_class->get_drm_info = GST_DEBUG_FUNCPTR (gst_cencdrm_marlin_drm_info);
  basedrm_class->start = GST_DEBUG_FUNCPTR (gst_cencdrm_marlin_start);
  basedrm_class->stop = GST_DEBUG_FUNCPTR (gst_cencdrm_marlin_stop);
  basedrm_class->drm_init = GST_DEBUG_FUNCPTR (gst_cencdrm_marlin_drm_init);
  basedrm_class->get_license_challenge =
      GST_DEBUG_FUNCPTR (gst_cencdrm_marlin_get_license_challenge);
  basedrm_class->request_license =
      GST_DEBUG_FUNCPTR (gst_cencdrm_marlin_request_license);
  basedrm_class->store_license =
      GST_DEBUG_FUNCPTR (gst_cencdrm_marlin_store_license);
  basedrm_class->is_playback_allowed =
      GST_DEBUG_FUNCPTR (gst_cencdrm_marlin_is_playback_allowed);
  basedrm_class->prepare_decrypt =
      GST_DEBUG_FUNCPTR (gst_cencdrm_marlin_prepare_decrypt);
  basedrm_class->set_kid = GST_DEBUG_FUNCPTR (gst_cencdrm_marlin_set_kid);
  basedrm_class->set_iv = GST_DEBUG_FUNCPTR (gst_cencdrm_marlin_set_iv);
  basedrm_class->decrypt = GST_DEBUG_FUNCPTR (gst_cencdrm_marlin_decrypt);
  basedrm_class->get_key_info =
      GST_DEBUG_FUNCPTR (gst_cencdrm_marlin_get_key_info);
}

static void
gst_cencdrm_marlin_init (GstCencDrmMarlin * self)
{
  GST_DEBUG_OBJECT (self, "Marlin DRM for CENC initialization");

  self->ms3_url = NULL;
  self->ms3_url_length = 0;
  self->type = MEDIA_TYPE_MAX;
  self->license_state = CENCDRM_MAX_LICENSE;
  self->iv_bytes = NULL;
  self->kid_bytes = NULL;
}

static gboolean
gst_cencdrm_marlin_drm_info (GstBaseDrm * basedrm,
    GstDRMSystemInfo * drm_system_info)
{
  GstCencDrmMarlin *self = GST_CENCDRM_MARLIN (basedrm);

  GST_DEBUG_OBJECT (self, "Marlin DRM for CENC specific static info");

  if (!drm_system_info) {
    GST_ERROR_OBJECT (self, "NULL pointer drm_system_info");
    return FALSE;
  }

  drm_system_info->system_ids =
      g_list_append (drm_system_info->system_ids, g_strdup (MARLINDRM_UUID));
  drm_system_info->xml_node_name =
      g_memdup (&cencdrm_marlin_xml_node_name[0],
      sizeof (cencdrm_marlin_xml_node_name));

  if (!drm_system_info->system_ids || !drm_system_info->xml_node_name) {
    GST_ERROR_OBJECT (self,
        "Either system_ids %p, xml_node_name %p is a NULL pointer",
        drm_system_info->system_ids, drm_system_info->xml_node_name);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_cencdrm_marlin_start (GstBaseDrm * basedrm)
{
  GstCencDrmMarlin *self = GST_CENCDRM_MARLIN (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;

  drm_system_info->is_svp = TRUE;
  module = g_module_open ("/usr/lib/libdrmcontroller.so.1", G_MODULE_BIND_LAZY);
  if (!module) {
    GST_ERROR_OBJECT (self, "Failed to open a module: %s", g_module_error ());
    return FALSE;
  }

  if (!g_module_symbol (module, "API_DRM_Load",
          (gpointer *) & drm_func.drm_load)) {
    GST_ERROR_OBJECT (self, "Failed to get a symbol: %s", g_module_error ());
    goto Error;
  }

  if (!g_module_symbol (module, "API_DRM_ResolveUri",
          (gpointer *) & drm_func.drm_resolve_uri)) {
    GST_ERROR_OBJECT (self, "Failed to get a symbol: %s", g_module_error ());
    goto Error;
  }

  if (!g_module_symbol (module, "API_DRM_Decrypt",
          (gpointer *) & drm_func.drm_decrypt)) {
    GST_ERROR_OBJECT (self, "Failed to get a symbol: %s", g_module_error ());
    goto Error;
  }

  if (!g_module_symbol (module, "API_DRM_Release",
          (gpointer *) & drm_func.drm_release)) {
    GST_ERROR_OBJECT (self, "Failed to get a symbol: %s", g_module_error ());
    goto Error;
  }

  if (!g_module_symbol (module, "API_DRM_GetDecryptHandle",
          (gpointer *) & drm_func.drm_enabler)) {
    GST_ERROR_OBJECT (self, "Failed to get a symbol: %s", g_module_error ());
    goto Error;
  }

  if (!drm_ctrl_handle) {
    drm_ctrl_handle = drm_func.drm_load ("marlin", NULL, NULL);
    if (!drm_ctrl_handle) {
      GST_ERROR_OBJECT (self, "Failed to create drm controller handler");
      goto Error;
    }
  }

  return TRUE;

Error:
  g_module_close (module);

  return FALSE;
}

static gboolean
gst_cencdrm_marlin_stop (GstBaseDrm * basedrm)
{
  GstCencDrmMarlin *self = GST_CENCDRM_MARLIN (basedrm);

  GST_DEBUG_OBJECT (self, "Marlin DRM for CENC close");

  if (drm_ctrl_handle) {
    drm_func.drm_release (drm_ctrl_handle);
    drm_ctrl_handle = NULL;
  }

  g_module_close (module);

#if 0
  if (self->ms3_url) {
    g_free (self->ms3_url);
    self->ms3_url = NULL;
  }
#endif

  if (self->iv_bytes) {
    g_bytes_unref (self->iv_bytes);
    self->iv_bytes = NULL;
  }

  if (self->kid_bytes) {
    g_bytes_unref (self->kid_bytes);
    self->kid_bytes = NULL;
  }

  self->license_state = CENCDRM_MAX_LICENSE;

  return TRUE;
}

static gboolean
gst_cencdrm_marlin_prepare_decrypt (GstBaseDrm * basedrm)
{
  GstCencDrmMarlin *self = GST_CENCDRM_MARLIN (basedrm);
  GstCaps *src_caps;
  const gchar *name;

  GST_DEBUG_OBJECT (self, "Marlin DRM for CENC prepare decrypt context");

  src_caps =
      gst_pad_get_current_caps (GST_BASE_TRANSFORM_SRC_PAD (&self->parent));
  if (!src_caps) {
    GST_ERROR_OBJECT (self, "Source caps unknown");
    return FALSE;
  }
  name = gst_structure_get_name (gst_caps_get_structure ((src_caps), 0));
  gst_caps_unref (src_caps);
  GST_DEBUG_OBJECT (self, "Src pad structure name %s", name);

  if (strncmp (name, "video/", 6) == 0) {
    self->type = MEDIA_TYPE_VIDEO;
  } else if (strncmp (name, "audio/", 6) == 0) {
    self->type = MEDIA_TYPE_AUDIO;
  } else {
    GST_ERROR_OBJECT (self, "Unsupported media type");
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Media type %s",
      (self->type == MEDIA_TYPE_VIDEO) ? "VIDEO" : "AUDIO");

  return TRUE;
}

static gboolean
gst_cencdrm_marlin_set_kid (GstBaseDrm * basedrm, guint8 * kid_data,
    gsize kid_size)
{
  GstCencDrmMarlin *self = GST_CENCDRM_MARLIN (basedrm);

  GST_DEBUG_OBJECT (self, "Marlin DRM for CENC set kid");

  if (self->kid_bytes)
    g_bytes_unref (self->kid_bytes);
  self->kid_bytes = g_bytes_new (kid_data, kid_size);

  return TRUE;
}

static gboolean
gst_cencdrm_marlin_set_iv (GstBaseDrm * basedrm, guint8 * iv_data,
    gsize iv_size)
{
  GstCencDrmMarlin *self = GST_CENCDRM_MARLIN (basedrm);

  GST_DEBUG_OBJECT (self, "Marlin DRM for CENC set iv");

  if (self->iv_bytes)
    g_bytes_unref (self->iv_bytes);
  self->iv_bytes = g_bytes_new (iv_data, iv_size);

  return TRUE;
}

static gboolean
gst_cencdrm_marlin_decrypt (GstBaseDrm * basedrm, GstDecryptInfo * decrypt_info)
{
  GstCencDrmMarlin *self = GST_CENCDRM_MARLIN (basedrm);
  guint8 *iv_data = NULL;
  guint8 *kid_data = NULL;
  gsize iv_size, kid_size;
  guint64 iv = 0;
  guint32 i;
  guint32 subsample_count = decrypt_info->subsample_count;
  guint32 *subsample_info = decrypt_info->subsample_info;
  guint8 *src = decrypt_info->data;

  GST_DEBUG_OBJECT (self, "decrypt");

  iv_data = (guint8 *) g_bytes_get_data (self->iv_bytes, &iv_size);
  kid_data = (guint8 *) g_bytes_get_data (self->kid_bytes, &kid_size);

  if (!iv_data || !kid_data)
    return FALSE;

  for (i = 0; i < subsample_count * 2; i += 2) {
    guint32 n_bytes_clear = subsample_info[i];
    guint32 n_bytes_encrypted = subsample_info[i + 1];

    src += n_bytes_clear;

    if (n_bytes_encrypted) {
      if (!drm_func.drm_decrypt (drm_ctrl_handle, src,
              n_bytes_encrypted, "CENC", iv_data, kid_data)) {
        GST_ERROR_OBJECT (self, "Failed to decrypt");
        return FALSE;
      }

      iv = GST_READ_UINT64_BE (iv_data + 8);
      iv += (n_bytes_encrypted >> AES128_BLOCKSIZE_RADIX2);
      GST_WRITE_UINT64_BE (iv_data + 8, iv);

      src += n_bytes_encrypted;
    }
  }
  return TRUE;
}

static gboolean
gst_cencdrm_marlin_is_playback_allowed (GstBaseDrm * basedrm)
{
  GstCencDrmMarlin *self = GST_CENCDRM_MARLIN (basedrm);

  GST_DEBUG_OBJECT (self, "is_playback_allowed");

  return ((self->license_state == CENCDRM_LICENSE_GRANTED) ? TRUE : FALSE);
}

static gboolean
gst_cencdrm_marlin_drm_init (GstBaseDrm * basedrm, guint8 * header, guint size)
{
  GstCencDrmMarlin *self = GST_CENCDRM_MARLIN (basedrm);

  GST_DEBUG_OBJECT (self, "Marlin DRM for CENC initialization");

  return TRUE;
}

static gboolean
gst_cencdrm_marlin_get_license_challenge (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info)
{
  GstCencDrmMarlin *self = GST_CENCDRM_MARLIN (basedrm);

  GST_DEBUG_OBJECT (self, "get_license_challenge");

  return TRUE;
}

static gboolean
gst_cencdrm_marlin_request_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info)
{
  GstCencDrmMarlin *self = GST_CENCDRM_MARLIN (basedrm);

  GST_DEBUG_OBJECT (self, "request license");
#if 0
  if (!drm_func.drm_resolve_uri (drm_ctrl_handle, self->ms3_url)) {
    GST_ERROR_OBJECT (self, "Failed to request license");
    self->license_state = CENCDRM_LICENSE_REFUSED;
    return FALSE;
  } else {
    self->license_state = CENCDRM_LICENSE_GRANTED;
  }
#endif

  self->license_state = CENCDRM_LICENSE_GRANTED;

  return TRUE;
}

static gboolean
gst_cencdrm_marlin_store_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info)
{
  GstCencDrmMarlin *self = GST_CENCDRM_MARLIN (basedrm);

  GST_DEBUG_OBJECT (self, "store license");

  return TRUE;
}

static GstBuffer *
gst_cencdrm_marlin_get_key_info (GstBaseDrm * basedrm)
{
  GstCencDrmMarlin *self = GST_CENCDRM_MARLIN (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  GstBuffer *key_info = NULL;
  GstByteWriter bw;
  void *playback_enabler = NULL;
  guint8 *kid = NULL;
  guint kid_size;

  if (!drm_system_info->is_svp)
    return NULL;

  if (!drm_func.drm_enabler (drm_ctrl_handle, &playback_enabler)) {
    GST_ERROR_OBJECT (self, "failed to get playback_enabler");
    return NULL;
  }

  gst_byte_writer_init (&bw);

  kid = g_bytes_get_data (self->kid_bytes, &kid_size);

  if (!gst_byte_writer_put_data (&bw, kid, kid_size)) {
    GST_ERROR_OBJECT (self, "failed to put kid");
    return NULL;
  }

  if (!gst_byte_writer_put_uint32_le (&bw, (guint32) playback_enabler)) {
    GST_ERROR_OBJECT (self, "failed to put playback_enabler");
    return NULL;
  }
  key_info = gst_byte_writer_reset_and_get_buffer (&bw);

  return key_info;
}
