/* GStreamer CENC DRM Widevine element
 * Copyright (C) 2019-2022 by LG Electronics Inc.
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

/**
 * SECTION:element-cencdrmwidevine
 * Decrypts media that has been encrypted / protected using Widevine DRM.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>
#include <gst/gst.h>
#include <gst/gstelement.h>
#include <gst/base/gstbytewriter.h>
#include <gst/basedrm/gstbasedrm.h>
#include <glib.h>
#include <libsoup/soup.h>
#include <libxml/parser.h>
#include <json-c/json.h>
#include <gmodule.h>

#include <wv_cdm_wrapper.h>

#include "gstcencdrmwidevine.h"

#define GST_CAT_DEFAULT gst_cencdrm_widevine_debug_category
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define UUID_STRING_LEN 36
#define KEY_ID_SIZE 16

/* UUID is System ID of Widevine DRM system */
#define WIDEVINE_UUID "edef8ba9-79d6-4ace-a3c8-27dcd51d21ed"

#define LIB_DILE_CRYPTO_PATH "/usr/lib/libdile_crypto.so.0"

static gint8 service_count = 0;
static CDM_WRAPPER_CLASS *ott_service = NULL;
static CDM_WRAPPER_CLASS *ota_service = NULL;

/* static variables */
static GstCencDrmWidevine *gst_cencdrm_self = NULL;
static Callback callback;

/* prototypes */
static gboolean gst_cencdrm_widevine_drm_info (GstBaseDrm * basedrm,
    GstDRMSystemInfo * drm_system_info);
static gboolean gst_cencdrm_widevine_start (GstBaseDrm * basedrm);
static gboolean gst_cencdrm_widevine_is_playback_allowed (GstBaseDrm * basedrm);
static gboolean gst_cencdrm_widevine_drm_init (GstBaseDrm * basedrm,
    guint8 * header, guint size);
static gboolean gst_cencdrm_widevine_get_license_challenge (GstBaseDrm *
    basedrm, GstDRMLicenseInfo * drm_license_info);
static gboolean gst_cencdrm_widevine_request_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info);
static gboolean gst_cencdrm_widevine_store_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info);
static gboolean gst_cencdrm_widevine_prepare_decrypt (GstBaseDrm * basedrm);
static gboolean gst_cencdrm_widevine_set_kid (GstBaseDrm * basedrm,
    guint8 * kid_data, gsize kid_size);
static gboolean gst_cencdrm_widevine_set_iv (GstBaseDrm * basedrm,
    guint8 * iv_data, gsize iv_size);
static gboolean gst_cencdrm_widevine_decrypt (GstBaseDrm * basedrm,
    GstDecryptInfo * decryptInfo);
static gboolean gst_cencdrm_widevine_stop (GstBaseDrm * basedrm);
static GstBuffer *gst_cencdrm_widevine_get_key_info (GstBaseDrm * basedrm);
static gboolean gst_cencdrm_widevine_restore_original_pssh (GstBaseDrm *
    basedrm, guint8 * original_pssh, guint original_pssh_size, guint8 ** data,
    guint * data_size);
static gboolean
gst_cencdrm_widevine_set_video_info (GstBaseDrm * basedrm,
    guint32 width, guint32 height);
static gboolean
gst_cencdrm_widevine_generateRequest_operation (GstElement * element,
    const gchar * system_id, const gchar * service, const gchar * drm_msg_type,
    const guint8 * kid, guint32 kid_length, const guint8 * session_id,
    guint32 session_id_length, const guint8 * drm_data, guint32 drm_data_length,
    gint32 request_id);
static gboolean gst_cencdrm_widevine_update_operation (GstElement * element,
    const gchar * system_id, const gchar * service, const gchar * drm_msg_type,
    const guint8 * kid, guint32 kid_length, const guint8 * session_id,
    guint32 session_id_length, const guint8 * drm_data, guint32 drm_data_length,
    gint32 request_id);
static gboolean
gst_cencdrm_widevine_individualizationResponse_operation (GstElement * element,
    const gchar * system_id, const gchar * service, const gchar * drm_msg_type,
    const guint8 * kid, guint32 kid_length, const guint8 * session_id,
    guint32 session_id_length, const guint8 * drm_data, guint32 drm_data_length,
    gint32 request_id);
static gboolean
gst_cencdrm_widevine_serviceCertificateResponse_operation (GstElement * element,
    const gchar * system_id, const gchar * service, const gchar * drm_msg_type,
    const guint8 * kid, guint32 kid_length, const guint8 * session_id,
    guint32 session_id_length, const guint8 * drm_data, guint32 drm_data_length,
    gint32 request_id);
static void gst_cencdrm_widevine_set_context (GstElement * element,
    GstContext * context);
static void gst_cencdrm_widevine_free_notify_info (GstCencDrmWidevineNotifyInfo
    * notify_info);
static gboolean gst_cencdrm_widevine_save_notify_info (GstBaseDrm * basedrm,
    const gchar * drm_msg_type, const guint8 * drm_data,
    guint32 drm_data_length, gint32 request_id);
static gboolean gst_cencdrm_widevine_notify (GstBaseDrm * basedrm,
    const gchar * system_id, const gchar * service, const gchar * drm_msg_type,
    const guint8 * kid, guint32 kid_length, const guint8 * session_id,
    guint32 session_id_length, const guint8 * drm_data, guint32 drm_data_length,
    gint32 request_id);
static gboolean gst_cencdrm_widevine_periodic_notify_info (GstBaseDrm *
    basedrm);
static gboolean gst_cencdrm_widevine_handle_notify_info (GstBaseDrm * basedrm);
static void gst_cencdrm_widevine_send_error (GstBaseDrm * basedrm);
gboolean plugin_init (GstPlugin * plugin);

static gint (*DILE_CRYPTO_ATSC3_Pearl_ReadPrivKey) (guint8 * pData, gint nSize);

const gchar cencdrm_widevine_xml_node_name[] = "cenc:pssh";
static GModule *module_dile_crypto;

#define CENCDRM_WIDEVINE_CAPS(uuid) \
  "application/x-cenc, original-media-type=(string) " \
  "{ video/x-h264, video/x-h265, " SINK_DECODE_AUDIO_CAPS "}, " \
  "protection-system=(string) " uuid

/* pad templates */
static GstStaticPadTemplate
    gst_cencdrm_widevine_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CENCDRM_WIDEVINE_CAPS (WIDEVINE_UUID))
    );

static GstStaticPadTemplate gst_cencdrm_widevine_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("video/x-h264, secure_area = (string) svp, parsed = (boolean) true, drm-type = (string) widevine-modular; "
        "video/x-h265, secure_area = (string) svp, parsed = (boolean) true, drm-type = (string) widevine-modular; "
        SRC_DECODE_AUDIO_CAPS)
    );

/* class initialization */
/* for libsoup SSL client cert */
typedef struct _GetTlsCertInteraction GetTlsCertInteraction;
typedef struct _GetTlsCertInteractionClass GetTlsCertInteractionClass;

struct _GetTlsCertInteraction
{
  GTlsInteraction parent_instance;
  GTlsCertificate *cert;
};

struct _GetTlsCertInteractionClass
{
  GTlsInteractionClass parent_class;
};

static GType _get_tls_cert_interaction_get_type (void);

G_DEFINE_TYPE (GetTlsCertInteraction, _get_tls_cert_interaction,
    G_TYPE_TLS_INTERACTION);

static GTlsInteractionResult
_get_tls_cert_interaction_request_certificate (GTlsInteraction *
    interaction, GTlsConnection * connection, GTlsCertificateRequestFlags flags,
    GCancellable * cancellable, GError ** error)
{
  GetTlsCertInteraction *self = (GetTlsCertInteraction *) interaction;
  g_tls_connection_set_certificate (connection, self->cert);
  return G_TLS_INTERACTION_HANDLED;
}

static void
_get_tls_cert_interaction_init (GetTlsCertInteraction * interaction)
{
}

static void
_get_tls_cert_interaction_class_init (GetTlsCertInteractionClass * klass)
{
  GTlsInteractionClass *interaction_class = G_TLS_INTERACTION_CLASS (klass);
  interaction_class->request_certificate =
      _get_tls_cert_interaction_request_certificate;
}

GetTlsCertInteraction *
_get_tls_cert_interaction_new (GTlsCertificate * cert)
{
  GetTlsCertInteraction *self =
      g_object_new (_get_tls_cert_interaction_get_type (), NULL);
  self->cert = cert;
  return self;
}

#define gst_cencdrm_widevine_parent_class parent_class
G_DEFINE_TYPE (GstCencDrmWidevine, gst_cencdrm_widevine, GST_TYPE_BASEDRM);

static void
gst_cencdrm_widevine_class_init (GstCencDrmWidevineClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseDrmClass *basedrm_class = GST_BASEDRM_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_cencdrm_widevine_debug_category,
      "cencdrmwidevine", 0, "Widevine DRM for CENC");

  GST_DEBUG ("cencdrmwidevine class init");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_cencdrm_widevine_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_cencdrm_widevine_src_template));

  gst_element_class_set_static_metadata (element_class,
      "Widevine DRM for CENC",
      GST_ELEMENT_FACTORY_KLASS_DECRYPTOR,
      "Decrypts Widevine DRM protected media in ISOBMFF CENC format",
      "Chihyoung Kim <chihyoung2.kim@lge.com>");

  basedrm_class->get_drm_info =
      GST_DEBUG_FUNCPTR (gst_cencdrm_widevine_drm_info);
  basedrm_class->start = GST_DEBUG_FUNCPTR (gst_cencdrm_widevine_start);
  basedrm_class->stop = GST_DEBUG_FUNCPTR (gst_cencdrm_widevine_stop);
  basedrm_class->drm_init = GST_DEBUG_FUNCPTR (gst_cencdrm_widevine_drm_init);
  basedrm_class->get_license_challenge =
      GST_DEBUG_FUNCPTR (gst_cencdrm_widevine_get_license_challenge);
  basedrm_class->request_license =
      GST_DEBUG_FUNCPTR (gst_cencdrm_widevine_request_license);
  basedrm_class->store_license =
      GST_DEBUG_FUNCPTR (gst_cencdrm_widevine_store_license);
  basedrm_class->is_playback_allowed =
      GST_DEBUG_FUNCPTR (gst_cencdrm_widevine_is_playback_allowed);
  basedrm_class->prepare_decrypt =
      GST_DEBUG_FUNCPTR (gst_cencdrm_widevine_prepare_decrypt);
  basedrm_class->set_kid = GST_DEBUG_FUNCPTR (gst_cencdrm_widevine_set_kid);
  basedrm_class->set_iv = GST_DEBUG_FUNCPTR (gst_cencdrm_widevine_set_iv);
  basedrm_class->decrypt = GST_DEBUG_FUNCPTR (gst_cencdrm_widevine_decrypt);
  basedrm_class->get_key_info =
      GST_DEBUG_FUNCPTR (gst_cencdrm_widevine_get_key_info);
  basedrm_class->restore_original_pssh =
      GST_DEBUG_FUNCPTR (gst_cencdrm_widevine_restore_original_pssh);
  basedrm_class->set_video_info =
      GST_DEBUG_FUNCPTR (gst_cencdrm_widevine_set_video_info);
  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_cencdrm_widevine_set_context);
}

