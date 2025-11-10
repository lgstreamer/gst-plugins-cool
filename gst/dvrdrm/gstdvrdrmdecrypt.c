/* GStreamer DVR DRM decrypt element
 * Copyright (C) 2016 LG Electronics, Inc.
 *
 * Authors:
 *   Jinuk Jeon <jinuk.jeon@lge.com>
 *   Donghyeok Yang <donghyeok.yang@lge.com>
 *   Youngik Kim <youngik0707.kim@lge.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gmodule.h>
#include "gstdvrdrmdecrypt.h"

#define GST_CAT_DEFAULT gst_dvrdrm_decrypt_debug_category
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DVRDRM_DECRYPT_UUID "4747982f-82a3-44a8-bb1d-a8c2ba5dcf9d"

#define LIB_DILE_DRM_PATH "/usr/lib/libdile_drm.so.0"
#define LIB_DVR_PATH "/usr/lib/libdvr.so"
const gchar dvr_drm_decrypt_xml_node_name[] = "DvrContentId";
static const gchar *_gpId_DVRDeviceSecret = "DVRDeviceSecret";
static const gchar *_gpId_DVRKey = "96B14BF8";

#define OK 0
#define NOT_OK -1
enum
{
  DVB_SEED = 0,
  ATSC_SEED = 1,
  ARIB_SEED = 1,
  NULL_SEED = 3
};


/* prototypes */
static void gst_dvrdrm_decrypt_class_init (GstDvrDrmDecryptClass * klass);
static void gst_dvrdrm_decrypt_init (GstDvrDrmDecrypt * dvrdrmdecrypt);
static gboolean gst_dvrdrm_decrypt_sink_event_handler (GstBaseTransform * trans,
    GstEvent * event);
static gboolean gst_dvrdrm_decrypt_start (GstBaseTransform * trans);
static gboolean gst_dvrdrm_decrypt_stop (GstBaseTransform * trans);
static GstFlowReturn gst_dvrdrm_decrypt_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);
static gboolean gst_dvrdrm_decrypt_drm_info (GstBaseTransform * trans,
    GstDvrSystemInfo * dvr_system_info);
static void gst_dvrdrm_decrypt_remove_codec_fields (GstStructure * fields);
static GstCaps *gst_dvrdrm_decrypt_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static void gst_dvrdrm_decrypt_pad_linked_cb (GstPad * pad, GstPad * peer,
    gpointer user_data);
static void gst_dvrdrm_decrypt_finalize (GObject * object);
static gboolean gst_dvrdrm_decrypt_module_init (GstDvrDrmDecrypt *
    dvrdrmdecrypt);
static gboolean gst_dvrdrm_decrypt_mac_pre (GstDvrDrmDecrypt * dvrdrmdecrypt,
    guchar ** ppMac);
static gboolean gst_dvrdrm_decrypt_seed_init (GstDvrDrmDecrypt * dvrdrmdecrypt);

#define UUID_STRING_LEN 36
#define KEY_ID_SIZE 16
#define AES128_BLOCKSIZE_RADIX2 4
#define DVR_SERIAL_LENGTH_BYTES 12
#define DVR_MAC_ADDR_LENGTH_BYTES 6
#define CONTEXTID_LENGTH 32

static GMutex dvrdrm_decrypt_mutex;
guint8 dvrdrm_decrypt_mutex_init = 0;

static gint (*DILE_DRM_AESHWInit) (gint mode, guchar * pKey, guchar * pIV,
    gint operation, gint isPadding);
static gint (*DILE_DRM_AESHWUpdate) (guchar * pOutData, guint * pOutDataSize,
    guchar * pInData, guint nInDataSize);
static gint (*DILE_DRM_AESHWFinish) (guchar * pOutData, guint * pOutDataSize);
static gint (*DILE_DRM_GetHWID) (guchar * pOutData, guint * pOutDataSize);
static gint (*DILE_DRM_GetSecureData) (gchar * pDataPath, guchar * pData,
    guint * pLenght);
static gint (*dvr_crypto_get_cipher_key) (gint seed_option,
    const guchar * seed1, guint length1, const guchar * seed2, guint length2,
    const guchar master[16], const guchar wrapped[24], guchar k[16]);
