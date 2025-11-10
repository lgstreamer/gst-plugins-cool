/* GStreamer DVR DRM decrypt element
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

#ifndef  __GST_DVRDRM_DECRYPT_H__
#define  __GST_DVRDRM_DECRYPT_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

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
#define GST_TYPE_DVRDRM_DECRYPT                (gst_dvrdrm_decrypt_get_type())
#define GST_DVRDRM_DECRYPT(obj)                (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DVRDRM_DECRYPT,GstDvrDrmDecrypt))
#define GST_DVRDRM_DECRYPT_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DVRDRM_DECRYPT,GstDvrDrmDecryptClass))
#define GST_DVRDRM_DECRYPT_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_DVRDRM_DECRYPT, GstDvrDrmDecryptClass))
#define GST_IS_DVRDRM_DECRYPT(obj)             (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DVRDRM_DECRYPT))
#define GST_IS_DVRDRM_DECRYPT_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DVRDRM_DECRYPT))
#define GST_DVRDRM_DECRYPT_CAST(obj)           ((GstDvrDrmDecrypt *) (obj))
typedef struct _GstDvrDrmDecrypt GstDvrDrmDecrypt;
typedef struct _GstDvrDrmDecryptClass GstDvrDrmDecryptClass;
typedef struct _GstDvrDrmDecryptInfo GstDvrDrmDecryptInfo;
typedef struct _GstDvrSystemInfo GstDvrSystemInfo;

#define MAKE_FOURCC(a, b, c, d) \
    ((guint) ((a) << 24 | (b) << 16 | (c) << 8 | (d)))

struct _GstDvrDrmDecryptInfo
{
  guint8 *data;
  guint data_size;
  guint64 offset_block;
  guint16 offset_byte;
};

struct _GstDvrSystemInfo
{
  GList *system_ids;
  guint8 *xml_node_name;
  gchar *drmclient_id;
};

/**
 * GstDvrDrmDecrypt:
 * Dvr Drm Decryptobject
 */
struct _GstDvrDrmDecrypt
{
  GstBaseTransform element;

  GstDvrDrmDecryptInfo decrypt_info;
  GstDvrSystemInfo dvr_system_info;
  gchar *mac;
  gchar *serial;
  gchar *broadcast_type;
  guint seed_type;
  guchar *cipher_key;
  gchar *mediauri;
};

typedef struct _GstDvr_atom_t {
  guint32 type;
  guint8 *data;
  guint32 size;
} GstDvr_atom_t;

struct _GstDvrDrmDecryptClass
{
  GstBaseTransformClass parent_class;
};

GType gst_dvrdrm_decrypt_get_type (void);

G_END_DECLS
#endif
