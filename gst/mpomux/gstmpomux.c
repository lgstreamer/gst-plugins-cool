// --------------------------------------------------------------------------
//  LG ELECTRONICS INC., SEOUL, KOREA
//  Copyright(c) 2013 by LG Electronics Inc.
//
//  All rights reserved. No part of this work may be reproduced, stored in a
//  retrieval system, or transmitted by any means without prior written
//  permission of LG Electronics Inc.
// --------------------------------------------------------------------------

/**
 * SECTION:element-mpomux
 *
 * FIXME:Describe mpomux here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! mpomux ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include <string.h>
#include <stdio.h>

#include "gstmpomux.h"

GST_DEBUG_CATEGORY_STATIC (gst_mpomux_debug);
#define GST_CAT_DEFAULT gst_mpomux_debug

#define ELEMENT_NAME "mpomux"
//#define VERSION                "1.0"

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE ("video_sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("image/jpeg")
    );

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE ("video_src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("image/jpeg")
    );

//GST_BOILERPLATE (GstMPOMux, gst_mpomux, GstElement,
 //   GST_TYPE_ELEMENT);

static void gst_mpomux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mpomux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstPad *gst_mpomux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);

static void gst_mpomux_class_init (GstMPOMuxClass * klass);
static void gst_mpomux_init (GstMPOMux * mpomux, GstMPOMuxClass * gclass);

static GstElementClass *parent_class = NULL;

/* TEST */
static gboolean gst_mpomux_setcaps (GstPad * pad, GstCaps * caps);

static gboolean
gst_mpomux_handle_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static gboolean
gst_mpomux_handle_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

