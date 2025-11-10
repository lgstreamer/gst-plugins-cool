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

/**
 * SECTION:element-cencdrmplayready
 * Decrypts media that has been encrypted / protected using PlayReady DRM.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include <gst/gst.h>
#include <gst/gstelement.h>
#include <gst/base/gstbytewriter.h>
#include <gst/basedrm/gstbasedrm.h>
#include <glib.h>
#include <libxml/parser.h>

#include "gstcencdrmplayready.h"

#include <drmmanager.h>
#include <drmsecuretimeconstants.h>
#include <drmsecurecore.h>
#include <drmutf.h>
#include <drmbytemanip.h>
#include <drmtoolsnetio.h>

#define GST_CAT_DEFAULT gst_cencdrm_playready_debug_category
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/* UUID is System ID of PlayReady DRM system */
#define PLAYREADY_UUID_BE "9a04f079-9840-4286-ab92-e65be0885f95"
#define PLAYREADY_UUID_LE "79f0049a-4098-8642-ab92-e65be0885f95"

/* PlayReady DVB CA System ID for HbbTV service */
#define PLAYREADY_CA_SYSTEM_ID "urn:dvb:casystemid:19219"

/* prototypes */
static gboolean gst_cencdrm_playready_drm_info (GstBaseDrm * basedrm,
    GstDRMSystemInfo * drm_system_info);
static gboolean gst_cencdrm_playready_start (GstBaseDrm * basedrm);
static gboolean gst_cencdrm_playready_is_playback_allowed (GstBaseDrm *
    basedrm);
static gboolean gst_cencdrm_playready_drm_init (GstBaseDrm * basedrm,
    guint8 * header, guint size);
static gboolean gst_cencdrm_playready_get_license_challenge (GstBaseDrm *
    basedrm, GstDRMLicenseInfo * drm_license_info);
static gboolean gst_cencdrm_playready_request_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info);
static gboolean gst_cencdrm_playready_store_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info);
static gboolean gst_cencdrm_playready_prepare_decrypt (GstBaseDrm * basedrm);
static gboolean gst_cencdrm_playready_set_kid (GstBaseDrm * basedrm,
    guint8 * kid_data, gsize kid_size);
static gboolean gst_cencdrm_playready_set_iv (GstBaseDrm * basedrm,
    guint8 * iv_data, gsize iv_size);
static gboolean gst_cencdrm_playready_decrypt (GstBaseDrm * basedrm,
    GstDecryptInfo * decryptInfo);
static gboolean gst_cencdrm_playready_resolve_custom_pssi (GstBaseDrm * basedrm,
    gchar * custom_pssi, guint8 ** header, guint * size, gboolean * ignore);
static GstBuffer *gst_cencdrm_playready_get_key_info (GstBaseDrm * basedrm);
static gboolean gst_cencdrm_playready_stop (GstBaseDrm * basedrm);

static gboolean gst_cencdrm_playready_check_key_rotation (GstCencDrmPlayready *
    self, guint8 * header, guint size, gboolean * key_rotation);
static gboolean gst_cencdrm_playready_sync_drm_time (GstCencDrmPlayready *
    self);
static DRM_RESULT gst_cencdrm_playready_policy_callback (const DRM_VOID *
    output_levels, DRM_POLICY_CALLBACK_TYPE callback_type, const DRM_KID * kid,
    const DRM_LID * lid, const DRM_VOID * data);
static gboolean gst_cencdrm_playready_bind_license (GstBaseDrm * basedrm);

gboolean plugin_init (GstPlugin * plugin);

const char cencdrm_playready_xml_node_name[] = "mspr:pro";
static void *drm_ctrl_handle = NULL;

static struct _DrmCtrl
{
  void *(*load) (const char *drm_type, const char *drm_client_id,
      const char *uri);
  void (*notify) (int error_state, char *content_id, char *drm_system_id,
      char *rights_issuer_url);
  int (*release) (void *ctrl_handle);
} drm_ctrl;

#define CENCDRM_PLAYREADY_CAPS(uuid) \
  "application/x-cenc, original-media-type=(string) " \
  "{ video/x-h264, video/x-h265, " SINK_DECODE_AUDIO_CAPS "}, " \
  "protection-system=(string) " uuid

/* pad templates */
static GstStaticPadTemplate
    gst_cencdrm_playready_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CENCDRM_PLAYREADY_CAPS (PLAYREADY_UUID_BE) ";"
        CENCDRM_PLAYREADY_CAPS (PLAYREADY_UUID_LE))
    );