/* convert a DRM KeyId (UUID) in bytes representation to string representation */
static gchar *
gst_cencdrm_widevine_uuid_bytes_to_string (const guint8 * uuid_bytes)
{
  gsize uuid_string_length = UUID_STRING_LEN + 1;
  gchar *uuid_string = (gchar *) g_malloc (uuid_string_length);

  snprintf (uuid_string, uuid_string_length,
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-"
      "%02x%02x-%02x%02x%02x%02x%02x%02x",
      uuid_bytes[0], uuid_bytes[1], uuid_bytes[2], uuid_bytes[3],
      uuid_bytes[4], uuid_bytes[5], uuid_bytes[6], uuid_bytes[7],
      uuid_bytes[8], uuid_bytes[9], uuid_bytes[10], uuid_bytes[11],
      uuid_bytes[12], uuid_bytes[13], uuid_bytes[14], uuid_bytes[15]);

  return uuid_string;
}

static guint8
gst_cencdrm_widevine_hex_char_to_byte (gchar hex_char)
{
  if ('0' <= hex_char && hex_char <= '9')
    return hex_char - '0';
  else if ('A' <= hex_char && hex_char <= 'F')
    return hex_char - 'A' + 0x0A;
  else if ('a' <= hex_char && hex_char <= 'f')
    return hex_char - 'a' + 0x0a;
  else
    return 0;
}

static void
gst_cencdrm_widevine_callback_message (const gchar * session_id,
    guint32 session_id_size, const gchar * message, guint32 message_size)
{
  GST_DEBUG_OBJECT (gst_cencdrm_self, "message_session_id[%s]", session_id);

  if (message_size == 0 || session_id_size == 0)
    return;

  GST_DEBUG_OBJECT (gst_cencdrm_self,
      "received data for requesting license[%d]", message_size);

  if (gst_cencdrm_self) {
    gst_cencdrm_self->generated_request_message =
        (gchar *) g_memdup (message, message_size);
    gst_cencdrm_self->generated_message_length = message_size;
  }
}

static void
gst_cencdrm_widevine_callback_key_status (const gchar * session_id,
    guint32 session_id_size, Key_Info * key_info, Key_Status * key_status,
    guint32 num_of_keys, bool has_new_usable_key)
{
  CDM_WRAPPER_CLASS *cdm_wrapper;

  if (gst_cencdrm_self->license_type == LICENSE_OTA) {
    cdm_wrapper = (CDM_WRAPPER_CLASS *) gst_cencdrm_self->cdm_wrapper_ota;
  } else {
    cdm_wrapper = (CDM_WRAPPER_CLASS *) gst_cencdrm_self->cdm_wrapper_ott;
  }

  if (session_id_size == 0)
    return;

  GST_DEBUG_OBJECT (gst_cencdrm_self,
      "has_new_usable_key : %d", has_new_usable_key);
  if (has_new_usable_key == exist_usable_key) {
    gst_cencdrm_self->has_usable_key = TRUE;

  } else {
    if (gst_cencdrm_self->is_exist_license) {
      GST_DEBUG_OBJECT (gst_cencdrm_self,
          "remove a license bacause of not valid key");
      cdm_wrapper->RemoveLicense (cdm_wrapper, session_id, session_id_size);
    }
    gst_cencdrm_self->has_usable_key = FALSE;
  }
}

static void
gst_cencdrm_widevine_init (GstCencDrmWidevine * self)
{
  GST_DEBUG_OBJECT (self, "cencdrmwidevine initialization");

  self->iv_bytes = NULL;
  self->kid_bytes = NULL;
  self->license_state = WIDEVINE_LICENSE_REFUSED;
  self->session_id_length = 0;
  memset (self->session_id, '\0', sizeof (self->session_id));
  self->group_license_id_length = 0;
  memset (self->group_license_id, '\0', sizeof (self->group_license_id));
  self->network_connectivity = FALSE;
  self->global_service_id = NULL;
  self->system_id = NULL;
  self->generated_request_message = NULL;
  self->generated_message_length = 0;
  self->notify_info = NULL;
  self->is_response = FALSE;
  self->retry_count = 0;
  self->content_id = NULL;
  self->content_id_size = 0;
  self->license_url = NULL;
  self->license_url_size = 0;
  self->has_usable_key = TRUE;
  self->is_exist_license = FALSE;
  self->init_data = NULL;
  self->init_data_size = 0;
  self->done_ready_license = FALSE;
  self->seamless_mode = FALSE;
  self->seamless_app = FALSE;
  self->is_svp = FALSE;
  self->license_type = LICENSE_OTA;
  self->cdm_wrapper_ota = NULL;
  self->cdm_wrapper_ott = NULL;
  self->thread = NULL;
  callback.message = gst_cencdrm_widevine_callback_message;
  callback.keyStatus = gst_cencdrm_widevine_callback_key_status;
}

static void
gst_cencdrm_widevine_dump_http_transaction (GstCencDrmWidevine * self,
    SoupMessage * msg, guint8 * req_body, guint32 req_body_length)
{
  SoupMessageHeadersIter iter;
  const gchar *hname, *value;
  GST_DEBUG_OBJECT (self, ">>>>>>>>>>>>>>>> http request >>>>>>>>>>>>>>>>>");
  GST_DEBUG_OBJECT (self, "%s %s HTTP/1.%d", msg->method,
      soup_uri_to_string (soup_message_get_uri (msg), TRUE),
      soup_message_get_http_version (msg));
  GST_DEBUG_OBJECT (self, "HOST: %s", soup_message_get_uri (msg)->host);
  soup_message_headers_iter_init (&iter, msg->request_headers);
  while (soup_message_headers_iter_next (&iter, &hname, &value))
    GST_DEBUG_OBJECT (self, "%s: %s", hname, value);
  if (req_body != NULL) {
    GST_DEBUG_OBJECT (self, "[%d]", req_body_length);
  }
  GST_DEBUG_OBJECT (self, "<<<<<<<<<<<<<<<< http request <<<<<<<<<<<<<<<<<");

  GST_DEBUG_OBJECT (self, ">>>>>>>>>>>>>>>> http response >>>>>>>>>>>>>>>>");
  GST_DEBUG_OBJECT (self, "HTTP/1.%d %d %s",
      soup_message_get_http_version (msg), msg->status_code,
      msg->reason_phrase);
  soup_message_headers_iter_init (&iter, msg->response_headers);
  while (soup_message_headers_iter_next (&iter, &hname, &value)) {
    GST_DEBUG_OBJECT (self, "%s: %s", hname, value);
    if (g_strcmp0 (hname, "Content-Length") == 0) {
      GST_DEBUG_OBJECT (self, "Let's copy the length[%d]", atoi (value));
//      msg->response_body->length = atoi(value);
    }
  }
  if (msg->response_body && msg->response_body->data) {
    GST_DEBUG_OBJECT (self, "[%]", G_GOFFSET_FORMAT,
        msg->response_body->length);
  } else
    GST_DEBUG_OBJECT (self, "response body does not exist!!");
  GST_DEBUG_OBJECT (self, "<<<<<<<<<<<<<<<< http response <<<<<<<<<<<<<<<<");
}

static gboolean
gst_cencdrm_widevide_send_http_request (GstCencDrmWidevine * self,
    const gchar * url, guint32 url_length,
    guint8 * req_body, guint32 req_body_length,
    gchar ** response, guint32 * response_length,
    const gchar * content_type, SoupMessageHeaders * hdrs)
{
  gboolean ret = FALSE;
  SoupSession *session = NULL;
  SoupMessage *msg = NULL;
  guint status;
  GTlsCertificate *client_cert = NULL;
  GetTlsCertInteraction *interaction = NULL;
  gchar *cert_buffer = NULL;
  guint cert_size = 0;
  guchar key_buffer[5120] = { '0' };
  guint key_size = 0;
  GError *error = NULL;

  session =
      soup_session_new_with_options ("proxy-resolver", NULL, "timeout", 5,
      NULL);
  if (!session) {
    GST_ERROR_OBJECT (self, "failed to create a soup session");
    goto ERROR;
  }

  if (DILE_CRYPTO_ATSC3_Pearl_ReadPrivKey (key_buffer, &key_size) != 0) {
    GST_DEBUG_OBJECT (self, "fail to read key");
    goto ERROR;
  }

  if (!g_file_get_contents
      ("/etc/ssl/certs/ca-certificates/atsc3_pearl/LG_webOS_TV_H.crt",
          &cert_buffer, &cert_size, &error)) {
    GST_DEBUG_OBJECT (self, "fail to read cert [%s]", error->message);
    goto ERROR;
  }

  memcpy (key_buffer + key_size, cert_buffer, cert_size);
  key_size += cert_size;

  client_cert = g_tls_certificate_new_from_pem (key_buffer, key_size, &error);
  if (!client_cert) {
    GST_DEBUG_OBJECT (self, "fail to get GTlsCertificate[%s]", error->message);
    goto ERROR;
  }
  interaction = _get_tls_cert_interaction_new (client_cert);
  g_object_set (session, SOUP_SESSION_TLS_INTERACTION, interaction, NULL);

  msg = soup_message_new (SOUP_METHOD_POST, url);
  if (!msg) {
    GST_ERROR_OBJECT (self, "failed to create a soup message");
    goto ERROR;
  }

  soup_message_headers_append (msg->request_headers, "user-agent",
      "ATSC3/2019A1A2C1 (+DRM;Sonic;40VX700WDR;1.32.455;2.002;com.example.2019VX700;)");
  soup_message_headers_append (msg->request_headers, "accept",
      "application/octet-stream");
  soup_message_headers_append (msg->request_headers, "content-type",
      "application/octet-stream");

  soup_message_set_request (msg, content_type, SOUP_MEMORY_COPY,
      (const char *) req_body, (gsize) req_body_length);

  if (hdrs) {
    SoupMessageHeadersIter iter;
    const char *hname, *value;
    soup_message_headers_iter_init (&iter, hdrs);
    while (soup_message_headers_iter_next (&iter, &hname, &value))
      soup_message_headers_append (msg->request_headers, hname, value);
  }

  status = soup_session_send_message (session, msg);
  if (!SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
    goto ERROR;
  }

  if (!msg->response_body->length || !msg->response_body->data) {
    GST_ERROR_OBJECT (self,
        "Wrong response body length %" G_GOFFSET_FORMAT " or data %p",
        msg->response_body->length, msg->response_body->data);
    goto ERROR;
  }

  GST_ERROR_OBJECT (self, "response_data: %s ", msg->response_body->data);
  *response = g_memdup (msg->response_body->data, msg->response_body->length);
  if (!(*response)) {
    GST_ERROR_OBJECT (self, "failed to allocate memory");
    goto ERROR;
  }
  *response_length = msg->response_body->length;

  ret = TRUE;

ERROR:
  if (session) {
    soup_session_abort (session);
    g_object_unref (session);
  }

  if (msg)
    g_object_unref (msg);

  g_free (cert_buffer);

  if (client_cert)
    g_object_unref (client_cert);

  if (interaction)
    g_object_unref (interaction);

  return ret;
}

