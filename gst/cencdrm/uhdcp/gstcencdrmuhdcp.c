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

/**
 * SECTION:element-cencdrmuhdcp
 * Decrypts media that has been encrypted / protected using UHDCP
 * (UHD Content Protection) scheme.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include <gst/gst.h>
#include <gst/gstelement.h>
#include <gst/base/gstbytereader.h>
#include <gst/basedrm/gstbasedrm.h>
#include <glib.h>
#include <gmodule.h>

#include "gstcencdrmuhdcp.h"

#define GST_CAT_DEFAULT gst_cencdrm_uhdcp_debug_category
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/* DigiCAP is a kind of UHDCP System */
#define DIGICAP_UUID "dcf4e3e3-62f1-5818-7ba6-0a6fe33ff3dd"

#define LIB_DILE_UHDCP_PATH "/usr/lib/libdile_uhdcp.so.1"

/* prototypes */
static gboolean gst_cencdrm_uhdcp_drm_info (GstBaseDrm * basedrm,
    GstDRMSystemInfo * drm_system_info);
static gboolean gst_cencdrm_uhdcp_start (GstBaseDrm * basedrm);
static gboolean gst_cencdrm_uhdcp_is_playback_allowed (GstBaseDrm * basedrm);
static gboolean gst_cencdrm_uhdcp_drm_init (GstBaseDrm * basedrm,
    guint8 * header, guint size);
static gboolean gst_cencdrm_uhdcp_get_license_challenge (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info);
static gboolean gst_cencdrm_uhdcp_request_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info);
static gboolean gst_cencdrm_uhdcp_store_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info);
static gboolean gst_cencdrm_uhdcp_prepare_decrypt (GstBaseDrm * basedrm);
static gboolean gst_cencdrm_uhdcp_set_kid (GstBaseDrm * basedrm,
    guint8 * kid_data, gsize kid_size);
static gboolean gst_cencdrm_uhdcp_set_iv (GstBaseDrm * basedrm,
    guint8 * iv_data, gsize iv_size);
static gboolean gst_cencdrm_uhdcp_decrypt (GstBaseDrm * basedrm,
    GstDecryptInfo * decryptInfo);
static gboolean gst_cencdrm_uhdcp_stop (GstBaseDrm * basedrm);
gboolean plugin_init (GstPlugin * plugin);

static gint (*DILE_UHDCP_Decrypt) (guchar * pData, guint data_size,
    guchar * pIV, guchar * pKID, gulong block_offset, gushort byte_offset);

const gchar cencdrm_uhdcp_xml_node_name[] = "cenc:pssh";
static GModule *module_dile_uhdcp;

#define CENCDRM_UHDCP_CAPS(uuid) \
  "application/x-cenc, original-media-type=(string) " \
  "{ video/x-h264, video/x-h265, " SINK_DECODE_AUDIO_CAPS "}, " \
  "protection-system=(string) " uuid

/* pad templates */
static GstStaticPadTemplate
    gst_cencdrm_uhdcp_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CENCDRM_UHDCP_CAPS (DIGICAP_UUID))
    );

static GstStaticPadTemplate gst_cencdrm_uhdcp_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264; video/x-h265; " SRC_DECODE_AUDIO_CAPS)
    );

/* class initialization */
#define gst_cencdrm_uhdcp_parent_class parent_class
G_DEFINE_TYPE (GstCencDrmUhdcp, gst_cencdrm_uhdcp, GST_TYPE_BASEDRM);