static GstStaticPadTemplate gst_cencdrm_playready_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("video/x-h264, secure_area = (string) svp, parsed = (boolean) true, drm-type = (string) playready; "
        "video/x-h265, secure_area = (string) svp, parsed = (boolean) true, drm-type = (string) playready; "
        SRC_DECODE_AUDIO_CAPS)
    );

/* class initialization */
#define gst_cencdrm_playready_parent_class parent_class
G_DEFINE_TYPE (GstCencDrmPlayready, gst_cencdrm_playready, GST_TYPE_BASEDRM);

static void
gst_cencdrm_playready_class_init (GstCencDrmPlayreadyClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseDrmClass *basedrm_class = GST_BASEDRM_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_cencdrm_playready_debug_category,
      "cencdrmplayready", 0, "Playready DRM for CENC");

  GST_DEBUG ("cencdrmplayready class init");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_cencdrm_playready_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_cencdrm_playready_src_template));

  gst_element_class_set_static_metadata (element_class,
      "Playready DRM for CENC",
      GST_ELEMENT_FACTORY_KLASS_DECRYPTOR,
      "Decrypts Playready DRM protected media in ISOBMFF CENC format",
      "Chihyoung Kim <chihyoung2.kim@lge.com>");

  basedrm_class->get_drm_info =
      GST_DEBUG_FUNCPTR (gst_cencdrm_playready_drm_info);
  basedrm_class->start = GST_DEBUG_FUNCPTR (gst_cencdrm_playready_start);
  basedrm_class->stop = GST_DEBUG_FUNCPTR (gst_cencdrm_playready_stop);
  basedrm_class->drm_init = GST_DEBUG_FUNCPTR (gst_cencdrm_playready_drm_init);
  basedrm_class->get_license_challenge =
      GST_DEBUG_FUNCPTR (gst_cencdrm_playready_get_license_challenge);
  basedrm_class->request_license =
      GST_DEBUG_FUNCPTR (gst_cencdrm_playready_request_license);
  basedrm_class->store_license =
      GST_DEBUG_FUNCPTR (gst_cencdrm_playready_store_license);
  basedrm_class->is_playback_allowed =
      GST_DEBUG_FUNCPTR (gst_cencdrm_playready_is_playback_allowed);
  basedrm_class->prepare_decrypt =
      GST_DEBUG_FUNCPTR (gst_cencdrm_playready_prepare_decrypt);
  basedrm_class->set_kid = GST_DEBUG_FUNCPTR (gst_cencdrm_playready_set_kid);
  basedrm_class->set_iv = GST_DEBUG_FUNCPTR (gst_cencdrm_playready_set_iv);
  basedrm_class->decrypt = GST_DEBUG_FUNCPTR (gst_cencdrm_playready_decrypt);
  basedrm_class->resolve_custom_pssi =
      GST_DEBUG_FUNCPTR (gst_cencdrm_playready_resolve_custom_pssi);
  basedrm_class->get_key_info =
      GST_DEBUG_FUNCPTR (gst_cencdrm_playready_get_key_info);
}

static void
gst_cencdrm_playready_init (GstCencDrmPlayready * self)
{
  GST_DEBUG_OBJECT (self, "cencdrmplayready initialization");

  self->module_drmcontroller = NULL;
  self->app_context = NULL;
  self->opaque_buffer = NULL;
  self->revocation_buffer = NULL;
  self->decrypt_context = NULL;
  self->iv_bytes = NULL;
  self->kid_bytes = NULL;
  self->kid_history = NULL;
  self->hds_path = NULL;
  self->license = PLAYREADY_LICENSE_REFUSED;
}