static gboolean
gst_cencdrm_widevine_drm_info (GstBaseDrm * basedrm,
    GstDRMSystemInfo * drm_system_info)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);

  GST_DEBUG_OBJECT (self, "drm info");

  if (!drm_system_info) {
    GST_ERROR_OBJECT (self, "NULL pointer drm_system_info");
    return FALSE;
  }

  drm_system_info->system_ids =
      g_list_append (drm_system_info->system_ids, g_strdup (WIDEVINE_UUID));
  drm_system_info->xml_node_name =
      g_memdup (&cencdrm_widevine_xml_node_name[0],
      sizeof (cencdrm_widevine_xml_node_name));

  if (!drm_system_info->system_ids || !drm_system_info->xml_node_name) {
    GST_ERROR_OBJECT (self,
        "Either system_ids %p, xml_node_name %p is a NULL pointer",
        drm_system_info->system_ids, drm_system_info->xml_node_name);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_cencdrm_widevine_start (GstBaseDrm * basedrm)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  GstDRMRightsErrorInfo *rights_error_info = &basedrm->rights_error_info;
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  GstDRMLicenseInfo *drm_license_info = &basedrm->drm_license_info;

  GST_DEBUG_OBJECT (self, "Widevine DRM for CENC open");

  drm_system_info->is_svp = TRUE;

  g_free (rights_error_info->drm_system_id);
  rights_error_info->drm_system_id = g_strdup (WIDEVINE_UUID);

  module_dile_crypto = g_module_open (LIB_DILE_CRYPTO_PATH, G_MODULE_BIND_LAZY);
  if (!module_dile_crypto) {
    GST_ERROR_OBJECT (self, "Failed to open a module: %s", g_module_error ());
    goto error_module;
  }
  if (!g_module_symbol (module_dile_crypto,
          "DILE_CRYPTO_ATSC3_Pearl_ReadPrivKey",
          (gpointer *) & DILE_CRYPTO_ATSC3_Pearl_ReadPrivKey))
    goto error_symbol;

  return TRUE;

error_symbol:
  GST_ERROR_OBJECT (self, "Failed to get a symbol: %s", g_module_error ());

error_module:
  g_module_close (module_dile_crypto);

  return FALSE;
}

static gboolean
gst_cencdrm_widevine_stop (GstBaseDrm * basedrm)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  GstDRMRightsErrorInfo *rights_error_info = &basedrm->rights_error_info;
  gchar group_license_url[100] = "";

  CDM_WRAPPER_CLASS *cdm_wrapper_ota =
      (CDM_WRAPPER_CLASS *) self->cdm_wrapper_ota;
  CDM_WRAPPER_CLASS *cdm_wrapper_ott =
      (CDM_WRAPPER_CLASS *) self->cdm_wrapper_ott;

  GST_DEBUG_OBJECT (self,
      "self->cdm_wrapper_ott : [%p] , self->cdm_wrapper_ota : [%p]",
      self->cdm_wrapper_ott, self->cdm_wrapper_ota);
  GST_DEBUG_OBJECT (self, "TestLog");

  drm_system_info->is_svp = FALSE;

  if (self->iv_bytes) {
    g_bytes_unref (self->iv_bytes);
    self->iv_bytes = NULL;
  }

  if (self->kid_bytes) {
    g_bytes_unref (self->kid_bytes);
    self->kid_bytes = NULL;
  }
  self->license_state = WIDEVINE_LICENSE_REFUSED;

  if (self->global_service_id) {
    g_free (self->global_service_id);
  }

  if (self->generated_request_message) {
    g_free (self->generated_request_message);
  }

  if (self->notify_info) {
    gst_cencdrm_widevine_free_notify_info (self->notify_info);
    self->notify_info = NULL;
  }

  if (service_count > 0) {
    service_count--;
    if (self->seamless_mode) {
      service_count--;
    }

    if (service_count > 0) {
      return TRUE;
    }
  }

  if (cdm_wrapper_ota && (service_count == 0)) {
    cdm_wrapper_ota->Close (cdm_wrapper_ota, self->group_license_id,
        self->group_license_id_length);
    memset (self->group_license_id, '\0', sizeof (self->group_license_id));
    self->group_license_id_length = 0;
  }

  if (cdm_wrapper_ott && (service_count == 0)) {
    cdm_wrapper_ott->Close (cdm_wrapper_ott, self->session_id,
        self->session_id_length);
    memset (self->session_id, '\0', sizeof (self->session_id));
    self->session_id_length = 0;
  }

  if (service_count == 0) {
    g_module_close (module_dile_crypto);

    g_free (self->cdm_wrapper_ota);
    g_free (self->cdm_wrapper_ott);
    g_free (self->content_id);
    g_free (self->init_data);
  }

  return TRUE;
}

static gboolean
gst_cencdrm_widevine_prepare_decrypt (GstBaseDrm * basedrm)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  GstDecryptInfo *decrypt_info = &basedrm->decrypt_info;
  guint8 *kid_data = NULL;
  guint32 kid_size = 0;
  Encryption_Scheme encryption_scheme = 0;
  CDM_WRAPPER_CLASS *cdm_wrapper;
  Status r = wv_success;

  GST_DEBUG_OBJECT (self, "prepare decrypt");

  if ((self->license_state == WIDEVINE_LICENSE_DEFERRED) ||
      (self->seamless_app)) {

    if (!gst_cencdrm_widevine_handle_notify_info (basedrm)) {
      GST_DEBUG_OBJECT (self, "failed to prepare decrypt");
      if (!self->cdm_wrapper_ota) {
        gst_cencdrm_widevine_send_error (basedrm);
        return FALSE;
      } else {
        self->seamless_app = FALSE;
      }

      GST_DEBUG_OBJECT (self, "self->seamless_app : %s ",
          self->seamless_app ? "TRUE" : "FALSE");
    }

    if (self->license_state != WIDEVINE_LICENSE_GRANTED) {
      GST_DEBUG_OBJECT (self, "fail to get license");
      return FALSE;
    }
  }

  if (self->is_svp) {
    GST_DEBUG_OBJECT (self, "select key");

    kid_data = (guint8 *) g_bytes_get_data (self->kid_bytes, &kid_size);
    if (!kid_data || kid_size == 0) {
      return FALSE;
    }

    if (decrypt_info->scheme_type == FOURCC_cenc
        || decrypt_info->scheme_type == FOURCC_cens) {
      encryption_scheme = wv_aesCtr;
    } else if (decrypt_info->scheme_type == FOURCC_cbc1
        || decrypt_info->scheme_type == FOURCC_cbcs) {
      encryption_scheme = wv_aesCbc;
    }

    if (self->done_ready_license) {
      if (self->cdm_wrapper_ott == NULL)
        return FALSE;
      cdm_wrapper = (CDM_WRAPPER_CLASS *) self->cdm_wrapper_ott;
    } else {
      if (self->cdm_wrapper_ota == NULL)
        return FALSE;
      cdm_wrapper = (CDM_WRAPPER_CLASS *) self->cdm_wrapper_ota;
    }

    if ((r = cdm_wrapper->SelectKey (cdm_wrapper, (gchar *) kid_data,
                kid_size, encryption_scheme)) != wv_success) {
      GST_ERROR_OBJECT (self, "failed to select key[%d]", r);
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
gst_cencdrm_widevine_set_kid (GstBaseDrm * basedrm, guint8 * kid_data,
    gsize kid_size)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);

  if (self->kid_bytes)
    g_bytes_unref (self->kid_bytes);
  self->kid_bytes = g_bytes_new (kid_data, kid_size);

  return TRUE;
}

static gboolean
gst_cencdrm_widevine_set_iv (GstBaseDrm * basedrm, guint8 * iv_data,
    gsize iv_size)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);

  if (self->iv_bytes)
    g_bytes_unref (self->iv_bytes);
  self->iv_bytes = g_bytes_new (iv_data, iv_size);

  return TRUE;
}

static gboolean
gst_cencdrm_widevine_decrypt (GstBaseDrm * basedrm,
    GstDecryptInfo * decrypt_info)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  InputBuffer input;
  OutputBuffer output;
  guint8 *iv_data, *kid_data;
  gsize iv_size, kid_size;
  guint32 subsample_count = decrypt_info->subsample_count;
  guint32 *subsample_info = decrypt_info->subsample_info;
  guint8 *src = decrypt_info->data;
  guint32 subsample_order = 1;
  guint32 bytes_decrypted = 0;
  guint32 block_offset = 0;
  guint32 i;
  CDM_WRAPPER_CLASS *cdm_wrapper;
  Status r = wv_success;

  GST_DEBUG_OBJECT (self, "decrypt");

  kid_data = (guint8 *) g_bytes_get_data (self->kid_bytes, &kid_size);
  if (kid_data) {
    input.key_id = kid_data;
    input.key_id_length = kid_size;
  }

  iv_data = (guint8 *) g_bytes_get_data (self->iv_bytes, &iv_size);
  if (iv_data) {
    input.iv = iv_data;
    input.iv_length = iv_size;
  }

  if (decrypt_info->scheme_type == FOURCC_cenc
      || decrypt_info->scheme_type == FOURCC_cens) {
    input.encryption_scheme = wv_aesCtr;
  } else if (decrypt_info->scheme_type == FOURCC_cbc1
      || decrypt_info->scheme_type == FOURCC_cbcs) {
    input.encryption_scheme = wv_aesCbc;
  }

  input.is_video = drm_system_info->is_svp;

  for (i = 0; i < subsample_count * 2; i += 2) {
    guint32 n_bytes_clear = subsample_info[i];
    guint32 n_bytes_encrypted = subsample_info[i + 1];

    src += n_bytes_clear;
    if (n_bytes_encrypted) {
      input.data = src;
      input.data_length = n_bytes_encrypted;

      if (decrypt_info->scheme_type == FOURCC_cbcs
          || decrypt_info->scheme_type == FOURCC_cens) {
        input.pattern.crypt_byte_block = decrypt_info->crypt_byte_block;
        input.pattern.skip_byte_block = decrypt_info->skip_byte_block;
      } else {
        input.pattern.crypt_byte_block = 0;
        input.pattern.skip_byte_block = 0;
      }

      if (subsample_order == 1) {
        input.first_subsample = TRUE;
        input.last_subsample = FALSE;
      } else if (subsample_order == subsample_count) {
        input.last_subsample = TRUE;
      } else {
        input.first_subsample = FALSE;
        input.last_subsample = FALSE;
      }
      input.block_offset = block_offset;

      output.data = (guint8 *) g_malloc (n_bytes_encrypted);
      output.data_length = n_bytes_encrypted;
      output.data_offset = 0;
      output.is_secure = drm_system_info->is_svp;

      if (self->done_ready_license) {
        if (self->cdm_wrapper_ott == NULL)
          return FALSE;
        cdm_wrapper = (CDM_WRAPPER_CLASS *) self->cdm_wrapper_ott;
      } else {
        if (self->cdm_wrapper_ota == NULL)
          return FALSE;
        cdm_wrapper = (CDM_WRAPPER_CLASS *) self->cdm_wrapper_ota;
      }

      if ((r = cdm_wrapper->Decrypt (cdm_wrapper, input, output)) != wv_success) {
        if (r == wv_noKey) {
          gst_cencdrm_widevine_send_error (basedrm);
          GST_ERROR_OBJECT (self, "there is no key, maybe keyId mismatch[%d]",
              r);
        }
        GST_ERROR_OBJECT (self, "failed to decrypt[%d]", r);
        g_free (output.data);
        return FALSE;
      }

      memcpy (src, output.data, n_bytes_encrypted);
      g_free (output.data);

      bytes_decrypted += n_bytes_encrypted;
      block_offset = (bytes_decrypted % 16);
      subsample_order++;
      src += n_bytes_encrypted;
    }
  }

  return TRUE;
}