static gint (*dvr_umf_read_atom_in_header) (const gchar* dvr_root_path,
    const gchar * rid, guint atom_type, gint atom_id, const gchar* user_name,
    GstDvr_atom_t *out);


static GModule *module_dile;
static GModule *module_libdvr;

#define DVRDRM_DECRYPT_CAPS \
  "application/x-dvr, original-media-type=(string) " \
  "{ video/x-h264, video/x-h265, " SINK_DECODE_AUDIO_CAPS "} "


/* pad templates */

static GstStaticPadTemplate
    gst_dvrdrm_decrypt_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (DVRDRM_DECRYPT_CAPS)
    );

static GstStaticPadTemplate gst_dvrdrm_decrypt_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264; video/x-h265; " SRC_DECODE_AUDIO_CAPS)
    );
/*
static GstStaticPadTemplate
    gst_dvrdrm_decrypt_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_dvrdrm_decrypt_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);
*/
/* class initialization */
#define parent_class gst_dvrdrm_decrypt_parent_class
G_DEFINE_TYPE (GstDvrDrmDecrypt, gst_dvrdrm_decrypt, GST_TYPE_BASE_TRANSFORM);

static void
gst_dvrdrm_decrypt_class_init (GstDvrDrmDecryptClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_dvrdrm_decrypt_debug_category, "dvrdrmdecrypt",
      0, "Dvr Drm Decrypt class for ATSC3.0 PVR support");
  GST_DEBUG ("dvrdrmdecrypt class init");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_dvrdrm_decrypt_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_dvrdrm_decrypt_src_template));

  gst_element_class_set_static_metadata (element_class,
      "DVR DRM Decrypt for ATSC3.0",
      GST_ELEMENT_FACTORY_KLASS_DECRYPTOR,
      "Decrypts DVR DRM protected media in ATSC3.0",
      "Jinuk Jeon <jinuk.jeon@lge.com>");
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_dvrdrm_decrypt_finalize);

  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_dvrdrm_decrypt_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_dvrdrm_decrypt_stop);
  base_transform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_dvrdrm_decrypt_transform_ip);
  base_transform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_dvrdrm_decrypt_transform_caps);
  base_transform_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_dvrdrm_decrypt_sink_event_handler);
  base_transform_class->transform_ip_on_passthrough = FALSE;

  /* Default implementations in dvrdrmdecrypt class */

  if (dvrdrm_decrypt_mutex_init == 0) {
    g_mutex_init (&dvrdrm_decrypt_mutex);
    dvrdrm_decrypt_mutex_init = 1;
  }
}

static void
gst_dvrdrm_decrypt_init (GstDvrDrmDecrypt * dvrdrmdecrypt)
{
  GstDvrSystemInfo *dvr_system_info = &dvrdrmdecrypt->dvr_system_info;
  GstDvrDrmDecryptInfo *decrypt_info = &dvrdrmdecrypt->decrypt_info;

  GST_DEBUG_OBJECT (dvrdrmdecrypt, "DVR DRM Decrypt init");
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (dvrdrmdecrypt), TRUE);
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (dvrdrmdecrypt),
      FALSE);
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (dvrdrmdecrypt), FALSE);

  g_signal_connect (G_OBJECT (GST_BASE_TRANSFORM (dvrdrmdecrypt)->sinkpad),
      "linked", (GCallback) gst_dvrdrm_decrypt_pad_linked_cb,
      GST_BASE_TRANSFORM (dvrdrmdecrypt));
  decrypt_info->data = NULL;
  dvrdrmdecrypt->mac = NULL;
  dvrdrmdecrypt->serial = NULL;
  dvrdrmdecrypt->mediauri = NULL;
  dvrdrmdecrypt->broadcast_type = NULL;
  dvrdrmdecrypt->cipher_key = NULL;
  dvr_system_info->system_ids = NULL;
  dvr_system_info->xml_node_name = NULL;
  dvr_system_info->drmclient_id = NULL;
}