static void
gst_cencdrm_uhdcp_class_init (GstCencDrmUhdcpClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseDrmClass *basedrm_class = GST_BASEDRM_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_cencdrm_uhdcp_debug_category,
      "cencdrmuhdcp", 0, "UHDCP plugin for ATSC 3.0 CAS support");

  GST_DEBUG ("cencdrmuhdcp class init");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_cencdrm_uhdcp_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_cencdrm_uhdcp_src_template));

  gst_element_class_set_static_metadata (element_class,
      "UHDCP descrambler",
      GST_ELEMENT_FACTORY_KLASS_DECRYPTOR,
      "Decrypts UHDCP protected media in ISOBMFF CENC format",
      "Yujin Lee <yujin.lee@lge.com>");

  basedrm_class->get_drm_info = GST_DEBUG_FUNCPTR (gst_cencdrm_uhdcp_drm_info);
  basedrm_class->start = GST_DEBUG_FUNCPTR (gst_cencdrm_uhdcp_start);
  basedrm_class->stop = GST_DEBUG_FUNCPTR (gst_cencdrm_uhdcp_stop);
  basedrm_class->drm_init = GST_DEBUG_FUNCPTR (gst_cencdrm_uhdcp_drm_init);
  basedrm_class->get_license_challenge =
      GST_DEBUG_FUNCPTR (gst_cencdrm_uhdcp_get_license_challenge);
  basedrm_class->request_license =
      GST_DEBUG_FUNCPTR (gst_cencdrm_uhdcp_request_license);
  basedrm_class->store_license =
      GST_DEBUG_FUNCPTR (gst_cencdrm_uhdcp_store_license);
  basedrm_class->is_playback_allowed =
      GST_DEBUG_FUNCPTR (gst_cencdrm_uhdcp_is_playback_allowed);
  basedrm_class->prepare_decrypt =
      GST_DEBUG_FUNCPTR (gst_cencdrm_uhdcp_prepare_decrypt);
  basedrm_class->set_kid = GST_DEBUG_FUNCPTR (gst_cencdrm_uhdcp_set_kid);
  basedrm_class->set_iv = GST_DEBUG_FUNCPTR (gst_cencdrm_uhdcp_set_iv);
  basedrm_class->decrypt = GST_DEBUG_FUNCPTR (gst_cencdrm_uhdcp_decrypt);
}

static void
gst_cencdrm_uhdcp_init (GstCencDrmUhdcp * self)
{
  GST_DEBUG_OBJECT (self, "cencdrmuhdcp initialization");

  self->iv_bytes = NULL;
  self->kid_bytes = NULL;
  self->init_done = FALSE;
  self->license = UHDCP_LICENSE_REFUSED;
}