static gboolean
gst_cencdrm_widevine_is_playback_allowed (GstBaseDrm * basedrm)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;

  GST_DEBUG_OBJECT (self, "is playback allowed [%d][%s]",
      self->license_state,
      self->license_state == 0 ? "GRANTED" :
      self->license_state == 1 ? "REFUSED" :
      self->license_state == 2 ? "DEFERRED" : "UNDEFINED");

  if (self->license_state == WIDEVINE_LICENSE_DEFERRED)
    return TRUE;

  return ((self->license_state == WIDEVINE_LICENSE_GRANTED) ? TRUE : FALSE);
}

static gboolean
gst_cencdrm_widevine_drm_init_for_group_license (GstBaseDrm * basedrm,
    guint8 * header, guint size)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  gchar group_license_url[100] = "";
  CDM_WRAPPER_CLASS *cdm_wrapper;
  Status r = wv_success;

  service_count++;

  g_return_val_if_fail (drm_system_info->group_license_url != NULL, FALSE);
  sscanf (drm_system_info->group_license_url, "%*[^/]%*[/]%[^.]",
      group_license_url);

  GST_DEBUG_OBJECT (self, "group_license_url: %s", group_license_url);

  if (ota_service) {
    cdm_wrapper = self->cdm_wrapper_ota = ota_service;
  } else {
    self->cdm_wrapper_ota =
        (CDM_WRAPPER_CLASS *) g_malloc (sizeof (CDM_WRAPPER_CLASS));
    cdm_wrapper = ota_service = self->cdm_wrapper_ota;
    InitializeCdm (cdm_wrapper, &callback);
    if ((r = cdm_wrapper->CreateCdm (cdm_wrapper, wv_ota)) != wv_success) {
      GST_DEBUG_OBJECT (self, "fail to widevine init[%d]", r);
      return FALSE;
    }
/*
    if ((r = CreateSession (wv_temporary, self->session_id,
                &self->session_id_length)) != wv_success) {
      GST_DEBUG_OBJECT (self, "fail to create session[%d]", r);
      return FALSE;
    }
*/
  }
//  GST_DEBUG_OBJECT (self, "session [%s][%d]", self->session_id,
//      self->session_id_length);

  if ((r = cdm_wrapper->Load (cdm_wrapper, group_license_url,
              strlen (group_license_url))) != wv_success) {
    GST_DEBUG_OBJECT (self, "fail to load[%d]", r);
    return FALSE;
  }

  memset (self->group_license_id, '\0', sizeof (self->group_license_id));
  memcpy (self->group_license_id, group_license_url,
      strlen (group_license_url));
  self->group_license_id_length = strlen (group_license_url);

  if ((r = cdm_wrapper->LoadEmbeddedKeys (cdm_wrapper, self->group_license_id,
              self->group_license_id_length, wv_cenc, (gchar *) header,
              size)) != wv_success) {
    GST_DEBUG_OBJECT (self, "fail to load embedded key[%d]", r);
    return FALSE;
  }

  self->license_type = LICENSE_OTA;
  self->license_state = WIDEVINE_LICENSE_GRANTED;

  return TRUE;
}

static gboolean
gst_cencdrm_widevine_drm_init_for_url_license (GstBaseDrm * basedrm,
    guint8 * header, guint size)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  Status r = wv_success;
  Session_Type session_type = wv_temporary;
  gchar persistent_session_id[256] = { 0, };
  guint32 persistent_session_id_size = 0;
  CDM_WRAPPER_CLASS *cdm_wrapper = NULL;

  service_count++;

  if (ott_service) {
    cdm_wrapper = self->cdm_wrapper_ott = ott_service;
  } else {
    self->cdm_wrapper_ott =
        (CDM_WRAPPER_CLASS *) g_malloc (sizeof (CDM_WRAPPER_CLASS));
    cdm_wrapper = ott_service = self->cdm_wrapper_ott;
    InitializeCdm (cdm_wrapper, &callback);

    if ((r = cdm_wrapper->CreateCdm (cdm_wrapper, wv_ott)) != wv_success) {
      GST_DEBUG_OBJECT (self, "fail to widevine init[%d]", r);
      return FALSE;
    }
  }

  if (self->content_id_size != 0) {
    session_type = wv_persistentLicense;
  } else {
    session_type = wv_temporary;
  }

  if (session_type == wv_persistentLicense) {
    if ((self->is_exist_license =
            cdm_wrapper->IsExistingLicense (cdm_wrapper,
                self->global_service_id, strlen (self->global_service_id),
                self->content_id, strlen (self->content_id),
                persistent_session_id, &persistent_session_id_size))) {

      GST_DEBUG_OBJECT (self, "is_exist_licene : [%s]",
          self->is_exist_license ? "true" : "false");
      if ((r = cdm_wrapper->Load (cdm_wrapper, persistent_session_id,
                  persistent_session_id_size)) != wv_success) {
        GST_DEBUG_OBJECT (self, "fail to load[%d]", r);
        return FALSE;
      }

      memset (self->session_id, 0, sizeof (self->session_id));
      memcpy (self->session_id, persistent_session_id,
          persistent_session_id_size);
      self->session_id_length = persistent_session_id_size;

      r = cdm_wrapper->LoadEmbeddedKeys (cdm_wrapper, self->session_id,
          self->session_id_length, wv_cenc, (gchar *) header, size);
      if (r == wv_success || r == wv_unexpectedError) {
        self->done_ready_license = TRUE;
        self->license_state = WIDEVINE_LICENSE_GRANTED;
        return TRUE;
      }

      GST_DEBUG_OBJECT (self, "fail to load embedded key[%d]", r);
      return FALSE;
    }
  }

  if (self->session_id_length == 0) {
    if ((r = cdm_wrapper->CreateSession (cdm_wrapper, session_type,
                self->session_id, &self->session_id_length)) != wv_success) {
      GST_DEBUG_OBJECT (self, "fail to create session[%d]", r);
      return FALSE;
    }
  }

  GST_DEBUG_OBJECT (self, "session [%s][%d]", self->session_id,
      self->session_id_length);

  gst_cencdrm_self = self;

  if ((r = cdm_wrapper->GenerateRequestMessage (cdm_wrapper, self->session_id,
              self->session_id_length, wv_cenc, (gchar *) header,
              size)) != wv_success) {
    GST_DEBUG_OBJECT (self, "GenerateRequestMessage by wv_cenc [%d]", r);
    gst_cencdrm_self = NULL;
    return FALSE;
  }

  self->license_type = LICENSE_OTT_RMP;
  gst_cencdrm_self = NULL;
  return TRUE;

}

static gboolean
gst_cencdrm_widevine_drm_init_for_content_license (GstBaseDrm * basedrm,
    guint8 * header, guint size)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  Status r = wv_success;
  Session_Type session_type = wv_temporary;
  gchar persistent_session_id[256] = { 0, };
  guint32 persistent_session_id_size = 0;
  CDM_WRAPPER_CLASS *cdm_wrapper;

  service_count++;

  if (ott_service) {
    cdm_wrapper = self->cdm_wrapper_ott = ott_service;
  } else {
    self->cdm_wrapper_ott =
        (CDM_WRAPPER_CLASS *) g_malloc (sizeof (CDM_WRAPPER_CLASS));
    cdm_wrapper = ott_service = self->cdm_wrapper_ott;
    InitializeCdm (cdm_wrapper, &callback);

    if ((r = cdm_wrapper->CreateCdm (cdm_wrapper, wv_ott)) != wv_success) {
      GST_DEBUG_OBJECT (self, "fail to widevine init[%d]", r);
      return FALSE;
    }
  }

  if (self->content_id_size != 0) {
    session_type = wv_persistentLicense;
  } else {
    session_type = wv_temporary;
  }

  if (session_type == wv_persistentLicense) {
    if ((self->is_exist_license =
            cdm_wrapper->IsExistingLicense (cdm_wrapper,
                self->global_service_id, strlen (self->global_service_id),
                self->content_id, strlen (self->content_id),
                persistent_session_id, &persistent_session_id_size))) {

      if ((r = cdm_wrapper->Load (cdm_wrapper, persistent_session_id,
                  persistent_session_id_size)) != wv_success) {
        GST_DEBUG_OBJECT (self, "fail to load[%d]", r);
        return FALSE;
      }

      memset (self->session_id, 0, sizeof (self->session_id));
      memcpy (self->session_id, persistent_session_id,
          persistent_session_id_size);
      self->session_id_length = persistent_session_id_size;

      r = cdm_wrapper->LoadEmbeddedKeys (cdm_wrapper, self->session_id,
          self->session_id_length, wv_cenc, (gchar *) header, size);
      if (r == wv_success || r == wv_unexpectedError) {
        self->done_ready_license = TRUE;
        self->license_state = WIDEVINE_LICENSE_GRANTED;
        return TRUE;
      }

      GST_DEBUG_OBJECT (self, "fail to load embedded key[%d]", r);
      return FALSE;
    }
  }

  if (self->session_id_length == 0) {
    if ((r = cdm_wrapper->CreateSession (cdm_wrapper, session_type,
                self->session_id, &self->session_id_length)) != wv_success) {
      GST_DEBUG_OBJECT (self, "fail to create session[%d]", r);
      return FALSE;
    }
  }

  GST_DEBUG_OBJECT (self, "session [%s][%d]", self->session_id,
      self->session_id_length);

  gst_cencdrm_self = self;

  if ((r = cdm_wrapper->GenerateRequestMessage (cdm_wrapper, self->session_id,
              self->session_id_length, wv_cenc, (gchar *) header,
              size)) != wv_success) {
    GST_DEBUG_OBJECT (self, "GenerateRequestMessage by wv_cenc [%d]", r);
    gst_cencdrm_self = NULL;
    return FALSE;
  }

  self->license_type = LICENSE_OTT_APP;
  gst_cencdrm_self = NULL;

  return TRUE;
}