static void
gst_dvrdrm_decrypt_finalize (GObject * object)
{
  GstDvrDrmDecrypt *dvrdrmdecrypt = GST_DVRDRM_DECRYPT (object);

  GST_DEBUG_OBJECT (dvrdrmdecrypt, "finalize");
  g_free (dvrdrmdecrypt->mac);
  dvrdrmdecrypt->mac = NULL;
  g_free (dvrdrmdecrypt->serial);
  dvrdrmdecrypt->serial = NULL;
  g_free (dvrdrmdecrypt->mediauri);
  dvrdrmdecrypt->mediauri = NULL;
  g_free (dvrdrmdecrypt->broadcast_type);
  dvrdrmdecrypt->broadcast_type = NULL;
  g_free (dvrdrmdecrypt->cipher_key);
  dvrdrmdecrypt->cipher_key = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);

}

static void
gst_dvrdrm_decrypt_pad_linked_cb (GstPad * pad, GstPad * peer,
    gpointer user_data)
{
  GstSmartPropertiesReturn ret;
  GstDvrDrmDecrypt *dvrdrmdecrypt = GST_DVRDRM_DECRYPT (user_data);

  ret = gst_element_get_smart_properties (GST_ELEMENT_CAST (dvrdrmdecrypt),
      "dvr-mac", &dvrdrmdecrypt->mac,
      "dvr-serial", &dvrdrmdecrypt->serial,
      "drm-mediauri", &dvrdrmdecrypt->mediauri,
      "dvr-broadcast-type", &dvrdrmdecrypt->broadcast_type, NULL);

  if (g_strcmp0 (dvrdrmdecrypt->broadcast_type, "DVB") == 0 ||
      g_strcmp0 (dvrdrmdecrypt->broadcast_type, "dvb") == 0) {
    dvrdrmdecrypt->seed_type = DVB_SEED;
  } else if (g_strcmp0 (dvrdrmdecrypt->broadcast_type, "ATSC") == 0 ||
      g_strcmp0 (dvrdrmdecrypt->broadcast_type, "atsc") == 0) {
    dvrdrmdecrypt->seed_type = ATSC_SEED;
  } else if (g_strcmp0 (dvrdrmdecrypt->broadcast_type, "ARIB") == 0 ||
      g_strcmp0 (dvrdrmdecrypt->broadcast_type, "arib") == 0) {
    dvrdrmdecrypt->seed_type = ARIB_SEED;
  } else
    dvrdrmdecrypt->seed_type = NULL_SEED;

  GST_INFO_OBJECT (dvrdrmdecrypt,
      "dvrdrmdecrypt received responsed of custom query: [%d]", ret);
  GST_INFO_OBJECT (dvrdrmdecrypt,
      "Smart property results: dvr-mac[%s] dvr-serial[%s] dvr-brodcast-type[%s]",
      dvrdrmdecrypt->mac, dvrdrmdecrypt->serial, dvrdrmdecrypt->broadcast_type);
  if (ret != GST_SMART_PROPERTIES_OK) {
    GST_DEBUG_OBJECT (dvrdrmdecrypt, "Failed to get smart-properties");
  }
}

static gboolean
gst_dvrdrm_decrypt_drm_info (GstBaseTransform * trans,
    GstDvrSystemInfo * dvr_system_info)
{
  GstDvrDrmDecrypt *dvrdrmdecrypt = GST_DVRDRM_DECRYPT (trans);

  GST_DEBUG_OBJECT (dvrdrmdecrypt, "DVR DRM Decrypt for specific static info");

  if (!dvr_system_info) {
    GST_ERROR_OBJECT (dvrdrmdecrypt, "NULL pointer drm_system_info");
    return FALSE;
  }

  dvr_system_info->system_ids =
      g_list_append (dvr_system_info->system_ids,
      g_strdup (DVRDRM_DECRYPT_UUID));
  dvr_system_info->xml_node_name =
      g_memdup (&dvr_drm_decrypt_xml_node_name[0],
      sizeof (dvr_drm_decrypt_xml_node_name));

  if (!dvr_system_info->system_ids || !dvr_system_info->xml_node_name) {
    GST_ERROR_OBJECT (dvrdrmdecrypt,
        "Either system_ids %p, xml_node_name %p is a NULL pointer",
        dvr_system_info->system_ids, dvr_system_info->xml_node_name);
    return FALSE;
  }

  return TRUE;
}