static gboolean
gst_cencdrm_playready_sync_drm_time (GstCencDrmPlayready * self)
{
  gchar *url = NULL;

  DRM_RESULT dr = DRM_SUCCESS;
  DRM_APP_CONTEXT *app_context = (DRM_APP_CONTEXT *) self->app_context;
  DRMFILETIME system_time;
  DRM_SECURETIME_CLOCK_TYPE clock_type;
  DRM_BYTE *challenge = NULL;
  DRM_DWORD challenge_size = 0;
  DRM_BYTE *response = NULL;
  DRM_DWORD response_size = 0;

  dr = Drm_SecureTime_GetValue (app_context, &system_time, &clock_type);
  if (dr == DRM_E_NOTIMPL || dr == DRM_E_CLK_NOT_SUPPORTED) {
    /* no need to do securetime protocol. */
    dr = DRM_SUCCESS;
  }

  if (dr == DRM_E_SECURETIME_CLOCK_NOT_SET || dr == DRM_E_TEE_CLOCK_DRIFTED) {
    const gchar redirect_key[] = "drm_clock_acquire_redirect: ";

    ChkDR (Drm_SecureTime_GenerateChallenge (app_context, &challenge_size,
            &challenge));

    ChkDR (DRM_TOOLS_NETIO_SendData (g_dstrHttpsSecureTimeServerUrl.pszString,
            eDRM_TOOLS_NET_SECURETIME_POST, challenge, challenge_size,
            &response, &response_size));
    response[response_size] = '\0';

    if (g_str_has_prefix ((const gchar *) response, redirect_key)) {
      url = g_strdup ((const gchar *) &response[strlen (redirect_key)]);
      SAFE_OEM_FREE (response);
      response_size = 0;
      ChkDR (DRM_TOOLS_NETIO_SendData (url, eDRM_TOOLS_NET_SECURETIME_POST,
              challenge, challenge_size, &response, &response_size));
    }
    ChkDR (Drm_SecureTime_ProcessResponse (app_context, response_size,
            response));
  }
  ChkDR (dr);

ErrorExit:
  g_free (url);
  SAFE_OEM_FREE (challenge);
  SAFE_OEM_FREE (response);

  return DRM_SUCCEEDED (dr);
}