GType
gst_mpomux_get_type (void)
{
  static GType gst_mpomux_type = 0;

  if (!gst_mpomux_type) {
    static const GTypeInfo gst_mpomux_info = {
      sizeof (GstMPOMuxClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_mpomux_class_init,
      NULL,
      NULL,
      sizeof (GstMPOMux),
      0,
      (GInstanceInitFunc) gst_mpomux_init,
      NULL
    };

    gst_mpomux_type = g_type_register_static (GST_TYPE_ELEMENT,
        "MPOMux", &gst_mpomux_info, 0);
  }

  return gst_mpomux_type;
}

/* GObject vmethod implementations */

static void
gst_mpomux_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "MPOMux",
      "Generic",
      "MPO muxer with two jpeg input sink pad and one MPO src pad.",
      "Hyunwoo Park <<hyonwoo.park@lge.com>>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

static void
gst_mpomux_class_finalize (GObject * obj)
{
  GstMPOMux *mpomux = GST_MPOMUX (obj);

  if (mpomux->buf != NULL) {
    gst_buffer_unref (mpomux->buf);
  }
  //G_OBJECT_CLASS (parent_class)->finalize (obj);
}

/* initialize the mpomux's class */
static void
gst_mpomux_class_init (GstMPOMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_mpomux_class_finalize;

  gobject_class->set_property = gst_mpomux_set_property;
  gobject_class->get_property = gst_mpomux_get_property;

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_mpomux_request_new_pad);

  gst_element_class_set_details_simple (gstelement_class,
      "MPOMux",
      "Generic",
      "MPO muxer with two jpeg input sink pad and one MPO src pad.",
      "Hyunwoo Park <<hyonwoo.park@lge.com>>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));

}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_mpomux_init (GstMPOMux * mpomux, GstMPOMuxClass * gclass)
{
  GstMPOMux_videopad *vpad;

  mpomux->force_2d = FALSE;
  mpomux->silent = FALSE;
  mpomux->mux_mpo = TRUE;
  mpomux->sinkpad_number = 0;
  mpomux->have_group_id = FALSE;
  mpomux->group_id = G_MAXUINT;

  //if (getenv ("GST_MPOMUX_FORCE_2D") != NULL)
  if (getenv ("GST_MPOMUX_FORCE_2D") != 0)
    mpomux->force_2d = TRUE;

}

#if 0
static void
dump (const char *name, const unsigned char *data, int len)
{
  int a, b;
  gchar *addr = NULL;
  gchar hex[3 * 16 + 1], asc[16 + 1];

  hex[3 * 16] = 0;
  asc[16] = 0;

  if (name != NULL && name[0] != 0)
    GST_DEBUG (name);

  for (a = 0, b = 0; a < len;) {
    if (a % 16 == 0) {
      if (addr != NULL)
        g_free (addr);
      addr = g_strdup_printf ("%04x:", a);
    }
    sprintf (hex + a * 3, " %02x", data[a]);
    a++;
    if (a % 16 == 0) {
      for (; b < a; b++)
        asc[b] = (' ' <= data[b] && data[b] <= '~') ? data[b] : '.';
      GST_DEBUG ("%s%s  %s", addr, hex, asc);
    }
  }

  if (a % 16 != 0) {
    for (; a % 16 != 0; a++)
      sprintf (hex + a * 3, "   ");

    for (; b < len; b++)
      asc[b] = (' ' <= data[b] && data[b] <= '~') ? data[b] : '.';
    asc[b] = 0;
    GST_DEBUG ("%s%s  %s", addr, hex, asc);
  }

  if (addr != NULL)
    g_free (addr);
}
#endif

enum _jpeg_exif_type
{
  jpeg_exif_type_not_defined = 0,
  jpeg_exif_type_byte,
  jpeg_exif_type_ascii,
  jpeg_exif_type_short,
  jpeg_exif_type_long,
  jpeg_exif_type_rational_longs,
  jpeg_exif_type_not_defined2,
  jpeg_exif_type_undefined_8bit,
  jpeg_exif_type_not_defined3,
  jpeg_exif_type_signed_long,
  jpeg_exif_type_rational_signed_longs,
};

static unsigned short
lxjpeg__get16 (const unsigned char *buf, unsigned short byteorder)
{
  if (byteorder == 0x4949)
    return buf[0] | buf[1] << 8;
  else
    return buf[1] | buf[0] << 8;
}

#define lxjpeg_get16(b)        lxjpeg__get16(b,byteorder)

static unsigned int
lxjpeg__get32 (const unsigned char *buf, unsigned short byteorder)
{
  if (byteorder == 0x4949)
    return buf[0] | buf[1] << 8 | buf[2] << 16 | buf[3] << 24;
  else
    return buf[3] | buf[2] << 8 | buf[1] << 16 | buf[0] << 24;
}

#define lxjpeg_get32(b)        lxjpeg__get32(b,byteorder)

static int
parse_ifd (void *priv, const unsigned char *buf_base, int len_base,
    const unsigned char *buf, unsigned short byteorder);

static int
exif_func (void *priv, unsigned short tag, enum _jpeg_exif_type type,
    const unsigned char *data_buf, int size, unsigned short byteorder,
    const unsigned char *base_buf, int base_size)
{
  const static struct
  {
    unsigned short tag;
    const char *name;
  } tag_names[] = {
    {
    0x100, "ImageWidth",}, {
    0x101, "ImageLength",}, {
    0x102, "BitsPerSample",}, {
    0x103, "Compression",}, {
    0x106, "PhotometricInterpretation",}, {
    0x112, "Orientation",}, {
    0x115, "SamplesPerPixel",}, {
    0x11c, "PlanarConfiguration",}, {
    0x212, "YCbCrSubSampling",}, {
    0x213, "YCbCrPositioning",}, {
    0x11a, "XResolution",}, {
    0x11b, "YResolution",}, {
    0x128, "ResolutionUnit",}, {
    0x111, "StripOffsets",}, {
    0x116, "RowsPerStrip",}, {
    0x117, "StripByteCounts",}, {
    0x201, "JPEGInterchangeFormat",}, {
    0x202, "JPEGInterchangeFormatLength",}, {
    0x12d, "TransferFunction",}, {
    0x13e, "WhitePoint",}, {
    0x13f, "PrimaryChromaticities",}, {
    0x211, "YCbCrCoefficients",}, {
    0x214, "ReferenceBlackWhite",}, {
    0x132, "DateTime",}, {
    0x10e, "ImageDescription",}, {
    0x10f, "Make",}, {
    0x110, "Model",}, {
    0x131, "Software",}, {
    0x13b, "Artist",}, {
    0x8298, "Copyright",},
        /* Exif IFD */
    {
    0x8769, "Exif IFD",}, {
    0x9000, "ExifVersion",}, {
    0xa000, "FlashpixVersion",}, {
    0xa001, "ColorSpace",}, {
    0x9101, "ComponentsConfiguration",}, {
    0x9102, "CompressedBitsPerPixel",}, {
    0xa002, "PixelXDimension",}, {
    0xa003, "PixelYDimension",}, {
    0x927c, "MakerNote",}, {
    0x9286, "UserComment",}, {
    0xa004, "RelatedSoundFile",}, {
    0x9003, "DateTimeOriginal",}, {
    0x9004, "DateTimeDigitized",}, {
    0x9290, "SubSecTime",}, {
    0x9291, "SubSecTimeOriginal",}, {
    0x9292, "SubSecTimeDigitized",}, {
    0xa420, "ImageUniqueID",}, {
    0x829a, "ExposureTime",}, {
    0x8822, "ExposureProgram",}, {
    0x8824, "SpectralSensitivity",}, {
    0x8827, "ISOSpeedRatings",}, {
    0x8828, "OECF",}, {
    0x9201, "ShutterSpeedValue",}, {
    0x9202, "ApertureValue",}, {
    0x9203, "BrightnessValue",}, {
    0x9204, "ExposureBiasValue",}, {
    0x9205, "MaxApertureValue",}, {
    0x9206, "SubjectDistance",}, {
    0x9207, "MeteringMode",}, {
    0x9208, "LightSource",}, {
    0x9209, "Flash",}, {
    0x920a, "FocalLength",}, {
    0x9214, "SubjectArea",}, {
    0xa20b, "FlashEnergy",}, {
    0xa20c, "SpatialFrequencyResponse",}, {
    0xa20e, "FocalPlaneXResolution",}, {
    0xa20f, "FocalPlaneYResolution",}, {
    0xa210, "FocalPlaneResolutionUnit",}, {
    0xa214, "SubjectLocation",}, {
    0xa215, "ExposureIndex",}, {
    0xa217, "SensingMethod",}, {
    0xa300, "FileSource",}, {
    0xa301, "SceneType",}, {
    0xa302, "CFAPattern",}, {
    0xa401, "CustomRendered",}, {
    0xa402, "ExposureMode",}, {
    0xa403, "WhiteBalance",}, {
    0xa404, "DigitalZoomRatio",}, {
    0xa405, "FocalLengthIn35mmFilm",}, {
    0xa406, "SceneCaptureType",}, {
    0xa407, "GainControl",}, {
    0xa408, "Contrast",}, {
    0xa409, "Saturation",}, {
    0xa40a, "Sharpness",}, {
    0xa40b, "DeviceSettingDescription",}, {
    0xa40c, "SubjectDistanceRange",},
        /* Multi-Picture Format
         *
         * http://stackoverflow.com/questions/3746272/decoding-an-mpf-exif-block-in-an-mpo-stereo-image-file
         */
    {
    0xb000, "MPFVersion",}, {
    0xb001, "NumberOfImages",}, {
    0xb002, "MPEntry",}, {
    0xb003, "ImageUIDList",}, {
    0xb004, "TotalFrames",}, {
    0xb101, "MPIndividualNum",}, {
    0xb201, "PanOrientation",}, {
    0xb202, "PanOverlap_H",}, {
    0xb203, "PanOverlap_V",}, {
    0xb204, "BaseViewpointNum",}, {
    0xb205, "ConvergenceAngle",}, {
    0xb206, "BaselineLength",}, {
    0xb207, "VerticalDivergence",}, {
    0xb208, "AxisDistance_X",}, {
    0xb209, "AxisDistance_Y",}, {
    0xb20a, "AxisDistance_Z",}, {
    0xb20b, "YawAngle",}, {
    0xb20c, "PitchAngle",}, {
    0xb20d, "RollAngle",},
        /* Fuji Film extended.
         *
         * http://www.ozhiker.com/electronics/pjmt/jpeg_info/fujifilm_mn.html
         */
    {
    0, "Version:FUJIFILM",}, {
    4096, "Quality:FUJIFILM",}, {
    4097, "Sharpness:FUJIFILM",}, {
    4098, "White Balance:FUJIFILM",}, {
    4099, "Colour Saturation:FUJIFILM",}, {
    4100, "Tone (Contrast):FUJIFILM",}, {
    4112, "Flash Mode:FUJIFILM",}, {
    4113, "Flash Strength:FUJIFILM",}, {
    4128, "Macro:FUJIFILM",}, {
    4129, "Focus Mode:FUJIFILM",}, {
    4144, "Slow Sync:FUJIFILM",}, {
    4145, "Picture Mode:FUJIFILM",}, {
    4352, "Continuous taking or auto bracketing mode:FUJIFILM",}, {
    4864, "Blur Warning:FUJIFILM",}, {
    4865, "Focus warning:FUJIFILM",}, {
    4866, "Auto Exposure Warning:FUJIFILM",}, {
  0xffff, "unknown(%04x)",},};
  int b;
  gchar *print_name;

  for (b = 0; tag_names[b].tag != 0xffff; b++)
    if (tag_names[b].tag == tag)
      break;
  {
    char name[strlen (tag_names[b].name) + 16];

    sprintf (name, tag_names[b].name, tag);
    print_name = g_strdup_printf ("%32s(type:%d,size:%d): ", name, type, size);
  }
  if (type == 1) {
    /* byte */
    unsigned char data;

    data = *data_buf;
    GST_DEBUG ("%s%u(0x%02x)", print_name, data, data);
  } else if (type == 2) {
    /* ascii */
    GST_DEBUG ("%s%s", print_name, data_buf);
  } else if (type == 3) {
    /* short */
    unsigned short data;

    data = lxjpeg_get16 (data_buf);
    GST_DEBUG ("%s%u(0x%04x)", print_name, data, data);
  } else if (type == 4) {
    /* long */
    unsigned int data;

    data = lxjpeg_get32 (data_buf);
    GST_DEBUG ("%s%u(0x%08x)", print_name, data, data);

    if (tag == 0xb101) {
      GstMPOMux_videopad *vpad;
      /* it seems to be MPO */

      vpad = (GstMPOMux_videopad *) priv;
      vpad->MPIndividualNum = data;
    } else if (tag == 0xb204) {
      GstMPOMux_videopad *vpad;
      /* it seems to be MPO */

      vpad = (GstMPOMux_videopad *) priv;
      vpad->BaseViewpointNum = data;
    }
  } else if (type == 5) {
    /* rational */
    unsigned int num, den;

    num = lxjpeg_get32 (data_buf);
    den = lxjpeg_get32 (data_buf + 4);
    GST_DEBUG ("%s%u/%u", print_name, num, den);

    if (tag == 0xb206) {
      GstMPOMux_videopad *vpad;
      /* it seems to be MPO */

      vpad = (GstMPOMux_videopad *) priv;
      vpad->BaselineLength_num = num;
      vpad->BaselineLength_den = den;
    }
  } else if (type == 7) {
    /* undefined */

    if ((tag == 0xb000 || tag == 0x9000) && size == 4) {
      unsigned char ver[5];

      memcpy (ver, data_buf, 4);
      ver[4] = 0;
      GST_DEBUG ("%s%s", print_name, ver);
    } else if (tag == 0x9101 && size == 4) {
      GST_DEBUG ("%s%d%d%d%d", print_name,
          data_buf[0], data_buf[1], data_buf[2], data_buf[3]);
    } else if (tag == 0xb002) {
      int a;

      GST_DEBUG ("%s", print_name);
      for (a = 0; a < size; a += 16) {
        GST_DEBUG ("  Individual Image Attribute    : %08x",
            lxjpeg_get32 (data_buf + a + 0));
        GST_DEBUG ("  Individual Image Size         : %08x",
            lxjpeg_get32 (data_buf + a + 4));
        GST_DEBUG ("  Individual Image Data Offset  : %08x",
            lxjpeg_get32 (data_buf + a + 8));
        GST_DEBUG ("  Dependint image 1 Entry Number: %04x",
            lxjpeg_get16 (data_buf + a + 12));
        GST_DEBUG ("  Dependint image 2 Entry Number: %04x",
            lxjpeg_get16 (data_buf + a + 14));
      }
    } else if (tag == 0x927c && size > 8 &&
        data_buf[0] == 'F' &&
        data_buf[1] == 'U' &&
        data_buf[2] == 'J' &&
        data_buf[3] == 'I' &&
        data_buf[4] == 'F' &&
        data_buf[5] == 'I' && data_buf[6] == 'L' && data_buf[7] == 'M') {
      unsigned int offset = lxjpeg_get32 (data_buf + 8);

      GST_DEBUG ("%s", print_name);
      GST_DEBUG ("FUJIFILM extended IFDs...");
      parse_ifd (priv, data_buf, size, data_buf + offset, 0x4949);
      GST_DEBUG ("end of FUJIFILM extended IFDs.");
    } else {
      GST_DEBUG ("%s", print_name);
      //dump (NULL, data_buf, size);
    }
  } else if (type == 9) {
    /* signed long */
    int data;

    data = (int) lxjpeg_get32 (data_buf);
    GST_DEBUG ("%s%d(0x%08x)", print_name, data, data);
  } else if (type == 10) {
    /* signed rational */
    int num, den;

    num = (int) lxjpeg_get32 (data_buf);
    den = (int) lxjpeg_get32 (data_buf + 4);
    GST_DEBUG ("%s%d/%d", print_name, num, den);

    if (tag == 0xb205) {
      GstMPOMux_videopad *vpad;
      /* it seems to be MPO */

      vpad = (GstMPOMux_videopad *) priv;
      vpad->ConvergenceAngle_num = num;
      vpad->ConvergenceAngle_den = den;
    }
  } else
    GST_INFO ("unknown data %d", type);

  g_free (print_name);

  return 0;
}

static int
parse_ifd (void *priv, const unsigned char *buf_base, int len_base,
    const unsigned char *buf, unsigned short byteorder)
{
  unsigned int off;
  int len;
  int a, field_count;
  unsigned int next_offset;

  len = (buf_base + len_base) - buf;

  GST_DEBUG ("length %d", len_base);

  off = 0;
  if (len < 2) {
    GST_DEBUG ("too small buffer length");
    return -1;
  }
  field_count = lxjpeg_get16 (buf + off);
  GST_DEBUG ("field_count %d", field_count);
  off += 2;
  if (len < off + field_count * 12) {
    GST_DEBUG ("too small buffer length");
    return -1;
  }
  for (a = 0; a < field_count; a++) {
    unsigned short tag, type;
    unsigned int count, offset;
    int type_size;
    const static int _type_size[] = {
      0,                        // 0. not defined
      1,                        // 1. byte
      1,                        // 2. ascii
      2,                        // 3. short
      4,                        // 4. long
      8,                        // 5. rational, two longs
      0,                        // 6. not defined
      1,                        // 7. undefined, 8bit
      0,                        // 8. not defined
      4,                        // 9. signed long
      8,                        // 0. rational, two signed longs
    };

    tag = lxjpeg_get16 (buf + off + a * 12 + 0);
    type = lxjpeg_get16 (buf + off + a * 12 + 2);
    count = lxjpeg_get32 (buf + off + a * 12 + 4);
    offset = lxjpeg_get32 (buf + off + a * 12 + 8);

    //GST_DEBUG ("tag %04x, type %04x, count %08x, offset %08x",
    //        tag, type, count, offset);
    if (type >= sizeof (_type_size) / sizeof (_type_size[0])) {
      GST_DEBUG ("unknown type %d", type);
      continue;
    }
    type_size = _type_size[type];
    if (type_size * count <= 4)
      offset = buf - buf_base + off + a * 12 + 8;
    else if (buf - buf_base + len < offset + type_size * count) {
      GST_DEBUG ("too small buffer size");
      continue;
    }

    exif_func (priv, tag, (enum _jpeg_exif_type) type,
        buf_base + offset, count * _type_size[type], byteorder,
        buf_base, len_base);

    if (tag == 0x8769 && type == 4) {
      /* long */
      unsigned int data;

      data = lxjpeg_get32 (buf_base + offset);
      if (tag == 0x8769)
        parse_ifd (priv, buf_base, len_base, buf_base + data, byteorder);
    }
  }

  off += field_count * 12;
  next_offset = lxjpeg_get32 (buf + off);
  GST_DEBUG ("next offset %d(%08x)", next_offset, next_offset);

  return next_offset;
}

static gboolean
gst_mpomux_check_mpo (GstMPOMux * mpomux, GstPad * sinkpad, GstCaps * caps)
{
  GList *tmp;
  GstMPOMux_videopad *vpad;
  const guint8 *codec_buf = NULL;
  guint codec_len;

  /* check that we already did for this sink pad */
  /* search vpad for the sinkpad */
  for (tmp = mpomux->videopads; tmp; tmp = g_list_next (tmp)) {
    vpad = tmp->data;
    if (vpad->sinkpad == sinkpad) {
      if (vpad->configured) {
        GST_DEBUG ("its already checked. it %s the MPO tag",
            vpad->has_mpotag ? "has" : "doesnot have");

        return vpad->has_mpotag;
      } else
        break;
    }
  }
  if (tmp == NULL) {
    GST_ERROR ("Oops??");
    return FALSE;
  }

  GST_DEBUG ("video pad index: %d", vpad->index);

  /* get "codec_data" buffer */
  GstStructure *s;
  const GValue *codec_data;
  GstBuffer *buffer;
  GstMapInfo map;

  s = gst_caps_get_structure (caps, 0);
  codec_data = gst_structure_get_value (s, "codec_data");

  if (codec_data != NULL) {
    buffer = gst_value_get_buffer (codec_data);

    gst_buffer_map (buffer, &map, GST_MAP_READ);

    codec_buf = map.data;
    codec_len = map.size;
  }

  /* check the mpo tag */
  if (codec_buf != NULL &&
      codec_len > 8 &&
      codec_buf[0] == 'A' &&
      codec_buf[1] == 'V' && codec_buf[2] == 'I' && codec_buf[3] == 'F') {
    /* initial value */
    vpad->MPIndividualNum = 0;
    vpad->BaseViewpointNum = 1;
    vpad->ConvergenceAngle_num = 0;
    vpad->ConvergenceAngle_den = 1;
    vpad->BaselineLength_num = 0;
    vpad->BaselineLength_den = 1;

    /* parse the tag */
    parse_ifd (vpad, codec_buf + 8, codec_len - 8, codec_buf + 8, 0x4949);

    gst_buffer_unmap (buffer, &map);

    /* FIXME:
     * check the MPO tag here */
    if (vpad->MPIndividualNum != 0) {
      GST_DEBUG ("we have MPO tag. %d", vpad->MPIndividualNum);
      vpad->has_mpotag = TRUE;
    } else
      vpad->has_mpotag = FALSE;
  }

  if (mpomux->force_2d != FALSE) {
    GST_DEBUG ("force to 2D");
    vpad->has_mpotag = FALSE;
  }


  /* if there is no mpo tag on this or the first sink pad,
   * make new src pad */
  if (vpad->index == 0 || vpad->has_mpotag == FALSE) {
    GstElement *element = GST_ELEMENT (mpomux);
    GstEvent *event;
    gchar *name;
    gchar *stream_id;
    GstPadTemplate *pad_tmpl;

    name = g_strdup_printf ("video_%u", vpad->index);
    GST_DEBUG ("make srcpad, %s", name);

    pad_tmpl = gst_static_pad_template_get (&src_factory);
    vpad->srcpad = gst_pad_new_from_template (pad_tmpl, name);
    g_free (name);
    gst_object_unref (pad_tmpl);

    /* zeldasky */
    gst_pad_set_event_function (vpad->srcpad,
        GST_DEBUG_FUNCPTR (gst_mpomux_handle_src_event));

    gst_pad_set_active (vpad->srcpad, TRUE);
    gst_element_add_pad (element, vpad->srcpad);

    stream_id =
        gst_pad_create_stream_id_printf (vpad->srcpad,
        GST_ELEMENT_CAST (mpomux), "%03u", vpad->index);

    event = gst_pad_get_sticky_event (vpad->sinkpad, GST_EVENT_STREAM_START, 0);
    if (event) {
      if (gst_event_parse_group_id (event, &mpomux->group_id))
        mpomux->have_group_id = TRUE;
      else
        mpomux->have_group_id = FALSE;
      gst_event_unref (event);
    } else if (!mpomux->have_group_id) {
      mpomux->have_group_id = TRUE;
      mpomux->group_id = gst_util_group_id_next ();
    }

    event = gst_event_new_stream_start (stream_id);
    if (mpomux->have_group_id)
      gst_event_set_group_id (event, mpomux->group_id);

    gst_pad_push_event (vpad->srcpad, event);
    g_free (stream_id);

    gst_pad_set_caps (vpad->srcpad, caps);
  }

  vpad->configured = TRUE;
  mpomux->mux_mpo = mpomux->mux_mpo && vpad->has_mpotag;

  return vpad->has_mpotag;
}

static gboolean
gst_mpomux_setcaps (GstPad * pad, GstCaps * caps)
{
  GstMPOMux *mpomux;

  mpomux = GST_MPOMUX (gst_pad_get_parent (pad));

  //GST_DEBUG ("set caps %"GST_PTR_FORMAT, gst_pad_get_current_caps (caps));

  gst_mpomux_check_mpo (mpomux, pad, caps);

  gst_object_unref (mpomux);

  return TRUE;
}

static GstPadLinkReturn
gst_mpomux_link (GstPad * pad, GstObject * parent, GstPad * peer)
{
  GstMPOMux *mpomux;
  GstCaps *caps;
  /* TEST */
  GList *tmp;
  GstMPOMux_videopad *vpad;

  mpomux = GST_MPOMUX (gst_pad_get_parent (pad));

  GST_DEBUG ("we got a peer");

  caps = gst_pad_get_current_caps (peer);

  if (caps != NULL) {
    GST_DEBUG ("check mpo with peer pad caps");
    gst_mpomux_check_mpo (mpomux, pad, caps);
  }

  gst_object_unref (mpomux);
  return GST_PAD_LINK_OK;
}

static GstFlowReturn
gst_mpomux_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstMPOMux *mpomux;
  GstPad *srcpad = NULL;
  GList *tmp;
  GstMPOMux_videopad *vpad;
  GstMapInfo bufmap, mpoBuf_map, outBuf_map;

  gst_buffer_map (buf, &bufmap, GST_MAP_READ);

  mpomux = GST_MPOMUX (GST_OBJECT_PARENT (pad));

  /* get the default src pad */
  tmp = g_list_first (mpomux->videopads);
  if (tmp) {
    vpad = tmp->data;
    srcpad = vpad->srcpad;
  }
  if (srcpad == NULL) {
    GST_WARNING ("no source pad??");
    gst_buffer_unmap (buf, &bufmap);
    return GST_FLOW_ERROR;
  }

  /* if we have onlf one sink pad, just bypass the data */
  if (mpomux->sinkpad_number < 2) {
    GST_DEBUG ("we only have single videa pad. push.");
    gst_buffer_unmap (buf, &bufmap);
    return gst_pad_push (srcpad, buf);
  }

  /* search the pad on the registered list */
  vpad = NULL;
  for (tmp = mpomux->videopads; tmp; tmp = g_list_next (tmp)) {
    vpad = tmp->data;

    if (vpad->sinkpad == pad)
      break;
  }

  if (vpad == NULL) {
    GST_ERROR ("Oops???");
    gst_buffer_unmap (buf, &bufmap);
    return GST_FLOW_ERROR;
  }

  if (vpad->srcpad != NULL)
    srcpad = vpad->srcpad;

  if (mpomux->mux_mpo != TRUE) {
    GST_DEBUG ("not a MPO???");
    gst_buffer_unmap (buf, &bufmap);
    return gst_pad_push (srcpad, buf);
  } else {
    GST_DEBUG ("mux the stream. save the buffer to the local buffer,"
        " %dth sinkpad, %d bytes", vpad->index, bufmap.size);

    if (bufmap.size < 2) {
      GST_WARNING ("too small input buffer");
      gst_buffer_unmap (buf, &bufmap);
      return GST_FLOW_OK;
    }

    /* mux */
    if (vpad->index + 1 < mpomux->sinkpad_number) {
      /*
       * sample jpeg data of MPO
       *
       *  first part of jpeg.
       *
       *     ffe2, size:   bc, Application specific
       *     MPF data
       *     0000: 4d 50 46 00 49 49 2a 00 08 00 00 00 03 00 00 b0  MPF.II*.........
       *                       |     |     |           |     + first field
       *                       |     |     |           + field count
       *                       |     |     + data offset from the byteorder
       *                       |     + magic
       *                       + byteorder
       *     0010: 07 00 04 00 00 00 30 31 30 30 01 b0 04 00 01 00  ......0100......
       *                                         + second field
       *     0020: 00 00 02 00 00 00 02 b0 07 00 20 00 00 00 32 00  .......... ...2.
       *                             + third field
       *     0030: 00 00 52 00 00 00 02 00 02 20 00 19 48 00 00 00  ..R...... ..H...
       *                 |           + start of MPEntry
       *                 + next offset
       *     0040: 00 00 00 00 00 00 02 00 02 00 bf 10 4a 00 36 8e  ............J.6.
       *     0050: 47 00 00 00 00 00 04 00 01 b1 04 00 01 00 00 00  G...............
       *                             |     + firest field
       *                             + field count
       *     0060: 01 00 00 00 04 b2 04 00 01 00 00 00 01 00 00 00  ................
       *                       + second field
       *     0070: 05 b2 0a 00 01 00 00 00 88 00 00 00 06 b2 05 00  ................
       *           |                                   + forth field
       *           + third field
       *     0080: 01 00 00 00 90 00 00 00 00 00 00 00 00 00 00 00  ................
       *                                               | ConvergenceAngle
       *     0090: 01 00 00 00 4d 00 00 00 e8 03 00 00 00 00 00 00  ....M...........
       *                       | BaselineLength
       *     00a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
       *     00b0: 00 00 00 00 00 00 00 00 00 00 00 00              ............
       *                           MPFVersion: 0100
       *                       NumberOfImages: 2(0x00000002)
       *                              MPEntry:
       *       Individual Image Attribute    : 20020002
       *       Individual Image Size         : 00481900
       *       Individual Image Data Offset  : 00000000
       *       Dependint image 1 Entry Number: 0000
       *       Dependint image 2 Entry Number: 0000
       *       Individual Image Attribute    : 00020002
       *       Individual Image Size         : 004a10bf
       *       Individual Image Data Offset  : 00478e36
       *       Dependint image 1 Entry Number: 0000
       *       Dependint image 2 Entry Number: 0000
       *     parse_ifd.212 next offset 82(00000052)
       *                      MPIndividualNum: 1(0x00000001)
       *                     BaseViewpointNum: 1(0x00000001)
       *                     ConvergenceAngle: 0/1
       *                       BaselineLength: 77/1000
       *     parse_ifd.212 next offset 0(00000000)
       *
       *
       *  second part of jpeg
       *
       *     ffe2, size:   7c, Application specific
       *     MPF data
       *     0000: 4d 50 46 00 49 49 2a 00 08 00 00 00 05 00 00 b0  MPF.II*.........
       *                       |     |     |           |     + first field
       *                       |     |     |           + field count
       *                       |     |     + data offset from the byteorder
       *                       |     + magic
       *                       + byteorder
       *     0010: 07 00 04 00 00 00 30 31 30 30 01 b1 04 00 01 00  ......0100......
       *                                         + second field
       *     0020: 00 00 02 00 00 00 04 b2 04 00 01 00 00 00 01 00  ................
       *                             + third field
       *     0030: 00 00 05 b2 0a 00 01 00 00 00 4a 00 00 00 06 b2  ..........J.....
       *                 |                                   + fifth field
       *                 + forth field
       *     0040: 05 00 01 00 00 00 52 00 00 00 00 00 00 00 16 00  ......R.........
       *                                                     + ConvergenceAngle
       *     0050: 00 00 0a 00 00 00 4d 00 00 00 e8 03 00 00 00 00  ......M.........
       *                             + BaselineLength
       *     0060: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
       *     0070: 00 00 00 00 00 00 00 00 00 00 00 00              ............
       *                           MPFVersion: 0100
       *                      MPIndividualNum: 2(0x00000002)
       *                     BaseViewpointNum: 1(0x00000001)
       *                     ConvergenceAngle: 22/10
       *                       BaselineLength: 77/1000
       *     parse_ifd.212 next offset 0(00000000)
       *
       */
      const guint8 marker_data[] = {
        0xff, 0xe2, 0x00, 0xbe,
        0x4d, 0x50, 0x46, 0x00, 0x49, 0x49, 0x2a, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0xb0,
        0x07, 0x00, 0x04, 0x00, 0x00, 0x00, 0x30, 0x31,
        0x30, 0x30, 0x01, 0xb0, 0x04, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0xb0,
        0x07, 0x00, 0x20, 0x00, 0x00, 0x00, 0x32, 0x00,
        0x00, 0x00, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00,
        0x02, 0x20, 0x00, 0x19, 0x48, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
        0x02, 0x00, 0xbf, 0x10, 0x4a, 0x00, 0x36, 0x8e,
        0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
        0x01, 0xb1, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x04, 0xb2, 0x04, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x05, 0xb2, 0x0a, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x88, 0x00, 0x00, 0x00, 0x06, 0xb2, 0x05, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x4d, 0x00, 0x00, 0x00,
        0xe8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
      };

      if (mpomux->buf != NULL)
        gst_buffer_unref (mpomux->buf);

      mpomux->buf =
          gst_buffer_new_and_alloc (bufmap.size + sizeof (marker_data));

      if (mpomux->buf != NULL) {
        guint8 *src, *dst;
        guint src_len, dst_len;

        gst_buffer_map (mpomux->buf, &mpoBuf_map, GST_MAP_READWRITE);

        src = bufmap.data;
        src_len = bufmap.size;

        dst = mpoBuf_map.data;
        dst_len = mpoBuf_map.size;

        memcpy (dst, src, 2);   // copy SOI
        memcpy (dst + 2, marker_data, sizeof (marker_data));
        memcpy (dst + 2 + sizeof (marker_data), src + 2, src_len - 2);

        /* MPEntry:1st Individual Image Size */
        dst[2 + 4 + 0x3a + 0] = (dst_len >> 0) & 0xff;
        dst[2 + 4 + 0x3a + 1] = (dst_len >> 8) & 0xff;
        dst[2 + 4 + 0x3a + 2] = (dst_len >> 16) & 0xff;
        dst[2 + 4 + 0x3a + 3] = (dst_len >> 24) & 0xff;

        /* MPIndividualNum */
        dst[2 + 4 + 0x60 + 0] = (vpad->MPIndividualNum >> 0) & 0xff;
        dst[2 + 4 + 0x60 + 1] = (vpad->MPIndividualNum >> 8) & 0xff;
        dst[2 + 4 + 0x60 + 2] = (vpad->MPIndividualNum >> 16) & 0xff;
        dst[2 + 4 + 0x60 + 3] = (vpad->MPIndividualNum >> 24) & 0xff;

        /* BaseViewpointNum */
        dst[2 + 4 + 0x6c + 0] = (vpad->BaseViewpointNum >> 0) & 0xff;
        dst[2 + 4 + 0x6c + 1] = (vpad->BaseViewpointNum >> 8) & 0xff;
        dst[2 + 4 + 0x6c + 2] = (vpad->BaseViewpointNum >> 16) & 0xff;
        dst[2 + 4 + 0x6c + 3] = (vpad->BaseViewpointNum >> 24) & 0xff;

        /* ConvergenceAngle_num */
        dst[2 + 4 + 0x8c + 0] = (vpad->ConvergenceAngle_num >> 0) & 0xff;
        dst[2 + 4 + 0x8c + 1] = (vpad->ConvergenceAngle_num >> 8) & 0xff;
        dst[2 + 4 + 0x8c + 2] = (vpad->ConvergenceAngle_num >> 16) & 0xff;
        dst[2 + 4 + 0x8c + 3] = (vpad->ConvergenceAngle_num >> 24) & 0xff;

        /* ConvergenceAngle_den */
        dst[2 + 4 + 0x90 + 0] = (vpad->ConvergenceAngle_den >> 0) & 0xff;
        dst[2 + 4 + 0x90 + 1] = (vpad->ConvergenceAngle_den >> 8) & 0xff;
        dst[2 + 4 + 0x90 + 2] = (vpad->ConvergenceAngle_den >> 16) & 0xff;
        dst[2 + 4 + 0x90 + 3] = (vpad->ConvergenceAngle_den >> 24) & 0xff;

        /* BaselineLength_num */
        dst[2 + 4 + 0x94 + 0] = (vpad->BaselineLength_num >> 0) & 0xff;
        dst[2 + 4 + 0x94 + 1] = (vpad->BaselineLength_num >> 8) & 0xff;
        dst[2 + 4 + 0x94 + 2] = (vpad->BaselineLength_num >> 16) & 0xff;
        dst[2 + 4 + 0x94 + 3] = (vpad->BaselineLength_num >> 24) & 0xff;

        /* BaselineLength_den */
        dst[2 + 4 + 0x98 + 0] = (vpad->BaselineLength_den >> 0) & 0xff;
        dst[2 + 4 + 0x98 + 1] = (vpad->BaselineLength_den >> 8) & 0xff;
        dst[2 + 4 + 0x98 + 2] = (vpad->BaselineLength_den >> 16) & 0xff;
        dst[2 + 4 + 0x98 + 3] = (vpad->BaselineLength_den >> 24) & 0xff;

        gst_buffer_unmap (mpomux->buf, &mpoBuf_map);
      } else
        GST_WARNING ("Oops");

      gst_buffer_unmap (buf, &bufmap);
      gst_buffer_unref (buf);

      return GST_FLOW_OK;
    } else if (vpad->index + 1 == mpomux->sinkpad_number) {
      guint8 *src, *dst;
      guint src_len;

      GstBuffer *outBuf = NULL;

      const guint8 marker_data[] = {
        0xff, 0xe2, 0x00, 0x7e,
        0x4d, 0x50, 0x46, 0x00, 0x49, 0x49, 0x2a, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0xb0,
        0x07, 0x00, 0x04, 0x00, 0x00, 0x00, 0x30, 0x31,
        0x30, 0x30, 0x01, 0xb1, 0x04, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0xb2,
        0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x05, 0xb2, 0x0a, 0x00, 0x01, 0x00,
        0x00, 0x00, 0x4a, 0x00, 0x00, 0x00, 0x06, 0xb2,
        0x05, 0x00, 0x01, 0x00, 0x00, 0x00, 0x52, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x00,
        0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x4d, 0x00,
        0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
      };

      if (gst_buffer_get_size (mpomux->buf) == 0) {
        gst_buffer_unmap (buf, &bufmap);
        gst_buffer_unref (buf);
        return GST_FLOW_OK;
      }

      gst_buffer_map (mpomux->buf, &mpoBuf_map, GST_MAP_READWRITE);

      outBuf = gst_buffer_new_and_alloc
          (bufmap.size + mpoBuf_map.size + sizeof (marker_data));

      gst_buffer_map (outBuf, &outBuf_map, GST_MAP_READWRITE);

      src = mpoBuf_map.data;
      src_len = mpoBuf_map.size;
      dst = outBuf_map.data;

      memcpy (dst, src, src_len);

      /* MPEntry:2ed Individual Image Size */
      dst[2 + 4 + 0x4a + 0] = ((bufmap.size + 0x80) >> 0) & 0xff;
      dst[2 + 4 + 0x4a + 1] = ((bufmap.size + 0x80) >> 8) & 0xff;
      dst[2 + 4 + 0x4a + 2] = ((bufmap.size + 0x80) >> 16) & 0xff;
      dst[2 + 4 + 0x4a + 3] = ((bufmap.size + 0x80) >> 24) & 0xff;

      /* MPEntry:2ed Individual Image Data Offset */
      dst[2 + 4 + 0x4e + 0] = ((src_len - 10) >> 0) & 0xff;
      dst[2 + 4 + 0x4e + 1] = ((src_len - 10) >> 8) & 0xff;
      dst[2 + 4 + 0x4e + 2] = ((src_len - 10) >> 16) & 0xff;
      dst[2 + 4 + 0x4e + 3] = ((src_len - 10) >> 24) & 0xff;

      dst = dst + src_len;

      src = bufmap.data;
      src_len = bufmap.size;

      memcpy (dst, src, 2);
      memcpy (dst + 2, marker_data, sizeof (marker_data));
      memcpy (dst + 2 + sizeof (marker_data), src + 2, src_len - 2);

      /* MPIndividualNum */
      dst[2 + 4 + 0x22 + 0] = (vpad->MPIndividualNum >> 0) & 0xff;
      dst[2 + 4 + 0x22 + 1] = (vpad->MPIndividualNum >> 8) & 0xff;
      dst[2 + 4 + 0x22 + 2] = (vpad->MPIndividualNum >> 16) & 0xff;
      dst[2 + 4 + 0x22 + 3] = (vpad->MPIndividualNum >> 24) & 0xff;

      /* BaseViewpointNum */
      dst[2 + 4 + 0x2e + 0] = (vpad->BaseViewpointNum >> 0) & 0xff;
      dst[2 + 4 + 0x2e + 1] = (vpad->BaseViewpointNum >> 8) & 0xff;
      dst[2 + 4 + 0x2e + 2] = (vpad->BaseViewpointNum >> 16) & 0xff;
      dst[2 + 4 + 0x2e + 3] = (vpad->BaseViewpointNum >> 24) & 0xff;

      /* ConvergenceAngle_num */
      dst[2 + 4 + 0x4e + 0] = (vpad->ConvergenceAngle_num >> 0) & 0xff;
      dst[2 + 4 + 0x4e + 1] = (vpad->ConvergenceAngle_num >> 8) & 0xff;
      dst[2 + 4 + 0x4e + 2] = (vpad->ConvergenceAngle_num >> 16) & 0xff;
      dst[2 + 4 + 0x4e + 3] = (vpad->ConvergenceAngle_num >> 24) & 0xff;

      /* ConvergenceAngle_den */
      dst[2 + 4 + 0x52 + 0] = (vpad->ConvergenceAngle_den >> 0) & 0xff;
      dst[2 + 4 + 0x52 + 1] = (vpad->ConvergenceAngle_den >> 8) & 0xff;
      dst[2 + 4 + 0x52 + 2] = (vpad->ConvergenceAngle_den >> 16) & 0xff;
      dst[2 + 4 + 0x52 + 3] = (vpad->ConvergenceAngle_den >> 24) & 0xff;

      /* BaselineLength_num */
      dst[2 + 4 + 0x56 + 0] = (vpad->BaselineLength_num >> 0) & 0xff;
      dst[2 + 4 + 0x56 + 1] = (vpad->BaselineLength_num >> 8) & 0xff;
      dst[2 + 4 + 0x56 + 2] = (vpad->BaselineLength_num >> 16) & 0xff;
      dst[2 + 4 + 0x56 + 3] = (vpad->BaselineLength_num >> 24) & 0xff;

      /* BaselineLength_den */
      dst[2 + 4 + 0x5a + 0] = (vpad->BaselineLength_den >> 0) & 0xff;
      dst[2 + 4 + 0x5a + 1] = (vpad->BaselineLength_den >> 8) & 0xff;
      dst[2 + 4 + 0x5a + 2] = (vpad->BaselineLength_den >> 16) & 0xff;
      dst[2 + 4 + 0x5a + 3] = (vpad->BaselineLength_den >> 24) & 0xff;

      GST_BUFFER_TIMESTAMP (outBuf) = GST_BUFFER_TIMESTAMP (buf);
      GST_BUFFER_DURATION (outBuf) = GST_BUFFER_DURATION (buf);

      GST_DEBUG ("push %d bytes.", outBuf_map.size);

      gst_buffer_unmap (mpomux->buf, &mpoBuf_map);
      gst_buffer_unmap (outBuf, &outBuf_map);
      gst_buffer_unmap (buf, &bufmap);

      gst_buffer_unref (buf);
      gst_buffer_unref (mpomux->buf);

      gst_buffer_ref (outBuf);

      mpomux->buf = NULL;

      return gst_pad_push (srcpad, outBuf);

    } else {
      gst_buffer_unmap (buf, &bufmap);
      return GST_FLOW_OK;
    }
  }
}