static gboolean
gst_dvrdrm_decrypt_module_init (GstDvrDrmDecrypt * dvrdrmdecrypt)
{

  g_mutex_lock (&dvrdrm_decrypt_mutex);

  GST_DEBUG_OBJECT (dvrdrmdecrypt, "Open DRM");

  module_dile = g_module_open (LIB_DILE_DRM_PATH, G_MODULE_BIND_LAZY);
  if (!module_dile) {
    GST_ERROR_OBJECT (dvrdrmdecrypt, "Failed to open a module: %s",
        g_module_error ());
    goto error_module;
  }
  if (!g_module_symbol (module_dile, "DILE_DRM_AESHWInit",
          (gpointer *) & DILE_DRM_AESHWInit))
    goto error_symbol;
  if (!g_module_symbol (module_dile, "DILE_DRM_AESHWUpdate",
          (gpointer *) & DILE_DRM_AESHWUpdate))
    goto error_symbol;
  if (!g_module_symbol (module_dile, "DILE_DRM_AESHWFinish",
          (gpointer *) & DILE_DRM_AESHWFinish))
    goto error_symbol;
  if (!g_module_symbol (module_dile, "DILE_DRM_GetHWID",
          (gpointer *) & DILE_DRM_GetHWID))
    goto error_symbol;
  if (!g_module_symbol (module_dile, "DILE_DRM_GetSecureData",
          (gpointer *) & DILE_DRM_GetSecureData))
    goto error_symbol;

  module_libdvr = g_module_open (LIB_DVR_PATH, G_MODULE_BIND_LAZY);
  if (!module_libdvr) {
    GST_ERROR_OBJECT (dvrdrmdecrypt, "Failed to open a module: %s",
        g_module_error ());
    goto error_module;
  }
  if (!g_module_symbol (module_libdvr, "dvr_crypto_get_cipher_key",
          (gpointer *) & dvr_crypto_get_cipher_key))
    goto error_symbol;
  if (!g_module_symbol (module_libdvr, "dvr_umf_read_atom_in_header",
          (gpointer *) & dvr_umf_read_atom_in_header))
    goto error_symbol;

  g_mutex_unlock (&dvrdrm_decrypt_mutex);
  return TRUE;

error_symbol:
  GST_ERROR_OBJECT (dvrdrmdecrypt, "Failed to get a symbol: %s",
      g_module_error ());

error_module:
  g_module_close (module_dile);
  g_module_close (module_libdvr);
  g_mutex_unlock (&dvrdrm_decrypt_mutex);
  return FALSE;

}


static gboolean
gst_dvrdrm_decrypt_mac_pre (GstDvrDrmDecrypt * dvrdrmdecrypt, guchar ** ppMac)
{
  gboolean res = TRUE;
  guint i, t, j = 0;
  guchar temp[7] = "000000";
  if (dvrdrmdecrypt->mac == NULL)
    return FALSE;

  for (i = 0; i < 6; i++) {
    t = *(dvrdrmdecrypt->mac + 2 * i);

    if ((t >= '0') && (t <= '9'))
      j = (t - '0') << 4;
    else if ((t >= 'a') && (t <= 'f'))
      j = (t - 'a' + 10) << 4;
    else if ((t >= 'A') && (t <= 'F'))
      j = (t - 'A' + 10) << 4;
    else
      res = FALSE;

    t = *(dvrdrmdecrypt->mac + 2 * i + 1);

    if ((t >= '0') && (t <= '9'))
      j ^= (t - '0');
    else if ((t >= 'a') && (t <= 'f'))
      j ^= (t - 'a' + 10);
    else if ((t >= 'A') && (t <= 'F'))
      j ^= (t - 'A' + 10);
    else
      res = FALSE;
    temp[i] = (guchar) j;
  }

  *ppMac = g_strndup (temp, DVR_MAC_ADDR_LENGTH_BYTES);
  GST_DEBUG_OBJECT (dvrdrmdecrypt, "mac %02x%02x%02x%02x%02x%02x ",
      (*ppMac)[0], (*ppMac)[1], (*ppMac)[2], (*ppMac)[3], (*ppMac)[4],
      (*ppMac)[5]);
  if (*ppMac == NULL) {
    GST_DEBUG_OBJECT (dvrdrmdecrypt, "mac NULL");
    res = FALSE;
  }

  return res;
}

static gboolean
gst_dvrdrm_decrypt_serial_comp (GstDvrDrmDecrypt * dvrdrmdecrypt)
{
  gboolean res = TRUE;
  gint ret = 0;
  gchar *dvr_root = NULL;
  gchar *contextId = NULL;
  gchar *offset = NULL;
  GstDvr_atom_t stAtomData;

  offset = g_strrstr (dvrdrmdecrypt->mediauri, "lg_dvr");
  dvr_root = g_strndup (dvrdrmdecrypt->mediauri, offset-dvrdrmdecrypt->mediauri+6);
  contextId =  g_strndup (dvrdrmdecrypt->mediauri+strlen(dvr_root)+1, CONTEXTID_LENGTH);
  GST_DEBUG_OBJECT (dvrdrmdecrypt, "dvr_root = %s , contextId = %s", dvr_root, contextId);
  memset (&stAtomData, 0, sizeof(GstDvr_atom_t));

  //get recordings serial number using libdvr function
  ret = dvr_umf_read_atom_in_header (dvr_root, contextId, MAKE_FOURCC('T','V','I','D'),
      0, "dvrdrm", &stAtomData);
  GST_DEBUG_OBJECT (dvrdrmdecrypt, "get recorde TV ID, ret = %d", ret);
  if (stAtomData.data != NULL) {
    if (0 != strcmp (dvrdrmdecrypt->serial, stAtomData.data)) {
      GST_ERROR_OBJECT (dvrdrmdecrypt, "not matching serial key!!");
      res = FALSE;
    }
  } else {
    GST_ERROR_OBJECT (dvrdrmdecrypt, "UMF doesn't have TV ID");
  }

  g_free(dvr_root);
  g_free(contextId);
  g_free(stAtomData.data);

  return res;
}

static gboolean
gst_dvrdrm_decrypt_seed_init (GstDvrDrmDecrypt * dvrdrmdecrypt)
{
  gboolean res = FALSE;
  guint tempLength = 16;
  guint tempseLength = 24;
  guchar tempkey[16] = { 0, };
  guchar tempsekey[24] = { 0, };
  guchar tempcipher[16] = { 0, };
  guchar serial[12] = { 0, };
  guchar *mac = NULL;

  g_mutex_lock (&dvrdrm_decrypt_mutex);

  if (dvrdrmdecrypt->serial == NULL || dvrdrmdecrypt->mac == NULL ||
      dvrdrmdecrypt->mediauri == NULL) {
    g_mutex_unlock (&dvrdrm_decrypt_mutex);
    return FALSE;
  }
  if (DILE_DRM_GetSecureData == NULL || dvr_crypto_get_cipher_key == NULL ||
      dvr_umf_read_atom_in_header == NULL) {
    g_mutex_unlock (&dvrdrm_decrypt_mutex);
    return FALSE;
  }

  if (DILE_DRM_GetSecureData (_gpId_DVRKey, tempkey, &tempLength) == OK) {
    GST_DEBUG_OBJECT (dvrdrmdecrypt, "dvr key init ok");
    res = TRUE;
  } else {
    GST_ERROR_OBJECT (dvrdrmdecrypt, "dvr key init fail");
    res = FALSE;
    goto seed_fail;
  }

  if (dvrdrmdecrypt->seed_type == DVB_SEED) {
    if (DILE_DRM_GetSecureData (_gpId_DVRDeviceSecret, tempsekey,
            &tempseLength) == OK) {
      GST_DEBUG_OBJECT (dvrdrmdecrypt, "wrapped cipher key init ok");
      res = TRUE;
    } else {
      GST_ERROR_OBJECT (dvrdrmdecrypt, "wrapped cipher key init fail");
      res = FALSE;
      goto seed_fail;
    }
  }

  memcpy (serial, (const gchar *) dvrdrmdecrypt->serial,
      strlen (dvrdrmdecrypt->serial) >
      12 ? 12 : strlen (dvrdrmdecrypt->serial));
  GST_DEBUG_OBJECT (dvrdrmdecrypt, "serial num %c%c%c%c%c%c%c%c%c%c%c%c",
      serial[0], serial[1], serial[2], serial[3], serial[4], serial[5],
      serial[6], serial[7], serial[8], serial[9], serial[10], serial[11]);

  if (!gst_dvrdrm_decrypt_serial_comp (dvrdrmdecrypt))
    goto seed_fail;

  if (gst_dvrdrm_decrypt_mac_pre (dvrdrmdecrypt, &mac) == TRUE) {
    GST_DEBUG_OBJECT (dvrdrmdecrypt, "mac pre ok");
    res = TRUE;
  } else {
    GST_ERROR_OBJECT (dvrdrmdecrypt, "mac pre fail");
    res = FALSE;
    goto seed_fail;
  }


  if (dvr_crypto_get_cipher_key (dvrdrmdecrypt->seed_type,
          (const guchar *) serial, sizeof (serial), (const guchar *) mac,
          DVR_MAC_ADDR_LENGTH_BYTES, (const guchar *) tempkey,
          (const guchar *) tempsekey, tempcipher) < 0) {
    GST_ERROR_OBJECT (dvrdrmdecrypt, "cipher key init fail");
    res = FALSE;
    goto seed_fail;
  } else {
    dvrdrmdecrypt->cipher_key = g_memdup (tempcipher, (gsize) 16);
    GST_DEBUG_OBJECT (dvrdrmdecrypt, "cipher key init ok");
    res = TRUE;
  }

seed_fail:
  g_mutex_unlock (&dvrdrm_decrypt_mutex);
  g_free (mac);
  return res;
}

