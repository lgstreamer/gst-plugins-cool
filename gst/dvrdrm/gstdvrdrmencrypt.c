/* GStreamer DVR DRM encrypt element
 * Copyright (C) 2016 LG Electronics, Inc.
 *
 * Authors:
 *   Jinuk Jeon <jinuk.jeon@lge.com>
 *   Donghyeok Yang <donghyeok.yang@lge.com>
 *   Youngik Kim <youngik0707.kim@lge.com>
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
#include <glib-object.h>
#include "gstdvrdrmencrypt.h"

#define GST_CAT_DEFAULT gst_dvrdrm_encrypt_debug_category
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DVRDRM_ENCRYPT_UUID "4747982f-82a3-44a8-bb1d-a8c2ba5dcf9d"

#define LIB_DILE_DRM_PATH "/usr/lib/libdile_drm.so.0"
#define LIB_CRYPTO_PATH "/usr/lib/libcrypto.so.1.0.2"
#define LIB_DVR_PATH "/usr/lib/libdvr.so"

static const gchar *_gpId_DVRDeviceSecret = "DVRDeviceSecret";
static const gchar *_gpId_DVRKey = "96B14BF8";

#define DEFAULT_PROP_MAC ""
#define DEFAULT_PROP_SERIAL ""
#define DEFAULT_PROP_BROADCAST_TYPE ""
#define OK 0
#define NOT_OK -1


enum
{
  ARG_0,
  PROP_MAC,
  PROP_SERIAL,
  PROP_BROADCAST_TYPE
};

enum
{
  DVB_SEED = 0,
  ATSC_SEED = 1,
  ARIB_SEED = 1,
  NULL_SEED = 3
};


/* prototypes */
static void gst_dvrdrm_encrypt_class_init (GstDvrDrmEncryptClass * klass);
static void gst_dvrdrm_encrypt_init (GstDvrDrmEncrypt * dvrdrmencrypt);
static gboolean gst_dvrdrm_encrypt_sink_event_handler (GstBaseTransform * trans,
    GstEvent * event);
static gboolean gst_dvrdrm_encrypt_start (GstBaseTransform * trans);
static gboolean gst_dvrdrm_encrypt_stop (GstBaseTransform * trans);
static GstFlowReturn gst_dvrdrm_encrypt_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);
static void gst_dvrdrm_encrypt_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dvrdrm_encrypt_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * psepc);
static void gst_dvrdrm_encrypt_finalize (GObject * object);
static gboolean gst_dvrdrm_encrypt_module_init (GstDvrDrmEncrypt *
    dvrdrmencrypt);
static gboolean gst_dvrdrm_encrypt_mac_pre (GstDvrDrmEncrypt * dvrdrmencrypt,
    guchar ** ppMac);
static gboolean gst_dvrdrm_encrypt_seed_init (GstDvrDrmEncrypt * dvrdrmencrypt);

#define UUID_STRING_LEN 36
#define KEY_ID_SIZE 16
#define AES128_BLOCKSIZE_RADIX2 4
#define DVR_SERIAL_LENGTH_BYTES 12
#define DVR_MAC_ADDR_LENGTH_BYTES 6

static GMutex dvrdrm_encrypt_mutex;
guint8 dvrdrm_encrypt_mutex_init = 0;

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
static gint (*DVR_EVP_EncryptInit) (EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
    const guchar *key, const guchar *iv);
static gint (*DVR_EVP_EncryptUpdate) (EVP_CIPHER_CTX *ctx, guchar *out, int *outl,
    guchar *in, int inl);
static EVP_CIPHER *(*DVR_EVP_aes_128_ecb) (void);
static void (*DVR_EVP_CIPHER_CTX_init) (EVP_CIPHER_CTX *ctx);
static gint (*DVR_EVP_CIPHER_CTX_cleanup) (EVP_CIPHER_CTX *ctx);

static GModule *module_dile;
static GModule *module_crypto;
static GModule *module_libdvr;

#define DVRDRM_ENCRYPT_CAPS \
  "application/x-dvr, original-media-type=(string) " \
  "{ video/x-h264, video/x-h265, " SINK_DECODE_AUDIO_CAPS "} "

/* pad templates */
/*
static GstStaticPadTemplate
    gst_dvrdrm_encrypt_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (DVRDRM_ENCRYPT_CAPS)
    );

static GstStaticPadTemplate gst_dvrdrm_encrypt_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264; video/x-h265; " SRC_DECODE_AUDIO_CAPS)
    );
*/
static GstStaticPadTemplate
    gst_dvrdrm_encrypt_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_dvrdrm_encrypt_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* class initialization */
#define parent_class gst_dvrdrm_encrypt_parent_class
G_DEFINE_TYPE (GstDvrDrmEncrypt, gst_dvrdrm_encrypt, GST_TYPE_BASE_TRANSFORM);

static void
gst_dvrdrm_encrypt_class_init (GstDvrDrmEncryptClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_dvrdrm_encrypt_debug_category, "dvrdrmencrypt",
      0, "Dvr Drm Encrypt class for ATSC3.0 PVR support");
  GST_DEBUG ("dvrdrmencrypt class init");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_dvrdrm_encrypt_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_dvrdrm_encrypt_src_template));

  gst_element_class_set_static_metadata (element_class,
      "DVR DRM Encrypt for ATSC3.0",
      GST_ELEMENT_FACTORY_KLASS_ENCRYPTOR,
      "Encrypts DVR DRM protected media in ATSC3.0",
      "Jinuk Jeon <jinuk.jeon@lge.com>");

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_dvrdrm_encrypt_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_dvrdrm_encrypt_get_property);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_dvrdrm_encrypt_finalize);

  g_object_class_install_property (gobject_class, PROP_MAC,
      g_param_spec_string ("dvr-mac", "Dvr Mac", "Dvr Infomation MacAddress",
          DEFAULT_PROP_MAC, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SERIAL,
      g_param_spec_string ("dvr-serial", "Dvr Serial",
          "Dvr Infomation Serial Number", DEFAULT_PROP_SERIAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BROADCAST_TYPE,
      g_param_spec_string ("dvr-broadcast-type", "Dvr Broadcast Type",
          "Dvr Infomation BroadCast Type", DEFAULT_PROP_BROADCAST_TYPE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_dvrdrm_encrypt_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_dvrdrm_encrypt_stop);
  base_transform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_dvrdrm_encrypt_transform_ip);
  base_transform_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_dvrdrm_encrypt_sink_event_handler);
  base_transform_class->transform_ip_on_passthrough = FALSE;

  /* Default implementations in dvrdrmencrypt class */

  if (dvrdrm_encrypt_mutex_init == 0) {
    g_mutex_init (&dvrdrm_encrypt_mutex);
    dvrdrm_encrypt_mutex_init = 1;
  }
}

static void
gst_dvrdrm_encrypt_init (GstDvrDrmEncrypt * dvrdrmencrypt)
{
  GstDvrDrmEncryptInfo *encrypt_info = &dvrdrmencrypt->encrypt_info;

  GST_LOG_OBJECT (dvrdrmencrypt, "DVR DRM Encrypt init");
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (dvrdrmencrypt), TRUE);
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (dvrdrmencrypt),
      FALSE);
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (dvrdrmencrypt), FALSE);
  encrypt_info->data = NULL;
  dvrdrmencrypt->mac = NULL;
  dvrdrmencrypt->serial = NULL;
  dvrdrmencrypt->broadcast_type = NULL;
  dvrdrmencrypt->cipher_key = NULL;
}

static void
gst_dvrdrm_encrypt_finalize (GObject * object)
{
  GstDvrDrmEncrypt *dvrdrmencrypt = GST_DVRDRM_ENCRYPT (object);

  GST_DEBUG_OBJECT (dvrdrmencrypt, "finalize");
  g_free (dvrdrmencrypt->mac);
  dvrdrmencrypt->mac = NULL;
  g_free (dvrdrmencrypt->serial);
  dvrdrmencrypt->serial = NULL;
  g_free (dvrdrmencrypt->broadcast_type);
  dvrdrmencrypt->broadcast_type = NULL;
  g_free (dvrdrmencrypt->cipher_key);
  dvrdrmencrypt->cipher_key = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);

}

static void
gst_dvrdrm_encrypt_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDvrDrmEncrypt *dvrdrmencrypt = GST_DVRDRM_ENCRYPT (object);

  switch (prop_id) {
    case PROP_MAC:
      g_free (dvrdrmencrypt->mac);
      dvrdrmencrypt->mac = g_value_dup_string (value);
      break;
    case PROP_SERIAL:
      g_free (dvrdrmencrypt->serial);
      dvrdrmencrypt->serial = g_value_dup_string (value);
      break;
    case PROP_BROADCAST_TYPE:
      g_free (dvrdrmencrypt->broadcast_type);
      dvrdrmencrypt->broadcast_type = g_value_dup_string (value);
      if (g_strcmp0 (dvrdrmencrypt->broadcast_type, "DVB") == 0) {
        dvrdrmencrypt->seed_type = DVB_SEED;
      } else if (g_strcmp0 (dvrdrmencrypt->broadcast_type, "ATSC") == 0) {
        dvrdrmencrypt->seed_type = ATSC_SEED;
      } else if (g_strcmp0 (dvrdrmencrypt->broadcast_type, "ARIB") == 0) {
        dvrdrmencrypt->seed_type = ARIB_SEED;
      } else
        dvrdrmencrypt->seed_type = NULL_SEED;

      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static void
gst_dvrdrm_encrypt_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDvrDrmEncrypt *dvrdrmencrypt = GST_DVRDRM_ENCRYPT (object);

  switch (prop_id) {
    case PROP_MAC:
      g_value_set_string (value, dvrdrmencrypt->mac);
      break;
    case PROP_SERIAL:
      g_value_set_string (value, dvrdrmencrypt->serial);
      break;
    case PROP_BROADCAST_TYPE:
      g_value_set_string (value, dvrdrmencrypt->broadcast_type);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;

  }
}


static gboolean
gst_dvrdrm_encrypt_module_init (GstDvrDrmEncrypt * dvrdrmencrypt)
{

  g_mutex_lock (&dvrdrm_encrypt_mutex);

  GST_DEBUG_OBJECT (dvrdrmencrypt, "Open DRM");

  module_dile = g_module_open (LIB_DILE_DRM_PATH, G_MODULE_BIND_LAZY);
  if (!module_dile) {
    GST_ERROR_OBJECT (dvrdrmencrypt, "Failed to open a module: %s",
        g_module_error ());
    goto error_module;
  }
/*  if (!g_module_symbol (module_dile, "DILE_DRM_AESHWInit",
          (gpointer *) & DILE_DRM_AESHWInit))
    goto error_symbol;
  if (!g_module_symbol (module_dile, "DILE_DRM_AESHWUpdate",
          (gpointer *) & DILE_DRM_AESHWUpdate))
    goto error_symbol;
  if (!g_module_symbol (module_dile, "DILE_DRM_AESHWFinish",
          (gpointer *) & DILE_DRM_AESHWFinish))
    goto error_symbol;*/
  if (!g_module_symbol (module_dile, "DILE_DRM_GetHWID",
          (gpointer *) & DILE_DRM_GetHWID))
    goto error_symbol;
  if (!g_module_symbol (module_dile, "DILE_DRM_GetSecureData",
          (gpointer *) & DILE_DRM_GetSecureData))
    goto error_symbol;

  module_crypto = g_module_open (LIB_CRYPTO_PATH, G_MODULE_BIND_LAZY);
  if (!module_crypto) {
    GST_ERROR_OBJECT (dvrdrmencrypt, "Failed to open a module: %s",
        g_module_error ());
       goto error_module;
  }
  if (!g_module_symbol (module_crypto, "EVP_EncryptInit",
          (gpointer *) & DVR_EVP_EncryptInit))
    goto error_symbol;
  if (!g_module_symbol (module_crypto, "EVP_EncryptUpdate",
          (gpointer *) & DVR_EVP_EncryptUpdate))
    goto error_symbol;
  if (!g_module_symbol (module_crypto, "EVP_aes_128_ecb",
          (gpointer *) & DVR_EVP_aes_128_ecb))
    goto error_symbol;
  if (!g_module_symbol (module_crypto, "EVP_CIPHER_CTX_init",
          (gpointer *) & DVR_EVP_CIPHER_CTX_init))
    goto error_symbol;
  if (!g_module_symbol (module_crypto, "EVP_CIPHER_CTX_cleanup",
          (gpointer *) & DVR_EVP_CIPHER_CTX_cleanup))
    goto error_symbol;

  module_libdvr = g_module_open (LIB_DVR_PATH, G_MODULE_BIND_LAZY);
  if (!module_libdvr) {
    GST_ERROR_OBJECT (dvrdrmencrypt, "Failed to open a module: %s",
        g_module_error ());
    goto error_module;
  }
  if (!g_module_symbol (module_libdvr, "dvr_crypto_get_cipher_key",
          (gpointer *) & dvr_crypto_get_cipher_key))
    goto error_symbol;

  g_mutex_unlock (&dvrdrm_encrypt_mutex);
  return TRUE;

error_symbol:
  GST_ERROR_OBJECT (dvrdrmencrypt, "Failed to get a symbol: %s",
      g_module_error ());

error_module:
  g_module_close (module_dile);
  g_module_close (module_crypto);
  g_module_close (module_libdvr);
  g_mutex_unlock (&dvrdrm_encrypt_mutex);
  return FALSE;

}


static gboolean
gst_dvrdrm_encrypt_mac_pre (GstDvrDrmEncrypt * dvrdrmencrypt, guchar ** ppMac)
{
  gboolean res = TRUE;
  guint i, t, j = 0;
  guchar temp[7] = "000000";

  if (dvrdrmencrypt->mac == NULL)
    return FALSE;

  for (i = 0; i < 6; i++) {
    t = *(dvrdrmencrypt->mac + 2 * i);

    if ((t >= '0') && (t <= '9'))
      j = (t - '0') << 4;
    else if ((t >= 'a') && (t <= 'f'))
      j = (t - 'a' + 10) << 4;
    else if ((t >= 'A') && (t <= 'F'))
      j = (t - 'A' + 10) << 4;
    else
      res = FALSE;

    t = *(dvrdrmencrypt->mac + 2 * i + 1);

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

  *ppMac = g_strndup (temp, 6);
  GST_DEBUG_OBJECT (dvrdrmencrypt, "mac %02x%02x%02x%02x%02x%02x ",
      (*ppMac)[0], (*ppMac)[1], (*ppMac)[2], (*ppMac)[3], (*ppMac)[4],
      (*ppMac)[5]);
  if (*ppMac == NULL) {
    GST_DEBUG_OBJECT (dvrdrmencrypt, "mac NULL");
    res = FALSE;
  }

  return res;
}


static gboolean
gst_dvrdrm_encrypt_seed_init (GstDvrDrmEncrypt * dvrdrmencrypt)
{
  gboolean res = FALSE;
  guint tempLength = 16;
  guint tempseLength = 24;
  guchar tempkey[16] = { 0, };
  guchar tempsekey[24] = { 0, };
  guchar tempcipher[16] = { 0, };
  guchar serial[12] = { 0, };
  guchar *mac = NULL;

  g_mutex_lock (&dvrdrm_encrypt_mutex);

  if (dvrdrmencrypt->serial == NULL || dvrdrmencrypt->mac == NULL) {
    g_mutex_unlock (&dvrdrm_encrypt_mutex);
    return FALSE;
  }
  if (DILE_DRM_GetSecureData == NULL || dvr_crypto_get_cipher_key == NULL) {
    g_mutex_unlock (&dvrdrm_encrypt_mutex);
    return FALSE;
  }

  if (DILE_DRM_GetSecureData (_gpId_DVRKey, tempkey, &tempLength) == OK) {
    GST_DEBUG_OBJECT (dvrdrmencrypt, "dvr key init ok");
    res = TRUE;
  } else {
    GST_ERROR_OBJECT (dvrdrmencrypt, "dvr key init fail");
    res = FALSE;
    goto seed_fail;
  }

  if (dvrdrmencrypt->seed_type == DVB_SEED) {
    if (DILE_DRM_GetSecureData (_gpId_DVRDeviceSecret, tempsekey,
            &tempseLength) == OK) {
      GST_DEBUG_OBJECT (dvrdrmencrypt, "wrapped cipher key init ok");
      res = TRUE;
    } else {
      GST_ERROR_OBJECT (dvrdrmencrypt, "wrapped cipher key init fail");
      res = FALSE;
      goto seed_fail;
    }
  }

  memcpy (serial, (const gchar *) dvrdrmencrypt->serial,
      strlen (dvrdrmencrypt->serial) >
      12 ? 12 : strlen (dvrdrmencrypt->serial));
  GST_DEBUG_OBJECT (dvrdrmencrypt, "serial num %c%c%c%c%c%c%c%c%c%c%c%c",
      serial[0], serial[1], serial[2], serial[3], serial[4], serial[5],
      serial[6], serial[7], serial[8], serial[9], serial[10], serial[11]);

  if (gst_dvrdrm_encrypt_mac_pre (dvrdrmencrypt, &mac) == TRUE) {
    GST_DEBUG_OBJECT (dvrdrmencrypt, "mac pre ok");
    res = TRUE;
  } else {
    GST_ERROR_OBJECT (dvrdrmencrypt, "mac pre fail");
    res = FALSE;
    goto seed_fail;
  }


  if (dvr_crypto_get_cipher_key (dvrdrmencrypt->seed_type,
          (const guchar *) serial, sizeof (serial), (const guchar *) mac,
          DVR_MAC_ADDR_LENGTH_BYTES, (const guchar *) tempkey,
          (const guchar *) tempsekey, tempcipher) < 0) {
    GST_ERROR_OBJECT (dvrdrmencrypt, "cipher key init fail");
    res = FALSE;
    goto seed_fail;
  } else {
    dvrdrmencrypt->cipher_key = g_memdup (tempcipher, (guint) 16);
    GST_DEBUG_OBJECT (dvrdrmencrypt, "cipher key init ok");
    res = TRUE;
  }

seed_fail:
  g_mutex_unlock (&dvrdrm_encrypt_mutex);
  g_free (mac);
  return res;
}

static gboolean
gst_dvrdrm_encrypt_start (GstBaseTransform * trans)
{
  GstDvrDrmEncrypt *dvrdrmencrypt = GST_DVRDRM_ENCRYPT (trans);

  GST_DEBUG_OBJECT (dvrdrmencrypt, "start");

  /* start the DRM system */
  if (!gst_dvrdrm_encrypt_module_init (dvrdrmencrypt))
    goto error_start;
  if (!gst_dvrdrm_encrypt_seed_init (dvrdrmencrypt))
    goto error_start;

  return TRUE;

error_start:
  GST_ERROR_OBJECT (dvrdrmencrypt, "Failed to start encrypt");
  return FALSE;
}

static gboolean
gst_dvrdrm_encrypt_stop (GstBaseTransform * trans)
{
  GstDvrDrmEncrypt *dvrdrmencrypt = GST_DVRDRM_ENCRYPT (trans);

  GST_DEBUG_OBJECT (dvrdrmencrypt, "stop");

  /* stop the DVR DRM Encrypt system */
  g_mutex_lock (&dvrdrm_encrypt_mutex);
  g_module_close (module_dile);
  g_module_close (module_crypto);
  g_module_close (module_libdvr);
  g_mutex_unlock (&dvrdrm_encrypt_mutex);

  return TRUE;
}

static gboolean
gst_dvrdrm_encrypt_encrypt_init (GstDvrDrmEncrypt * dvrdrmencrypt)
{
  gboolean res = TRUE;

  GST_LOG_OBJECT (dvrdrmencrypt, "encrypt_init");

  if (DILE_DRM_AESHWInit == NULL) {
    GST_ERROR_OBJECT (dvrdrmencrypt, "Failed to init AESHW");
    return FALSE;
  }

  if (DILE_DRM_AESHWInit (2, dvrdrmencrypt->cipher_key, NULL, 0, 0) == -1) {
    GST_ERROR_OBJECT (dvrdrmencrypt, "encrypt init fail");
    res = FALSE;
  }
  return res;
}

static GstFlowReturn
gst_dvrdrm_encrypt_transform_ip (GstBaseTransform * base, GstBuffer * buf)
{
  GstDvrDrmEncrypt *dvrdrmencrypt = GST_DVRDRM_ENCRYPT (base);
  GstDvrDrmEncryptInfo *encrypt_info = &dvrdrmencrypt->encrypt_info;
  GstFlowReturn ret = GST_FLOW_NOT_SUPPORTED;
  GstMapInfo map;
  EVP_CIPHER_CTX ctx;

  guchar *outbuf = NULL;
  guint outbuf_size = 0;

  GST_LOG_OBJECT (dvrdrmencrypt, "encrypt in-place");

  if (!buf) {
    GST_ERROR_OBJECT (dvrdrmencrypt, "Failed to get writable buffer");
    return GST_FLOW_ERROR;
  }
  g_mutex_lock (&dvrdrm_encrypt_mutex);

  if (DVR_EVP_CIPHER_CTX_init == NULL || DVR_EVP_EncryptInit == NULL ||
          DVR_EVP_aes_128_ecb == NULL || DVR_EVP_EncryptUpdate == NULL ||
          DVR_EVP_CIPHER_CTX_cleanup == NULL) {
    GST_ERROR_OBJECT (dvrdrmencrypt, "Failed to init moudle");
    g_mutex_unlock (&dvrdrm_encrypt_mutex);
    return GST_FLOW_ERROR;
  }
  DVR_EVP_CIPHER_CTX_init(&ctx);

  GST_LOG_OBJECT (dvrdrmencrypt, "encrypt_init");
  if (DVR_EVP_EncryptInit(&ctx, DVR_EVP_aes_128_ecb(), dvrdrmencrypt->cipher_key,
          NULL) == FALSE) {
    GST_ERROR_OBJECT (dvrdrmencrypt, "encrypt init fail");
    g_mutex_unlock (&dvrdrm_encrypt_mutex);
    goto release;
  }
/*  if (gst_dvrdrm_encrypt_encrypt_init (dvrdrmencrypt) == FALSE) {
    g_mutex_unlock (&dvrdrm_encrypt_mutex);
    goto release;
  }*/
  if (!gst_buffer_map (buf, &map, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (dvrdrmencrypt, "Failed to map buffer");
    g_mutex_unlock (&dvrdrm_encrypt_mutex);
    goto release;
  }

  GST_LOG_OBJECT (dvrdrmencrypt, "input data size %" G_GSIZE_FORMAT, map.size);

  if (map.size) {
    encrypt_info->offset_block = map.size >> AES128_BLOCKSIZE_RADIX2;
    encrypt_info->offset_byte =
        map.size - (encrypt_info->offset_block << AES128_BLOCKSIZE_RADIX2);
    encrypt_info->data = map.data;
    encrypt_info->data_size = map.size;
    outbuf = (guchar *) g_malloc (map.size);
/*  if (DILE_DRM_AESHWUpdate (outbuf, &outbuf_size, encrypt_info->data,
            encrypt_info->data_size) == -1) {
      g_mutex_unlock (&dvrdrm_encrypt_mutex);
      goto beach;
    }*/
    if (DVR_EVP_EncryptUpdate(&ctx, outbuf, &outbuf_size, encrypt_info->data,
            encrypt_info->data_size) == FALSE) {
      GST_ERROR_OBJECT (dvrdrmencrypt, "encrypt update fail");
      g_mutex_unlock (&dvrdrm_encrypt_mutex);
      goto beach;
    }
    GST_LOG_OBJECT (dvrdrmencrypt, "%d bytes update encrypted", outbuf_size);
    memcpy (map.data, outbuf, outbuf_size);
/*  if (DILE_DRM_AESHWFinish (outbuf, &outbuf_size) == -1) {
      g_mutex_unlock (&dvrdrm_encrypt_mutex);
      goto beach;
    }
    GST_LOG_OBJECT (dvrdrmencrypt, "%d bytes finish encrypted", outbuf_size);
    memcpy (map.data + (map.size - outbuf_size), outbuf, outbuf_size);*/
  }
  g_mutex_unlock (&dvrdrm_encrypt_mutex);
  ret = GST_FLOW_OK;

beach:
  gst_buffer_unmap (buf, &map);
  if (outbuf != NULL) {
    g_free (outbuf);
    outbuf = NULL;
  }
release:
  DVR_EVP_CIPHER_CTX_cleanup(&ctx);

  return ret;
}

static gboolean
gst_dvrdrm_encrypt_sink_event_handler (GstBaseTransform * trans,
    GstEvent * event)
{
  gboolean ret = FALSE;
  GstDvrDrmEncrypt *dvrdrmencrypt = GST_DVRDRM_ENCRYPT (trans);
  GST_LOG_OBJECT (dvrdrmencrypt, "sink event");
  switch (GST_EVENT_TYPE (event)) {
      /*
         case GST_EVENT_PROTECTION:
         GST_DEBUG_OBJECT (dvrdrmencrypt, "received protection event");
         gst_event_parse_protection (event, &system_id, &pssi, &loc);
         GST_DEBUG_OBJECT (dvrdrmencrypt, "system_id: %s", system_id);

         gst_event_unref (event);
         break;
       */
    default:
      ret = GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
      break;
  }


  return ret;
}