static void
gst_cencdrm_widevine_send_error (GstBaseDrm * basedrm)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  GST_DEBUG_OBJECT (self, "send drm decrypt fail error");

  GST_ELEMENT_ERROR (GST_ELEMENT_CAST (basedrm), STREAM, DECRYPT_NOKEY,
      ("Encrypted Content"), (NULL));
}

static void
get_seamless_license (GstBaseDrm * basedrm)
{

  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  GstDRMLicenseInfo *drm_license_info = &basedrm->drm_license_info;
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  guint32 retry_count = 0;
  gboolean ret = FALSE;

  self->seamless_mode = TRUE;

  if (self->license_url) {
    ret =
        gst_cencdrm_widevine_drm_init_for_url_license (basedrm, self->init_data,
        self->init_data_size);
    if (!ret)
      goto release;

  } else {
    if (self->content_id) {
      ret =
          gst_cencdrm_widevine_drm_init_for_content_license (basedrm,
          self->init_data, self->init_data_size);
      if (!ret)
        goto release;
    }
  }

  if (self->done_ready_license)
    goto release;

  GstCencDrmWidevineNotifyInfo *notify_info = self->notify_info;

  const gulong micro_seconds = 0x00000005;

  while (!self->generated_request_message && (retry_count < 10)) {
    GST_DEBUG_OBJECT (self, " loop for get license challenge data : %p", self);
    g_usleep (micro_seconds);
    retry_count++;
  }

  if (self->license_type == LICENSE_OTT_APP) {
    self->seamless_app = TRUE;
  }

  if (!gst_cencdrm_widevine_get_license_challenge (basedrm, drm_license_info)) {
    goto release;
  }

  if (!gst_cencdrm_widevine_request_license (basedrm, drm_license_info)) {
    goto release;
  }

  if (!gst_cencdrm_widevine_store_license (basedrm, drm_license_info)) {
    goto release;
  }

release:

  if (!ret)
    gst_cencdrm_widevine_send_error (basedrm);

  g_thread_exit (0);
  return;
}

static gboolean
gst_cencdrm_widevine_drm_init (GstBaseDrm * basedrm, guint8 * header,
    guint size)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  gchar group_license_url[100] = "";
  gboolean ret = FALSE;
  Status r = wv_success;

  GST_DEBUG_OBJECT (self, "drm init");

  g_return_val_if_fail (header != NULL, FALSE);

  self->init_data = (guint8 *) g_malloc (size);
  memcpy (self->init_data, header, size);
  self->init_data_size = size;

  GST_DEBUG_OBJECT (self,
      "network[%s] license_type[%s] group[%s] url[%s] content[%s]",
      self->network_connectivity ? "true" : "false",
      drm_system_info->license_type, drm_system_info->group_license_url,
      drm_system_info->license_url, drm_system_info->content_id);

  if (!drm_system_info->license_type) {
    GST_DEBUG_OBJECT (self, "It doesn't have any drm license info");
    ret = gst_cencdrm_widevine_is_playback_allowed (basedrm);
    goto release;
  }

  GST_DEBUG_OBJECT (self, "drm_system_info->is_svp : %s",
      drm_system_info->is_svp ? "TRUE" : "FALSE");
  self->is_svp = drm_system_info->is_svp;

  if (drm_system_info->system_ids->data) {
    self->system_id = (gchar *) g_strdup (drm_system_info->system_ids->data);
  }

  if (drm_system_info->license_url) {
    if (self->license_url == NULL) {
      self->license_url_size = strlen (drm_system_info->license_url);
      self->license_url = (gchar *) g_malloc (self->license_url_size + 1);
      if (self->license_url == NULL) {
        ret = FALSE;
        goto release;
      }

      strcpy (self->license_url, drm_system_info->license_url);
      GST_DEBUG_OBJECT (self,
          "drm_init_license_url copy : self->license_url: self->license_url : [%s], self->license_url_size : [%d]",
          self->license_url, self->license_url_size);
    }

    char *ptr = strstr (drm_system_info->license_url, "content_id=");

    if (ptr) {
      ptr = ptr + strlen ("content_id=");

      if (strlen (ptr) == 0) {
        ret = FALSE;
        goto release;
      }

      self->content_id_size = strlen (ptr);
      if (self->content_id == NULL) {
        self->content_id = (gchar *) g_malloc (self->content_id_size + 1);
        if (!self->content_id) {
          ret = FALSE;
          goto release;
        }
      }

      strcpy (self->content_id, ptr);
      GST_DEBUG_OBJECT (self,
          "content_id: self->content_id : [%s], content_id_size : [%d]",
          self->content_id, self->content_id_size);
    } else {
      ret = FALSE;
      goto release;
    }

    bool support_seamless = TRUE;

    if (self->network_connectivity ||
        (!self->network_connectivity && !drm_system_info->group_license_url)) {
      if (drm_system_info->group_license_url) {
        if (support_seamless == TRUE) {
          ret =
              gst_cencdrm_widevine_drm_init_for_group_license (basedrm, header,
              size);
          if (ret) {
            /* get_seamless_license:
               init cdm and generating request message , chaecking of existing license */
            self->thread =
                g_thread_new ("get_seamless_license", get_seamless_license,
                basedrm);
          } else {
            /* Get OTT indivisulized license via LA URL */
            ret =
                gst_cencdrm_widevine_drm_init_for_url_license (basedrm, header,
                size);
          }
        } else {
          ret =
              gst_cencdrm_widevine_drm_init_for_url_license (basedrm, header,
              size);
        }
      } else {
        /* Get OTT indivisulized license via LA URL */
        ret =
            gst_cencdrm_widevine_drm_init_for_url_license (basedrm, header,
            size);
      }
    } else {
      if (drm_system_info->group_license_url) {
        // Get group license
        ret =
            gst_cencdrm_widevine_drm_init_for_group_license (basedrm, header,
            size);
      } else {
        if (drm_system_info->content_id) {
          // Maybe use NRT
          ret = FALSE;
        } else {
          /* Notify error "Encrypted Content" to gibbs */
          ret =
              gst_cencdrm_widevine_drm_init_for_url_license (basedrm, header,
              size);
        }
      }
    }
  } else {
    if (drm_system_info->group_license_url) {
      // Get group license
      ret =
          gst_cencdrm_widevine_drm_init_for_group_license (basedrm, header,
          size);

      if (drm_system_info->content_id && self->network_connectivity) {
        char *ptr = strstr (drm_system_info->content_id, "contentId/");
        if (ptr) {
          ptr = ptr + strlen ("contentId/");
          if (strlen (ptr) == 0) {
            ret = FALSE;
            goto release;
          }

          self->content_id_size = strlen (ptr);
          if (self->content_id == NULL) {
            self->content_id = (gchar *) g_malloc (self->content_id_size + 1);
            if (!self->content_id) {
              ret = FALSE;
              goto release;
            }
          }

          strcpy (self->content_id, ptr);
        }

        if (ret) {
          self->thread =
              g_thread_new ("get_seamless_license", get_seamless_license,
              basedrm);
        } else {
          /* Get OTT indivisulized license via A/344 App */
          ret =
              gst_cencdrm_widevine_drm_init_for_content_license (basedrm,
              header, size);
        }
      }
    } else {
      if (drm_system_info->content_id) {
        if (self->network_connectivity) {
          char *ptr = strstr (drm_system_info->content_id, "contentId/");
          if (ptr) {
            ptr = ptr + strlen ("contentId/");

            if (strlen (ptr) == 0) {
              ret = FALSE;
              goto release;
            }

            self->content_id_size = strlen (ptr);
            if (self->content_id == NULL) {
              self->content_id = (gchar *) g_malloc (self->content_id_size + 1);
              if (!self->content_id) {
                ret = FALSE;
                goto release;
              }
            }
            strcpy (self->content_id, ptr);
            GST_DEBUG_OBJECT (self, "content_id_size : [%d]",
                self->content_id_size);
          }
          // Get OTT indivisulized license via A/344 App
          ret =
              gst_cencdrm_widevine_drm_init_for_content_license (basedrm,
              header, size);
        } else {
          // Maybe use NRT
          ret = FALSE;
        }
      } else {
        // Notify error "Encrypted Content" to gibbs
        ret = FALSE;
      }
    }
  }

release:

  if (!ret)
    gst_cencdrm_widevine_send_error (basedrm);

  return ret;
}

static gboolean
gst_cencdrm_widevine_get_license_challenge (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;

  GST_DEBUG_OBJECT (self, "get license challenge");

  if (drm_license_info == NULL)
    return FALSE;

  if (self->license_url && (self->license_type == LICENSE_OTT_RMP)) {
    drm_license_info->url_length = strlen (self->license_url);
    drm_license_info->url = g_strdup (self->license_url);

    drm_license_info->challenge_length = self->generated_message_length;
    drm_license_info->challenge = self->generated_request_message;
    self->generated_request_message = NULL;
    self->generated_message_length = 0;
  } else if (self->content_id && (self->license_type == LICENSE_OTT_APP)) {
    drm_license_info->url_length = strlen (self->content_id);
    drm_license_info->url = g_strdup (self->content_id);

    drm_license_info->challenge_length = self->generated_message_length;
    drm_license_info->challenge = self->generated_request_message;
    self->generated_request_message = NULL;
    self->generated_message_length = 0;
  }

  return TRUE;
}

static gboolean
gst_cencdrm_widevine_request_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  SoupMessageHeaders *hdrs = NULL;
  gboolean ret = FALSE;
  gchar persistent_session_id[256] = { 0, };
  guint32 persistent_session_id_size = 0;

  GST_DEBUG_OBJECT (self, "request license");
//  hdrs = soup_message_headers_new (SOUP_MESSAGE_HEADERS_REQUEST);
//  soup_message_headers_append (hdrs, "SOAPAction",
//      "\"http://drm-proxy-service.atsc3-modelmarket.com/wv/license?broadcaster_id=LHR-yotta&amp;content_id=cid_profile1\"");
//  soup_message_headers_append (hdrs, "content-id", "\"cid_profile1\"");

  GST_DEBUG_OBJECT (self, "content_id : [%s]", self->content_id);

  GST_DEBUG_OBJECT (self, "is_exist_licene : [%s]",
      self->is_exist_license ? "true" : "false");
  GST_DEBUG_OBJECT (self, "has_usable_key : [%s]",
      self->has_usable_key ? "true" : "fasle");
  GST_DEBUG_OBJECT (self, "self->license_type : %d ", self->license_type);
  if ((self->content_id_size != 0) && self->is_exist_license
      && self->has_usable_key) {
    self->license_state = WIDEVINE_LICENSE_GRANTED;
    ret = TRUE;
    goto beach;
  }

  if (self->license_url && (self->license_type == LICENSE_OTT_RMP)) {
    GST_DEBUG_OBJECT (self, "license_url: %s", self->license_url);
    if (self->network_connectivity) {
      if (gst_cencdrm_widevide_send_http_request (self,
              (const gchar *) drm_license_info->url,
              drm_license_info->url_length, drm_license_info->challenge,
              drm_license_info->challenge_length,
              (gchar **) & drm_license_info->response,
              &drm_license_info->response_length, "application/octet-stream",
              hdrs)) {
        GST_DEBUG_OBJECT (self, "succeeded to get license");
        ret = TRUE;
        goto beach;
      }
    }

    GST_DEBUG_OBJECT (self, "failed to get license");
    if (self->content_id && !drm_system_info->group_license_url) {
      GST_DEBUG_OBJECT (self, "license request notify");
      ret =
          gst_cencdrm_widevine_save_notify_info (basedrm, "licenseRequest",
          drm_license_info->challenge, drm_license_info->challenge_length, -1);
      self->license_state = WIDEVINE_LICENSE_DEFERRED;
    } else if (drm_system_info->group_license_url) {
      GST_DEBUG_OBJECT (self, "fallback group_license");

      if (self->cdm_wrapper_ota != NULL)
        goto beach;

      ret =
          gst_cencdrm_widevine_drm_init_for_group_license (basedrm,
          self->init_data, self->init_data_size);
    } else {
      ret = FALSE;
      goto beach;
    }
  } else if (self->content_id && (self->license_type == LICENSE_OTT_APP)) {
    GST_DEBUG_OBJECT (self, "drm_license_info->challenge: %s",
        drm_license_info->challenge);
    ret =
        gst_cencdrm_widevine_save_notify_info (basedrm, "licenseRequest",
        drm_license_info->challenge, drm_license_info->challenge_length, -1);
    if (!self->seamless_app)
      self->license_state = WIDEVINE_LICENSE_DEFERRED;
  } else {
    ret = TRUE;
  }

beach:
  if (hdrs)
    soup_message_headers_free (hdrs);

  if (!ret)
    gst_cencdrm_widevine_send_error (basedrm);
  return ret;
}

static gboolean
gst_cencdrm_widevine_store_license (GstBaseDrm * basedrm,
    GstDRMLicenseInfo * drm_license_info)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  Status r = wv_success;
  gchar persistent_session_id[256] = { 0, };
  guint32 persistent_session_id_size = 0;
  CDM_WRAPPER_CLASS *cdm_wrapper = (CDM_WRAPPER_CLASS *) self->cdm_wrapper_ott;

  if (cdm_wrapper == NULL)
    return FALSE;

  GST_DEBUG_OBJECT (self, "store license");
  GST_DEBUG_OBJECT (self, "content_id : [%s]", self->content_id);
  GST_DEBUG_OBJECT (self, "self->license_type : [%d]", self->license_type);
  if (self->content_id_size && self->is_exist_license && self->has_usable_key) {
    self->license_state = WIDEVINE_LICENSE_GRANTED;
    return TRUE;
  }

  GST_DEBUG_OBJECT (self, "self : [%p]", self);
  if (self->license_url) {
    GST_DEBUG_OBJECT (self, "drm_license_info->response : [%s]",
        drm_license_info->response);
    if (drm_license_info->response) {
      if ((r = cdm_wrapper->Update (cdm_wrapper, self->session_id,
                  self->session_id_length, (gchar *) drm_license_info->response,
                  drm_license_info->response_length, self->global_service_id,
                  strlen (self->global_service_id), self->content_id,
                  strlen (self->content_id))) != wv_success) {
        GST_DEBUG_OBJECT (self, "fail to Update[%d]", r);
        gst_cencdrm_widevine_send_error (basedrm);
        return FALSE;
      }
      self->license_state = WIDEVINE_LICENSE_GRANTED;
      self->done_ready_license = TRUE;
    }
  } else if (self->content_id && (self->license_type == LICENSE_OTT_APP)) {
    /* wait until app call update with timeout */
  }

  return TRUE;
}

static GstBuffer *
gst_cencdrm_widevine_get_key_info (GstBaseDrm * basedrm)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  GstDecryptInfo *decrypt_info = &basedrm->decrypt_info;
  GstByteWriter bw;
  GstBuffer *key_info;
  guint8 *kid_data = NULL;
  guint32 kid_size = 0, oec_session_id = 0, encryption_scheme = 0;
  CDM_WRAPPER_CLASS *cdm_wrapper;
  Status r = wv_success;

  GST_DEBUG_OBJECT (self, "get key info");

  if (!drm_system_info->is_svp) {
    GST_DEBUG_OBJECT (self, "fail to drm_system_info->is_svp");
    return NULL;
  }

  gst_byte_writer_init (&bw);

  kid_data = (guint8 *) g_bytes_get_data (self->kid_bytes, &kid_size);
  if (!kid_data || kid_size == 0) {
    GST_DEBUG_OBJECT (self, "fail to !kid_data || kid_size == %d", kid_size);

    return FALSE;
  }

  if (!gst_byte_writer_put_data (&bw, kid_data, kid_size)) {
    GST_ERROR_OBJECT (self, "failed to put kid");
    return NULL;
  }

  if (!gst_byte_writer_put_uint32_le (&bw, (guint32) kid_size)) {
    GST_DEBUG_OBJECT (self,
        "fail to gst_byte_writer_put_uint32_le (&bw, (guint32) kid_size) == %d",
        kid_size);
    return NULL;
  }

  if (self->done_ready_license) {
    if (self->cdm_wrapper_ott == NULL)
      return FALSE;
    cdm_wrapper = (CDM_WRAPPER_CLASS *) self->cdm_wrapper_ott;
  } else {
    if (self->cdm_wrapper_ota == NULL)
      return FALSE;
    cdm_wrapper = (CDM_WRAPPER_CLASS *) self->cdm_wrapper_ota;
  }

  if ((r = cdm_wrapper->GetSvpInfo (cdm_wrapper, (gchar *) kid_data, kid_size,
              &oec_session_id)) != wv_success) {
    GST_DEBUG_OBJECT (self, "fail to get oec session id[%d]", r);
    return FALSE;
  }

  if (!gst_byte_writer_put_uint32_le (&bw, (guint32) oec_session_id)) {
    GST_DEBUG_OBJECT (self, "fail to oec_session_id");

    return NULL;
  }

  if (decrypt_info->scheme_type == FOURCC_cenc
      || decrypt_info->scheme_type == FOURCC_cens) {
    encryption_scheme = 0;
  } else if (decrypt_info->scheme_type == FOURCC_cbc1
      || decrypt_info->scheme_type == FOURCC_cbcs) {
    encryption_scheme = 1;
  }
  if (!gst_byte_writer_put_uint32_le (&bw, (guint32) encryption_scheme)) {

    GST_DEBUG_OBJECT (self, "fail to encryption_scheme");
    return NULL;
  }

  key_info = gst_byte_writer_reset_and_get_buffer (&bw);

  return key_info;
}

static gboolean
gst_cencdrm_widevine_restore_original_pssh (GstBaseDrm * basedrm,
    guint8 * original_pssh, guint original_pssh_size, guint8 ** data,
    guint * data_size)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);

  GST_DEBUG_OBJECT (self, "restore original pssh");

  if (!original_pssh || original_pssh_size == 0) {
    GST_ERROR_OBJECT (basedrm, "failed to restore original pssh");
    return FALSE;
  }

  *data = original_pssh;
  *data_size = original_pssh_size;

  return TRUE;
}