static gboolean
gst_dvrdrm_decrypt_start (GstBaseTransform * trans)
{
  GstDvrDrmDecrypt *dvrdrmdecrypt = GST_DVRDRM_DECRYPT (trans);

  GST_DEBUG_OBJECT (dvrdrmdecrypt, "start");

  /* start the DRM system */
  if (!gst_dvrdrm_decrypt_module_init (dvrdrmdecrypt))
    goto error_start;
  if (!gst_dvrdrm_decrypt_seed_init (dvrdrmdecrypt))
    goto error_start;

  return TRUE;

error_start:
  GST_ERROR_OBJECT (dvrdrmdecrypt, "Failed to start decrypt");
  return FALSE;
}

static gboolean
gst_dvrdrm_decrypt_stop (GstBaseTransform * trans)
{
  GstDvrDrmDecrypt *dvrdrmdecrypt = GST_DVRDRM_DECRYPT (trans);
  GstDvrSystemInfo *dvr_system_info = &dvrdrmdecrypt->dvr_system_info;

  GST_DEBUG_OBJECT (dvrdrmdecrypt, "stop");

  /* stop the DVR DRM Decrypt system */
  g_mutex_lock (&dvrdrm_decrypt_mutex);
  g_module_close (module_dile);
  g_module_close (module_libdvr);
  g_mutex_unlock (&dvrdrm_decrypt_mutex);

  /* release theDvrSystemInfo */

  if (dvr_system_info->system_ids) {
    g_list_free_full (dvr_system_info->system_ids, g_free);
    dvr_system_info->system_ids = NULL;
  }

  if (dvr_system_info->xml_node_name) {
    g_free (dvr_system_info->xml_node_name);
    dvr_system_info->xml_node_name = NULL;
  }

  if (dvr_system_info->drmclient_id) {
    g_free (dvr_system_info->drmclient_id);
    dvr_system_info->drmclient_id = NULL;
  }

  return TRUE;
}

static void
gst_dvrdrm_decrypt_remove_codec_fields (GstStructure * fields)
{
  gint j, n_fields = gst_structure_n_fields (fields);
  for (j = n_fields - 1; j >= 0; --j) {
    const gchar *field_name;
    field_name = gst_structure_nth_field_name (fields, j);
    if (g_strcmp0 (field_name, "base-profile") == 0 ||
        g_strcmp0 (field_name, "codec_data") == 0 ||
        g_strcmp0 (field_name, "height") == 0 ||
        g_strcmp0 (field_name, "framerate") == 0 ||
        g_strcmp0 (field_name, "level") == 0 ||
        g_strcmp0 (field_name, "pixel-aspect-ratio") == 0 ||
        g_strcmp0 (field_name, "profile") == 0 ||
        g_strcmp0 (field_name, "rate") == 0 ||
        g_strcmp0 (field_name, "width") == 0) {
      gst_structure_remove_field (fields, field_name);
    }
  }
}

static GstCaps *
gst_dvrdrm_decrypt_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstDvrDrmDecrypt *dvrdrmdecrypt = GST_DVRDRM_DECRYPT (trans);
  GstDvrSystemInfo *dvr_system_info = &dvrdrmdecrypt->dvr_system_info;
  GstCaps *new_caps = NULL;
  gint i, j;

  g_return_val_if_fail (direction != GST_PAD_UNKNOWN, NULL);
  new_caps = gst_caps_new_empty ();

  /* Call once to get drm system info */
  if (!dvr_system_info->system_ids) {
    if (gst_dvrdrm_decrypt_drm_info (trans, dvr_system_info) == FALSE) {
      GST_ERROR_OBJECT (dvrdrmdecrypt, "Failed to get DRM info");
      return NULL;
    }
    GST_DEBUG_OBJECT (dvrdrmdecrypt, "DRM info received");
  }

  GST_DEBUG_OBJECT (dvrdrmdecrypt,
      "direction: %s   caps: %" GST_PTR_FORMAT "   filter:" " %" GST_PTR_FORMAT,
      (direction == GST_PAD_SRC) ? "Src" : "Sink", caps, filter);

  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    GstStructure *in = gst_caps_get_structure (caps, i);
    GstStructure *out = NULL;
    gboolean duplicate = FALSE;

    if (direction == GST_PAD_SINK) {
      gint n_fields;

      if (!gst_structure_has_field (in, "original-media-type"))
        continue;

      out = gst_structure_copy (in);
      n_fields = gst_structure_n_fields (in);

      gst_structure_set_name (out,
          gst_structure_get_string (out, "original-media-type"));

      /* filter out the DRM related fields from the down-stream caps */
      for (j = 0; j < n_fields; ++j) {
        const gchar *field_name;

        field_name = gst_structure_nth_field_name (in, j);

        if (g_str_has_prefix (field_name, "protection-system") ||
            g_str_has_prefix (field_name, "original-media-type")) {
          gst_structure_remove_field (out, field_name);
        }
      }
      duplicate = gst_caps_is_subset_structure (new_caps, out);
      if (!duplicate) {
        gst_caps_append_structure (new_caps, out);
      } else {
        gst_structure_free (out);
      }
    } else {                    /* GST_PAD_SRC */
      GList *system_ids = dvr_system_info->system_ids;
      while (system_ids) {
        out = gst_structure_copy (in);
        gst_dvrdrm_decrypt_remove_codec_fields (out);
        gst_structure_set (out,
            "protection-system", G_TYPE_STRING,
            system_ids->data, "original-media-type",
            G_TYPE_STRING, gst_structure_get_name (in), NULL);
        gst_structure_set_name (out, "application/x-dvr");
        duplicate = gst_caps_is_subset_structure (new_caps, out);
        if (!duplicate) {
          gst_caps_append_structure (new_caps, out);
        } else {
          gst_structure_free (out);
        }
        system_ids = g_list_next (system_ids);
      }
    }
  }

  if (filter) {
    GstCaps *intersection;

    GST_DEBUG_OBJECT (dvrdrmdecrypt, "Using filter caps %" GST_PTR_FORMAT,
        filter);
    intersection =
        gst_caps_intersect_full (new_caps, filter, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (new_caps);
    new_caps = intersection;
  }

  GST_DEBUG_OBJECT (dvrdrmdecrypt, "returning %" GST_PTR_FORMAT, new_caps);
  return new_caps;
}

static gboolean
gst_dvrdrm_decrypt_decrypt_init (GstDvrDrmDecrypt * dvrdrmdecrypt)
{
  gboolean res = TRUE;

  GST_LOG_OBJECT (dvrdrmdecrypt, "decrypt_init");

  if (DILE_DRM_AESHWInit == NULL) {
    GST_ERROR_OBJECT (dvrdrmdecrypt, "Failed to init AESHW");
    return FALSE;
  }

  if (DILE_DRM_AESHWInit (2, dvrdrmdecrypt->cipher_key, NULL, 1, 0) == -1) {
    GST_ERROR_OBJECT (dvrdrmdecrypt, "decrypt init fail");
    res = FALSE;
  }
  return res;
}

static GstFlowReturn
gst_dvrdrm_decrypt_transform_ip (GstBaseTransform * base, GstBuffer * buf)
{
  GstDvrDrmDecrypt *dvrdrmdecrypt = GST_DVRDRM_DECRYPT (base);
  GstDvrDrmDecryptInfo *decrypt_info = &dvrdrmdecrypt->decrypt_info;
  GstFlowReturn ret = GST_FLOW_NOT_SUPPORTED;
  GstMapInfo map;

  guchar *outbuf = NULL;
  guint outbuf_size = 0;

  GST_LOG_OBJECT (dvrdrmdecrypt, "decrypt in-place");

  if (!buf) {
    GST_ERROR_OBJECT (dvrdrmdecrypt, "Failed to get writable buffer");
    return GST_FLOW_ERROR;
  }
  g_mutex_lock (&dvrdrm_decrypt_mutex);
  if (DILE_DRM_AESHWUpdate == NULL || DILE_DRM_AESHWFinish == NULL) {
    GST_ERROR_OBJECT (dvrdrmdecrypt, "Failed to init moudle");
    g_mutex_unlock (&dvrdrm_decrypt_mutex);
    return GST_FLOW_ERROR;
  }
  if (gst_dvrdrm_decrypt_decrypt_init (dvrdrmdecrypt) == FALSE) {
    g_mutex_unlock (&dvrdrm_decrypt_mutex);
    goto release;
  }
  if (!gst_buffer_map (buf, &map, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (dvrdrmdecrypt, "Failed to map buffer");
    g_mutex_unlock (&dvrdrm_decrypt_mutex);
    goto release;
  }

  GST_LOG_OBJECT (dvrdrmdecrypt, "data size %" G_GSIZE_FORMAT, map.size);

  if (map.size) {
    decrypt_info->offset_block = map.size >> AES128_BLOCKSIZE_RADIX2;
    decrypt_info->offset_byte =
        map.size - (decrypt_info->offset_block << AES128_BLOCKSIZE_RADIX2);
    decrypt_info->data = map.data;
    decrypt_info->data_size = map.size;
    outbuf = (guchar *) g_malloc (map.size);
    if (DILE_DRM_AESHWUpdate (outbuf, &outbuf_size, decrypt_info->data,
            decrypt_info->data_size) == -1) {
      g_mutex_unlock (&dvrdrm_decrypt_mutex);
      goto beach;
    }
    GST_LOG_OBJECT (dvrdrmdecrypt, "%d bytes update decrypted", outbuf_size);
    memcpy (map.data, outbuf, outbuf_size);
    if (DILE_DRM_AESHWFinish (outbuf, &outbuf_size) == -1) {
      g_mutex_unlock (&dvrdrm_decrypt_mutex);
      goto beach;
    }
    GST_LOG_OBJECT (dvrdrmdecrypt, "%d bytes finish decrypted", outbuf_size);
    memcpy (map.data + (map.size - outbuf_size), outbuf, outbuf_size);
  }
  g_mutex_unlock (&dvrdrm_decrypt_mutex);
  ret = GST_FLOW_OK;

beach:
  gst_buffer_unmap (buf, &map);
  if (outbuf != NULL) {
    g_free (outbuf);
    outbuf = NULL;
  }
release:

  return ret;
}

static gboolean
gst_dvrdrm_decrypt_sink_event_handler (GstBaseTransform * trans,
    GstEvent * event)
{
  gboolean ret = FALSE;
  GstDvrDrmDecrypt *dvrdrmdecrypt = GST_DVRDRM_DECRYPT (trans);
  GST_LOG_OBJECT (dvrdrmdecrypt, "sink envet");
  switch (GST_EVENT_TYPE (event)) {
      /*
         case GST_EVENT_PROTECTION:
         GST_DEBUG_OBJECT (dvrdrmdecrypt, "received protection event");
         gst_event_parse_protection (event, &system_id, &pssi, &loc);
         GST_DEBUG_OBJECT (dvrdrmdecrypt, "system_id: %s", system_id);

         gst_event_unref (event);
         break;
       */
    default:
      ret = GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
      break;
  }


  return ret;
}