static GstPad *
gst_mpomux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps)
{
  GstMPOMux *mpomux;
  GstElementClass *klass;
  GstPad *pad;

  mpomux = GST_MPOMUX (element);
  klass = GST_ELEMENT_GET_CLASS (element);

  GST_DEBUG ("request pad - %s, caps - %" GST_PTR_FORMAT, name, caps);

  if (templ->direction != GST_PAD_SINK) {
    GST_WARNING ("wrong direction");
    return NULL;
  }

  if (templ == gst_element_class_get_pad_template (klass, "video_sink_%u")) {
    gchar *name;
    GstMPOMux_videopad *vpad;

    name = g_strdup_printf ("video_sink_%u", mpomux->sinkpad_number);
    pad = gst_pad_new_from_template (templ, name);
    g_free (name);

    //gst_pad_set_setcaps_function (pad, GST_DEBUG_FUNCPTR (gst_mpomux_setcaps));
    gst_pad_set_chain_function (pad, GST_DEBUG_FUNCPTR (gst_mpomux_chain));
    gst_pad_set_link_function (pad, GST_DEBUG_FUNCPTR (gst_mpomux_link));
    gst_pad_set_active (pad, TRUE);

    gst_element_add_pad (element, pad);

    vpad = g_malloc0 (sizeof (*vpad));
    vpad->sinkpad = pad;
    vpad->index = mpomux->sinkpad_number;
    vpad->configured = FALSE;
    mpomux->videopads = g_list_append (mpomux->videopads, vpad);

    mpomux->sinkpad_number++;

    /* zeldasky */
    gst_pad_set_event_function (vpad->sinkpad,
        GST_DEBUG_FUNCPTR (gst_mpomux_handle_sink_event));

    return pad;
  } else {
    GST_WARNING ("unknown pad template");
    return NULL;
  }
}