static gboolean
gst_cencdrm_widevine_set_video_info (GstBaseDrm * basedrm,
    guint32 width, guint32 height)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  CDM_WRAPPER_CLASS *cdm_wrapper;
  Status r = wv_success;

  GST_DEBUG_OBJECT (self, "set video resolution info");

  if (width == 0 || height == 0)
    return FALSE;

  if (self->cdm_wrapper_ott != NULL) {
    if (strlen (self->session_id) == 0 || self->session_id_length == 0)
      return FALSE;

    cdm_wrapper = (CDM_WRAPPER_CLASS *) self->cdm_wrapper_ott;
    if ((r = cdm_wrapper->SetVideoResolution (cdm_wrapper, self->session_id,
                self->session_id_length, width, height)) != wv_success) {
      GST_DEBUG_OBJECT (self, "fail to SetVideoResolution[%d]", r);
      return FALSE;
    }
  }
  if (self->cdm_wrapper_ota != NULL) {
    if (strlen (self->group_license_id) == 0
        || self->group_license_id_length == 0)
      return FALSE;
    cdm_wrapper = (CDM_WRAPPER_CLASS *) self->cdm_wrapper_ota;
    if ((r = cdm_wrapper->SetVideoResolution (cdm_wrapper,
                self->group_license_id, self->group_license_id_length, width,
                height)) != wv_success) {
      GST_DEBUG_OBJECT (self, "fail to SetVideoResolution[%d]", r);
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
get_cencdrm_widevine_check_response_pair (GstCencDrmWidevine * self,
    const gchar * system_id, const gchar * service, const gchar * drm_msg_type,
    const guint8 * kid, guint32 kid_length, const guint8 * session_id,
    guint32 session_id_length, gint32 request_id)
{
  GstCencDrmWidevineNotifyInfo *notify_info = self->notify_info;

  if (!notify_info) {
    GST_DEBUG_OBJECT (self, "Already handled message");
    return FALSE;
  }

  if (g_strcmp0 (drm_msg_type, "generateRequest") == 0) {
    return TRUE;
  } else if (g_strcmp0 (drm_msg_type, "update") == 0) {
    if (g_strcmp0 (notify_info->drm_msg_type, "licenseRequest") != 0)
      return FALSE;
  } else if (g_strcmp0 (drm_msg_type, "individualizationResponse") == 0) {
    if (g_strcmp0 (notify_info->drm_msg_type, "individualizationRequest") != 0)
      return FALSE;
  } else if (g_strcmp0 (drm_msg_type, "serviceCertificateResponse") == 0) {
    if (g_strcmp0 (notify_info->drm_msg_type, "serviceCertificateRequest") != 0)
      return FALSE;
  } else {
    GST_DEBUG_OBJECT (self, "Invalid message type[%s]", drm_msg_type);
    return FALSE;
  }

  if (g_strcmp0 (system_id, notify_info->system_id) != 0
//      || g_strcmp0 (service, notify_info->service) != 0
//      || kid_length != notify_info->kid_length
//      || memcmp (kid, notify_info->kid, kid_length) != 0
      || session_id_length != notify_info->session_id_length
      || memcmp (session_id, notify_info->session_id, session_id_length) != 0)
    return FALSE;

  return TRUE;
}

static gboolean
gst_cencdrm_widevine_operation_result (GstBaseDrm * basedrm,
    gboolean returnValue, struct json_object *message, gint32 error_code,
    const gchar * error_message, gint32 request_id)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  GstStructure *encrypted_structure;
  GstMessage *encrypted_message;
  struct json_object *json_result = NULL;
  struct json_object *json_message = NULL;
  struct json_object *json_error = NULL;

  GST_DEBUG_OBJECT (self, "gst_cencdrm_widevine_operation_result");

  json_result = json_object_new_object ();
  json_object_object_add (json_result, "requestId",
      json_object_new_int (request_id));

  if (returnValue) {
    if (message != NULL) {
      json_message = json_object_new_object ();
      json_object_object_add (json_message, "message", message);
      json_object_object_add (json_result, "result", json_message);
    } else
      json_object_object_add (json_result, "result", json_object_new_object ());
  } else {
    json_error = json_object_new_object ();
    json_object_object_add (json_error, "code",
        json_object_new_int (error_code));
    json_object_object_add (json_error, "message",
        json_object_new_string (error_message));
    json_object_object_add (json_result, "error", json_error);
  }

  encrypted_structure = gst_structure_new ("drm-result",
      "result", G_TYPE_STRING, json_object_to_json_string (json_result), NULL);

  GST_DEBUG_OBJECT (self,
      "Posting message to application: %" GST_PTR_FORMAT, encrypted_structure);

  encrypted_message = gst_message_new_element (GST_OBJECT_CAST (self),
      encrypted_structure);

  gst_element_post_message (GST_ELEMENT_CAST (basedrm), encrypted_message);

  if (json_result)
    json_object_put (json_result);

  return TRUE;
}

static gboolean
gst_cencdrm_widevine_generateRequest_operation (GstElement * element,
    const gchar * system_id, const gchar * service, const gchar * drm_msg_type,
    const guint8 * kid, guint32 kid_length, const guint8 * session_id,
    guint32 session_id_length, const guint8 * drm_data, guint32 drm_data_length,
    gint32 request_id)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (element);
  Status r = wv_success;
  struct json_object *message = NULL;
  gchar *encoded_kid = NULL;
  gchar *encoded_drm_data = NULL;
  CDM_WRAPPER_CLASS *cdm_wrapper = (CDM_WRAPPER_CLASS *) self->cdm_wrapper_ott;

  if (!get_cencdrm_widevine_check_response_pair (self, system_id, service,
          drm_msg_type, kid, kid_length, session_id, session_id_length,
          request_id))
    return FALSE;

  gst_cencdrm_self = self;
  if ((r = cdm_wrapper->GenerateRequestMessage (cdm_wrapper,
              (gchar *) session_id, session_id_length, wv_cenc,
              (gchar *) drm_data, drm_data_length)) != wv_success
      || self->generated_request_message == NULL) {
    GST_DEBUG_OBJECT (self, "GenerateRequestMessage by wv_cenc [%d]", r);
    gst_cencdrm_self = NULL;

    gst_cencdrm_widevine_operation_result ((GstBaseDrm *) element,
        FALSE, NULL, -100, "EME TypeError", request_id);
    return FALSE;
  }
  gst_cencdrm_self = NULL;

  encoded_kid = g_base64_encode ((const guchar *) kid, kid_length);
  encoded_drm_data =
      g_base64_encode ((const guchar *) self->generated_request_message,
      self->generated_message_length);

  message = json_object_new_object ();
  json_object_object_add (message, "kid", json_object_new_string (encoded_kid));
  json_object_object_add (message, "drmSessionId",
      json_object_new_string (session_id));
  json_object_object_add (message, "drmMsgType",
      json_object_new_string ("licenseRequest"));
  json_object_object_add (message, "drmData",
      json_object_new_string (encoded_drm_data));

  gst_cencdrm_widevine_operation_result ((GstBaseDrm *) element,
      TRUE, message, 0, NULL, request_id);

  if (self->generated_request_message) {
    g_free (self->generated_request_message);
    self->generated_request_message = NULL;
    self->generated_message_length = 0;
  }

  return TRUE;
}

static gboolean
gst_cencdrm_widevine_update_operation (GstElement * element,
    const gchar * system_id, const gchar * service, const gchar * drm_msg_type,
    const guint8 * kid, guint32 kid_length, const guint8 * session_id,
    guint32 session_id_length, const guint8 * drm_data, guint32 drm_data_length,
    gint32 request_id)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (element);
  Status r = wv_success;
  gchar persistent_session_id[256] = { 0, };
  guint32 persistent_session_id_size = 0;
  CDM_WRAPPER_CLASS *cdm_wrapper = (CDM_WRAPPER_CLASS *) self->cdm_wrapper_ott;

  if (!get_cencdrm_widevine_check_response_pair (self, system_id, service,
          drm_msg_type, kid, kid_length, session_id, session_id_length,
          request_id))
    return FALSE;

  self->is_response = TRUE;

  GstCencDrmWidevineNotifyInfo *notify_info = self->notify_info;

  GST_DEBUG_OBJECT (self, "content_id : [%s]", self->content_id);

  if ((self->content_id_size != 0) && self->is_exist_license
      && self->has_usable_key) {
    self->license_state = WIDEVINE_LICENSE_GRANTED;
    return TRUE;
  }

  if ((r = cdm_wrapper->Update (cdm_wrapper, (gchar *) session_id,
              session_id_length,
              (gchar *) drm_data,
              drm_data_length,
              self->global_service_id,
              strlen (self->global_service_id),
              self->content_id, strlen (self->content_id))) != wv_success) {
    GST_DEBUG_OBJECT (self, "fail to Update[%d]", r);

    gst_cencdrm_widevine_operation_result ((GstBaseDrm *) element,
        FALSE, NULL, -100, "EME TypeError", request_id);

    return TRUE;
  }

  self->done_ready_license = TRUE;
  self->license_state = WIDEVINE_LICENSE_GRANTED;

  gst_cencdrm_widevine_operation_result ((GstBaseDrm *) element,
      TRUE, NULL, 0, NULL, request_id);

  return TRUE;
}

static gboolean
gst_cencdrm_widevine_individualizationResponse_operation (GstElement * element,
    const gchar * system_id, const gchar * service, const gchar * drm_msg_type,
    const guint8 * kid, guint32 kid_length, const guint8 * session_id,
    guint32 session_id_length, const guint8 * drm_data, guint32 drm_data_length,
    gint32 request_id)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (element);
  Status r = wv_success;

  if (!get_cencdrm_widevine_check_response_pair (self, system_id, service,
          drm_msg_type, kid, kid_length, session_id, session_id_length,
          request_id))
    return FALSE;

  self->is_response = TRUE;

  // anything

  return TRUE;
}

static gboolean
gst_cencdrm_widevine_serviceCertificateResponse_operation (GstElement * element,
    const gchar * system_id, const gchar * service, const gchar * drm_msg_type,
    const guint8 * kid, guint32 kid_length, const guint8 * session_id,
    guint32 session_id_length, const guint8 * drm_data, guint32 drm_data_length,
    gint32 request_id)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (element);
  Status r = wv_success;

  if (!get_cencdrm_widevine_check_response_pair (self, system_id, service,
          drm_msg_type, kid, kid_length, session_id, session_id_length,
          request_id))
    return FALSE;

  self->is_response = TRUE;

  // anything

  return TRUE;
}

/* convert a DRM KeyId (UUID) in string representation to bytes representation */
static guint8 *
gst_cencdrm_widevine_uuid_string_to_bytes (const gchar * uuid_string)
{
  gsize uuid_bytes_length = KEY_ID_SIZE;
  guint8 *uuid_bytes = (guint8 *) malloc (uuid_bytes_length);

  uuid_bytes[0] =
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[0]) << 4 |
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[1]);
  uuid_bytes[1] =
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[2]) << 4 |
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[3]);
  uuid_bytes[2] =
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[4]) << 4 |
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[5]);
  uuid_bytes[3] =
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[6]) << 4 |
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[7]);
  uuid_bytes[4] =
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[9]) << 4 |
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[10]);
  uuid_bytes[5] =
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[11]) << 4 |
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[12]);
  uuid_bytes[6] =
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[14]) << 4 |
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[15]);
  uuid_bytes[7] =
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[16]) << 4 |
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[17]);
  uuid_bytes[8] =
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[19]) << 4 |
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[20]);
  uuid_bytes[9] =
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[21]) << 4 |
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[22]);
  uuid_bytes[10] =
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[24]) << 4 |
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[25]);
  uuid_bytes[11] =
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[26]) << 4 |
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[27]);
  uuid_bytes[12] =
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[28]) << 4 |
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[29]);
  uuid_bytes[13] =
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[30]) << 4 |
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[31]);
  uuid_bytes[14] =
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[32]) << 4 |
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[33]);
  uuid_bytes[15] =
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[34]) << 4 |
      gst_cencdrm_widevine_hex_char_to_byte (uuid_string[35]);

  return uuid_bytes;
}

static void
gst_cencdrm_widevine_set_context (GstElement * element, GstContext * context)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (element);
  gboolean ret = FALSE;
  const GstStructure *s = NULL;
  const gchar *systemId = NULL;
  const gchar *service = NULL;
  const gchar *kid = NULL;
  const gchar *sessionId = NULL;
  const gchar *drmMsgType = NULL;
  const gchar *drmData = NULL;
  gint requestId = 0;
  guint8 *decoded_kid = NULL;
  guint decoded_kid_length = 0;
  guint8 *decoded_drm_data = NULL;
  guint decoded_drm_data_length = 0;

  GST_DEBUG_OBJECT (self, "set context");

  g_return_val_if_fail (GST_IS_CONTEXT (context), FALSE);

  if (g_strcmp0 (gst_context_get_context_type (context),
          "network-available") == 0) {

    s = gst_context_get_structure (context);
    ret =
        gst_structure_get_boolean (s, "available", &self->network_connectivity);
    if (!ret)
      self->network_connectivity = FALSE;
    GST_DEBUG_OBJECT (self, "network state is [%d][%s]", ret,
        self->network_connectivity ? "true" : "false");
  } else if (g_strcmp0 (gst_context_get_context_type (context),
          "atsc-3.0") == 0) {
    s = gst_context_get_structure (context);
    self->global_service_id =
        g_strdup (gst_structure_get_string (s, "global-service-id"));
    GST_DEBUG_OBJECT (self, "global service id is [%s]",
        self->global_service_id);
  } else if (g_strcmp0 (gst_context_get_context_type (context),
          "drm-operation") == 0) {
    s = gst_context_get_structure (context);
    systemId = gst_structure_get_string (s, "systemId");
    service = gst_structure_get_string (s, "service");
    kid = gst_structure_get_string (s, "kid");
    sessionId = gst_structure_get_string (s, "sessionId");
    drmMsgType = gst_structure_get_string (s, "drmMsgType");
    drmData = gst_structure_get_string (s, "drmData");
    gst_structure_get_int (s, "requestId", &requestId);

    GST_DEBUG_OBJECT (self,
        "drmOperation systemId[%s] service[%s] kid[%s] sessionId[%s] drmMsgType[%s] drmData[%s] requestId[%d]",
        systemId, service, kid, sessionId, drmMsgType, drmData, requestId);

    decoded_kid = gst_cencdrm_widevine_uuid_string_to_bytes (kid);
    decoded_kid_length = KEY_ID_SIZE;
    decoded_drm_data =
        (guint8 *) g_base64_decode (drmData,
        (gsize *) & decoded_drm_data_length);

    if (g_strcmp0 (drmMsgType, "generateRequest") == 0) {
      gst_cencdrm_widevine_generateRequest_operation (element, systemId,
          service, drmMsgType, decoded_kid, decoded_kid_length, sessionId,
          strlen (sessionId), decoded_drm_data, decoded_drm_data_length,
          requestId);
    } else if (g_strcmp0 (drmMsgType, "update") == 0) {
      gst_cencdrm_widevine_update_operation (element, systemId, service,
          drmMsgType, decoded_kid, decoded_kid_length, sessionId,
          strlen (sessionId), decoded_drm_data, decoded_drm_data_length,
          requestId);
    } else if (g_strcmp0 (drmMsgType, "individualizationResponse") == 0) {
      gst_cencdrm_widevine_individualizationResponse_operation (element,
          systemId, service, drmMsgType, decoded_kid, decoded_kid_length,
          sessionId, strlen (sessionId), decoded_drm_data,
          decoded_drm_data_length, requestId);
    } else if (g_strcmp0 (drmMsgType, "serviceCertificateResponse") == 0) {
      gst_cencdrm_widevine_serviceCertificateResponse_operation (element,
          systemId, service, drmMsgType, decoded_kid, decoded_kid_length,
          sessionId, strlen (sessionId), decoded_drm_data,
          decoded_drm_data_length, requestId);
    } else {
      GST_DEBUG_OBJECT (self, "Not support msg type");
    }
  }

  if (decoded_kid)
    g_free (decoded_kid);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);

  return;
}

static void
gst_cencdrm_widevine_free_notify_info (GstCencDrmWidevineNotifyInfo *
    notify_info)
{
  g_return_if_fail (notify_info != NULL);

  if (notify_info->system_id)
    g_free (notify_info->system_id);

  if (notify_info->service)
    g_free (notify_info->service);

  if (notify_info->drm_msg_type)
    g_free (notify_info->drm_msg_type);

  if (notify_info->kid)
    g_free (notify_info->kid);

  if (notify_info->session_id)
    g_free (notify_info->session_id);

  if (notify_info->drm_data)
    g_free (notify_info->drm_data);
}

static gboolean
gst_cencdrm_widevine_save_notify_info (GstBaseDrm * basedrm,
    const gchar * drm_msg_type, const guint8 * drm_data,
    guint32 drm_data_length, gint32 request_id)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  GstDRMSystemInfo *drm_system_info = &basedrm->drm_system_info;
  guint8 *kid_data = NULL;
  guint32 kid_size = 0;

  g_mutex_lock (&self->notify_transaction_lock);

  GstCencDrmWidevineNotifyInfo *notify_info = (GstCencDrmWidevineNotifyInfo *)
      g_malloc (sizeof (GstCencDrmWidevineNotifyInfo));

  if (self->system_id != NULL) {
    notify_info->system_id = (gchar *) g_strdup (self->system_id);
  }

  notify_info->service = (gchar *) g_strdup (self->global_service_id);
  notify_info->drm_msg_type = (gchar *) g_strdup (drm_msg_type);

  if (self->kid_bytes != NULL) {
    kid_data = (guint8 *) g_bytes_get_data (self->kid_bytes, &kid_size);
    if (kid_data && kid_size != 0) {
      notify_info->kid = g_memdup (kid_data, kid_size);
      notify_info->kid_length = kid_size;
    } else {
      notify_info->kid = NULL;
      notify_info->kid_length = 0;
    }
  } else {
    notify_info->kid = NULL;
    notify_info->kid_length = 0;
  }

  notify_info->session_id = (gchar *) g_strdup (self->session_id);
  notify_info->session_id_length = self->session_id_length;
  notify_info->drm_data = (guint8 *) g_memdup (drm_data, drm_data_length);
  notify_info->drm_data_length = drm_data_length;
  notify_info->request_id = request_id;

  self->notify_info = notify_info;
  self->is_response = FALSE;

  return TRUE;
}


static gboolean
gst_cencdrm_widevine_notify (GstBaseDrm * basedrm,
    const gchar * system_id,
    const gchar * service,
    const gchar * drm_msg_type,
    const guint8 * kid,
    guint32 kid_length,
    const guint8 * session_id,
    guint32 session_id_length,
    const guint8 * drm_data, guint32 drm_data_length, gint32 request_id)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  GstStructure *encrypted_structure;
  GstMessage *encrypted_message;
  gchar *encoded_kid = NULL;
  gchar *encoded_drm_data = NULL;
  gchar *decoded_drm_data = NULL;
  GST_DEBUG_OBJECT (self, "gst_cencdrm_widevine_notify");

  encoded_kid = gst_cencdrm_widevine_uuid_bytes_to_string (kid);
  encoded_drm_data =
      g_base64_encode ((const guchar *) drm_data, drm_data_length);

  encrypted_structure = gst_structure_new ("drm-encrypted",
      "systemId", G_TYPE_STRING, system_id,
      "service", G_TYPE_STRING, service,
      "kid", G_TYPE_STRING, encoded_kid,
      "sessionId", G_TYPE_STRING, (gchar *) session_id,
      "drmMsgType", G_TYPE_STRING, drm_msg_type,
      "drmData", G_TYPE_STRING, encoded_drm_data, NULL);

  if (request_id != -1)
    gst_structure_set (encrypted_structure, "requestId", request_id, NULL);

  GST_DEBUG_OBJECT (self,
      "Posting message to application: %" GST_PTR_FORMAT, encrypted_structure);

  encrypted_message = gst_message_new_element (GST_OBJECT_CAST (self),
      encrypted_structure);

  gst_element_post_message (GST_ELEMENT_CAST (basedrm), encrypted_message);

  if (encoded_kid)
    g_free (encoded_kid);

  if (encoded_drm_data)
    g_free (encoded_drm_data);

  return TRUE;
}

static gboolean
gst_cencdrm_check_notify_info (GstCencDrmWidevine * self)
{
  GST_DEBUG_OBJECT (self, "get response from app");

  if (self->is_response)
    return TRUE;
  else
    return FALSE;
}

static gboolean
gst_cencdrm_notify_info_callback (GstClock * clock, GstClockTime time,
    GstClockID id, gpointer user_data)
{
  GstCencDrmWidevine *self = (GstCencDrmWidevine *) user_data;

  GST_DEBUG_OBJECT (self, "A/344 drm.notify callback");

  g_mutex_lock (&self->notify_info_lock);
  if (!gst_cencdrm_check_notify_info (self)) {
    GST_DEBUG_OBJECT (self, "fail to get response by app. retry_count[%d]",
        self->retry_count);
    if (self->retry_count > 2) {        // 3 second

      self->retry_count = 0;

      g_cond_signal (&self->notify_info_cond);
      g_mutex_unlock (&self->notify_info_lock);
      return FALSE;
    }
    self->retry_count++;
  } else {
    GST_DEBUG_OBJECT (self, "Success to get response callback");
    g_cond_signal (&self->notify_info_cond);
  }
  g_mutex_unlock (&self->notify_info_lock);

  return TRUE;
}

static gboolean
gst_cencdrm_widevine_periodic_notify_info (GstBaseDrm * basedrm)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  GstClock *clock;
  GstClockID clock_id;
  GstClockTime base;
  GstClockReturn wait_ret;

  clock = gst_system_clock_obtain ();
  if (!clock) {
    GST_ERROR_OBJECT (self, "Failed to create instance of GstSystemClock");
    return FALSE;
  }
  base = gst_clock_get_time (clock);

  clock_id = gst_clock_new_periodic_id (clock, base, GST_SECOND);
  if (!clock_id) {
    GST_ERROR_OBJECT (self, "Failed to create periodic id");
    return FALSE;
  }

  g_mutex_lock (&self->notify_info_lock);
/*
  if (G_UNLIKELY (self->is_response)) {
    g_mutex_unlock (&self->notify_info_lock);
    goto release;
  }
*/

  wait_ret =
      gst_clock_id_wait_async (clock_id, gst_cencdrm_notify_info_callback, self,
      NULL);
  if (wait_ret != GST_CLOCK_OK) {
    gst_clock_id_unref (clock_id);
    gst_object_unref (G_OBJECT (clock));
  }

  g_cond_wait (&self->notify_info_cond, &self->notify_info_lock);
  g_mutex_unlock (&self->notify_info_lock);

  gst_clock_id_unschedule (clock_id);

release:
  gst_clock_id_unref (clock_id);
  gst_object_unref (G_OBJECT (clock));

  return TRUE;
}

static gboolean
gst_cencdrm_widevine_handle_notify_info (GstBaseDrm * basedrm)
{
  GstCencDrmWidevine *self = GST_CENCDRM_WIDEVINE (basedrm);
  guint8 *kid_data = NULL;
  guint32 kid_size = 0;
  GList *it;
  GstCencDrmWidevineNotifyInfo *notify_info = NULL;
  gboolean result = FALSE;

  if (self->notify_info == NULL) {
    GST_DEBUG_OBJECT (self, "There is no data to be handled");
    return TRUE;
  }

  if (self->notify_info->kid == NULL) {
    kid_data = (guint8 *) g_bytes_get_data (self->kid_bytes, &kid_size);
    if (!kid_data || kid_size == 0) {
      GST_ERROR_OBJECT (self, "Failed to get kid");
      result = FALSE;
      goto release;
    }
  } else {
    kid_data = self->notify_info->kid;
    kid_size = self->notify_info->kid_length;
  }

  gst_cencdrm_widevine_notify (basedrm,
      self->notify_info->system_id,
      self->notify_info->service,
      self->notify_info->drm_msg_type,
      kid_data,
      kid_size,
      self->notify_info->session_id,
      self->notify_info->session_id_length,
      self->notify_info->drm_data,
      self->notify_info->drm_data_length, self->notify_info->request_id);

  gst_cencdrm_widevine_periodic_notify_info (basedrm);

  if (self->is_response)
    result = TRUE;
  else
    result = FALSE;

release:
  gst_cencdrm_widevine_free_notify_info (self->notify_info);
  if (self->notify_info) {
    g_free (self->notify_info);
    self->notify_info = NULL;
  }

  g_mutex_unlock (&self->notify_transaction_lock);

  return result;
}