static gboolean
gst_cencdrm_playready_drm_info (GstBaseDrm * basedrm,
    GstDRMSystemInfo * drm_system_info)
{
  GstCencDrmPlayready *self = GST_CENCDRM_PLAYREADY (basedrm);

  GST_DEBUG_OBJECT (self, "drm info");

  if (!drm_system_info) {
    GST_ERROR_OBJECT (self, "NULL pointer drm_system_info");
    return FALSE;
  }

  drm_system_info->system_ids =
      g_list_append (drm_system_info->system_ids, g_strdup (PLAYREADY_UUID_BE));
  drm_system_info->system_ids =
      g_list_append (drm_system_info->system_ids, g_strdup (PLAYREADY_UUID_LE));
  drm_system_info->xml_node_name =
      g_memdup (&cencdrm_playready_xml_node_name[0],
      sizeof (cencdrm_playready_xml_node_name));

  if (!drm_system_info->system_ids || !drm_system_info->xml_node_name) {
    GST_ERROR_OBJECT (self,
        "Either system_ids %p, xml_node_name %p is a NULL pointer",
        drm_system_info->system_ids, drm_system_info->xml_node_name);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_cencdrm_playready_start (GstBaseDrm * basedrm)
{
  GstCencDrmPlayready *self = GST_CENCDRM_PLAYREADY (basedrm);
  GstDRMRightsErrorInfo *rights_error_info = &basedrm->rights_error_info;
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  gchar *drm_path = NULL;
  gchar *hds_path = NULL;

  drm_system_info->is_svp = TRUE;

  GST_DEBUG_OBJECT (self, "Playready DRM for CENC open");

  g_free (rights_error_info->drm_system_id);
  rights_error_info->drm_system_id = g_strdup (PLAYREADY_CA_SYSTEM_ID);

  self->module_drmcontroller =
      g_module_open ("/usr/lib/libdrmcontroller.so.1", G_MODULE_BIND_LAZY);
  if (!self->module_drmcontroller) {
    GST_ERROR_OBJECT (self, "failed to open a module: %s", g_module_error ());
    goto error;
  }

  if (!g_module_symbol (self->module_drmcontroller, "API_DRM_Load",
          (gpointer *) & drm_ctrl.load) ||
      !g_module_symbol (self->module_drmcontroller,
          "API_DRM_NotifyDRMRightsErr", (gpointer *) & drm_ctrl.notify) ||
      !g_module_symbol (self->module_drmcontroller, "API_DRM_Release",
          (gpointer *) & drm_ctrl.release)) {
    GST_ERROR_OBJECT (self, "faile to get a symbol: %s", g_module_error ());
    goto error;
  }

  drm_path = g_strjoin ("/", g_getenv ("HOME"), ".mediadrm/playready", NULL);
  if (0 != g_mkdir_with_parents (drm_path, 0700)) {
    GST_ERROR_OBJECT (self, "failed to mkdir(%s)", drm_path);
    goto error;
  }

  hds_path = g_strjoin ("/", drm_path, "playready-4.0.hds", NULL);
  self->hds_path = g_array_new (TRUE, TRUE, sizeof (DRM_WCHAR));
  for (guint i = 0; i <= strlen (hds_path); i++) {
    DRM_WCHAR wc = DRM_ONE_WCHAR (hds_path[i], '\0');
    g_array_append_val (self->hds_path, wc);
  }

  g_free (drm_path);
  g_free (hds_path);
  return TRUE;

error:
  g_module_close (self->module_drmcontroller);
  g_free (drm_path);
  g_free (hds_path);
  return FALSE;
}

static gboolean
gst_cencdrm_playready_stop (GstBaseDrm * basedrm)
{
  GstCencDrmPlayready *self = GST_CENCDRM_PLAYREADY (basedrm);
  GstDRMRightsErrorInfo *rights_error_info = &basedrm->rights_error_info;

  GST_DEBUG_OBJECT (self, "close");

  if (self->decrypt_context) {
    Drm_Reader_Close ((DRM_DECRYPT_CONTEXT *) self->decrypt_context);
    g_free (self->decrypt_context);
    self->decrypt_context = NULL;
  }

  if (self->app_context) {
    Drm_Uninitialize ((DRM_APP_CONTEXT *) self->app_context);
    g_free (self->app_context);
    self->app_context = NULL;
  }

  g_free (self->opaque_buffer);
  self->opaque_buffer = NULL;

  g_free (self->revocation_buffer);
  self->revocation_buffer = NULL;

  if (drm_ctrl_handle) {
    if (rights_error_info->error_state != RIGHTS_ERROR_NONE) {
      drm_ctrl.notify (rights_error_info->error_state,
          rights_error_info->content_id, rights_error_info->drm_system_id,
          rights_error_info->rights_issuer_url);
    }
    drm_ctrl.release (drm_ctrl_handle);
    drm_ctrl_handle = NULL;
  }

  if (self->kid_history) {
    g_bytes_unref (self->kid_history);
    self->kid_history = NULL;
  }

  if (self->iv_bytes) {
    g_bytes_unref (self->iv_bytes);
    self->iv_bytes = NULL;
  }

  if (self->kid_bytes) {
    g_bytes_unref (self->kid_bytes);
    self->kid_bytes = NULL;
  }

  if (self->hds_path) {
    g_array_unref (self->hds_path);
    self->hds_path = NULL;
  }

  g_module_close (self->module_drmcontroller);

  return TRUE;
}

static DRM_RESULT
gst_cencdrm_playready_policy_callback (const DRM_VOID * output_levels,
    DRM_POLICY_CALLBACK_TYPE callback_type,
    const DRM_KID * kid, const DRM_LID * lid, const DRM_VOID * data)
{
  return DRM_SUCCESS;
}

static gboolean
gst_cencdrm_playready_bind_license (GstBaseDrm * basedrm)
{
  GstCencDrmPlayready *self = GST_CENCDRM_PLAYREADY (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;

  DRM_RESULT dr = DRM_SUCCESS;
  DRM_APP_CONTEXT *app_context = (DRM_APP_CONTEXT *) self->app_context;
  DRM_DECRYPT_CONTEXT *decrypt_context =
      (DRM_DECRYPT_CONTEXT *) self->decrypt_context;
  const DRM_CONST_STRING *rights[1] = { &g_dstrDRM_RIGHT_PLAYBACK };

  if (self->license == PLAYREADY_LICENSE_GRANTED) {
    GST_DEBUG_OBJECT (self, "license granted");
    goto ErrorExit;
  }

  if (decrypt_context) {
    Drm_Reader_Close (decrypt_context);
  } else {
    ChkMem (self->decrypt_context = g_new0 (DRM_DECRYPT_CONTEXT, 1));
    decrypt_context = (DRM_DECRYPT_CONTEXT *) self->decrypt_context;
  }

  if (drm_system_info->is_svp && !DRM_SECURECORE_IsInternal ()) {
    DRM_DWORD decryption_mode = OEM_TEE_DECRYPTION_MODE_HANDLE;
    ChkDR (Drm_Content_SetProperty (app_context, DRM_CSP_DECRYPTION_OUTPUT_MODE,
            (const DRM_BYTE *) &decryption_mode, sizeof (DRM_DWORD)));
  }

  ChkDR (Drm_Reader_Bind (app_context, rights, DRM_NO_OF (rights),
          gst_cencdrm_playready_policy_callback, NULL, decrypt_context));

  ChkDR (Drm_Reader_Commit (app_context, gst_cencdrm_playready_policy_callback,
          NULL));

  GST_DEBUG_OBJECT (self, "license granted");
  self->license = PLAYREADY_LICENSE_GRANTED;

ErrorExit:
  return DRM_SUCCEEDED (dr);
}

static gboolean
gst_cencdrm_playready_prepare_decrypt (GstBaseDrm * basedrm)
{
  GstCencDrmPlayready *self = GST_CENCDRM_PLAYREADY (basedrm);

  GST_TRACE_OBJECT (self, "prepare decrypt");

  return ((self->license == PLAYREADY_LICENSE_GRANTED) ? TRUE : FALSE);
}

static gboolean
gst_cencdrm_playready_set_kid (GstBaseDrm * basedrm, guint8 * kid_data,
    gsize kid_size)
{
  GstCencDrmPlayready *self = GST_CENCDRM_PLAYREADY (basedrm);

  if (self->kid_bytes)
    g_bytes_unref (self->kid_bytes);
  self->kid_bytes = g_bytes_new (kid_data, kid_size);

  return TRUE;
}

static gboolean
gst_cencdrm_playready_set_iv (GstBaseDrm * basedrm, guint8 * iv_data,
    gsize iv_size)
{
  GstCencDrmPlayready *self = GST_CENCDRM_PLAYREADY (basedrm);

  if (self->iv_bytes)
    g_bytes_unref (self->iv_bytes);
  self->iv_bytes = g_bytes_new (iv_data, iv_size);

  return TRUE;
}

static gboolean
gst_cencdrm_playready_decrypt (GstBaseDrm * basedrm,
    GstDecryptInfo * decrypt_info)
{
  GstCencDrmPlayready *self = GST_CENCDRM_PLAYREADY (basedrm);
  DRM_DWORD encrypted_region_counts = decrypt_info->subsample_count;
  guint8 *iv_data;
  gsize iv_size;

  DRM_RESULT dr = DRM_SUCCESS;
  DRM_DECRYPT_CONTEXT *decrypt_context =
      (DRM_DECRYPT_CONTEXT *) self->decrypt_context;

  DRM_DWORD region_skip[2] = { 0, };
  DRM_DWORD region_skip_size = 0;

  DRM_DWORD opaque_clear_content_size = 0;
  DRM_BYTE *opaque_clear_content = NULL;
  DRM_DWORD encrypted_size = (DRM_DWORD) decrypt_info->data_size;
  DRM_BYTE *encrypted_src = (DRM_BYTE *) decrypt_info->data;

  iv_data = (guint8 *) g_bytes_get_data (self->iv_bytes, &iv_size);
  DRM_UINT64 iv[2] = { DRM_UI64HL (0, 0), DRM_UI64HL (0, 0) };
  if (iv_size)
    DRM_BIG_ENDIAN_BYTES_TO_NATIVE_QWORD (iv[0], iv_data);
  if (iv_size > sizeof (DRM_UINT64))
    DRM_BIG_ENDIAN_BYTES_TO_NATIVE_QWORD (iv[1], iv_data + sizeof (DRM_UINT64));

  if (decrypt_info->scheme_type == FOURCC_cens
      || decrypt_info->scheme_type == FOURCC_cbcs) {
    region_skip_size = 2;
    region_skip[0] = decrypt_info->crypt_byte_block;
    region_skip[1] = decrypt_info->skip_byte_block;
  }
  ChkDR (Drm_Reader_DecryptMultipleOpaque (decrypt_context,
          1, &iv[0], &iv[1],
          &encrypted_region_counts,
          encrypted_region_counts * 2, decrypt_info->subsample_info,
          region_skip_size, region_skip_size ? region_skip : NULL,
          encrypted_size, encrypted_src,
          &opaque_clear_content_size, &opaque_clear_content));

  memcpy (encrypted_src, opaque_clear_content, encrypted_size);

  DRM_Reader_FreeOpaqueDecryptedContent (decrypt_context,
      opaque_clear_content_size, opaque_clear_content);

ErrorExit:
  return DRM_SUCCEEDED (dr);
}

static gboolean
gst_cencdrm_playready_get_xml_node_content (GstCencDrmPlayready * self,
    xmlNode * a_node, gchar ** content)
{
  xmlChar *node_content = NULL;
  gboolean exists = FALSE;

  node_content = xmlNodeGetContent (a_node);
  if (node_content) {
    exists = TRUE;
    *content = (gchar *) node_content;
    GST_DEBUG_OBJECT (self, " - %s: %s", a_node->name, *content);
  }

  return exists;
}

static gboolean
gst_cencdrm_playready_resolve_custom_pssi (GstBaseDrm * basedrm,
    gchar * custom_pssi, guint8 ** header, guint * size, gboolean * ignore)
{
  GstCencDrmPlayready *self = GST_CENCDRM_PLAYREADY (basedrm);
  xmlDocPtr doc;
  xmlNode *root_element = NULL;
  gchar *encoded_header = NULL;
  gboolean ret = FALSE;

  LIBXML_TEST_VERSION
      /* parse "data" into a document (which is a libxml2 tree structure xmlDoc) */
      doc = xmlReadMemory ((const char *) custom_pssi,
      strlen (custom_pssi), "CustomPssi.xml", NULL, 0);
  if (!doc) {
    GST_ERROR_OBJECT (self, "Failed to parse custom pssi XML");
    goto beach;
  }
  root_element = xmlDocGetRootElement (doc);

  if (root_element->type != XML_ELEMENT_NODE
      || xmlStrcmp (root_element->name, (xmlChar *) "mspr:pro") != 0) {
    GST_ERROR_OBJECT (self, "Failed to find mspr:pro element");
    *ignore = TRUE;
    ret = TRUE;
    goto beach;
  }

  gst_cencdrm_playready_get_xml_node_content (self, root_element,
      &encoded_header);
  if (!encoded_header) {
    GST_ERROR_OBJECT (self, "mspr:pro value not present");
    goto beach;
  }

  GST_DEBUG_OBJECT (self, "mspr:pro = %s", encoded_header);

  *header = (guint8 *) g_base64_decode (encoded_header, (gsize *) size);

  if (!(*header)) {
    GST_ERROR_OBJECT (basedrm, "invalid base64-encoded pssh data");
    goto beach;
  }

  *ignore = FALSE;
  ret = TRUE;

beach:
  if (doc)
    xmlFreeDoc (doc);
  if (encoded_header)
    xmlFree (encoded_header);

  return ret;
}

static gboolean
gst_cencdrm_playready_is_playback_allowed (GstBaseDrm * basedrm)
{
  GstCencDrmPlayready *self = GST_CENCDRM_PLAYREADY (basedrm);

  GST_DEBUG_OBJECT (self, "is playback allowed");

  if (!gst_cencdrm_playready_bind_license (basedrm)) {
    GST_DEBUG_OBJECT (self, "failed to bind license");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_cencdrm_playready_check_key_rotation (GstCencDrmPlayready * self,
    guint8 * header, guint size, gboolean * key_rotation)
{
  GBytes *kid_bytes = NULL;

  DRM_RESULT dr = DRM_SUCCESS;
  DRM_BYTE *header_data = NULL;
  DRM_DWORD header_data_size = 0;
  DRM_DWORD wchar_count = 0;
  DRM_CONST_STRING dstr_content_header = DRM_EMPTY_DRM_STRING;
  DRM_CONST_STRING dstr_kid = DRM_EMPTY_DRM_STRING;
  DRM_KID decoded_kid = DRM_ID_EMPTY;
  DRM_DWORD decoded_kid_size = sizeof (DRM_KID);

  ChkArg (header && size && key_rotation);
  *key_rotation = FALSE;

  /* Try to check input is PRO and get WRMHEADER */
  dr = DRM_PRO_GetRecord (header, size, PLAYREADY_WRMHEADER,
      &header_data, &header_data_size);
  /* If it's not PRO, try to get WRMHEADER from input buffer */
  if (DRM_FAILED (dr)) {
    /* The header is Unicode, therefore its size must be even */
    ChkBOOL (size % sizeof (DRM_WCHAR) == 0, DRM_E_CH_INVALID_HEADER);

    /* Make sure that there is enough data to process BOM (byte order mark). */
    ChkBOOL (size > 2, DRM_E_CH_INVALID_HEADER);

    /* Have to fail if first 2 8-bit units indicates big endian BOM (byte order mark). */
    /* PK accepts input in little endian format only. */
    ChkBOOL (!IS_BIG_ENDIAN_UTF16_BOM (header), DRM_E_CH_INVALID_HEADER);

    /* Check if first 2 8-bit units are little endian BOM (byte order mark) */
    if (IS_LITTLE_ENDIAN_UTF16_BOM (header)) {
      /* Point to header without UTF-16 BOM. */
      header_data = header + sizeof (DRM_WCHAR);
      header_data_size = size - sizeof (DRM_WCHAR);
    } else {
      header_data = header;
      header_data_size = size;
    }
  }

  wchar_count = header_data_size / sizeof (DRM_WCHAR);
  if (((DRM_WCHAR *) (header_data))[wchar_count - 1] == DRM_WCHAR_CAST ('\0')) {
    /* Remove the NULL terminating character if it is there. */
    header_data_size -= sizeof (DRM_WCHAR);
  }

  DRM_DSTR_FROM_PB (&dstr_content_header, header_data, header_data_size);
  ChkDR (DRM_HDR_GetAttribute (&dstr_content_header, NULL,
          DRM_HEADER_ATTRIB_KID, &dstr_kid, NULL, NULL, 0));
  ChkDR (DRM_B64_DecodeW (&dstr_kid, &decoded_kid_size,
          (DRM_BYTE *) & decoded_kid, 0));

  if (!self->kid_history) {
    self->kid_history = g_bytes_new (decoded_kid.rgb, sizeof (decoded_kid));
    GST_DEBUG_OBJECT (self, "we got the new kid");
    goto ErrorExit;
  }

  kid_bytes = g_bytes_new (decoded_kid.rgb, sizeof (decoded_kid));
  if (g_bytes_equal (kid_bytes, self->kid_history)) {
    GST_DEBUG_OBJECT (self, "no key rotation");
    goto ErrorExit;
  }

  g_bytes_unref (self->kid_history);
  self->kid_history = g_bytes_new (decoded_kid.rgb, sizeof (decoded_kid));
  *key_rotation = TRUE;

ErrorExit:
  if (kid_bytes)
    g_bytes_unref (kid_bytes);

  return DRM_SUCCEEDED (dr);
}

static gboolean
gst_cencdrm_playready_drm_init (GstBaseDrm * basedrm, guint8 * header,
    guint size)
{
  GstCencDrmPlayready *self = GST_CENCDRM_PLAYREADY (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  GstDRMRightsErrorInfo *rights_error_info = &basedrm->rights_error_info;
  gboolean key_rotation = TRUE;
  guint8 *kid = NULL;
  gsize kid_size;

  DRM_RESULT dr = DRM_SUCCESS;
  DRM_APP_CONTEXT *app_context = (DRM_APP_CONTEXT *) self->app_context;
  DRM_CHAR content_id[CCH_BASE64_EQUIV (sizeof (DRM_KID)) + 1] = { 0, };
  DRM_DWORD content_id_size = CCH_BASE64_EQUIV (sizeof (DRM_KID));

  GST_DEBUG_OBJECT (self, "drm init");

  if (!drm_ctrl_handle) {
    ChkMem (drm_ctrl_handle = drm_ctrl.load ("playready",
            drm_system_info->drmclient_id, NULL));
  }

  ChkBOOL (gst_cencdrm_playready_check_key_rotation (self, header, size,
          &key_rotation), DRM_E_CH_INVALID_HEADER);
  if (!key_rotation && self->license == PLAYREADY_LICENSE_GRANTED)
    goto ErrorExit;

  self->license = PLAYREADY_LICENSE_REFUSED;

  if (!app_context) {
    DRM_CONST_STRING drm_store = DRM_EMPTY_DRM_STRING;

    drm_store.pwszString = (const DRM_WCHAR *) self->hds_path->data;
    drm_store.cchString = (DRM_DWORD) self->hds_path->len;

    ChkMem (self->app_context = g_new0 (DRM_APP_CONTEXT, 1));
    app_context = (DRM_APP_CONTEXT *) self->app_context;
    ChkMem (self->opaque_buffer =
        g_malloc0 (MINIMUM_APPCONTEXT_OPAQUE_BUFFER_SIZE));
    ChkDR (Drm_Initialize (app_context, NULL, self->opaque_buffer,
            MINIMUM_APPCONTEXT_OPAQUE_BUFFER_SIZE, &drm_store));

    ChkMem (self->revocation_buffer = g_malloc0 (REVOCATION_BUFFER_SIZE));
    ChkDR (Drm_Revocation_SetBuffer (app_context, self->revocation_buffer,
            REVOCATION_BUFFER_SIZE));
  } else {
    GST_DEBUG_OBJECT (self, "key rotation detected");
    ChkDR (Drm_Reinitialize (app_context));
  }

  ChkBOOL (gst_cencdrm_playready_sync_drm_time (self), DRM_E_CLK_NOT_SET);

  ChkDR (Drm_Content_SetProperty (app_context, DRM_CSP_AUTODETECT_HEADER,
          header, size));

  /* set content_id of rights_error_info */
  kid = g_bytes_get_data (self->kid_history, &kid_size);
  ChkDR (DRM_B64_EncodeA (kid, kid_size, content_id, &content_id_size,
          DRM_BASE64_ENCODE_NO_FLAGS));
  content_id[content_id_size] = '\0';
  g_free (rights_error_info->content_id);
  rights_error_info->content_id = g_strdup ((const gchar *) content_id);

ErrorExit:
  return DRM_SUCCEEDED (dr);
}

static gboolean
gst_cencdrm_playready_get_license_challenge (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info)
{
  GstCencDrmPlayready *self = GST_CENCDRM_PLAYREADY (basedrm);
  GstDRMRightsErrorInfo *rights_error_info = &basedrm->rights_error_info;

  DRM_RESULT dr = DRM_SUCCESS;
  DRM_APP_CONTEXT *app_context = (DRM_APP_CONTEXT *) self->app_context;
  const DRM_CONST_STRING *rights[1] = { &g_dstrDRM_RIGHT_PLAYBACK };

  GST_DEBUG_OBJECT (self, "get license challenge");

  dr = Drm_LicenseAcq_GenerateChallenge (app_context, rights,
      DRM_NO_OF (rights), NULL, NULL, 0, NULL, &drm_license_info->url_length,
      NULL, NULL, NULL, &drm_license_info->challenge_length, NULL);
  DRM_REQUIRE_BUFFER_TOO_SMALL (dr);
  ChkMem (drm_license_info->url = g_malloc0 (drm_license_info->url_length + 1));
  ChkMem (drm_license_info->challenge =
      g_malloc0 (drm_license_info->challenge_length + 1));

  ChkDR (Drm_LicenseAcq_GenerateChallenge (app_context, rights,
          DRM_NO_OF (rights), NULL, NULL, 0, (DRM_CHAR *) drm_license_info->url,
          &drm_license_info->url_length, NULL, NULL,
          drm_license_info->challenge, &drm_license_info->challenge_length,
          NULL));
  drm_license_info->url[drm_license_info->url_length] = '\0';
  drm_license_info->challenge[drm_license_info->challenge_length] = '\0';

  g_free (rights_error_info->rights_issuer_url);
  rights_error_info->rights_issuer_url =
      g_strdup ((const gchar *) drm_license_info->url);

ErrorExit:
  return DRM_SUCCEEDED (dr);
}

static gboolean
gst_cencdrm_playready_request_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info)
{
  GstCencDrmPlayready *self = GST_CENCDRM_PLAYREADY (basedrm);

  DRM_RESULT dr = DRM_SUCCESS;

  GST_DEBUG_OBJECT (self, "request license");

  ChkDR (DRM_TOOLS_NETIO_SendData ((const DRM_CHAR *) drm_license_info->url,
          eDRM_TOOLS_NET_LICGET, drm_license_info->challenge,
          drm_license_info->challenge_length, &drm_license_info->response,
          &drm_license_info->response_length));

ErrorExit:
  GST_DEBUG_OBJECT (self, "url: %s", drm_license_info->url);
  GST_DEBUG_OBJECT (self, "challenge: %s", drm_license_info->challenge);
  GST_DEBUG_OBJECT (self, "response: %s", drm_license_info->response);

  return DRM_SUCCEEDED (dr);
}

static gboolean
gst_cencdrm_playready_store_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info)
{
  GstCencDrmPlayready *self = GST_CENCDRM_PLAYREADY (basedrm);
  GstDRMRightsErrorInfo *rights_error_info = &basedrm->rights_error_info;

  DRM_RESULT dr = DRM_SUCCESS;
  DRM_APP_CONTEXT *app_context = (DRM_APP_CONTEXT *) self->app_context;
  DRM_LICENSE_RESPONSE response = { eUnknownProtocol, {0} };

  GST_DEBUG_OBJECT (self, "store license");

  ChkDR (Drm_LicenseAcq_ProcessResponse (app_context,
          DRM_PROCESS_LIC_RESPONSE_SIGNATURE_NOT_REQUIRED,
          drm_license_info->response, drm_license_info->response_length,
          &response));
  ChkDR (response.m_dwResult);

  if (!gst_cencdrm_playready_bind_license (basedrm)) {
    GST_ERROR_OBJECT (self, "failed to bind license");
    rights_error_info->error_state = RIGHTS_ERROR_INVALID_LICENSE;
    return FALSE;
  }

ErrorExit:
  return DRM_SUCCEEDED (dr);
}

static GstBuffer *
gst_cencdrm_playready_get_key_info (GstBaseDrm * basedrm)
{
  GstCencDrmPlayready *self = GST_CENCDRM_PLAYREADY (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  GstByteWriter bw;
  GstBuffer *key_info;
  guint8 *kid = NULL;
  guint kid_size;

  if (!drm_system_info->is_svp)
    return NULL;

  gst_byte_writer_init (&bw);

  kid = g_bytes_get_data (self->kid_history, &kid_size);

  if (!gst_byte_writer_put_data (&bw, kid, kid_size)) {
    GST_ERROR_OBJECT (self, "failed to put kid");
    return NULL;
  }

  if (!gst_byte_writer_put_uint32_le (&bw, (guint32) self->decrypt_context)) {
    GST_ERROR_OBJECT (self, "failed to put decrypt_context");
    return NULL;
  }
  key_info = gst_byte_writer_reset_and_get_buffer (&bw);

  return key_info;
}