static void
gst_mpomux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMPOMux *mpomux = GST_MPOMUX (object);

  switch (prop_id) {
    case PROP_SILENT:
      mpomux->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mpomux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMPOMux *mpomux = GST_MPOMUX (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, mpomux->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* zeldasky */
static gboolean
gst_mpomux_handle_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean res = TRUE;

  GST_DEBUG ("Event Name %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);

      res = gst_mpomux_setcaps (pad, caps);

      break;
    }
    case GST_EVENT_STREAM_START:
      break;
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }

  return res;
}

static gboolean
gst_mpomux_handle_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res = TRUE;

  GST_DEBUG ("Event Name %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);

      res = gst_mpomux_setcaps (pad, caps);

      break;
    }
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }

  return res;
}

/* GstElement vmethod implementations */

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
mpomux_init (GstPlugin * mpomux)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template mpomux' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_mpomux_debug, ELEMENT_NAME,
      0, "Template mpomux");

  return gst_element_register (mpomux, ELEMENT_NAME, GST_RANK_NONE,
      GST_TYPE_MPOMUX);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstmpomux"
#endif

/* gstreamer looks for this structure to register mpomuxs
 *
 * exchange the string 'Template mpomux' with your mpomux description
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mpomux,
    "Template mpomux",
    mpomux_init, PACKAGE_VERSION, "LGPL", PACKAGE_NAME, PACKAGE_VERSION)
// vim:set sw=2 et:
