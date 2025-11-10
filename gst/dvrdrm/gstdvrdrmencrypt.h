/* GStreamer DVR DRM encrypt element
 * Copyright (C) 2016 LG Electronics, Inc.
 *
 * Authors:
 *   Jinuk Jeon <jinuk.jeon@lge.com>
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

#ifndef  __GST_DVRDRM_ENCRYPT_H__
#define  __GST_DVRDRM_ENCRYPT_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <openssl/evp.h>

/* Begin Declaration */
G_BEGIN_DECLS
#ifdef DISABLE_FLAC
#define FLAC_AUDIO_CAPS
#else
#define FLAC_AUDIO_CAPS \
    "audio/x-flac"
#endif
#define SINK_DECODE_AUDIO_CAPS \
    "audio/mpeg, "\
    "audio/mpeg-h, "\
    "audio/x-dts, " \
    "audio/x-dtsh, " \
    "audio/x-dtsl, " \
    "audio/x-dtse, " \
    "audio/x-ac3, " \
    "audio/x-eac3, " \
    "audio/x-ac4, " \
    "audio/x-private1-ac3, " \
    "audio/x-wma, " \
    "audio/x-lpcm-1, " \
    "audio/x-lpcm, " \
    "audio/x-private-lg-lpcm, " \
    "audio/x-private1-lpcm, " \
    "audio/x-private-ts-lpcm, " \
    "audio/x-adpcm, " \
    "audio/x-vorbis, " \
    "audio/AMR, " \
    "audio/AMR-WB, " \
    FLAC_AUDIO_CAPS ", " \
    "audio/x-mulaw, " \
    "audio/x-alaw, " \
    "audio/x-private1-dts, " \
    "audio/x-opus"
#define SRC_DECODE_AUDIO_CAPS \
    "audio/mpeg;"\
    "audio/mpeg-h;"\
    "audio/x-dts;" \
    "audio/x-dtsh;" \
    "audio/x-dtsl;" \
    "audio/x-dtse;" \
    "audio/x-ac3;" \
    "audio/x-eac3;" \
    "audio/x-ac4;" \
    "audio/x-private1-ac3;" \
    "audio/x-wma;" \
    "audio/x-lpcm-1;" \
    "audio/x-lpcm;" \
    "audio/x-private-lg-lpcm;" \
    "audio/x-private1-lpcm;" \
    "audio/x-private-ts-lpcm;" \
    "audio/x-adpcm;" \
    "audio/x-vorbis;" \
    "audio/AMR;" \
    "audio/AMR-WB;" \
    FLAC_AUDIO_CAPS ";" \
    "audio/x-mulaw;" \
    "audio/x-alaw;" \
    "audio/x-private1-dts;" \
    "audio/x-opus"
#define GST_TYPE_DVRDRM_ENCRYPT                (gst_dvrdrm_encrypt_get_type())
#define GST_DVRDRM_ENCRYPT(obj)                (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DVRDRM_ENCRYPT,GstDvrDrmEncrypt))
#define GST_DVRDRM_ENCRYPT_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DVRDRM_ENCRYPT,GstDvrDrmEncryptClass))
#define GST_DVRDRM_ENCRYPT_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_DVRDRM_ENCRYPT, GstDvrDrmEncryptClass))
#define GST_IS_DVRDRM_ENCRYPT(obj)             (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DVRDRM_ENCRYPT))
#define GST_IS_DVRDRM_ENCRYPT_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DVRDRM_ENCRYPT))
#define GST_DVRDRM_ENCRYPT_CAST(obj)           ((GstDvrDrmEncrypt *) (obj))
typedef struct _GstDvrDrmEncrypt GstDvrDrmEncrypt;
typedef struct _GstDvrDrmEncryptClass GstDvrDrmEncryptClass;
typedef struct _GstDvrDrmEncryptInfo GstDvrDrmEncryptInfo;

struct _GstDvrDrmEncryptInfo
{
  guint8 *data;
  guint data_size;
  guint64 offset_block;
  guint16 offset_byte;
};


/**
 * GstDvrDrmEncrypt:
 * Dvr Drm Encryptobject
 */
struct _GstDvrDrmEncrypt
{
  GstBaseTransform element;

  GstDvrDrmEncryptInfo encrypt_info;
  gchar *mac;
  gchar *serial;
  gchar *broadcast_type;
  guint seed_type;
  guchar *cipher_key;
};

struct _GstDvrDrmEncryptClass
{
  GstBaseTransformClass parent_class;
};

GType gst_dvrdrm_encrypt_get_type (void);

G_END_DECLS
#endif