static gboolean
gst_cencdrm_uhdcp_drm_info (GstBaseDrm * basedrm,
    GstDRMSystemInfo * drm_system_info)
{
  GstCencDrmUhdcp *self = GST_CENCDRM_UHDCP (basedrm);

  GST_DEBUG_OBJECT (self, "drm info");

  if (!drm_system_info) {
    GST_ERROR_OBJECT (self, "NULL pointer drm_system_info");
    return FALSE;
  }

  drm_system_info->system_ids =
      g_list_append (drm_system_info->system_ids, g_strdup (DIGICAP_UUID));
  drm_system_info->xml_node_name =
      g_memdup (&cencdrm_uhdcp_xml_node_name[0],
      sizeof (cencdrm_uhdcp_xml_node_name));

  if (!drm_system_info->system_ids || !drm_system_info->xml_node_name) {
    GST_ERROR_OBJECT (self,
        "Either system_ids %p, xml_node_name %p is a NULL pointer",
        drm_system_info->system_ids, drm_system_info->xml_node_name);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_cencdrm_uhdcp_start (GstBaseDrm * basedrm)
{
  GstCencDrmUhdcp *self = GST_CENCDRM_UHDCP (basedrm);

  GST_DEBUG_OBJECT (self, "UHDCP DRM for CENC open");

  module_dile_uhdcp = g_module_open (LIB_DILE_UHDCP_PATH, G_MODULE_BIND_LAZY);
  if (!module_dile_uhdcp) {
    GST_ERROR_OBJECT (self, "Failed to open a module: %s", g_module_error ());
    goto error_module;
  }
  if (!g_module_symbol (module_dile_uhdcp, "DILE_UHDCP_Decrypt",
          (gpointer *) & DILE_UHDCP_Decrypt))
    goto error_symbol;

  return TRUE;

error_symbol:
  GST_ERROR_OBJECT (self, "Failed to get a symbol: %s", g_module_error ());

error_module:
  g_module_close (module_dile_uhdcp);

  return FALSE;
}

static gboolean
gst_cencdrm_uhdcp_stop (GstBaseDrm * basedrm)
{
  GstCencDrmUhdcp *self = GST_CENCDRM_UHDCP (basedrm);

  GST_DEBUG_OBJECT (self, "close");

  if (self->iv_bytes)
    g_bytes_unref (self->iv_bytes);

  if (self->kid_bytes)
    g_bytes_unref (self->kid_bytes);

  g_module_close (module_dile_uhdcp);

  self->init_done = FALSE;

  return TRUE;
}

static gboolean
gst_cencdrm_uhdcp_prepare_decrypt (GstBaseDrm * basedrm)
{
  GstCencDrmUhdcp *self = GST_CENCDRM_UHDCP (basedrm);

  GST_DEBUG_OBJECT (self, "prepare decrypt");

  GST_DEBUG_OBJECT (self, "license granted");
  self->license = UHDCP_LICENSE_GRANTED;

  return TRUE;
}

static gboolean
gst_cencdrm_uhdcp_set_kid (GstBaseDrm * basedrm, guint8 * kid_data,
    gsize kid_size)
{
  GstCencDrmUhdcp *self = GST_CENCDRM_UHDCP (basedrm);

  GST_DEBUG_OBJECT (self, "set kid");

  if (!kid_data) {
    GST_ERROR_OBJECT (self, "invalid kid data");
    return FALSE;
  }

  if (kid_size != 16) {
    GST_ERROR_OBJECT (self, "invalid kid size");
    return FALSE;
  }

  if (self->kid_bytes)
    g_bytes_unref (self->kid_bytes);
  self->kid_bytes = g_bytes_new (kid_data, kid_size);

  return TRUE;
}

static gboolean
gst_cencdrm_uhdcp_set_iv (GstBaseDrm * basedrm, guint8 * iv_data, gsize iv_size)
{
  GstCencDrmUhdcp *self = GST_CENCDRM_UHDCP (basedrm);

  GST_DEBUG_OBJECT (self, "set iv");

  if (!iv_data) {
    GST_ERROR_OBJECT (self, "invalid iv data");
    return FALSE;
  }

  if (iv_size != 16 && iv_size != 8) {
    GST_ERROR_OBJECT (self, "invalid iv size");
    return FALSE;
  }

  if (self->iv_bytes)
    g_bytes_unref (self->iv_bytes);
  self->iv_bytes = g_bytes_new (iv_data, iv_size);

  return TRUE;
}

static gboolean
gst_cencdrm_uhdcp_decrypt (GstBaseDrm * basedrm, GstDecryptInfo * decrypt_info)
{
  GstCencDrmUhdcp *self = GST_CENCDRM_UHDCP (basedrm);
  guint8 *iv_data, *kid_data;
  gsize iv_size, kid_size;
  guint32 subsample_count = decrypt_info->subsample_count;
  guint32 *subsample_info = decrypt_info->subsample_info;
  guint8 *src = decrypt_info->data;
  guint64 offset_block = 0;
  guint16 offset_byte = 0;
  guint32 bytes_decrypted = 0;
  guint32 i;

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
      if (DILE_UHDCP_Decrypt (src, n_bytes_encrypted,
              iv_data, kid_data, offset_block, offset_byte) != 0) {
        GST_ERROR_OBJECT (self, "failed to decrypt");
        return FALSE;
      }
      bytes_decrypted += n_bytes_encrypted;
      offset_block = (bytes_decrypted >> AES128_BLOCKSIZE_RADIX2);
      offset_byte = bytes_decrypted - (offset_block << AES128_BLOCKSIZE_RADIX2);
      src += n_bytes_encrypted;
    }
  }
  return TRUE;
}

static gboolean
gst_cencdrm_uhdcp_is_playback_allowed (GstBaseDrm * basedrm)
{
  GstCencDrmUhdcp *self = GST_CENCDRM_UHDCP (basedrm);

  GST_DEBUG_OBJECT (self, "is playback allowed");

  return ((self->license == UHDCP_LICENSE_GRANTED) ? TRUE : FALSE);
}

static gboolean
gst_cencdrm_uhdcp_drm_init (GstBaseDrm * basedrm, guint8 * header, guint size)
{
  GstCencDrmUhdcp *self = GST_CENCDRM_UHDCP (basedrm);

  GST_DEBUG_OBJECT (self, "drm init");

  if (!self->init_done) {
    self->init_done = TRUE;
  }

  return TRUE;
}

static gboolean
gst_cencdrm_uhdcp_get_license_challenge (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info)
{
  GstCencDrmUhdcp *self = GST_CENCDRM_UHDCP (basedrm);

  GST_DEBUG_OBJECT (self, "get license challenge");

  return TRUE;
}

static gboolean
gst_cencdrm_uhdcp_request_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info)
{
  GstCencDrmUhdcp *self = GST_CENCDRM_UHDCP (basedrm);

  GST_DEBUG_OBJECT (self, "request license");

  return TRUE;
}

static gboolean
gst_cencdrm_uhdcp_store_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info)
{
  GstCencDrmUhdcp *self = GST_CENCDRM_UHDCP (basedrm);

  GST_DEBUG_OBJECT (self, "store license");

  return TRUE;
}
