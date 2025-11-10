/*
 * tsdemux.c
 * Copyright (C) 2009 Zaheer Abbas Merali
 *               2010 Edward Hervey
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *  Author: Youness Alaoui <youness.alaoui@collabora.co.uk>, Collabora Ltd.
 *  Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 *  Author: Edward Hervey <bilboed@bilboed.com>, Collabora Ltd.
 *
 * Authors:
 *   Zaheer Abbas Merali <zaheerabbas at merali dot org>
 *   Edward Hervey <edward.hervey@collabora.co.uk>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include <glib.h>
#include <gst/tag/tag.h>
#include <gst/pbutils/pbutils.h>
#include <gst/base/base.h>
#include <gst/audio/audio.h>

#include "mpegtsbase.h"
#include "tsdemux.h"
#include "gstmpegdesc.h"
#include "gstmpegdefs.h"
#include "mpegtspacketizer.h"
#include "pesparse.h"
#include <gst/codecparsers/gsth264parser.h>
#include <gst/codecparsers/gstmpegvideoparser.h>
#include <gst/video/video-color.h>

#include <math.h>

#define _gst_log2(x) (log(x)/log(2))

/*
 * tsdemux
 *
 * See TODO for explanations on improvements needed
 */

#define CONTINUITY_UNSET 255
#define MAX_CONTINUITY 15

/* default scaling_lists  */
const guint8 default_4x4_intra[16] =
    { 6, 13, 13, 20, 20, 20, 28, 28, 28, 28, 32, 32,
  32, 37, 37, 42
};

const guint8 default_4x4_inter[16] =
    { 10, 14, 14, 20, 20, 20, 24, 24, 24, 24, 27, 27,
  27, 30, 30, 34
};

const guint8 default_8x8_intra[64] =
    { 6, 10, 10, 13, 11, 13, 16, 16, 16, 16, 18, 18,
  18, 18, 18, 23, 23, 23, 23, 23, 23, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27,
  27, 27, 27, 27, 27, 29, 29, 29, 29, 29, 29, 29, 31, 31, 31, 31, 31, 31, 33,
  33, 33, 33, 33, 36, 36, 36, 36, 38, 38, 38, 40, 40, 42
};

const guint8 default_8x8_inter[64] =
    { 9, 13, 13, 15, 13, 15, 17, 17, 17, 17, 19, 19,
  19, 19, 19, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22, 22, 24, 24, 24,
  24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27, 27, 27, 27, 28,
  28, 28, 28, 28, 30, 30, 30, 30, 32, 32, 32, 33, 33, 35
};

const guint8 zigzag_8x8[64] = {
  0, 1, 8, 16, 9, 2, 3, 10,
  17, 24, 32, 25, 18, 11, 4, 5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63
};

const guint8 zigzag_4x4[16] = {
  0, 1, 4, 8,
  5, 2, 3, 6,
  9, 12, 13, 10,
  7, 11, 14, 15,
};

typedef struct
{
  guint par_n, par_d;
} PAR;

/* Table E-1 - Meaning of sample aspect ratio indicator (1..16) */
static PAR aspect_ratios[17] = {
  {0, 0},
  {1, 1},
  {12, 11},
  {10, 11},
  {16, 11},
  {40, 33},
  {24, 11},
  {20, 11},
  {32, 11},
  {80, 33},
  {18, 11},
  {15, 11},
  {64, 33},
  {160, 99},
  {4, 3},
  {3, 2},
  {2, 1}
};

/*****  Utils ****/
#define EXTENDED_SAR 255

#define TABLE_ID_UNSET 0xFF

#define PCR_WRAP_SIZE_128KBPS (((gint64)1490)*(1024*1024))
/* small PCR for wrap detection */
#define PCR_SMALL 17775000
/* maximal PCR time */
#define PCR_MAX_VALUE (((((guint64)1)<<33) * 300) + 298)
#define PTS_DTS_MAX_VALUE (((guint64)1) << 33)

//#define DUMP_TS3

#ifdef DUMP_TS3
FILE *dumpFp = NULL;
#endif //if DUMP_TS3

/* Seeking/Scanning related variables */

/* seek to SEEK_TIMESTAMP_OFFSET before the desired offset and search then
 * either accurately or for the next timestamp
 */
#define SEEK_TIMESTAMP_OFFSET (500 * GST_MSECOND)

#define PTS_DTS_MAX_VALUE (((guint64)1) << 33)

//#define DUMP_TS

#ifdef DUMP_TS
FILE *dumpFp = NULL;
#endif

#define GST_FLOW_REWINDING GST_FLOW_CUSTOM_ERROR

/* latency in nsecs */
#define TS_LATENCY (700 * GST_MSECOND)

GST_DEBUG_CATEGORY_STATIC (ts_demux_debug);
#define GST_CAT_DEFAULT ts_demux_debug

#define ABSDIFF(a,b) (((a) > (b)) ? ((a) - (b)) : ((b) - (a)))

static GQuark QUARK_TSDEMUX;
static GQuark QUARK_PID;
static GQuark QUARK_PCR;
static GQuark QUARK_OPCR;
static GQuark QUARK_PTS;
static GQuark QUARK_DTS;
static GQuark QUARK_OFFSET;

typedef struct _GstNalParser GstNalParser;
struct _GstNalParser
{
  const guint8 *data;
  guint size;

  guint byte;                   /* Byte position */
  guint bits_in_cache;          /* bitpos in the cache of next bit */
  guint8 first_byte;
  guint64 cache;                /* cached bytes */
};

typedef enum
{
  PENDING_PACKET_EMPTY = 0,     /* No pending packet/buffer
                                 * Push incoming buffers to the array */
  PENDING_PACKET_HEADER,        /* PES header needs to be parsed
                                 * Push incoming buffers to the array */
  PENDING_PACKET_BUFFER,        /* Currently filling up output buffer
                                 * Push incoming buffers to the bufferlist */
  PENDING_PACKET_DISCONT        /* Discontinuity in incoming packets
                                 * Drop all incoming buffers */
} PendingPacketState;

/* Pending buffer */
typedef struct
{
  /* The fully reconstructed buffer */
  GstBuffer *buffer;

  /* Raw PTS/DTS (in 90kHz units) */
  guint64 pts, dts;
} PendingBuffer;

typedef struct _HDCPInfo HDCPInfo;
typedef struct _TSDemuxStream TSDemuxStream;

typedef struct _TSDemuxH264ParsingInfos TSDemuxH264ParsingInfos;
typedef struct _TSDemuxJP2KParsingInfos TSDemuxJP2KParsingInfos;

/* Returns TRUE if a keyframe was found */
typedef gboolean (*GstTsDemuxKeyFrameScanFunction) (TSDemuxStream * stream,
    guint8 * data, const gsize data_size, const gsize max_frame_offset);

typedef struct
{
  guint8 *data;
  gsize size;
} SimpleBuffer;

struct _TSDemuxH264ParsingInfos
{
  /* H264 parsing data */
  GstH264NalParser *parser;
  GstByteWriter *sps;
  GstByteWriter *pps;
  GstByteWriter *sei;
  SimpleBuffer framedata;
};

struct _TSDemuxJP2KParsingInfos
{
  /* J2K parsing data */
  gboolean interlace;
};

struct _HDCPInfo
{
  /* HDCP Decryption Values */
  gboolean private_data;
  guint32 stream_counter;
  guint64 input_counter;
};

struct _TSDemuxStream
{
  MpegTSBaseStream stream;

  GstPad *pad;

  /* Whether the pad was added or not */
  gboolean active;

  /* Whether this is a sparse stream (subtitles or metadata) */
  gboolean sparse;

  /* TRUE if we are waiting for a valid timestamp */
  gboolean pending_ts;

  /* Output data */
  PendingPacketState state;

  /* Data being reconstructed (allocated) */
  guint8 *data;

  /* Size of data being reconstructed (if known, else 0) */
  guint expected_size;

  /* Amount of bytes in current ->data */
  guint current_size;
  /* Size of ->data */
  guint allocated_size;

  /* Current PTS/DTS for this stream (in running time) */
  GstClockTime pts;
  GstClockTime dts;

  /* Reference PTS used to detect gaps */
  GstClockTime gap_ref_pts;
  /* Number of outputted buffers */
  guint32 nb_out_buffers;
  /* Reference number of buffers for gaps */
  guint32 gap_ref_buffers;

  /* Current PTS/DTS for this stream (in 90kHz unit) */
  guint64 raw_pts, raw_dts;

  /* Whether this stream needs to send a newsegment */
  gboolean need_newsegment;

  /* Whether the next output buffer should be DISCONT */
  gboolean discont;

  /* The value to use when calculating the newsegment */
  GstClockTime first_pts;

  /* geunil.jung. For high speed trick */
  guint last_scan_offset;
  gboolean frame_scan_done;
  gboolean is_iframe;
  gboolean is_first_iframe_in_interlace;
  gint prev_frame_num;
  guint8 log2_max_frame_num_minus4;

  /* update the caps status about the video information */
  gboolean is_update_video_caps;

  GstTagList *taglist;

  HDCPInfo hdcp_info;

  gint continuity_counter;

  /* List of pending buffers */
  GList *pending;

  /* if != 0, output only PES from that substream */
  guint8 target_pes_substream;
  gboolean needs_keyframe;

  GstClockTime seeked_pts, seeked_dts;

  GstTsDemuxKeyFrameScanFunction scan_function;
  TSDemuxH264ParsingInfos h264infos;
  TSDemuxJP2KParsingInfos jp2kInfos;

  /* for Error detect */
  guint error_count;

  /* for HLS */
  GstClockTime last_valid_pts;
  GstClockTime last_valid_dts;
  guint64 ts_base_offset;
  guint8 ts_wrap_count;

  /* for HLS roll over case */
  gboolean is_roll_over;
};

typedef struct
{
  guint8 aspect_ratio_info_present_flag;
  guint8 aspect_ratio_idc;
  /* if aspect_ratio_idc == 255 */
  guint16 sar_width;
  guint16 sar_height;

  guint8 overscan_info_present_flag;
  /* if overscan_info_present_flag */
  guint8 overscan_appropriate_flag;

  guint8 video_signal_type_present_flag;
  guint8 video_format;
  guint8 video_full_range_flag;
  guint8 colour_description_present_flag;
  guint8 colour_primaries;
  guint8 transfer_characteristics;
  guint8 matrix_coefficients;

  guint8 chroma_loc_info_present_flag;
  guint8 chroma_sample_loc_type_top_field;
  guint8 chroma_sample_loc_type_bottom_field;

  guint8 timing_info_present_flag;
  /* if timing_info_present_flag */
  guint32 num_units_in_tick;
  guint32 time_scale;
  guint8 fixed_frame_rate_flag;

  /* calculated values */
  guint par_n;
  guint par_d;
} H264VUIParams;
/*for H264 VUI parameter*/

typedef struct
{
  gint id;

  guint8 profile_idc;
  guint8 constraint_set0_flag;
  guint8 constraint_set1_flag;
  guint8 constraint_set2_flag;
  guint8 constraint_set3_flag;
  guint8 level_idc;

  guint8 chroma_format_idc;
  guint8 separate_colour_plane_flag;
  guint8 bit_depth_luma_minus8;
  guint8 bit_depth_chroma_minus8;
  guint8 qpprime_y_zero_transform_bypass_flag;

  guint8 scaling_matrix_present_flag;
  guint8 scaling_lists_4x4[6][16];
  guint8 scaling_lists_8x8[6][64];

  guint8 log2_max_frame_num_minus4;
  guint8 pic_order_cnt_type;

  /* if pic_order_cnt_type == 0 */
  guint8 log2_max_pic_order_cnt_lsb_minus4;

  /* else if pic_order_cnt_type == 1 */
  guint8 delta_pic_order_always_zero_flag;
  gint32 offset_for_non_ref_pic;
  gint32 offset_for_top_to_bottom_field;
  guint8 num_ref_frames_in_pic_order_cnt_cycle;
  gint32 offset_for_ref_frame[255];

  guint32 num_ref_frames;
  guint8 gaps_in_frame_num_value_allowed_flag;
  guint32 pic_width_in_mbs_minus1;
  guint32 pic_height_in_map_units_minus1;
  guint8 frame_mbs_only_flag;

  guint8 mb_adaptive_frame_field_flag;

  guint8 direct_8x8_inference_flag;

  guint8 frame_cropping_flag;

  /* if frame_cropping_flag */
  guint32 frame_crop_left_offset;
  guint32 frame_crop_right_offset;
  guint32 frame_crop_top_offset;
  guint32 frame_crop_bottom_offset;

  guint8 vui_parameters_present_flag;
  /* if vui_parameters_present_flag */
  H264VUIParams vui_parameters;

  /* calculated values */
  guint8 chroma_array_type;
  guint32 max_frame_num;
  gint width, height;
  gint fps_num, fps_den;
  gboolean valid;

} H264SPS;
/*for H264 SPS*/


typedef struct
{
  guint8 profile_space;
  guint8 tier_flag;
  guint8 profile_idc;

  guint8 profile_compatibility_flag[32];

  guint8 progressive_source_flag;
  guint8 interlaced_source_flag;
  guint8 non_packed_constraint_flag;
  guint8 frame_only_constraint_flag;
  guint8 level_idc;

  guint8 sub_layer_profile_present_flag[6];
  guint8 sub_layer_level_present_flag[6];

  guint8 sub_layer_profile_space[6];
  guint8 sub_layer_tier_flag[6];
  guint8 sub_layer_profile_idc[6];
  guint8 sub_layer_profile_compatibility_flag[6][32];
  guint8 sub_layer_progressive_source_flag[6];
  guint8 sub_layer_interlaced_source_flag[6];
  guint8 sub_layer_non_packed_constraint_flag[6];
  guint8 sub_layer_frame_only_constraint_flag[6];
  guint8 sub_layer_level_idc[6];
} H265ProfileTierLevel;
/*for H265 SPS*/


#define VIDEO_CAPS \
  GST_STATIC_CAPS (\
    "video/mpeg, " \
      "mpegversion = (int) { 1, 2, 4 }, " \
      "systemstream = (boolean)FALSE, " \
      "width = (int) [ 16, 4096 ], " \
      "height = (int) [ 16, 4096 ]; "\
    "video/x-h264,stream-format=(string)byte-stream," \
      "alignment=(string)nal," \
      "width = (int) [ 16, 4096 ], " \
      "height = (int) [ 16, 4096 ];" \
    "video/x-h265,stream-format=(string)byte-stream," \
      "alignment=(string)nal," \
      "width = (int) [ 16, 4096 ], " \
      "height = (int) [ 16, 4096 ];" \
    "video/x-dirac;" \
    "video/x-cavs;" \
    "video/x-wmv," \
      "wmvversion = (int) 3, " \
      "format = (string) WVC1;" \
      "image/x-jpc;" \
)

#define AUDIO_CAPS \
  GST_STATIC_CAPS ( \
    "audio/mpeg, " \
      "mpegversion = (int) 1;" \
    "audio/mpeg, " \
      "mpegversion = (int) 2, " \
      "stream-format = (string) adts; " \
    "audio/mpeg, " \
      "mpegversion = (int) 4, " \
      "stream-format = (string) loas; " \
    "audio/x-lpcm, " \
      "width = (int) { 16, 20, 24 }, " \
      "rate = (int) { 48000, 96000 }, " \
      "channels = (int) [ 1, 8 ], " \
      "dynamic_range = (int) [ 0, 255 ], " \
      "emphasis = (boolean) { FALSE, TRUE }, " \
      "mute = (boolean) { FALSE, TRUE }; " \
    "audio/x-ac3; audio/x-eac3;" \
    "audio/x-ac4;" \
    "audio/x-dts; audio/x-dtsh; audio/x-dtse; audio/x-dtsl; " \
    "audio/x-opus;" \
    "audio/x-private-ts-lpcm;" \
    "audio/x-private2-lpcm" \
  )

/* Can also use the subpicture pads for text subtitles? */
#define SUBPICTURE_CAPS \
    GST_STATIC_CAPS ("subpicture/x-pgs; subpicture/x-dvd; subpicture/x-dvb")

static GstStaticPadTemplate video_template =
GST_STATIC_PAD_TEMPLATE ("video_%01x_%05x", GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    VIDEO_CAPS);

static GstStaticPadTemplate audio_template =
GST_STATIC_PAD_TEMPLATE ("audio_%01x_%05x",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    AUDIO_CAPS);

static GstStaticPadTemplate subpicture_template =
GST_STATIC_PAD_TEMPLATE ("subpicture_%01x_%05x",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    SUBPICTURE_CAPS);

static GstStaticPadTemplate private_template =
GST_STATIC_PAD_TEMPLATE ("private_%01x_%05x",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

enum
{
  PROP_0,
  PROP_PROGRAM_NUMBER,
  PROP_EMIT_STATS,
  /* FILL ME */
};

enum
{
  PAD_MODE_HLSV4_ALL = 0,
  PAD_MODE_HLSV4_VIDEO_ONLY,
  PAD_MODE_HLSV4_AUDIO_ONLY,
  /* FILL ME */
};

/* Pad functions */


/* mpegtsbase methods */
static void
gst_ts_demux_update_program (MpegTSBase * base, MpegTSBaseProgram * program);
static void
gst_ts_demux_program_started (MpegTSBase * base, MpegTSBaseProgram * program);
static void
gst_ts_demux_program_stopped (MpegTSBase * base, MpegTSBaseProgram * program);
static gboolean
gst_ts_demux_can_remove_program (MpegTSBase * base,
    MpegTSBaseProgram * program);
static void gst_ts_demux_reset (MpegTSBase * base);
static GstFlowReturn
gst_ts_demux_push (MpegTSBase * base, MpegTSPacketizerPacket * packet,
    GstMpegtsSection * section);
static void gst_ts_demux_flush (MpegTSBase * base, gboolean hard);
static GstFlowReturn gst_ts_demux_drain (MpegTSBase * base);
static gboolean
gst_ts_demux_stream_added (MpegTSBase * base, MpegTSBaseStream * stream,
    MpegTSBaseProgram * program);
static void
gst_ts_demux_stream_removed (MpegTSBase * base, MpegTSBaseStream * stream);
static GstFlowReturn gst_ts_demux_do_seek (MpegTSBase * base, GstEvent * event);
static void gst_ts_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ts_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_ts_demux_flush_streams (GstTSDemux * tsdemux, gboolean hard);
static GstFlowReturn
gst_ts_demux_push_pending_data (GstTSDemux * demux, TSDemuxStream * stream,
    MpegTSBaseProgram * program);
static void gst_ts_demux_stream_flush (TSDemuxStream * stream,
    GstTSDemux * demux, gboolean hard);

static gboolean push_event (MpegTSBase * base, GstEvent * event);
static void gst_ts_demux_check_and_sync_streams (GstTSDemux * demux,
    GstClockTime time);

/* geunil.jung. For high speed trick */
static void gst_ts_demux_reset_streams (MpegTSBase * base);

static void
_extra_init (void)
{
  QUARK_TSDEMUX = g_quark_from_string ("tsdemux");
  QUARK_PID = g_quark_from_string ("pid");
  QUARK_PCR = g_quark_from_string ("pcr");
  QUARK_OPCR = g_quark_from_string ("opcr");
  QUARK_PTS = g_quark_from_string ("pts");
  QUARK_DTS = g_quark_from_string ("dts");
  QUARK_OFFSET = g_quark_from_string ("offset");
}

#define gst_ts_demux_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstTSDemux, gst_ts_demux, GST_TYPE_MPEGTS_BASE,
    _extra_init ());
/**********Nal parser*********************/
/**
 * gst_nal_parser_init:
 * @reader: a #GstNalParser instance
 * @data: Data from which the #GstNalParser should read
 * @size: Size of @data in bytes
 *
 * Initializes a #GstNalParser instance to read from @data. This function
 * can be called on already initialized instances.
 *
 * Since: 0.10.22
 */
static void
gst_nal_parser_init (GstNalParser * reader, const guint8 * data, guint size)
{
  reader->data = data;
  reader->size = size;

  reader->byte = 0;
  reader->bits_in_cache = 0;
  /* fill with something other than 0 to detect emulation prevention bytes */
  reader->first_byte = 0xff;
  reader->cache = 0xff;
}

/**
 * gst_nal_parser_read:
 * @reader: a #GstNalParser instance
 * @nbits: number of bits to be read
 *
 * reads nbits a using #GstNalParser instance to read from @data. This function
 * can be called on already initialized instances.
 *
 */
static gboolean
gst_nal_parser_read (GstNalParser * reader, guint nbits)
{
  if (G_UNLIKELY (reader->byte * 8 + (nbits - reader->bits_in_cache) >
          reader->size * 8))
    return FALSE;

  while (reader->bits_in_cache < nbits) {
    guint8 byte;
    gboolean check_three_byte;

    check_three_byte = TRUE;
  next_byte:
    if (G_UNLIKELY (reader->byte >= reader->size))
      return FALSE;

    byte = reader->data[reader->byte++];

    /* check if the byte is a emulation_prevention_three_byte */
    if (check_three_byte && byte == 0x03 && reader->first_byte == 0x00 &&
        ((reader->cache & 0xff) == 0)) {
      /* next byte goes unconditionally to the cache, even if it's 0x03 */
      check_three_byte = FALSE;
      goto next_byte;
    }
    reader->cache = (reader->cache << 8) | reader->first_byte;
    reader->first_byte = byte;
    reader->bits_in_cache += 8;
  }

  return TRUE;
}

static inline gboolean
gst_nal_parser_skip (GstNalParser * nr, guint nbits)
{
  if (G_UNLIKELY (!gst_nal_parser_read (nr, nbits)))
    return FALSE;

  nr->bits_in_cache -= nbits;

  return TRUE;
}


#define GST_NAL_PARSER_READ_BITS(bits) \
static gboolean \
gst_nal_parser_get_bits_uint##bits (GstNalParser *reader, guint##bits *val, guint nbits) \
{ \
  guint shift; \
  \
  g_return_val_if_fail (reader != NULL, FALSE); \
  g_return_val_if_fail (val != NULL, FALSE); \
  g_return_val_if_fail (nbits <= bits, FALSE); \
  \
  if (!gst_nal_parser_read (reader, nbits)) \
    return FALSE; \
  \
  /* bring the required bits down and truncate */ \
  shift = reader->bits_in_cache - nbits; \
  *val = reader->first_byte >> shift; \
  \
  *val |= reader->cache << (8 - shift); \
  /* mask out required bits */ \
  if (nbits < bits) \
    *val &= ((guint##bits)1 << nbits) - 1; \
  \
  reader->bits_in_cache = shift; \
  \
  return TRUE; \
}

GST_NAL_PARSER_READ_BITS (8);
GST_NAL_PARSER_READ_BITS (16);
GST_NAL_PARSER_READ_BITS (32);

/**
 * gst_nal_parser_get_ue:
 * @reader: a #GstNalParser instance
 * @val: Pointer to a #guint32 to store the result
 *
 * Reads an unsigned Exp-Golomb value into val
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
static gboolean
gst_nal_parser_get_ue (GstNalParser * reader, guint32 * val)
{
  guint i = 0;
  guint8 bit;
  guint32 value;

  if (G_UNLIKELY (!gst_nal_parser_get_bits_uint8 (reader, &bit, 1)))
    return FALSE;

  while (bit == 0) {
    i++;
    if G_UNLIKELY
      ((!gst_nal_parser_get_bits_uint8 (reader, &bit, 1)))
          return FALSE;
  }

  g_return_val_if_fail (i <= 32, FALSE);

  if (G_UNLIKELY (!gst_nal_parser_get_bits_uint32 (reader, &value, i)))
    return FALSE;

  *val = (1 << i) - 1 + value;

  return TRUE;
}

/**
 * gst_nal_parser_get_se:
 * @reader: a #GstNalParser instance
 * @val: Pointer to a #gint32 to store the result
 *
 * Reads a signed Exp-Golomb value into val
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 */
static gboolean
gst_nal_parser_get_se (GstNalParser * reader, gint32 * val)
{
  guint32 value;

  if (G_UNLIKELY (!gst_nal_parser_get_ue (reader, &value)))
    return FALSE;

  if (value % 2)
    *val = (value / 2) + 1;
  else
    *val = -(value / 2);

  return TRUE;
}

#define CHECK_ALLOWED(val, min, max) { \
  if (val < min || val > max) { \
    GST_WARNING ("value not in allowed range. value: %d, range %d-%d", \
                     val, min, max); \
    goto error; \
  } \
}

#define READ_UINT8(reader, val, nbits) { \
  if (!gst_nal_parser_get_bits_uint8 (reader, &val, nbits)) { \
    GST_WARNING ("failed to read uint8, nbits: %d", nbits); \
    goto error; \
  } \
}

#define READ_UINT16(reader, val, nbits) { \
  if (!gst_nal_parser_get_bits_uint16 (reader, &val, nbits)) { \
    GST_WARNING ("failed to read uint16, nbits: %d", nbits); \
    goto error; \
  } \
}

#define READ_UINT32(reader, val, nbits) { \
  if (!gst_nal_parser_get_bits_uint32 (reader, &val, nbits)) { \
    GST_WARNING ("failed to read uint32, nbits: %d", nbits); \
    goto error; \
  } \
}

#define READ_UINT64(reader, val, nbits) { \
  if (!gst_nal_parser_get_bits_uint64 (reader, &val, nbits)) { \
    GST_WARNING ("failed to read uint64, nbits: %d", nbits); \
    goto error; \
  } \
}

#define READ_UE(nr, val) { \
  if (!gst_nal_parser_get_ue (nr, &val)) { \
    GST_WARNING ("failed to read UE"); \
    goto error; \
  } \
}

#define READ_UE_ALLOWED(nr, val, min, max) { \
  guint32 tmp; \
  READ_UE (nr, tmp); \
  CHECK_ALLOWED (tmp, min, max); \
  val = tmp; \
}

#define READ_SE(nr, val) { \
  if (!gst_nal_parser_get_se (nr, &val)) { \
    GST_WARNING ("failed to read SE"); \
    goto error; \
  } \
}

#define READ_SE_ALLOWED(nr, val, min, max) { \
  gint32 tmp; \
  READ_SE (nr, tmp); \
  CHECK_ALLOWED (tmp, min, max); \
  val = tmp; \
}
/**************************************************************************/

static void
gst_ts_demux_dispose (GObject * object)
{
  GstTSDemux *demux = GST_TS_DEMUX_CAST (object);

  gst_flow_combiner_free (demux->flowcombiner);

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_ts_demux_class_init (GstTSDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  MpegTSBaseClass *ts_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->set_property = gst_ts_demux_set_property;
  gobject_class->get_property = gst_ts_demux_get_property;
  gobject_class->dispose = gst_ts_demux_dispose;

  g_object_class_install_property (gobject_class, PROP_PROGRAM_NUMBER,
      g_param_spec_int ("program-number",
          "Program number",
          "Program Number to demux for (-1 to ignore)",
          -1, G_MAXINT, -1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_EMIT_STATS,
      g_param_spec_boolean ("emit-stats",
          "Emit statistics",
          "Emit messages for every pcr/opcr/pts/dts",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element_class = GST_ELEMENT_CLASS (klass);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&audio_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&subpicture_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&private_template));

  gst_element_class_set_static_metadata (element_class,
      "MPEG transport stream demuxer",
      "Codec/Demuxer",
      "Demuxes MPEG2 transport streams",
      "Zaheer Abbas Merali <zaheerabbas at merali dot org>\n"
      "Edward Hervey <edward.hervey@collabora.co.uk>");

  ts_class = GST_MPEGTS_BASE_CLASS (klass);
  ts_class->reset = GST_DEBUG_FUNCPTR (gst_ts_demux_reset);
  ts_class->push = GST_DEBUG_FUNCPTR (gst_ts_demux_push);
  ts_class->push_event = GST_DEBUG_FUNCPTR (push_event);
  ts_class->program_started = GST_DEBUG_FUNCPTR (gst_ts_demux_program_started);
  ts_class->program_stopped = GST_DEBUG_FUNCPTR (gst_ts_demux_program_stopped);
  ts_class->update_program = GST_DEBUG_FUNCPTR (gst_ts_demux_update_program);
  ts_class->can_remove_program = gst_ts_demux_can_remove_program;
  ts_class->stream_added = gst_ts_demux_stream_added;
  ts_class->stream_removed = gst_ts_demux_stream_removed;
  ts_class->seek = GST_DEBUG_FUNCPTR (gst_ts_demux_do_seek);
  ts_class->flush = GST_DEBUG_FUNCPTR (gst_ts_demux_flush);
  ts_class->drain = GST_DEBUG_FUNCPTR (gst_ts_demux_drain);

  /* geunil.jung. For high speed trick */
  ts_class->reset_stream = GST_DEBUG_FUNCPTR (gst_ts_demux_reset_streams);
}

static void
gst_ts_demux_reset (MpegTSBase * base)
{
  GstTSDemux *demux = (GstTSDemux *) base;

  demux->rate = 1.0;
  // For FastForward, segment->format set to GST_FORMAT_TIME
  demux->duration = -1;

  gst_segment_init (&demux->segment, GST_FORMAT_TIME);
  if (demux->segment_event) {
    gst_event_unref (demux->segment_event);
    demux->segment_event = NULL;
  }
#ifdef DUMP_TS
  if (dumpFp) {
    fclose (dumpFp);
    dumpFp = NULL;
  }
#endif

  if (demux->global_tags) {
    gst_tag_list_unref (demux->global_tags);
    demux->global_tags = NULL;
  }

  if (demux->previous_program) {
    mpegts_base_deactivate_and_free_program (base, demux->previous_program);
    demux->previous_program = NULL;
  }

  demux->have_group_id = FALSE;
  demux->group_id = G_MAXUINT;

  demux->last_seek_offset = -1;
  demux->program_generation = 0;

  demux->n_audio_streams = 0;
  demux->n_video_streams = 0;
  demux->n_private_streams = 0;
  demux->n_subpicture_streams = 0;

  demux->last_pts = GST_CLOCK_TIME_NONE;
  demux->rollover_pts = 0;
  demux->rollover_stream = NULL;
}

static void
pad_linked (GstPad * pad, GstPad * peer, gpointer user_data)
{
  GstSmartPropertiesReturn ret;
  MpegTSBase *base = GST_MPEGTS_BASE (user_data);
  GstTSDemux *demux = (GstTSDemux *) base;
  gchar *app_type_prop = NULL;

  GST_INFO_OBJECT (demux,
      "Smart property initials: dlna-opval[0x%03x] "
      "dlna-filelength[%" G_GUINT64_FORMAT "] "
      "dlna-duration[%" G_GUINT64_FORMAT "] "
      "mheg-ics[%d] thumbnail-mode[%d] real-time[%d] app-type[%s] dolby-vision-support[%d] srcpad-detect-mode[%u]",
      base->dlna_opval, base->dlna_filelength, base->dlna_duration,
      base->mheg_ics, demux->thumbnail_mode, base->real_time,
      (app_type_prop == NULL) ? "NULL" : app_type_prop,
      demux->dolby_vision_support, demux->srcpad_detect_mode);

  ret =
      gst_element_get_smart_properties (GST_ELEMENT_CAST (demux),
      "dlna-opval", &base->dlna_opval, "dlna-flagval", &base->dlna_flagval,
      "dlna-contentlength", &base->dlna_filelength, "dlna-duration",
      &base->dlna_duration, "mheg-ics", &base->mheg_ics, "thumbnail-mode",
      &demux->thumbnail_mode, "real-time", &base->real_time, "app-type",
      &app_type_prop, "dolby-vision-support", &demux->dolby_vision_support,
      "srcpad-detect-mode", &demux->srcpad_detect_mode, NULL);

  GST_INFO_OBJECT (demux, "tsdemux received response of custom query: [%d]",
      ret);
  GST_INFO_OBJECT (demux,
      "Smart property results: dlna-opval[0x%03x] dlna-flagval[0x%03x] "
      "dlna-filelength[%" G_GUINT64_FORMAT "] "
      "dlna-duration[%" G_GUINT64_FORMAT "] "
      "mheg-ics[%d] thumbnail-mode[%d] real-time[%d] app-type[%s] dolby-vision-support[%d] srcpad-detect-mode[%u]",
      base->dlna_opval, base->dlna_flagval, base->dlna_filelength,
      base->dlna_duration, base->mheg_ics, demux->thumbnail_mode,
      base->real_time, (app_type_prop == NULL) ? "NULL" : app_type_prop,
      demux->dolby_vision_support, demux->srcpad_detect_mode);

  if (base->mheg_ics)
    base->mode = BASE_MODE_PUSHING;

  if (app_type_prop != NULL) {
    if (!g_strcmp0 (app_type_prop, "RTC"))
      demux->app_type = APP_TYPE_RTC;

    g_free (app_type_prop);
  }
}

static void
gst_ts_demux_init (GstTSDemux * demux)
{
  MpegTSBase *base = (MpegTSBase *) demux;

  base->stream_size = sizeof (TSDemuxStream);
  base->parse_private_sections = TRUE;
  /* We are not interested in sections (all handled by mpegtsbase) */
  base->push_section = FALSE;

  demux->flowcombiner = gst_flow_combiner_new ();
  demux->requested_program_number = -1;
  demux->program_number = -1;
  demux->thumbnail_mode = FALSE;
  demux->dolby_vision_support = FALSE;
  demux->srcpad_detect_mode = PAD_MODE_HLSV4_ALL;

  gst_ts_demux_reset (base);

  /* custom query to source element */
  g_signal_connect (G_OBJECT (base->sinkpad), "linked", (GCallback) pad_linked,
      base);
}


static void
gst_ts_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTSDemux *demux = GST_TS_DEMUX (object);

  switch (prop_id) {
    case PROP_PROGRAM_NUMBER:
      /* FIXME: do something if program is switched as opposed to set at
       * beginning */
      demux->requested_program_number = g_value_get_int (value);
      break;
    case PROP_EMIT_STATS:
      demux->emit_statistics = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_ts_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTSDemux *demux = GST_TS_DEMUX (object);

  switch (prop_id) {
    case PROP_PROGRAM_NUMBER:
      g_value_set_int (value, demux->requested_program_number);
      break;
    case PROP_EMIT_STATS:
      g_value_set_boolean (value, demux->emit_statistics);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static gboolean
gst_ts_demux_get_duration (GstTSDemux * demux, GstClockTime * dur)
{
  MpegTSBase *base = (MpegTSBase *) demux;
  gboolean res = FALSE;

  if (!demux->program) {
    GST_DEBUG_OBJECT (demux, "No active program yet, can't provide duration");
    return FALSE;
  }

  if (base->file_size == -1) {
    gst_pad_peer_query_duration (base->sinkpad, GST_FORMAT_BYTES,
        &base->file_size);

    if (base->file_size == -1) {
      if (base->dlna_filelength == -1) {
        if (base->dlna_duration != -1) {
          GST_LOG_OBJECT (demux,
              "Set dlna_duration to duration by %" GST_TIME_FORMAT,
              GST_TIME_ARGS (base->dlna_duration));
          *dur = base->dlna_duration;
          demux->segment.duration = base->dlna_duration;
          demux->duration = base->dlna_duration;
          res = TRUE;
          return res;
        } else {
          GST_WARNING_OBJECT (demux, "Cannot set duration. There is no data.");
          res = FALSE;
          return res;
        }
      } else
        base->file_size = base->dlna_filelength;
    }
  }

  /* Convert it to duration */
  *dur =
      mpegts_packetizer_offset_to_ts (base->packetizer, base->file_size,
      demux->program->pcr_pid);
  if (GST_CLOCK_TIME_IS_VALID (*dur)) {
    demux->segment.duration = *dur;
    demux->duration = *dur;
    res = TRUE;
  }
  GST_LOG_OBJECT (demux, "Set duration: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (*dur));
  return res;
}

static gboolean
gst_ts_demux_srcpad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res = TRUE;
  GstFormat format;
  GstTSDemux *demux;
  MpegTSBase *base;

  demux = GST_TS_DEMUX (parent);
  base = GST_MPEGTS_BASE (demux);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    {
      GST_INFO ("query duration");
      gst_query_parse_duration (query, &format, NULL);
      if (format == GST_FORMAT_TIME) {
        if (!gst_pad_peer_query (base->sinkpad, query)) {
          GstClockTime dur;

          base->video_pcr_pid = demux->program->pcr_pid;

          if (base->mode == BASE_MODE_SCANNING ||
              base->mode == BASE_MODE_SEEK_FOR_SCAN) {
            res = FALSE;
            GST_INFO ("Now the mode is SCAN, so query abandon!!");
            break;
          }

          if (gst_ts_demux_get_duration (demux, &dur)) {
            gst_query_set_duration (query, GST_FORMAT_TIME, dur);
            res = TRUE;
          } else
            res = FALSE;
        }
      } else {
        GST_DEBUG_OBJECT (demux, "only query duration on TIME is supported");
        res = FALSE;
      }
      break;
    }
    case GST_QUERY_LATENCY:
    {
      GST_DEBUG ("query latency");
      res = gst_pad_peer_query (base->sinkpad, query);
      if (res) {
        GstClockTime min_lat, max_lat;
        gboolean live;

        /* According to H.222.0
           Annex D.0.3 (System Time Clock recovery in the decoder)
           and D.0.2 (Audio and video presentation synchronization)

           We can end up with an interval of up to 700ms between valid
           PTS/DTS. We therefore allow a latency of 700ms for that.
         */
        gst_query_parse_latency (query, &live, &min_lat, &max_lat);
        min_lat += TS_LATENCY;
        if (GST_CLOCK_TIME_IS_VALID (max_lat))
          max_lat += TS_LATENCY;
        gst_query_set_latency (query, live, min_lat, max_lat);
      }
      break;
    }
    case GST_QUERY_SEEKING:
    {
      GST_DEBUG ("query seeking");
      gst_query_parse_seeking (query, &format, NULL, NULL, NULL);
      GST_DEBUG ("asked for format %s", gst_format_get_name (format));
      if (format == GST_FORMAT_TIME) {
        gboolean seekable = FALSE;
        GstClockTime dur;

        if (gst_pad_peer_query (base->sinkpad, query))
          gst_query_parse_seeking (query, NULL, &seekable, NULL, NULL);

        /* If upstream is not seekable in TIME format we use
         * our own values here */
        if (!seekable) {
          if (base->file_size == -1) {
            gst_pad_peer_query_duration (base->sinkpad, GST_FORMAT_BYTES,
                &base->file_size);
            GST_ERROR ("file_size was queried!!! result: [%"
                G_GINT64_FORMAT "]", base->file_size);
            if (base->file_size == -1) {
              if (base->dlna_filelength != -1
                  && (base->dlna_opval & DLNA_ORG_OP_BYTE_RANGE))
                seekable = TRUE;
            } else
              seekable = TRUE;
          } else if (base->dlna_opval != DLNA_ORG_OP_INITIAL_VALUE) {
            GST_ERROR ("Estimated DLNA mode!! dlna-opval: [0x%03x]",
                base->dlna_opval);
            if (base->dlna_opval & DLNA_ORG_OP_BOTH_RANGE)
              seekable = TRUE;
          } else
            seekable = TRUE;

          if (gst_ts_demux_get_duration (demux, &dur)) {
            gst_query_set_seeking (query, GST_FORMAT_TIME, seekable, 0, dur);
            GST_DEBUG ("Gave duration: %" GST_TIME_FORMAT, GST_TIME_ARGS (dur));
          }
        }
      } else {
        GST_DEBUG_OBJECT (demux, "only TIME is supported for query seeking");
        res = FALSE;
      }
      break;
    }
    case GST_QUERY_CUSTOM:
    {
      gboolean trickable = TRUE;
      GstStructure *s;

      GST_DEBUG ("query custom");
      s = (GstStructure *) gst_query_get_structure (query);

      if (gst_structure_has_name (s, "custom-trickable")) {
        /* trickable is FALSE if:
         * 1) For UHD video at network case
         * 2) For multi-video track in currrent activated program */
        if ((base->mode == BASE_MODE_PUSHING && base->is_higher_than_FHD)
            || (demux->program && demux->program->video_num >= 2))
          trickable = FALSE;

        gst_structure_set (s, "trickable", G_TYPE_BOOLEAN, trickable, NULL);
        res = TRUE;
        break;
      } else {
        res = gst_pad_query_default (pad, parent, query);
        break;
      }
    }

    case GST_QUERY_SEGMENT:
    {
      GstFormat format;
      gint64 start, stop;

      format = demux->segment.format;

      start =
          gst_segment_to_stream_time (&demux->segment, format,
          demux->segment.start);
      if ((stop = demux->segment.stop) == -1)
        stop = demux->segment.duration;
      else
        stop = gst_segment_to_stream_time (&demux->segment, format, stop);

      GST_DEBUG_OBJECT (demux,
          "QUERY_SEGMENT: start %" GST_TIME_FORMAT ", stop %" GST_TIME_FORMAT,
          GST_TIME_ARGS (start), GST_TIME_ARGS (stop));

      gst_query_set_segment (query, demux->segment.rate, format, start, stop);
      res = TRUE;
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
  }

  return res;

}

static void
clear_simple_buffer (SimpleBuffer * sbuf)
{
  if (!sbuf->data)
    return;

  g_free (sbuf->data);
  sbuf->size = 0;
  sbuf->data = NULL;
}

static gboolean
scan_keyframe_h264 (TSDemuxStream * stream, const guint8 * data,
    const gsize data_size, const gsize max_frame_offset)
{
  gint offset = 0;
  GstH264NalUnit unit, frame_unit = { 0, };
  GstH264ParserResult res = GST_H264_PARSER_OK;
  TSDemuxH264ParsingInfos *h264infos = &stream->h264infos;

  GstH264NalParser *parser = h264infos->parser;

  if (G_UNLIKELY (parser == NULL)) {
    parser = h264infos->parser = gst_h264_nal_parser_new ();
    h264infos->sps = gst_byte_writer_new ();
    h264infos->pps = gst_byte_writer_new ();
    h264infos->sei = gst_byte_writer_new ();
  }

  while (res == GST_H264_PARSER_OK) {
    res =
        gst_h264_parser_identify_nalu (parser, data, offset, data_size, &unit);

    if (res != GST_H264_PARSER_OK && res != GST_H264_PARSER_NO_NAL_END) {
      GST_INFO_OBJECT (stream->pad, "Error identifying nalu: %i", res);
      break;
    }

    res = gst_h264_parser_parse_nal (parser, &unit);
    if (res != GST_H264_PARSER_OK) {
      break;
    }

    switch (unit.type) {
      case GST_H264_NAL_SEI:
        if (frame_unit.size)
          break;

        if (gst_byte_writer_put_data (h264infos->sei,
                unit.data + unit.sc_offset,
                unit.size + unit.offset - unit.sc_offset)) {
          GST_DEBUG ("adding SEI %u", unit.size + unit.offset - unit.sc_offset);
        } else {
          GST_WARNING ("Could not write SEI");
        }
        break;
      case GST_H264_NAL_PPS:
        if (frame_unit.size)
          break;

        if (gst_byte_writer_put_data (h264infos->pps,
                unit.data + unit.sc_offset,
                unit.size + unit.offset - unit.sc_offset)) {
          GST_DEBUG ("adding PPS %u", unit.size + unit.offset - unit.sc_offset);
        } else {
          GST_WARNING ("Could not write PPS");
        }
        break;
      case GST_H264_NAL_SPS:
        if (frame_unit.size)
          break;

        if (gst_byte_writer_put_data (h264infos->sps,
                unit.data + unit.sc_offset,
                unit.size + unit.offset - unit.sc_offset)) {
          GST_DEBUG ("adding SPS %u", unit.size + unit.offset - unit.sc_offset);
        } else {
          GST_WARNING ("Could not write SPS");
        }
        break;
        /* these units are considered keyframes in h264parse */
      case GST_H264_NAL_SLICE:
      case GST_H264_NAL_SLICE_DPA:
      case GST_H264_NAL_SLICE_DPB:
      case GST_H264_NAL_SLICE_DPC:
      case GST_H264_NAL_SLICE_IDR:
      {
        GstH264SliceHdr slice;

        if (h264infos->framedata.size)
          break;

        res = gst_h264_parser_parse_slice_hdr (parser, &unit, &slice,
            FALSE, FALSE);

        if (GST_H264_IS_I_SLICE (&slice) || GST_H264_IS_SI_SLICE (&slice)) {
          if (*(unit.data + unit.offset + 1) & 0x80) {
            /* means first_mb_in_slice == 0 */
            /* real frame data */
            GST_DEBUG_OBJECT (stream->pad, "Found keyframe at: %u",
                unit.sc_offset);
            frame_unit = unit;
          }
        }

        break;
      }
      default:
        break;
    }

    if (offset == unit.sc_offset + unit.size)
      break;

    offset = unit.sc_offset + unit.size;
  }

  /* We've got all the infos we need (SPS / PPS and a keyframe, plus
   * and possibly SEI units. We can stop rewinding the stream
   */
  if (gst_byte_writer_get_size (h264infos->sps) &&
      gst_byte_writer_get_size (h264infos->pps) &&
      (h264infos->framedata.size || frame_unit.size)) {
    guint8 *data = NULL;

    gsize tmpsize = gst_byte_writer_get_size (h264infos->pps);

    /*  We know that the SPS is first so just put all our data in there */
    data = gst_byte_writer_reset_and_get_data (h264infos->pps);
    gst_byte_writer_put_data (h264infos->sps, data, tmpsize);
    g_free (data);

    tmpsize = gst_byte_writer_get_size (h264infos->sei);
    if (tmpsize) {
      GST_DEBUG ("Adding SEI");
      data = gst_byte_writer_reset_and_get_data (h264infos->sei);
      gst_byte_writer_put_data (h264infos->sps, data, tmpsize);
      g_free (data);
    }

    if (frame_unit.size) {      /*  We found the everything in one go! */
      GST_DEBUG ("Adding Keyframe");
      gst_byte_writer_put_data (h264infos->sps,
          frame_unit.data + frame_unit.sc_offset,
          stream->current_size - frame_unit.sc_offset);
    } else {
      GST_DEBUG ("Adding Keyframe");
      gst_byte_writer_put_data (h264infos->sps,
          h264infos->framedata.data, h264infos->framedata.size);
      clear_simple_buffer (&h264infos->framedata);
    }

    g_free (stream->data);
    stream->current_size = gst_byte_writer_get_size (h264infos->sps);
    stream->data = gst_byte_writer_reset_and_get_data (h264infos->sps);
    gst_byte_writer_init (h264infos->sps);
    gst_byte_writer_init (h264infos->pps);
    gst_byte_writer_init (h264infos->sei);

    return TRUE;
  }

  if (frame_unit.size) {
    GST_DEBUG_OBJECT (stream->pad, "Keep the keyframe as this is the one"
        " we will push later");

    h264infos->framedata.data =
        g_memdup (frame_unit.data + frame_unit.sc_offset,
        stream->current_size - frame_unit.sc_offset);
    h264infos->framedata.size = stream->current_size - frame_unit.sc_offset;
  }

  return FALSE;
}

/* We merge data from TS packets so that the scanning methods get a continuous chunk,
 however the scanning method will return keyframe offset which needs to be translated
 back to actual offset in file */
typedef struct
{
  gint64 real_offset;           /* offset of TS packet */
  gint merged_offset;           /* offset of merged data in buffer */
} OffsetInfo;

static gboolean
gst_ts_demux_adjust_seek_offset_for_keyframe (TSDemuxStream * stream,
    guint8 * data, guint64 size)
{
  int scan_pid = -1;

  if (!stream->scan_function)
    return TRUE;

  scan_pid = ((MpegTSBaseStream *) stream)->pid;

  if (scan_pid != -1) {
    return stream->scan_function (stream, data, size, size);
  }

  return TRUE;
}

static GstFlowReturn
gst_ts_demux_do_seek (MpegTSBase * base, GstEvent * event)
{
  GList *tmp;

  GstTSDemux *demux = (GstTSDemux *) base;
  GstFlowReturn res = GST_FLOW_ERROR;
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  gboolean update;
  guint64 start_offset;

  gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
      &stop_type, &stop);

  if (format != GST_FORMAT_TIME) {
    GST_WARNING ("format != GST_FORMAT_TIME in do_seek");
    goto done;
  }

  GST_INFO ("seek event, rate: %f start: %" GST_TIME_FORMAT
      " stop: %" GST_TIME_FORMAT, rate, GST_TIME_ARGS (start),
      GST_TIME_ARGS (stop));

  /*
     In case of FastForward,
     do not check this flags below
     (Because GstSeekFlag is setting to GST_SEEK_FLAG_SKIP|GST_SEEK_FLAG_SEGMENT)
   */
#if 0
  if (flags & (GST_SEEK_FLAG_SEGMENT)) {
    GST_WARNING ("seek flags 0x%x are not supported", (int) flags);
    goto done;
  }
#endif

  if (base->mode == BASE_MODE_PUSHING)
    base->ignore_flush = FALSE;

  if (start_type != GST_SEEK_TYPE_NONE) {
    if (rate > 0) {
      start_offset =
          mpegts_packetizer_ts_to_offset (base->packetizer, MAX (0,
              start - SEEK_TIMESTAMP_OFFSET), demux->program->pcr_pid);
    } else {
      start_offset =
          mpegts_packetizer_ts_to_offset (base->packetizer, MAX (0,
              stop - SEEK_TIMESTAMP_OFFSET), demux->program->pcr_pid);
    }
    if (G_UNLIKELY (start_offset == -1)) {
      GST_WARNING ("Couldn't convert start position to an offset");
      goto done;
    }
  } else {
    for (tmp = demux->program->stream_list; tmp; tmp = tmp->next) {
      TSDemuxStream *stream = tmp->data;

      stream->need_newsegment = TRUE;
    }
    gst_segment_init (&demux->segment, GST_FORMAT_UNDEFINED);
    if (demux->segment_event) {
      gst_event_unref (demux->segment_event);
      demux->segment_event = NULL;
    }
    demux->rate = rate;
    res = GST_FLOW_OK;
    goto done;
  }

  /* record offset */
  if (base->dlna_opval != DLNA_ORG_OP_INITIAL_VALUE
      && base->dlna_filelength != -1)
    base->seek_offset =
        (start_offset >=
        base->dlna_filelength) ? (base->dlna_filelength - 1) : start_offset;
  else
    base->seek_offset = start_offset;

  if (rate > 2)
    base->seek_size_ratio = 0.8;
  else
    base->seek_size_ratio = 1.2;

  if (rate < 0 || rate > 2)
    base->high_speed_trick = TRUE;
  else if (base->is_higher_than_FHD && rate == 2)
    base->high_speed_trick = TRUE;
  else
    base->high_speed_trick = FALSE;

  if (base->iframe_interval == -1) {
    if (rate < 0) {
      if (base->packetizer->know_packet_size)
        base->trick_seek_size = 2 * 100 * base->packetizer->packet_size;
      else
        base->trick_seek_size = 2 * 100 * 188;
    } else
      base->trick_seek_size = 0;
  } else {
    base->trick_seek_size = base->iframe_interval * base->seek_size_ratio;
  }

  if (rate < 0) {
    base->seek_offset -= base->trick_seek_size;
    if ((gint64) base->seek_offset < 0)
      base->seek_offset = 0;
    base->trick_seek_offset = base->seek_offset;
  }

  GST_INFO
      ("high_speed_trick %d, rate %f,	konw_packet_size %d, packet_size %"
      G_GUINT16_FORMAT ", iframe_interval %" G_GUINT32_FORMAT
      ", trick_seek_size %" G_GUINT32_FORMAT ", trick_seek_offset %"
      G_GUINT64_FORMAT ", start_offset %" G_GUINT64_FORMAT
      ", seek_offset %" G_GUINT64_FORMAT ",  seek_size_ratio %f",
      base->high_speed_trick, rate, base->packetizer->know_packet_size,
      base->packetizer->packet_size, base->iframe_interval,
      base->trick_seek_size, base->trick_seek_offset, start_offset,
      base->seek_offset, base->seek_size_ratio);

  demux->last_seek_offset = base->seek_offset;
  demux->rate = rate;
  res = GST_FLOW_OK;

  if (base->mheg_ics) {
    demux->last_pts = GST_CLOCK_TIME_NONE;
    demux->rollover_pts = 0;
    demux->rollover_stream = NULL;
  }

  gst_segment_do_seek (&demux->segment, rate, format, flags, start_type, start,
      stop_type, stop, &update);
  if (!(flags & GST_SEEK_FLAG_ACCURATE))
    demux->reset_segment = TRUE;

  if (demux->segment_event) {
    gst_event_unref (demux->segment_event);
    demux->segment_event = NULL;
  }

  for (tmp = demux->program->stream_list; tmp; tmp = tmp->next) {
    TSDemuxStream *stream = tmp->data;

    if (flags & GST_SEEK_FLAG_ACCURATE)
      stream->needs_keyframe = TRUE;

    stream->seeked_pts = GST_CLOCK_TIME_NONE;
    stream->seeked_dts = GST_CLOCK_TIME_NONE;
    stream->need_newsegment = TRUE;
    stream->first_pts = GST_CLOCK_TIME_NONE;
  }

done:
  return res;
}

static void
gst_ts_demux_stream_reset (TSDemuxStream * stream)
{
  stream->pts = GST_CLOCK_TIME_NONE;

  if (stream->data)
    g_free (stream->data);
  stream->data = NULL;
  stream->state = PENDING_PACKET_EMPTY;
  stream->expected_size = 0;
  stream->allocated_size = 0;
  stream->current_size = 0;
  stream->pts = GST_CLOCK_TIME_NONE;
  stream->dts = GST_CLOCK_TIME_NONE;
  /* geunil.jung. For high speed trick */
  stream->last_scan_offset = 0;
  stream->frame_scan_done = FALSE;
  stream->is_iframe = FALSE;
  stream->is_first_iframe_in_interlace = FALSE;
  stream->is_update_video_caps = FALSE;
  stream->discont = TRUE;
}

/* geunil.jung. For high speed trick */
static void
gst_ts_demux_reset_streams (MpegTSBase * base)
{
  GstTSDemux *demux = GST_TS_DEMUX_CAST (base);

  g_list_foreach (demux->program->stream_list,
      (GFunc) gst_ts_demux_stream_reset, NULL);
}

static gboolean
gst_ts_demux_srcpad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res = TRUE;
  GstTSDemux *demux = GST_TS_DEMUX (parent);
  MpegTSBase *base = (MpegTSBase *) demux;
  GList *tmp;

  GST_DEBUG_OBJECT (pad, "Got event %s",
      gst_event_type_get_name (GST_EVENT_TYPE (event)));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      res = mpegts_base_handle_seek_event ((MpegTSBase *) demux, pad, event);
      if (!res)
        GST_WARNING ("seeking failed");
      gst_event_unref (event);
      /*DLNA forward stalling */
      if (base->dlna_opval == DLNA_ORG_OP_NONE && base->dlna_flagval == 0x1000
          && res) {
        demux->segment.rate = base->segment.rate;
        for (tmp = demux->program->stream_list; tmp; tmp = tmp->next) {
          TSDemuxStream *stream = (TSDemuxStream *) tmp->data;
          if (stream->pad)
            stream->need_newsegment = TRUE;
        }
      }
      break;
    default:
      res = gst_pad_event_default (pad, parent, event);
  }

#ifdef DUMP_TS
  if (dumpFp == NULL)
    dumpFp = fopen ("/tmp/dump_file_tsdemux.ts", "wb");
  if (dumpFp != NULL)
    g_print
        ("\n\n[#######################################DUMP_TS ] success file open\n\n");
  else
    g_print
        ("\n\n[#######################################DUMP_TS ] error file open\n\n");
#endif

  return res;
}

static void
clean_global_taglist (GstTagList * taglist)
{
  gst_tag_list_remove_tag (taglist, GST_TAG_CONTAINER_FORMAT);
  gst_tag_list_remove_tag (taglist, GST_TAG_CODEC);
}

static gboolean
push_event (MpegTSBase * base, GstEvent * event)
{
  GstTSDemux *demux = (GstTSDemux *) base;
  GList *tmp;
  gboolean early_ret = FALSE;

  if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT) {
    if (base->custom_seek_mode) {
      demux->rate = base->segment.rate;
      demux->segment.rate = base->segment.rate;
    }
    GST_DEBUG_OBJECT (base, "Ignoring segment event (recreated later)");
    gst_event_unref (event);
    return TRUE;

  } else if (GST_EVENT_TYPE (event) == GST_EVENT_TAG) {
    /* In case we receive tags before data, store them to send later
     * If we already have the program, send it right away */
    GstTagList *taglist;

    gst_event_parse_tag (event, &taglist);

    if (demux->global_tags == NULL) {
      demux->global_tags = gst_tag_list_copy (taglist);

      /* Tags that are stream specific for the container should be considered
       * global for the container streams */
      if (gst_tag_list_get_scope (taglist) == GST_TAG_SCOPE_STREAM) {
        gst_tag_list_set_scope (demux->global_tags, GST_TAG_SCOPE_GLOBAL);
      }
    } else {
      demux->global_tags = gst_tag_list_make_writable (demux->global_tags);
      gst_tag_list_insert (demux->global_tags, taglist, GST_TAG_MERGE_REPLACE);
    }
    clean_global_taglist (demux->global_tags);

    /* tags are stored to be used after if there are no streams yet,
     * so we should never reject */
    early_ret = TRUE;
  }

  if (G_UNLIKELY (demux->program == NULL)) {
    gst_event_unref (event);
    return early_ret;
  }

  for (tmp = demux->program->stream_list; tmp; tmp = tmp->next) {
    TSDemuxStream *stream = (TSDemuxStream *) tmp->data;
    if (stream->pad) {
      /* If we are pushing out EOS, flush out pending data first */
      if (GST_EVENT_TYPE (event) == GST_EVENT_EOS &&
          gst_pad_is_active (stream->pad))
        gst_ts_demux_push_pending_data (demux, stream, NULL);

      gst_event_ref (event);
      gst_pad_push_event (stream->pad, event);
    }
  }

  gst_event_unref (event);

  return TRUE;
}

static inline void
add_iso639_language_to_tags (TSDemuxStream * stream, gchar * lang_code)
{
  const gchar *lc, *lang_name = NULL;

  GST_LOG ("Add language code for stream: '%s'", lang_code);

  if (!stream->taglist)
    stream->taglist = gst_tag_list_new_empty ();

  /* descriptor contains ISO 639-2 code, we want the ISO 639-1 code */
  lc = gst_tag_get_language_code (lang_code);

  /* if required lang name can be retrived using gst_tag_get_language_name  and update to tag
     list */
  lang_name = gst_tag_get_language_name (lang_code);

  /* Only set tag if we have a valid one */
  if (lc || (lang_code[0] && lang_code[1]))
    gst_tag_list_add (stream->taglist, GST_TAG_MERGE_REPLACE,
        GST_TAG_LANGUAGE_CODE, (lc) ? lc : lang_code,
        GST_TAG_LANGUAGE_NAME, lang_name, NULL);
}

/*
* DVB subtitle (ETSI EN 300 468 v1.14.1)
* subtitling_descriptor() {
*   descriptor_tag             8 uimsbf
*   descriptor_length          8 uimsbf
*   for(i=0; i<N; i++) {
*     ISO_639_language_code    24 bslbf
*     subtitling_type          8  bslbf
*     composition_page_id      16 bslbf
*     ancillary_page_id        16 bslbf
*   }
* }

* DVB Teletext (ETSI EN 300 468 v1.14.1 - 6.2.43 Teletext descriptor)
* teletext_descriptor() {
    descriptor_tag		8 uimsbf
    descriptor_length		8 uimsbf
    for(i=0; i<N; i++) {
      ISO_639_language_code	24 bslbf
      teletext_type		5 uimsbf
      teletext_magazine_number	3 uimsbf
      teletext_page_number	8 uimsbf
    }
* }

* JCAP subtitle
* (ARIB STD-B10 Part2 / Version 4.4.-E1 / Table 6-32)
* data_component_descriptor() {
*   descriptor_tag                     8 uimsbf
*   descriptor_length                  8 uimsbf
*   data_component_id                  16 uimsbf
*   for(i=0 ; i<N ; i++) {
*     additional_data_component_info   8 uimsbf
*   }
* }
*/
static void
gst_ts_demux_set_caps_for_private_subtitle (TSDemuxStream * stream,
    GstCaps * caps, const guint8 * desc)
{
  GstStructure *subtitle_struct;
  GValue sublangcode_arr = { 0, };
  GValue subtitling_type_arr = { 0, };
  GValue composition_page_id_arr = { 0, };
  GValue ancillary_page_id_arr = { 0, };

  GValue sublangcode = { 0, };
  GValue composition_page_id = { 0, };
  GValue subtitling_type = { 0, };
  GValue ancillary_page_id = { 0, };
  GValue pesString = { 0, };

  int length;
  gchar lang_code[4];
  guint16 descriptor_data_id;   //JCAP

  const gchar *lc = NULL;
  gint index = 0;
  gint numOfSubtitles = 0;

  g_value_init (&sublangcode_arr, GST_TYPE_ARRAY);
  g_value_init (&subtitling_type_arr, GST_TYPE_ARRAY);
  g_value_init (&composition_page_id_arr, GST_TYPE_ARRAY);
  g_value_init (&ancillary_page_id_arr, GST_TYPE_ARRAY);


  g_value_init (&sublangcode, G_TYPE_STRING);
  g_value_init (&subtitling_type, G_TYPE_INT);
  g_value_init (&composition_page_id, G_TYPE_INT);
  g_value_init (&ancillary_page_id, G_TYPE_INT);
  g_value_init (&pesString, G_TYPE_STRING);

  if (!desc)
    return;

  GST_LOG ("@@@@@[%s][%d]@@@@@", __FUNCTION__, __LINE__);


  switch (desc[0]) {
    case GST_MTS_DESC_DVB_SUBTITLING:
      GST_INFO ("@@@@@ DVB subtitle @@@@@");
      length = DESC_LENGTH (desc);

      numOfSubtitles = (gint) (length / 8);

      GST_DEBUG ("DVB subtitle descriptor length: %d", length);
      GST_DEBUG ("DVB subtitle numOfSubtitles: %d", numOfSubtitles);

      desc = (desc + 2);
      while (index < numOfSubtitles) {
        lang_code[0] = GST_READ_UINT8 (desc);
        desc = (desc + 1);
        lang_code[1] = GST_READ_UINT8 (desc);
        desc = (desc + 1);
        lang_code[2] = GST_READ_UINT8 (desc);
        desc = (desc + 1);
        lang_code[3] = 0;

        lc = gst_tag_get_language_code (lang_code);
        if (!lc)
          lc = "und";

        g_value_set_string (&sublangcode, lc);

        g_value_set_int (&subtitling_type, GST_READ_UINT8 (desc));
        desc = (desc + 1);
        g_value_set_int (&composition_page_id, GST_READ_UINT16_BE (desc));
        desc = (desc + 2);
        g_value_set_int (&ancillary_page_id, GST_READ_UINT16_BE (desc));
        desc = (desc + 2);

        gst_value_array_append_value (&sublangcode_arr, &sublangcode);
        gst_value_array_append_value (&subtitling_type_arr, &subtitling_type);
        gst_value_array_append_value (&composition_page_id_arr,
            &composition_page_id);
        gst_value_array_append_value (&ancillary_page_id_arr,
            &ancillary_page_id);

        index++;
      }
      g_value_set_string (&pesString, "DVB");

      subtitle_struct = gst_structure_new_empty ("subpicture/x-dvb");
      gst_caps_append_structure (caps, subtitle_struct);
      gst_structure_set_value (gst_caps_get_structure (caps, 0), "pestype",
          &pesString);
      gst_structure_set_value (gst_caps_get_structure (caps, 0), "sublangcode",
          &sublangcode_arr);
      gst_structure_set_value (gst_caps_get_structure (caps, 0),
          "subtitlingType", &subtitling_type_arr);
      gst_structure_set_value (gst_caps_get_structure (caps, 0),
          "compositionPageId", &composition_page_id_arr);
      gst_structure_set_value (gst_caps_get_structure (caps, 0),
          "ancillaryPageId", &ancillary_page_id_arr);

      g_value_unset (&sublangcode_arr);
      g_value_unset (&subtitling_type_arr);
      g_value_unset (&composition_page_id_arr);
      g_value_unset (&ancillary_page_id_arr);

      g_value_unset (&sublangcode);
      g_value_unset (&composition_page_id);
      g_value_unset (&subtitling_type);
      g_value_unset (&ancillary_page_id);

      break;

    case GST_MTS_DESC_ISDB_DATA_COMPONENT:
      GST_INFO ("@@@@@ JCAP subtitle @@@@@");
      length = DESC_LENGTH (desc);
      descriptor_data_id = GST_READ_UINT16_BE (desc + 2);
      gst_caps_set_simple (caps, "pestype", G_TYPE_STRING, "JCAP", NULL);
      gst_caps_set_simple (caps, "descriptorDataId", G_TYPE_UINT,
          descriptor_data_id, NULL);
      break;
  }
}

static void
gst_ts_demux_set_caps_for_private_dvb_ac3_eac3_descriptor (TSDemuxStream *
    stream, const guint8 * desc)
{
  guint8 component_type;

  if (!desc)
    return;

  switch (DESC_TAG (desc)) {
    case GST_MTS_DESC_DVB_AC3:
    case GST_MTS_DESC_DVB_ENHANCED_AC3:
      if (DESC_DVB_AC_COMPONENT_type_flag (desc)) {
        component_type = ((DESC_DVB_AC_COMPONENT_type (desc) & 0x38) >> 3);

        if (component_type == 0x02) {
          /*gst_caps_set_simple (caps, "role", G_TYPE_STRING,
             "description+supplementary", NULL); */
          if (!stream->taglist)
            stream->taglist = gst_tag_list_new_empty ();

          gst_tag_list_add (stream->taglist, GST_TAG_MERGE_REPLACE,
              GST_TAG_AUDIO_DESCRIPTION, "description+supplementary", NULL);
        }
      }
      break;
    default:
      GST_DEBUG ("There is no DVB AC3 or EAC3 descriptor");
      break;
  }
}

static gboolean
gst_ts_demux_set_caps_for_private_teletext (TSDemuxStream * stream,
    GstCaps * caps, const guint8 * desc)
{
  GstStructure *teletext_struct;
  GValue langcode_arr = { 0, };
  GValue teletext_type_arr = { 0, };
  GValue teletext_magazine_number_arr = { 0, };
  GValue teletext_page_number_arr = { 0, };

  GValue langcode = { 0, };
  GValue teletext_type = { 0, };
  GValue teletext_magazine_number = { 0, };
  GValue teletext_page_number = { 0, };
  GValue pesString = { 0, };

  int length;
  gchar lang_code[4];
  gboolean is_valid = FALSE;

  gint index = 0;
  gint numOfTeletexts = 0;
  const gchar *lc = NULL;

  g_value_init (&langcode_arr, GST_TYPE_ARRAY);
  g_value_init (&teletext_type_arr, GST_TYPE_ARRAY);
  g_value_init (&teletext_magazine_number_arr, GST_TYPE_ARRAY);
  g_value_init (&teletext_page_number_arr, GST_TYPE_ARRAY);


  g_value_init (&langcode, G_TYPE_STRING);
  g_value_init (&teletext_type, G_TYPE_INT);
  g_value_init (&teletext_magazine_number, G_TYPE_INT);
  g_value_init (&teletext_page_number, G_TYPE_INT);
  g_value_init (&pesString, G_TYPE_STRING);

  if (!desc)
    return FALSE;

  GST_LOG ("@@@@@[%s][%d]@@@@@", __FUNCTION__, __LINE__);

  switch (DESC_TAG (desc)) {
    case GST_MTS_DESC_DVB_TELETEXT:
      GST_INFO ("@@@@@ DVB teletext parsing start @@@@@");
      length = DESC_LENGTH (desc);

      numOfTeletexts = (gint) (length / 5);

      GST_DEBUG ("DVB teletext descriptor length: %d", length);
      GST_DEBUG ("DVB teletext numOfTeletexts: %d", numOfTeletexts);

      desc = (desc + 2);
      while (index < numOfTeletexts) {
        if (DESC_DVB_TELETEXT_teletext_type (desc, index) == 0x02
            || DESC_DVB_TELETEXT_teletext_type (desc, index) == 0x05) {
          GST_INFO ("Detected the teletext_type !!!!");
          lang_code[0] = GST_READ_UINT8 (desc + (5 * index));
          lang_code[1] = GST_READ_UINT8 (desc + (5 * index) + 1);
          lang_code[2] = GST_READ_UINT8 (desc + (5 * index) + 2);
          lang_code[3] = 0;

          lc = gst_tag_get_language_code (lang_code);
          if (!lc)
            lc = "und";

          g_value_set_string (&langcode, lc);

          g_value_set_int (&teletext_type,
              DESC_DVB_TELETEXT_teletext_type (desc, index));
          g_value_set_int (&teletext_magazine_number,
              DESC_DVB_TELETEXT_teletext_magazine_number (desc, index));
          g_value_set_int (&teletext_page_number,
              DESC_DVB_TELETEXT_teletext_page_number (desc, index));

          gst_value_array_append_value (&langcode_arr, &langcode);
          gst_value_array_append_value (&teletext_type_arr, &teletext_type);
          gst_value_array_append_value (&teletext_magazine_number_arr,
              &teletext_magazine_number);
          gst_value_array_append_value (&teletext_page_number_arr,
              &teletext_page_number);

          is_valid = TRUE;
        }
        index++;
      }
      g_value_set_string (&pesString, "DVB");

      teletext_struct = gst_structure_new_empty ("application/x-teletext");
      gst_caps_append_structure (caps, teletext_struct);
      gst_structure_set_value (gst_caps_get_structure (caps, 0), "pestype",
          &pesString);
      gst_structure_set_value (gst_caps_get_structure (caps, 0),
          "teletextlangcode", &langcode_arr);
      gst_structure_set_value (gst_caps_get_structure (caps, 0), "teletextType",
          &teletext_type_arr);
      gst_structure_set_value (gst_caps_get_structure (caps, 0),
          "teletextMagazineNum", &teletext_magazine_number_arr);
      gst_structure_set_value (gst_caps_get_structure (caps, 0),
          "teletextPageNum", &teletext_page_number_arr);

      g_value_unset (&langcode_arr);
      g_value_unset (&teletext_type_arr);
      g_value_unset (&teletext_magazine_number_arr);
      g_value_unset (&teletext_page_number_arr);

      g_value_unset (&langcode);
      g_value_unset (&teletext_magazine_number);
      g_value_unset (&teletext_type);
      g_value_unset (&teletext_page_number);

      break;
  }
  return is_valid;
}

static void
gst_ts_demux_set_caps_for_private_dts (TSDemuxStream * stream, GstCaps ** caps)
{
  gint16 asset_construction;
  enum
  { DTS, DTS_HD, DTS_EXPRESS, DTS_LOSSLESS };
  gint8 dts_type = DTS;
  gboolean core_present = FALSE;
  gboolean ext_core = FALSE;
  gboolean ext_xll = FALSE;
  gboolean ext_lbr = FALSE;
  const GstDvrMpegtsDescriptor *desc = NULL;

  desc = mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
      GST_MTS_DESC_DVB_EXTENSION);

  if (desc) {
    core_present = (desc->data[3] & 0x80) >> 7;
    asset_construction = (desc->data[7] & 0xF8) >> 3;

    switch (asset_construction) {
      case 14:
      case 15:
      case 16:
      case 17:
        ext_xll = TRUE;
        break;
      case 18:
        ext_lbr = TRUE;
        break;
      case 19:
      case 20:
        ext_core = TRUE;
        break;
      case 21:
        ext_xll = TRUE;
        ext_core = TRUE;
        break;
    }

    if (!core_present) {
      if (ext_xll && !ext_core)
        dts_type = DTS_LOSSLESS;
      else if (ext_lbr)
        dts_type = DTS_EXPRESS;
      else
        dts_type = DTS_HD;
    } else
      dts_type = DTS_HD;
  }

  switch (dts_type) {
    case DTS_HD:
      *caps = gst_caps_new_empty_simple ("audio/x-dtsh");
      break;
    case DTS_EXPRESS:
      *caps = gst_caps_new_empty_simple ("audio/x-dtse");
      break;
    case DTS_LOSSLESS:
      *caps = gst_caps_new_empty_simple ("audio/x-dtsl");
      break;
    case DTS:
    default:
      *caps = gst_caps_new_empty_simple ("audio/x-dts");
      break;
  }
}

static void
gst_ts_demux_set_caps_for_private_dovi_video (GstTSDemux * tsdemux,
    TSDemuxStream * stream, GstCaps ** caps, const guint8 * desc,
    gboolean dolby_vision_support)
{
  MpegTSBase *base = (MpegTSBase *) tsdemux;
  gint8 dv_profile = -1;
  guint8 dv_level, rpu_present_flag, el_present_flag, bl_present_flag = 0;
  guint16 dependency_pid = 0;

  /* Profile ID:
   * determine the CODEC of BL/EL
   * 0 - 1, 9: AVC
   * 2 - 3: HEVC8
   * 4 - 8: HEVC10 */
  dv_profile = DESC_DOVI_VIDEO_STREAM_dv_profile (desc);
  switch (dv_profile) {
    case 0:
    case 1:
    case 9:
      *caps = gst_caps_new_simple ("video/x-h264",
          "stream-format", G_TYPE_STRING,
          "byte-stream", "alignment", G_TYPE_STRING,
          "au", "width", G_TYPE_INT, 0,
          "height", G_TYPE_INT, 0, "format", G_TYPE_STRING, "h264",
          "dolby-vision-profile", G_TYPE_INT, dv_profile, NULL);
      break;
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
      *caps = gst_caps_new_simple ("video/x-h265",
          "stream-format", G_TYPE_STRING,
          "byte-stream", "alignment", G_TYPE_STRING,
          "au", "width", G_TYPE_INT, 0,
          "height", G_TYPE_INT, 0, "format", G_TYPE_STRING, "h265",
          "dolby-vision-profile", G_TYPE_INT, dv_profile, NULL);
      break;
    default:
      GST_DEBUG ("Invalid range of dv_prifile!!!");
      return;
  }
  dv_level = DESC_DOVI_VIDEO_STREAM_dv_level (desc);

  rpu_present_flag = DESC_DOVI_VIDEO_STREAM_rpu_present_flag (desc);
  el_present_flag = DESC_DOVI_VIDEO_STREAM_el_present_flag (desc);
  bl_present_flag = DESC_DOVI_VIDEO_STREAM_bl_present_flag (desc);

  GST_DEBUG
      ("DOVI parsing Results: Profile ID [%d], Level [%u], bl_flag [%u], el_flag [%u], rpu_flag [%u]",
      dv_profile, dv_level, bl_present_flag, el_present_flag, rpu_present_flag);

  if (dolby_vision_support)
    gst_caps_set_simple (*caps, "dolby-vision", G_TYPE_BOOLEAN, TRUE, NULL);
  else {
    /* For non Dolby Vision model, we will not support non-SDR streams.
     * Profile ID: 1(dvav.pen), 3(dvhe.den), 5(dvhe.stn) */
    switch (dv_profile) {
      case 1:
      case 3:
      case 5:
        GST_ELEMENT_ERROR (base, STREAM, DEMUX,
            ("This platform cannot support the non-SDR DOVI stream."),
            ("This platform cannot support the non-SDR DOVI stream"));
        break;
      default:
        break;
    }
  }

  if (!bl_present_flag) {
    dependency_pid = DESC_DOVI_VIDEO_STREAM_dependency_pid (desc);
    GST_DEBUG ("using dual PID: BL_PID[0x%04x] for Dolby Vision stream!!!",
        dependency_pid);
    gst_caps_set_simple (*caps, "dolby-vision-track", G_TYPE_STRING, "dual",
        "need-compositor", G_TYPE_BOOLEAN, TRUE, NULL);
  }
}

static gboolean
gst_ts_demux_set_caps_for_private_atmos_audio (TSDemuxStream * stream,
    const guint8 * desc, gboolean dolby_atmos_support)
{
  gint length = 0;
  gint desc_index = 0;
  gboolean mainid_flag = FALSE;
  gboolean asvc_flag = FALSE;
  gboolean substream1_flag = FALSE;
  gboolean substream2_flag = FALSE;
  gboolean substream3_flag = FALSE;
  gboolean language_flag = FALSE;
  gboolean language_flag_2 = FALSE;
  gboolean component_type_flag = FALSE;
  gboolean bsid_flag = FALSE;
  gboolean flag_ec3_extension_type_a = FALSE;

  length = DESC_LENGTH (desc);

  GST_DEBUG ("Attempt parse ATMOS description");
  GST_MEMDUMP ("Description data", desc, MIN ((length + 2), 32));

  switch (DESC_TAG (desc)) {
    case GST_MTS_DESC_ENHANCED_AC3_AUDIO_STREAM:
      // ATSC
      mainid_flag = ((desc[2] & 0x20) >> 5);
      asvc_flag = ((desc[2] & 0x10) >> 4);
      substream1_flag = ((desc[2] & 0x04) >> 2);
      substream2_flag = ((desc[2] & 0x02) >> 1);
      substream3_flag = ((desc[2] & 0x01) >> 0);

      language_flag = ((desc[4] & 0x80) >> 7);
      language_flag_2 = ((desc[4] & 0x40) >> 6);

      desc_index = 5;

      if (mainid_flag)
        desc_index++;
      if (asvc_flag)
        desc_index++;
      if (substream1_flag)
        desc_index += 4;        // audio type(1byte) + lang(3bytes)
      if (substream2_flag)
        desc_index += 4;
      if (substream3_flag)
        desc_index += 4;
      if (language_flag)
        desc_index += 3;
      if (language_flag_2)
        desc_index += 3;

      GST_LOG ("ATSC - EAC3: desc length [%d], now_inx [%d]", length,
          desc_index);

      if (desc_index <= (length + 2)) {
        flag_ec3_extension_type_a = ((desc[desc_index] & 0x01) >> 0);
        GST_LOG ("IS ATMOS ? [%d]", flag_ec3_extension_type_a);
        // TODO: complexity_index_type
      }
      break;
    case GST_MTS_DESC_DVB_ENHANCED_AC3:
      // DVB
      component_type_flag = ((desc[2] & 0x80) >> 7);
      bsid_flag = ((desc[2] & 0x40) >> 6);
      mainid_flag = ((desc[2] & 0x20) >> 5);
      asvc_flag = ((desc[2] & 0x10) >> 4);
      substream1_flag = ((desc[2] & 0x04) >> 2);
      substream2_flag = ((desc[2] & 0x02) >> 1);
      substream3_flag = ((desc[2] & 0x01) >> 0);

      desc_index = 3;
      if (component_type_flag)
        desc_index++;
      if (bsid_flag)
        desc_index++;
      if (mainid_flag)
        desc_index++;
      if (asvc_flag)
        desc_index++;
      if (substream1_flag)
        desc_index++;
      if (substream2_flag)
        desc_index++;
      if (substream3_flag)
        desc_index++;

      GST_LOG ("DVB - EAC3: desc length [%d], now_inx [%d]", length,
          desc_index);

      if (desc_index <= (length + 2)) {
        flag_ec3_extension_type_a = ((desc[desc_index] & 0x01) >> 0);
        GST_LOG ("IS ATMOS ? [%d]", flag_ec3_extension_type_a);
        // TODO: complexity_index_typa
      }
      break;
      // TODO: AC-4
    default:
      break;
  }
  return flag_ec3_extension_type_a;
}


/* This function is about extracting the audio type in DVB stream
  * It is for MHEG module */
static guint8
gst_ts_demux_get_audio_type (TSDemuxStream * stream)
{
  const GstDvrMpegtsDescriptor *desc = NULL;
  guint8 ret = 0;

  desc = mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
      GST_MTS_DESC_ISO_639_LANGUAGE);

  if (desc) {
    if (!stream->taglist)
      stream->taglist = gst_tag_list_new_empty ();
    ret = DESC_ISO_639_LANGUAGE_audio_type_nth (desc->data, 0);
  }
  return ret;
}

static void
gst_ts_demux_create_tags (TSDemuxStream * stream)
{
  MpegTSBaseStream *bstream = (MpegTSBaseStream *) stream;
  const GstDvrMpegtsDescriptor *desc = NULL;
  const GstDvrMpegtsDescriptor *desc_stream_identifier = NULL;
  int i, nb;

  desc =
      mpegts_get_descriptor_from_stream (bstream,
      GST_MTS_DESC_ISO_639_LANGUAGE);

  desc_stream_identifier =
      mpegts_get_descriptor_from_stream (bstream,
      GST_MTS_DESC_DVB_STREAM_IDENTIFIER);

  if (!stream->taglist)
    stream->taglist = gst_tag_list_new_empty ();

  gst_tag_list_add (stream->taglist, GST_TAG_MERGE_REPLACE,
      GST_TAG_PID, bstream->pid, NULL);

  if (desc_stream_identifier) {
    guint component_tag = 0;
    component_tag =
        DESC_DVB_STREAM_IDENTIFIER_component_tag (desc_stream_identifier->data);
    gst_tag_list_add (stream->taglist, GST_TAG_MERGE_REPLACE,
        GST_TAG_COMPONENT_TAG, component_tag, NULL);
  }

  if (desc) {
    gchar *lang_code;

    nb = gst_mpegts_descriptor_parse_iso_639_language_nb (desc);

    GST_DEBUG ("Found ISO 639 descriptor (%d entries)", nb);

    for (i = 0; i < nb; i++)
      if (gst_mpegts_descriptor_parse_iso_639_language_idx
          (desc, i, &lang_code, NULL)) {
        add_iso639_language_to_tags (stream, lang_code);
        gst_tag_list_add (stream->taglist, GST_TAG_MERGE_REPLACE,
            GST_TAG_LANGUAGE_NAME, lang_code, NULL);

        g_free (lang_code);
      }

    return;
  }

  desc =
      mpegts_get_descriptor_from_stream (bstream, GST_MTS_DESC_DVB_SUBTITLING);

  if (desc) {
    gchar *lang_code;

    guint subtitle_type = 0;
    guint composition_page_id = 0;
    guint ancillary_page_id = 0;

    nb = gst_mpegts_descriptor_parse_dvb_subtitling_nb (desc);

    GST_DEBUG ("Found SUBTITLING descriptor (%d entries)", nb);

    for (i = 0; i < nb; i++) {
      if (gst_mpegts_descriptor_parse_dvb_subtitling_idx (desc, i, &lang_code,
              NULL, NULL, NULL)) {
        add_iso639_language_to_tags (stream, lang_code);
        g_free (lang_code);
      }
      subtitle_type = DESC_ISO_639_LANGUAGE_subtitle_type_nth (desc->data, i);
      composition_page_id =
          DESC_ISO_639_LANGUAGE_composition_page_id_nth (desc, i);
      ancillary_page_id =
          DESC_ISO_639_LANGUAGE_ancillary_page_id_nth (desc->data, i);

      gst_tag_list_add (stream->taglist, GST_TAG_MERGE_REPLACE,
          GST_TAG_PID, bstream->pid,
          GST_TAG_SUBTITLING_TYPE, subtitle_type,
          GST_TAG_COMPOSITION_PAGE_ID, composition_page_id,
          GST_TAG_ANCILLARY_PAGE_ID, ancillary_page_id, NULL);
    }
    return;
  }

  desc = mpegts_get_descriptor_from_stream (bstream, GST_MTS_DESC_DVB_TELETEXT);

  if (desc) {
    gchar lang_code[4];
    guint teletext_type = 0;
    guint teletext_magazine_number = 0;
    guint teletext_page_number = 0;
    const guint8 *descr = desc->data + 2;

    nb = (gint) (desc->length / 5);

    for (i = 0; i < nb; i++) {
      if (DESC_DVB_TELETEXT_teletext_type (descr, i) == 0x02
          || DESC_DVB_TELETEXT_teletext_type (descr, i) == 0x05) {
        lang_code[0] = GST_READ_UINT8 (descr);
        lang_code[1] = GST_READ_UINT8 (descr + 1);
        lang_code[2] = GST_READ_UINT8 (descr + 2);
        lang_code[3] = 0;

        add_iso639_language_to_tags (stream, lang_code);

        teletext_type = DESC_DVB_TELETEXT_teletext_type (descr, i);
        teletext_magazine_number =
            DESC_DVB_TELETEXT_teletext_magazine_number (descr, i);
        teletext_page_number =
            DESC_DVB_TELETEXT_teletext_page_number (descr, i);

        gst_tag_list_add (stream->taglist, GST_TAG_MERGE_REPLACE,
            GST_TAG_PID, bstream->pid,
            GST_TAG_TELETEXT_TYPE, teletext_type,
            GST_TAG_TELETEXT_MAGAZINE_NUMBER, teletext_magazine_number,
            GST_TAG_TELETEXT_PAGE_NUMBER, teletext_page_number, NULL);
      }
    }
    return;
  }
}

static GstPad *
create_pad_for_stream (MpegTSBase * base, MpegTSBaseStream * bstream,
    MpegTSBaseProgram * program)
{
  GstTSDemux *demux = GST_TS_DEMUX (base);
  TSDemuxStream *stream = (TSDemuxStream *) bstream;
  gchar *name = NULL;
  GstCaps *caps = NULL;
  GstPadTemplate *template = NULL;
  const GstDvrMpegtsDescriptor *desc = NULL;
  GstPad *pad = NULL;
  gboolean sparse = FALSE;
  gboolean is_audio = FALSE, is_video = FALSE, is_subpicture = FALSE,
      is_private = FALSE;
  const gchar *tag_name = NULL;
  const gchar *audioDescType = NULL;
  gchar *codec_name = NULL;
  gchar *lang_code = NULL;
  guint8 audio_type = 0;        //audio type for MHEG
  gboolean is_valid_teletext = TRUE;

  gst_ts_demux_create_tags (stream);

  GST_INFO
      ("Attempting to create pad for stream 0x%04x with stream_type %d(0x%02x)",
      bstream->pid, bstream->stream_type, bstream->stream_type);

  /* First handle BluRay-specific stream types since there is some overlap
   * between BluRay and non-BluRay streay type identifiers */
  if (program->registration_id == DRF_ID_HDMV) {
    switch (bstream->stream_type) {
      case ST_BD_AUDIO_AC3:
      {
        const GstDvrMpegtsDescriptor *ac3_desc;

        /* ATSC ac3 audio descriptor */
        ac3_desc =
            mpegts_get_descriptor_from_stream (bstream,
            GST_MTS_DESC_AC3_AUDIO_STREAM);
        if (ac3_desc && DESC_AC_AUDIO_STREAM_bsid (ac3_desc->data) != 16) {
          GST_LOG ("ac3 audio");
          is_audio = TRUE;
          caps = gst_caps_new_empty_simple ("audio/x-ac3");
        } else {
          is_audio = TRUE;
          caps = gst_caps_new_empty_simple ("audio/x-eac3");
        }
        tag_name = GST_TAG_AUDIO_CODEC;
        codec_name = g_strdup ("AC-3 audio");
        break;
      }
      case ST_BD_AUDIO_EAC3:
      case ST_BD_AUDIO_AC3_PLUS:
        is_audio = TRUE;
        desc =
            mpegts_get_descriptor_from_stream (bstream,
            GST_MTS_DESC_ENHANCED_AC3_AUDIO_STREAM);
        caps = gst_caps_new_empty_simple ("audio/x-eac3");
        if (desc
            && gst_ts_demux_set_caps_for_private_atmos_audio (stream,
                desc->data, TRUE))
          gst_caps_set_simple (caps, "immersive", G_TYPE_STRING, "ATMOS", NULL);
        tag_name = GST_TAG_AUDIO_CODEC;
        codec_name = g_strdup ("EAC-3 audio");
        break;
      case ST_BD_AUDIO_AC3_TRUE_HD:
        /* FIXME : Do not expose pad of trueHD codec until we have
         * ability to decode this codec. */
#if 0
        is_audio = TRUE;
        caps = gst_caps_new_empty_simple ("audio/x-true-hd");
        stream->target_pes_substream = 0x72;
        tag_name = GST_TAG_AUDIO_CODEC;
        codec_name = g_strdup ("TRUE HD AC-3 audio");
        break;
#endif
        goto done;
      case ST_BD_AUDIO_LPCM:
        is_audio = TRUE;
        caps = gst_caps_new_empty_simple ("audio/x-private-ts-lpcm");
        tag_name = GST_TAG_AUDIO_CODEC;
        codec_name = g_strdup ("Uncompressed PCM audio");
        break;
      case ST_BD_PGS_SUBPICTURE:
        is_subpicture = TRUE;
        caps = gst_caps_new_empty_simple ("subpicture/x-pgs");
        sparse = TRUE;
        break;
      case ST_BD_AUDIO_DTS:
      case ST_BD_AUDIO_DTS_HD:
      case ST_BD_AUDIO_DTS_HD_MASTER_AUDIO:
        is_audio = TRUE;
        caps = gst_caps_new_empty_simple ("audio/x-dts");
        stream->target_pes_substream = 0x71;
        break;
    }
  }

  if (caps)
    goto done;

  /* Handle non-BluRay stream types */
  switch (bstream->stream_type) {
    case GST_MPEGTS_STREAM_TYPE_VIDEO_MPEG1:
    case GST_MPEGTS_STREAM_TYPE_VIDEO_MPEG2:
    {
      /* FIXME : Use video decriptor (0x1) to refine caps with:
       * * frame_rate
       * * profile_and_level
       */
      GST_LOG ("mpeg video");
      is_video = TRUE;
      caps = gst_caps_new_simple ("video/mpeg",
          "mpegversion", G_TYPE_INT,
          bstream->stream_type ==
          GST_MPEGTS_STREAM_TYPE_VIDEO_MPEG1 ? 1 :
          2, "systemstream", G_TYPE_BOOLEAN, FALSE,
          "format", G_TYPE_STRING,
          bstream->stream_type ==
          GST_MPEGTS_STREAM_TYPE_VIDEO_MPEG1 ? "mp1v" : "mp2v", "width",
          G_TYPE_INT, 0, "height", G_TYPE_INT, 0, NULL);
      tag_name = GST_TAG_VIDEO_CODEC;
      codec_name =
          g_strdup_printf ("MPEG-%d video",
          bstream->stream_type == GST_MPEGTS_STREAM_TYPE_VIDEO_MPEG1 ? 1 : 2);
      program->video_num++;
      break;
    }
    case ST_PS_VIDEO_MPEG2_DCII:
    {
      /* FIXME : Use DCII registration code (ETV1 ?) to handle that special
       * Stream type (ST_PS_VIDEO_MPEG2_DCII) */
      /* For handling private MPEG2 video stream type. by LGE  */
      desc =
          mpegts_get_descriptor_from_program (program,
          GST_MTS_DESC_REGISTRATION);
      if (desc) {
        GST_LOG ("mpeg2 private video");
        is_video = TRUE;
        caps = gst_caps_new_simple ("video/mpeg",
            "mpegversion", G_TYPE_INT,
            2, "systemstream",
            G_TYPE_BOOLEAN, FALSE, "format", G_TYPE_STRING, "mp2v", NULL);
        tag_name = GST_TAG_VIDEO_CODEC;
        codec_name = g_strdup ("MPEG-2 video private");
        program->video_num++;
      }
      break;
    }
    case GST_MPEGTS_STREAM_TYPE_AUDIO_MPEG1:
    case GST_MPEGTS_STREAM_TYPE_AUDIO_MPEG2:
      GST_LOG ("mpeg audio");
      is_audio = TRUE;
      caps =
          gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT, 1,
          NULL);
      /* HDV is always mpeg 1 audio layer 2 */
      if (program->registration_id == DRF_ID_TSHV)
        gst_caps_set_simple (caps, "layer", G_TYPE_INT, 2, NULL);
      tag_name = GST_TAG_AUDIO_CODEC;
      codec_name = g_strdup_printf ("MPEG-%d audio",
          bstream->stream_type == GST_MPEGTS_STREAM_TYPE_AUDIO_MPEG1 ? 1 : 2);
      break;
    case GST_MPEGTS_STREAM_TYPE_PRIVATE_PES_PACKETS:
    {
      GST_LOG ("private data");
      /* FIXME: Move all of this into a common method (there might be other
       * types also, depending on registratino descriptors also
       */
      desc = mpegts_get_descriptor_from_stream (bstream, GST_MTS_DESC_DVB_AC3);
      if (desc) {
        GST_LOG ("ac3 audio");
        is_audio = TRUE;
        caps = gst_caps_new_empty_simple ("audio/x-ac3");
        gst_ts_demux_set_caps_for_private_dvb_ac3_eac3_descriptor (stream,
            desc->data);
        tag_name = GST_TAG_AUDIO_CODEC;
        codec_name = g_strdup ("AC-3 audio");
        break;
      }

      desc =
          mpegts_get_descriptor_from_stream (bstream,
          GST_MTS_DESC_DVB_ENHANCED_AC3);
      if (desc) {
        GST_LOG ("ac3 audio");
        is_audio = TRUE;
        caps = gst_caps_new_empty_simple ("audio/x-eac3");
        gst_ts_demux_set_caps_for_private_dvb_ac3_eac3_descriptor (stream,
            desc->data);
        if (gst_ts_demux_set_caps_for_private_atmos_audio (stream, desc->data,
                TRUE))
          gst_caps_set_simple (caps, "immersive", G_TYPE_STRING, "ATMOS", NULL);
        tag_name = GST_TAG_AUDIO_CODEC;
        codec_name = g_strdup ("EAC-3 audio");
        break;
      }

      desc =
          mpegts_get_descriptor_from_stream (bstream,
          GST_MTS_DESC_DVB_EXTENSION);
      if (desc != NULL && desc->tag_extension == GST_MTS_DESC_EXT_DVB_AC4) {
        GST_LOG ("DVB AC4 audio");
        is_audio = TRUE;
        caps = gst_caps_new_empty_simple ("audio/x-ac4");
        tag_name = GST_TAG_AUDIO_CODEC;
        codec_name = g_strdup ("AC-4 audio");
        break;
      }

      desc = mpegts_get_descriptor_from_stream (bstream, GST_MTS_DESC_DVB_DTS);
      if (desc) {
        GST_LOG ("DVB DTS audio");
        is_audio = TRUE;
        caps = gst_caps_new_empty_simple ("audio/x-dts");
        tag_name = GST_TAG_AUDIO_CODEC;
        codec_name = g_strdup ("DVB DTS audio");
        break;
      }

      desc =
          mpegts_get_descriptor_from_stream (bstream,
          GST_MTS_DESC_DVB_TELETEXT);
      if (desc) {
        GST_LOG ("DVB teletext");
        is_subpicture = TRUE;
        caps = gst_caps_new_empty ();
        is_valid_teletext =
            gst_ts_demux_set_caps_for_private_teletext (stream, caps,
            desc->data);
        sparse = TRUE;
        break;
      }

      desc =
          mpegts_get_descriptor_from_stream (bstream,
          GST_MTS_DESC_DVB_SUBTITLING);
      if (desc) {
        GST_LOG ("subtitling");
        is_subpicture = TRUE;
        caps = gst_caps_new_empty ();
        gst_ts_demux_set_caps_for_private_subtitle (stream, caps, desc->data);
        sparse = TRUE;
        break;
      }

      desc =
          mpegts_get_descriptor_from_stream (bstream,
          GST_MTS_DESC_ISDB_DATA_COMPONENT);
      if (desc) {
        GST_LOG ("JCAP subtitling");
        is_subpicture = TRUE;
        caps = gst_caps_new_empty_simple ("subpicture/x-dvb");
        gst_ts_demux_set_caps_for_private_subtitle (stream, caps, desc->data);
        sparse = TRUE;
        break;
      }

      switch (bstream->registration_id) {
        case DRF_ID_DTS1:
        case DRF_ID_DTS2:
        case DRF_ID_DTS3:
        case DRF_ID_DTSH:
          /* SMPTE registered DTS */
          is_audio = TRUE;
          gst_ts_demux_set_caps_for_private_dts (stream, &caps);
          tag_name = GST_TAG_AUDIO_CODEC;
          codec_name = g_strdup ("dts audio");
          break;
        case DRF_ID_S302M:
          is_audio = TRUE;
          caps = gst_caps_new_empty_simple ("audio/x-smpte-302m");
          tag_name = GST_TAG_AUDIO_CODEC;
          codec_name = g_strdup ("S302M audio");
          break;
        case DRF_ID_OPUS:
          desc = mpegts_get_descriptor_from_stream (bstream,
              GST_MTS_DESC_DVB_EXTENSION);
          if (desc != NULL && desc->tag_extension == 0x80 && desc->length >= 1) {       /* User defined (provisional Opus) */
            guint8 channel_config_code;
            GstByteReader br;

            /* skip tag, length and tag_extension */
            gst_byte_reader_init (&br, desc->data + 3, desc->length - 1);
            channel_config_code = gst_byte_reader_get_uint8_unchecked (&br);

            if ((channel_config_code & 0x8f) <= 8) {
              static const guint8 coupled_stream_counts[9] = {
                1, 0, 1, 1, 2, 2, 2, 3, 3
              };
              static const guint8 channel_map_a[8][8] = {
                {0},
                {0, 1},
                {0, 2, 1},
                {0, 1, 2, 3},
                {0, 4, 1, 2, 3},
                {0, 4, 1, 2, 3, 5},
                {0, 4, 1, 2, 3, 5, 6},
                {0, 6, 1, 2, 3, 4, 5, 7},
              };
              static const guint8 channel_map_b[8][8] = {
                {0},
                {0, 1},
                {0, 1, 2},
                {0, 1, 2, 3},
                {0, 1, 2, 3, 4},
                {0, 1, 2, 3, 4, 5},
                {0, 1, 2, 3, 4, 5, 6},
                {0, 1, 2, 3, 4, 5, 6, 7},
              };

              gint channels = -1, stream_count, coupled_count, mapping_family;
              guint8 *channel_mapping = NULL;

              channels = channel_config_code ? (channel_config_code & 0x0f) : 2;
              if (channel_config_code == 0 || channel_config_code == 0x80) {
                /* Dual Mono */
                mapping_family = 255;
                if (channel_config_code == 0) {
                  stream_count = 1;
                  coupled_count = 1;
                } else {
                  stream_count = 2;
                  coupled_count = 0;
                }
                channel_mapping = g_new0 (guint8, channels);
                memcpy (channel_mapping, &channel_map_a[1], channels);
              } else if (channel_config_code <= 8) {
                mapping_family = (channels > 2) ? 1 : 0;
                stream_count =
                    channel_config_code -
                    coupled_stream_counts[channel_config_code];
                coupled_count = coupled_stream_counts[channel_config_code];
                if (mapping_family != 0) {
                  channel_mapping = g_new0 (guint8, channels);
                  memcpy (channel_mapping, &channel_map_a[channels - 1],
                      channels);
                }
              } else if (channel_config_code >= 0x82
                  && channel_config_code <= 0x88) {
                mapping_family = 1;
                stream_count = channels;
                coupled_count = 0;
                channel_mapping = g_new0 (guint8, channels);
                memcpy (channel_mapping, &channel_map_b[channels - 1],
                    channels);
              } else if (channel_config_code == 0x81) {
                if (gst_byte_reader_get_remaining (&br) < 2) {
                  GST_WARNING_OBJECT (demux,
                      "Invalid Opus descriptor with extended channel configuration");
                  channels = -1;
                  break;
                }

                channels = gst_byte_reader_get_uint8_unchecked (&br);
                mapping_family = gst_byte_reader_get_uint8_unchecked (&br);

                /* Overwrite values from above */
                if (channels == 0) {
                  GST_WARNING_OBJECT (demux,
                      "Invalid Opus descriptor with extended channel configuration");
                  channels = -1;
                  break;
                }

                if (mapping_family == 0 && channels <= 2) {
                  stream_count = channels - coupled_stream_counts[channels];
                  coupled_count = coupled_stream_counts[channels];
                } else {
                  GstBitReader breader;
                  guint8 stream_count_minus_one, coupled_stream_count;
                  gint stream_count_minus_one_len, coupled_stream_count_len;
                  gint channel_mapping_len, i;

                  gst_bit_reader_init (&breader,
                      gst_byte_reader_get_data_unchecked
                      (&br, gst_byte_reader_get_remaining
                          (&br)), gst_byte_reader_get_remaining (&br));

                  stream_count_minus_one_len = ceil (_gst_log2 (channels));
                  if (!gst_bit_reader_get_bits_uint8 (&breader,
                          &stream_count_minus_one,
                          stream_count_minus_one_len)) {
                    GST_WARNING_OBJECT (demux,
                        "Invalid Opus descriptor with extended channel configuration");
                    channels = -1;
                    break;
                  }

                  stream_count = stream_count_minus_one + 1;
                  coupled_stream_count_len =
                      ceil (_gst_log2 (stream_count_minus_one + 2));

                  if (!gst_bit_reader_get_bits_uint8 (&breader,
                          &coupled_stream_count, coupled_stream_count_len)) {
                    GST_WARNING_OBJECT (demux,
                        "Invalid Opus descriptor with extended channel configuration");
                    channels = -1;
                    break;
                  }

                  coupled_count = coupled_stream_count;

                  channel_mapping_len =
                      ceil (_gst_log2 (stream_count_minus_one + 1 +
                          coupled_stream_count + 1));
                  channel_mapping = g_new0 (guint8, channels);
                  for (i = 0; i < channels; i++) {
                    if (!gst_bit_reader_get_bits_uint8 (&breader,
                            &channel_mapping[i], channel_mapping_len)) {
                      GST_WARNING_OBJECT (demux,
                          "Invalid Opus descriptor with extended channel configuration");
                      break;
                    }
                  }

                  /* error above */
                  if (i != channels) {
                    channels = -1;
                    g_free (channel_mapping);
                    channel_mapping = NULL;
                    break;
                  }
                }
              } else {
                g_assert_not_reached ();
              }

              if (channels != -1) {
                is_audio = TRUE;
                caps =
                    gst_codec_utils_opus_create_caps (48000, channels,
                    mapping_family, stream_count, coupled_count,
                    channel_mapping);

                g_free (channel_mapping);
              }
            } else {
              GST_WARNING_OBJECT (demux,
                  "unexpected channel config code 0x%02x", channel_config_code);
            }
          } else {
            GST_WARNING_OBJECT (demux, "Opus, but no extension descriptor");
          }
          break;
        case DRF_ID_HEVC:
          is_video = TRUE;
          caps = gst_caps_new_simple ("video/x-h265",
              "stream-format", G_TYPE_STRING, "byte-stream",
              "alignment", G_TYPE_STRING, "au",
              "format", G_TYPE_STRING, "h265", NULL);
          if (!demux->dolby_vision_support)
            gst_caps_set_simple (caps, "dolby-vision", G_TYPE_BOOLEAN, FALSE,
                NULL);
          tag_name = GST_TAG_VIDEO_CODEC;
          codec_name = g_strdup ("ITU H.265");
          program->video_num++;
          break;
        case DRF_ID_KLVA:
          sparse = TRUE;
          is_private = TRUE;
          caps = gst_caps_new_simple ("meta/x-klv",
              "parsed", G_TYPE_BOOLEAN, TRUE, NULL);
          break;
        case DRF_ID_AC3:
          is_audio = TRUE;
          caps = gst_caps_new_empty_simple ("audio/x-ac3");
          tag_name = GST_TAG_AUDIO_CODEC;
          codec_name = g_strdup ("AC-3 audio");
          break;
        case DRF_ID_AC4:
          is_audio = TRUE;
          caps = gst_caps_new_empty_simple ("audio/x-ac4");
          tag_name = GST_TAG_AUDIO_CODEC;
          codec_name = g_strdup ("AC-4 audio");
          break;
        case DRF_ID_DOVI:
          is_video = TRUE;

          desc =
              mpegts_get_descriptor_from_stream (bstream,
              GST_MTS_DESC_DOVI_VIDEO_STREAM);
          if (desc)
            gst_ts_demux_set_caps_for_private_dovi_video (demux, stream, &caps,
                desc->data, demux->dolby_vision_support);
          else {
            // TODO: Add to set caps for Non-SDR Compliant BL DOVI stream of Dual Dolby Vision PID
          }
          tag_name = GST_TAG_VIDEO_CODEC;
          codec_name = g_strdup ("DOVI video");
          program->video_num++;
          break;
      }
      if (caps)
        break;

      /* hack for itv hd (sid 10510, video pid 3401 */
      if (program->program_number == 10510 && bstream->pid == 3401) {
        is_video = TRUE;
        caps = gst_caps_new_simple ("video/x-h264",
            "stream-format", G_TYPE_STRING, "byte-stream",
            "alignment", G_TYPE_STRING, "au", "format", G_TYPE_STRING, "h264",
            NULL);
        if (!demux->dolby_vision_support)
          gst_caps_set_simple (caps, "dolby-vision", G_TYPE_BOOLEAN, FALSE,
              NULL);
        tag_name = GST_TAG_VIDEO_CODEC;
        codec_name = g_strdup ("h264 video private");
        program->video_num++;
      }
      break;
    }
    case ST_HDV_AUX_V:
      /* FIXME : Should only be used with specific PMT registration_descriptor */
      /* We don't expose those streams since they're only helper streams */
      /* template = gst_static_pad_template_get (&private_template); */
      /* name = g_strdup_printf ("private_%04x", bstream->pid); */
      /* caps = gst_caps_new_simple ("hdv/aux-v", NULL); */
      break;
    case ST_HDV_AUX_A:
      /* FIXME : Should only be used with specific PMT registration_descriptor */
      /* We don't expose those streams since they're only helper streams */
      /* template = gst_static_pad_template_get (&private_template); */
      /* name = g_strdup_printf ("private_%04x", bstream->pid); */
      /* caps = gst_caps_new_simple ("hdv/aux-a", NULL); */
      break;
    case GST_MPEGTS_STREAM_TYPE_AUDIO_AAC_ADTS:
      is_audio = TRUE;
      caps = gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 2,
          "stream-format", G_TYPE_STRING, "adts", NULL);
      tag_name = GST_TAG_AUDIO_CODEC;
      codec_name = g_strdup ("MPEG-4 AAC audio");
      break;
    case GST_MPEGTS_STREAM_TYPE_AUDIO_AAC_LATM:
      is_audio = TRUE;
      caps = gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 4,
          "stream-format", G_TYPE_STRING, "loas", NULL);
      tag_name = GST_TAG_AUDIO_CODEC;
      codec_name = g_strdup ("MPEG LOAS audio");
      break;
    case GST_MPEGTS_STREAM_TYPE_VIDEO_MPEG4:
      is_video = TRUE;
      caps = gst_caps_new_simple ("video/mpeg",
          "mpegversion", G_TYPE_INT, 4,
          "systemstream", G_TYPE_BOOLEAN, FALSE,
          "format", G_TYPE_STRING, "mp4v", NULL);
      tag_name = GST_TAG_VIDEO_CODEC;
      codec_name = g_strdup ("MPEG-4");
      program->video_num++;
      break;
    case GST_MPEGTS_STREAM_TYPE_VIDEO_H264:
      is_video = TRUE;

      desc =
          mpegts_get_descriptor_from_stream (bstream,
          GST_MTS_DESC_DOVI_VIDEO_STREAM);
      if (desc)
        gst_ts_demux_set_caps_for_private_dovi_video (demux, stream, &caps,
            desc->data, demux->dolby_vision_support);
      else
        caps = gst_caps_new_simple ("video/x-h264",
            "stream-format", G_TYPE_STRING, "byte-stream",
            "alignment", G_TYPE_STRING, "au",
            "width", G_TYPE_INT, 0,
            "height", G_TYPE_INT, 0, "format", G_TYPE_STRING, "h264", NULL);

      tag_name = GST_TAG_VIDEO_CODEC;
      codec_name = g_strdup ("ITU H.264");
      program->video_num++;
      break;
    case GST_MPEGTS_STREAM_TYPE_VIDEO_HEVC:
    case GST_MPEGTS_STREAM_TYPE_VIDEO_HEVC_H265:
      is_video = TRUE;

      desc =
          mpegts_get_descriptor_from_stream (bstream,
          GST_MTS_DESC_DOVI_VIDEO_STREAM);
      if (desc)
        gst_ts_demux_set_caps_for_private_dovi_video (demux, stream, &caps,
            desc->data, demux->dolby_vision_support);
      else
        caps = gst_caps_new_simple ("video/x-h265",
            "stream-format", G_TYPE_STRING,
            "byte-stream", "alignment", G_TYPE_STRING,
            "au", "width", G_TYPE_INT, 0,
            "height", G_TYPE_INT, 0, "format", G_TYPE_STRING, "h265", NULL);

      tag_name = GST_TAG_VIDEO_CODEC;
      codec_name = g_strdup ("ITU H.265");
      program->video_num++;
      break;
    case GST_MPEGTS_STREAM_TYPE_VIDEO_JP2K:
      is_video = TRUE;
      desc =
          mpegts_get_descriptor_from_stream (bstream, GST_MTS_DESC_J2K_VIDEO);
      if (desc == NULL) {
        caps = gst_caps_new_empty_simple ("image/x-jpc");
        break;
      } else {
        GstByteReader br;
        guint16 DEN_frame_rate = 0;
        guint16 NUM_frame_rate = 0;
        guint8 color_specification = 0;
        guint8 remaining_8b = 0;
        gboolean interlaced_video = 0;
        const gchar *interlace_mode = NULL;
        const gchar *colorspace = NULL;
        const gchar *colorimetry_mode = NULL;
        guint16 profile_and_level G_GNUC_UNUSED;
        guint32 horizontal_size G_GNUC_UNUSED;
        guint32 vertical_size G_GNUC_UNUSED;
        guint32 max_bit_rate G_GNUC_UNUSED;
        guint32 max_buffer_size G_GNUC_UNUSED;
        const guint desc_min_length = 24;

        if (desc->length < desc_min_length) {
          GST_ERROR
              ("GST_MPEGTS_STREAM_TYPE_VIDEO_JP2K: descriptor length %d too short",
              desc->length);
          return NULL;
        }

        /* Skip the descriptor tag and length */
        gst_byte_reader_init (&br, desc->data + 2, desc->length);

        profile_and_level = gst_byte_reader_get_uint16_be_unchecked (&br);
        horizontal_size = gst_byte_reader_get_uint32_be_unchecked (&br);
        vertical_size = gst_byte_reader_get_uint32_be_unchecked (&br);
        max_bit_rate = gst_byte_reader_get_uint32_be_unchecked (&br);
        max_buffer_size = gst_byte_reader_get_uint32_be_unchecked (&br);
        DEN_frame_rate = gst_byte_reader_get_uint16_be_unchecked (&br);
        NUM_frame_rate = gst_byte_reader_get_uint16_be_unchecked (&br);
        color_specification = gst_byte_reader_get_uint8_unchecked (&br);
        remaining_8b = gst_byte_reader_get_uint8_unchecked (&br);
        interlaced_video = remaining_8b & 0x40;
        /* we don't support demuxing interlaced at the moment */
        if (interlaced_video) {
          GST_ERROR
              ("GST_MPEGTS_STREAM_TYPE_VIDEO_JP2K: interlaced video not supported");
          return NULL;
        } else {
          interlace_mode = "progressive";
          stream->jp2kInfos.interlace = FALSE;
        }
        switch (color_specification) {
          case GST_MPEGTSDEMUX_JPEG2000_COLORSPEC_SRGB:
            colorspace = "sRGB";
            colorimetry_mode = GST_VIDEO_COLORIMETRY_SRGB;
            break;
          case GST_MPEGTSDEMUX_JPEG2000_COLORSPEC_REC601:
            colorspace = "sYUV";
            colorimetry_mode = GST_VIDEO_COLORIMETRY_BT601;
            break;
          case GST_MPEGTSDEMUX_JPEG2000_COLORSPEC_REC709:
          case GST_MPEGTSDEMUX_JPEG2000_COLORSPEC_CIELUV:
            colorspace = "sYUV";
            colorimetry_mode = GST_VIDEO_COLORIMETRY_BT709;
            break;
          default:
            break;
        }
        caps = gst_caps_new_simple ("image/x-jpc",
            "framerate", GST_TYPE_FRACTION, NUM_frame_rate, DEN_frame_rate,
            "interlace-mode", G_TYPE_STRING, interlace_mode,
            "colorimetry", G_TYPE_STRING, colorimetry_mode,
            "colorspace", G_TYPE_STRING, colorspace, NULL);
      }
      break;
    case ST_VIDEO_DIRAC:
      if (bstream->registration_id == 0x64726163) {
        GST_LOG ("dirac");
        /* dirac in hex */
        is_video = TRUE;
        caps = gst_caps_new_empty_simple ("video/x-dirac");
        tag_name = GST_TAG_VIDEO_CODEC;
        codec_name = g_strdup ("dirac");
        program->video_num++;
      }
      break;
    case ST_PRIVATE_EA:        /* Try to detect a VC1 stream */
    {
      gboolean is_vc1 = FALSE;

      /* Note/FIXME: RP-227 specifies that the registration descriptor
       * for vc1 can also contain other information, such as profile,
       * level, alignment, buffer_size, .... */
      if (bstream->registration_id == DRF_ID_VC1)
        is_vc1 = TRUE;
      if (!is_vc1) {
        GST_WARNING ("0xea private stream type found but no descriptor "
            "for VC1. Assuming plain VC1.");
      }

      is_video = TRUE;
      caps = gst_caps_new_simple ("video/x-wmv",
          "wmvversion", G_TYPE_INT, 3, "format", G_TYPE_STRING, "WVC1", NULL);
      tag_name = GST_TAG_VIDEO_CODEC;
      codec_name = g_strdup ("Microsoft Windows Media VC-1");
      program->video_num++;
      break;
    }
    case ST_PS_AUDIO_AC3:
      /* DVB_ENHANCED_AC3 */
      desc =
          mpegts_get_descriptor_from_stream (bstream,
          GST_MTS_DESC_DVB_ENHANCED_AC3);
      if (desc) {
        is_audio = TRUE;
        caps = gst_caps_new_empty_simple ("audio/x-eac3");
        gst_ts_demux_set_caps_for_private_dvb_ac3_eac3_descriptor (stream,
            desc->data);
        if (gst_ts_demux_set_caps_for_private_atmos_audio (stream, desc->data,
                TRUE))
          gst_caps_set_simple (caps, "immersive", G_TYPE_STRING, "ATMOS", NULL);
        tag_name = GST_TAG_AUDIO_CODEC;
        codec_name = g_strdup ("EAC-3 audio");
        break;
      }

      /* If stream has ac3 descriptor
       * OR program is ATSC (GA94)
       * OR stream registration is AC-3
       * then it's regular AC3 */
      if (bstream->registration_id == DRF_ID_AC3 ||
          program->registration_id == DRF_ID_GA94 ||
          mpegts_get_descriptor_from_stream (bstream, GST_MTS_DESC_DVB_AC3)) {
        is_audio = TRUE;
        caps = gst_caps_new_empty_simple ("audio/x-ac3");
        if (mpegts_get_descriptor_from_stream (bstream, GST_MTS_DESC_DVB_AC3))
          gst_ts_demux_set_caps_for_private_dvb_ac3_eac3_descriptor (stream,
              desc->data);
        tag_name = GST_TAG_AUDIO_CODEC;
        codec_name = g_strdup ("AC-3 audio");
        break;
      }

      GST_WARNING ("AC3 stream type found but no guaranteed "
          "way found to differentiate between AC3 and EAC3. "
          "Assuming plain AC3.");
      is_audio = TRUE;
      caps = gst_caps_new_empty_simple ("audio/x-ac3");
      tag_name = GST_TAG_AUDIO_CODEC;
      codec_name = g_strdup ("AC-3 audio");
      break;
    case ST_PS_AUDIO_EAC3:
    {
      /* ATSC_ENHANCED_AC3 */
      if (bstream->registration_id == DRF_ID_EAC3 ||
          (desc =
              mpegts_get_descriptor_from_stream (bstream,
                  GST_MTS_DESC_ATSC_EAC3))) {
        is_audio = TRUE;
        caps = gst_caps_new_empty_simple ("audio/x-eac3");
        if (desc
            && gst_ts_demux_set_caps_for_private_atmos_audio (stream,
                desc->data, TRUE))
          gst_caps_set_simple (caps, "immersive", G_TYPE_STRING, "ATMOS", NULL);
        tag_name = GST_TAG_AUDIO_CODEC;
        codec_name = g_strdup ("EAC-3 audio");
        break;
      }

      GST_ELEMENT_WARNING (demux, STREAM, DEMUX,
          ("Assuming ATSC E-AC3 audio stream."),
          ("ATSC E-AC3 stream type found but no guarantee way found to "
              "differentiate among other standards (DVB, ISDB and etc..)"));

      is_audio = TRUE;
      caps = gst_caps_new_empty_simple ("audio/x-eac3");
      tag_name = GST_TAG_AUDIO_CODEC;
      codec_name = g_strdup ("EAC-3 audio");
      break;
    }
    case ST_PS_AUDIO_LPCM2:
      is_audio = TRUE;
      caps = gst_caps_new_empty_simple ("audio/x-private2-lpcm");
      tag_name = GST_TAG_AUDIO_CODEC;
      codec_name = g_strdup ("2-ch LPCM audio via IEEE1394 Bus");
      break;
    case ST_PS_AUDIO_DTS:
      is_audio = TRUE;
      caps = gst_caps_new_empty_simple ("audio/x-dts");
      tag_name = GST_TAG_AUDIO_CODEC;
      codec_name = g_strdup ("DTS audio");
      break;
    case ST_PS_AUDIO_LPCM:
      is_audio = TRUE;
      caps = gst_caps_new_empty_simple ("audio/x-lpcm");
      tag_name = GST_TAG_AUDIO_CODEC;
      codec_name = g_strdup ("Uncompressed PCM audio");
      break;
    case ST_PS_DVD_SUBPICTURE:
      is_subpicture = TRUE;
      caps = gst_caps_new_empty_simple ("subpicture/x-dvd");
      sparse = TRUE;
      break;
    case 0x42:
      /* hack for Chinese AVS video stream which use 0x42 as stream_id
       * NOTE: this is unofficial and within the ISO reserved range. */
      /* To support Chinese Audio Video Standard(AVS) */
      is_video = TRUE;
      caps =
          gst_caps_new_simple ("video/x-cavs", "format", G_TYPE_STRING, "avs2",
          NULL);
      tag_name = GST_TAG_VIDEO_CODEC;
      codec_name = g_strdup ("AVS");
      break;
    default:
      GST_DEBUG ("Non-media stream (stream_type:0x%x). Not creating pad",
          bstream->stream_type);
      break;
  }

done:
  if (caps) {
    if (is_audio) {
      template = gst_static_pad_template_get (&audio_template);
      name =
          g_strdup_printf ("audio_%01x_%04x_%02u", demux->program_generation,
          bstream->pid, demux->n_audio_streams++);
      gst_stream_set_stream_type (bstream->stream_object,
          GST_STREAM_TYPE_AUDIO);
    } else if (is_video) {
      template = gst_static_pad_template_get (&video_template);
      name =
          g_strdup_printf ("video_%01x_%04x_%02u", demux->program_generation,
          bstream->pid, demux->n_video_streams++);
      gst_stream_set_stream_type (bstream->stream_object,
          GST_STREAM_TYPE_VIDEO);
    } else if (is_private) {
      template = gst_static_pad_template_get (&private_template);
      name =
          g_strdup_printf ("private_%01x_%04x_%02u", demux->program_generation,
          bstream->pid, demux->n_private_streams++);
    } else if (is_subpicture) {
      template = gst_static_pad_template_get (&subpicture_template);
      name =
          g_strdup_printf ("subpicture_%01x_%04x_%02u",
          demux->program_generation, bstream->pid, demux->n_private_streams++);
      gst_stream_set_stream_type (bstream->stream_object, GST_STREAM_TYPE_TEXT);
    } else
      g_assert_not_reached ();
  }

  /* Check thumbnail-mode and Do not add audio/subtitle pad */
  /* Choose SrcPad for HLSv4 structure */
  if ((demux->thumbnail_mode && !g_strrstr (name, "video"))
      || (!is_valid_teletext && g_strrstr (name, "private"))
      || ((demux->srcpad_detect_mode == PAD_MODE_HLSV4_VIDEO_ONLY)
          && !g_strrstr (name, "video"))
      || ((demux->srcpad_detect_mode == PAD_MODE_HLSV4_AUDIO_ONLY)
          && !g_strrstr (name, "audio"))) {
    GST_INFO_OBJECT (demux, "We don't need to add the pad");
    if (caps)
      gst_caps_unref (caps);
    if (template)
      gst_object_unref (template);
    g_free (name);
    g_free (codec_name);
    return NULL;
  }

  if (template && name && caps) {
    GstEvent *event;
    const gchar *stream_id;
    GstStreamFlags stream_flags = GST_STREAM_FLAG_NONE;
    guint8 ctags;
    const GstDvrMpegtsDescriptor *desc_audio;

    gst_caps_set_simple (caps, "container", G_TYPE_STRING, "ts", NULL);
    gst_caps_set_simple (caps, "pid", G_TYPE_UINT, bstream->pid, NULL);

    /* parse audio_type value of ISO_639_language_code descriptor in PMT
     * 1) MHEG-ICS: only use the audio_type value via "type"
     * 2) HbbTV 2.0.1: only use the audio_type == 0x03 (Visual impaired) via "role"
     */
    audio_type = gst_ts_demux_get_audio_type (stream);
    if (audio_type) {
      gst_caps_set_simple (caps, "type", G_TYPE_UINT, audio_type, NULL);
      if (audio_type == 0x03) {
        /*gst_caps_set_simple (caps, "role", G_TYPE_STRING,
           "description+supplementary", NULL); */
        audioDescType = "description+supplementary";
      }
    }

    if (stream->taglist
        && gst_tag_list_get_string (stream->taglist, GST_TAG_LANGUAGE_NAME,
            &lang_code)) {
      gst_caps_set_simple (caps, "langcode", G_TYPE_STRING, lang_code, NULL);
      GST_INFO ("This stream(0x%02x) contains language code(%s).",
          bstream->stream_type, lang_code);
    }

    /* Add component tag information to caps. For MHEG. */
    desc_audio =
        mpegts_get_descriptor_from_stream (bstream,
        GST_MTS_DESC_DVB_STREAM_IDENTIFIER);
    if (desc_audio) {
      ctags = DESC_DVB_STREAM_IDENTIFIER_component_tag (desc_audio->data);
      gst_caps_set_simple (caps, "ctags", G_TYPE_UINT, ctags, NULL);
    }

    /* Add DVB supplementary audio information to GstCaps, for HbbTV Cert. */
    desc_audio =
        mpegts_get_dvb_extension_descriptor_from_stream (bstream,
        GST_MTS_DESC_DVB_EXTENSION, GST_MTS_DESC_EXT_DVB_SUPPLEMENTARY_AUDIO);
    if (desc_audio
        &&
        (DESC_DVB_SUPPLEMENTARY_AUDIO_editorial_classification
            (desc_audio->data)) == 0x01) {
      /* editorial_classification:
       * 0x01: Audio description for the visually impaired
       */
      /*gst_caps_set_simple (caps, "role", G_TYPE_STRING,
         "description+supplementary", NULL); */
      audioDescType = "description+supplementary";
    }

    GST_INFO_OBJECT (caps,
        "stream:%p creating pad with name %s and caps %"
        GST_PTR_FORMAT, stream, name, caps);
    if (g_strrstr (name, "video") || g_strrstr (name, "audio"))
      program->is_valid_program = TRUE;
    pad = gst_pad_new_from_template (template, name);
    gst_pad_set_active (pad, TRUE);
    gst_pad_use_fixed_caps (pad);
    stream_id = gst_stream_get_stream_id (bstream->stream_object);

    event = gst_pad_get_sticky_event (base->sinkpad, GST_EVENT_STREAM_START, 0);
    if (event) {
      if (gst_event_parse_group_id (event, &demux->group_id))
        demux->have_group_id = TRUE;
      else
        demux->have_group_id = FALSE;
      gst_event_parse_stream_flags (event, &stream_flags);
      gst_event_unref (event);
    } else if (!demux->have_group_id) {
      demux->have_group_id = TRUE;
      demux->group_id = gst_util_group_id_next ();
    }
    event = gst_event_new_stream_start (stream_id);
    gst_event_set_stream (event, bstream->stream_object);
    if (demux->have_group_id)
      gst_event_set_group_id (event, demux->group_id);

    if (sparse)
      stream_flags |= GST_STREAM_FLAG_SPARSE;
    else
      stream_flags &= ~GST_STREAM_FLAG_SPARSE;

    gst_event_set_stream_flags (event, stream_flags);
    gst_stream_set_stream_flags (bstream->stream_object, stream_flags);

    stream->sparse = sparse;

    /* Set upstream-id on caps, to prevent caps event drop on downstream element.
     * If not, a downstream element (mostly identity) will drop caps event which
     * does not changed from previous one. However, we need to send caps to decoder
     * explicitly, in case of stream-change */
    gst_caps_set_simple (caps,
        "upstream-id", G_TYPE_STRING, bstream->stream_id, NULL);

    gst_stream_set_caps (bstream->stream_object, caps);
    if (!stream->taglist)
      stream->taglist = gst_tag_list_new_empty ();
    if (codec_name)
      gst_tag_list_add (stream->taglist, GST_TAG_MERGE_APPEND, tag_name,
          codec_name, NULL);
    if (audioDescType)
      gst_tag_list_add (stream->taglist, GST_TAG_MERGE_REPLACE,
          GST_TAG_AUDIO_DESCRIPTION, audioDescType, NULL);

    gst_pb_utils_add_codec_description_to_tag_list (stream->taglist, NULL,
        caps);
    gst_stream_set_tags (bstream->stream_object, stream->taglist);

    gst_pad_push_event (pad, event);
    gst_pad_set_caps (pad, caps);
    gst_pad_set_query_function (pad, gst_ts_demux_srcpad_query);
    gst_pad_set_event_function (pad, gst_ts_demux_srcpad_event);
  }

  g_free (name);
  g_free (codec_name);
  g_free (lang_code);
  if (template)
    gst_object_unref (template);
  if (caps)
    gst_caps_unref (caps);

  return pad;
}

static gboolean
gst_ts_demux_hdcp_decryption (guint64 input_counter, guint32 stream_counter,
    guint8 * data, gint32 datalen, guint8 ** decrypted_data)
{
  static const char _szAPIName[] = "decryptionPesPayloadByHdcp2";
  static gboolean (*_pfnDecryptPayload) (guint8 *, guint8 *, guint8 *,
      gint32, guint8 **) = NULL;

  if (_pfnDecryptPayload == NULL) {
    if ((_pfnDecryptPayload = dlsym (NULL, _szAPIName)) == NULL) {
      GST_WARNING ("Could not find symbol '%s'.", _szAPIName);
      return FALSE;
    }
  }

  return _pfnDecryptPayload ((guint8 *) & input_counter,
      (guint8 *) & stream_counter, data, datalen, decrypted_data);
}

static gboolean
gst_ts_demux_stream_added (MpegTSBase * base, MpegTSBaseStream * bstream,
    MpegTSBaseProgram * program)
{
  GstTSDemux *demux = (GstTSDemux *) base;
  TSDemuxStream *stream = (TSDemuxStream *) bstream;

  if (!stream->pad) {
    /* Create the pad */
    if (bstream->stream_type != 0xff) {
      stream->pad = create_pad_for_stream (base, bstream, program);
      if (stream->pad)
        gst_flow_combiner_add_pad (demux->flowcombiner, stream->pad);
    }

    if (base->mode != BASE_MODE_PUSHING
        && bstream->stream_type == GST_MPEGTS_STREAM_TYPE_VIDEO_H264) {
      stream->scan_function =
          (GstTsDemuxKeyFrameScanFunction) scan_keyframe_h264;
    } else {
      stream->scan_function = NULL;
    }

    stream->active = FALSE;

    stream->need_newsegment = TRUE;
    /* Reset segment if we're not doing an accurate seek */
    demux->reset_segment = (!(demux->segment.flags & GST_SEEK_FLAG_ACCURATE));
    stream->needs_keyframe = FALSE;
    stream->discont = TRUE;
    stream->pts = GST_CLOCK_TIME_NONE;
    stream->dts = GST_CLOCK_TIME_NONE;
    stream->first_pts = GST_CLOCK_TIME_NONE;
    stream->raw_pts = -1;
    stream->raw_dts = -1;
    stream->pending_ts = TRUE;
    stream->nb_out_buffers = 0;
    stream->gap_ref_buffers = 0;
    stream->gap_ref_pts = GST_CLOCK_TIME_NONE;
    /* Only wait for a valid timestamp if we have a PCR_PID */
    stream->pending_ts = program->pcr_pid < 0x1fff;
    stream->continuity_counter = CONTINUITY_UNSET;
  }

  return (stream->pad != NULL);
}

static void
tsdemux_h264_parsing_info_clear (TSDemuxH264ParsingInfos * h264infos)
{
  clear_simple_buffer (&h264infos->framedata);

  if (h264infos->parser) {
    gst_h264_nal_parser_free (h264infos->parser);
    gst_byte_writer_free (h264infos->sps);
    gst_byte_writer_free (h264infos->pps);
    gst_byte_writer_free (h264infos->sei);
  }
}

static void
gst_ts_demux_stream_removed (MpegTSBase * base, MpegTSBaseStream * bstream)
{
  TSDemuxStream *stream = (TSDemuxStream *) bstream;

  if (stream->pad) {
    gst_flow_combiner_remove_pad (GST_TS_DEMUX_CAST (base)->flowcombiner,
        stream->pad);
    if (stream->active) {

      if (gst_pad_is_active (stream->pad)) {
        /* Flush out all data */
        GST_DEBUG_OBJECT (stream->pad, "Flushing out pending data");
        gst_ts_demux_push_pending_data ((GstTSDemux *) base, stream, NULL);

        GST_DEBUG_OBJECT (stream->pad, "Pushing out EOS");
        gst_pad_push_event (stream->pad, gst_event_new_eos ());
        gst_pad_set_active (stream->pad, FALSE);
      }

      GST_DEBUG_OBJECT (stream->pad, "Removing pad");
      gst_element_remove_pad (GST_ELEMENT_CAST (base), stream->pad);
      stream->active = FALSE;
    } else {
      gst_object_unref (stream->pad);
    }
    stream->pad = NULL;
  }

  gst_ts_demux_stream_flush (stream, GST_TS_DEMUX_CAST (base), TRUE);

  if (stream->taglist != NULL) {
    gst_tag_list_unref (stream->taglist);
    stream->taglist = NULL;
  }

  tsdemux_h264_parsing_info_clear (&stream->h264infos);
}

static void
gst_ts_demux_detect_video_stream (GstTSDemux * tsdemux,
    MpegTSBaseStream * bstream)
{
  MpegTSBase *base = (MpegTSBase *) tsdemux;

  switch (bstream->stream_type) {
    case GST_MPEGTS_STREAM_TYPE_VIDEO_MPEG1:
    case GST_MPEGTS_STREAM_TYPE_VIDEO_MPEG2:
    case GST_MPEGTS_STREAM_TYPE_VIDEO_MPEG4:
    case GST_MPEGTS_STREAM_TYPE_VIDEO_H264:
    case GST_MPEGTS_STREAM_TYPE_VIDEO_HEVC_H265:
    case GST_MPEGTS_STREAM_TYPE_VIDEO_HEVC:
      base->video_pid = bstream->pid;
      break;
    default:
      break;
  }
}

static void
activate_pad_for_stream (GstTSDemux * tsdemux, TSDemuxStream * stream)
{
  if (stream->pad) {
    GST_DEBUG_OBJECT (tsdemux, "Activating pad %s:%s for stream %p",
        GST_DEBUG_PAD_NAME (stream->pad), stream);
    gst_element_add_pad ((GstElement *) tsdemux, stream->pad);
    stream->active = TRUE;
    GST_INFO_OBJECT (stream->pad, "done adding pad");

    /* Send GAP event to audio pad for only WiDi case */
    if ((tsdemux->app_type == APP_TYPE_RTC)
        && g_strrstr (GST_PAD_NAME (stream->pad), "audio"))
      gst_pad_push_event (stream->pad, gst_event_new_gap (0, 0));

    /* Set PID of video stream for reverse trick-mode */
    gst_ts_demux_detect_video_stream (tsdemux, (MpegTSBaseStream *) stream);
  } else if (((MpegTSBaseStream *) stream)->stream_type != 0xff) {
    GST_DEBUG_OBJECT (tsdemux,
        "stream %p (pid 0x%04x, type:0x%02x) has no pad", stream,
        ((MpegTSBaseStream *) stream)->pid,
        ((MpegTSBaseStream *) stream)->stream_type);
  }
}

static void
gst_ts_demux_stream_flush (TSDemuxStream * stream, GstTSDemux * tsdemux,
    gboolean hard)
{
  if (stream == NULL)
    return;

  GST_DEBUG ("flushing stream %p", stream);

  g_free (stream->data);
  stream->data = NULL;
  stream->state = PENDING_PACKET_EMPTY;
  stream->expected_size = 0;
  stream->allocated_size = 0;
  stream->current_size = 0;
  stream->discont = TRUE;
  stream->pts = GST_CLOCK_TIME_NONE;
  stream->dts = GST_CLOCK_TIME_NONE;
  stream->raw_pts = -1;
  stream->raw_dts = -1;
  stream->nb_out_buffers = 0;
  stream->gap_ref_buffers = 0;
  stream->gap_ref_pts = GST_CLOCK_TIME_NONE;
  stream->continuity_counter = CONTINUITY_UNSET;

  if (G_UNLIKELY (stream->pending)) {
    GList *tmp;

    GST_DEBUG ("clearing pending %p", stream);
    for (tmp = stream->pending; tmp; tmp = tmp->next) {
      PendingBuffer *pend = (PendingBuffer *) tmp->data;
      gst_buffer_unref (pend->buffer);
      g_slice_free (PendingBuffer, pend);
    }
    g_list_free (stream->pending);
    stream->pending = NULL;
  }

  /* FIXME: LGE didn't use hard flag yet.
   * The flag set as TRUE only for removed stream.
   * This flag should be used in future.
   */
  //if (hard) {
  if (tsdemux->app_type != APP_TYPE_RTC) {
    stream->first_pts = GST_CLOCK_TIME_NONE;
    stream->need_newsegment = TRUE;
  }
  //}

  /* geunil.jung. For high speed trick */
  stream->last_scan_offset = 0;
  stream->frame_scan_done = FALSE;
  stream->is_iframe = FALSE;
  stream->is_first_iframe_in_interlace = FALSE;

  /* for error detect */
  stream->error_count = 0;

  /* for HLS */
  stream->last_valid_pts = GST_CLOCK_TIME_NONE;
  stream->last_valid_dts = GST_CLOCK_TIME_NONE;
  stream->ts_base_offset = 0;
  stream->ts_wrap_count = 0;

  /* for HLS roll-over */
  stream->is_roll_over = FALSE;
}

static void
gst_ts_demux_flush_streams (GstTSDemux * demux, gboolean hard)
{
  GList *walk;
  if (!demux->program)
    return;

  // FIXME: We should change as smart.
  hard = TRUE;
  for (walk = demux->program->stream_list; walk; walk = g_list_next (walk))
    gst_ts_demux_stream_flush (walk->data, demux, hard);
}

static gboolean
gst_ts_demux_can_remove_program (MpegTSBase * base, MpegTSBaseProgram * program)
{
  GstTSDemux *demux = GST_TS_DEMUX (base);

  /* If it's our current active program, we return FALSE, we'll deactivate it
   * ourselves when the next program gets activated */
  if (demux->program == program) {
    GST_DEBUG
        ("Attempting to remove current program, delaying until new program gets activated");
    demux->previous_program = program;
    demux->program_number = -1;
    return FALSE;
  }
  return TRUE;
}

static void
gst_ts_demux_update_program (MpegTSBase * base, MpegTSBaseProgram * program)
{
  GstTSDemux *demux = GST_TS_DEMUX (base);
  GList *tmp;

  GST_DEBUG ("Updating program %d", program->program_number);
  /* Emit collection message */
  gst_element_post_message ((GstElement *) base,
      gst_message_new_stream_collection ((GstObject *) base,
          program->collection));

  /* Add all streams, then fire no-more-pads */
  for (tmp = program->stream_list; tmp; tmp = tmp->next) {
    TSDemuxStream *stream = (TSDemuxStream *) tmp->data;
    if (stream->pad && !stream->active) {
      activate_pad_for_stream (demux, stream);
      if (stream->sparse) {
        /* force sending of pending sticky events which have been stored on the
         * pad already and which otherwise would only be sent on the first buffer
         * or serialized event (which means very late in case of subtitle streams),
         * and playsink waits for stream-start or another serialized event */
        GST_DEBUG_OBJECT (stream->pad, "sparse stream, pushing GAP event");
        gst_pad_push_event (stream->pad, gst_event_new_gap (0, 0));
      }
    }
  }
}

static void
gst_ts_demux_program_started (MpegTSBase * base, MpegTSBaseProgram * program)
{
  GstTSDemux *demux = GST_TS_DEMUX (base);

  GST_DEBUG ("Current program %d, new program %d requested program %d",
      (gint) demux->program_number, program->program_number,
      demux->requested_program_number);

  if (demux->requested_program_number == program->program_number ||
      (demux->requested_program_number == -1 && demux->program_number == -1)) {

    GList *tmp;
    gboolean have_pads = FALSE;
    GPtrArray *pat;

    pat = base->pat;

    /* If activated-PAT has multi-program,
     * we need to check conformance of this program.
     * Do not activate program if:
     * 1) Recoding of Hikari settop box via DLNA has invalid multi-video tracks
     * 2) Invalid DolbyVision TS file has no signaling for multi-video tracks */
    if (pat && pat->len >= 2 && program->video_num >= 2) {
      guint16 i, nb_programs = 0;
      for (i = 0; i < pat->len; i++) {
        GstDvrMpegtsPatProgram *patp = g_ptr_array_index (pat, i);
        /* NIT was excluded from count of program.
         * NIT has program_number == '0' */
        if (patp->program_number != 0)
          nb_programs++;
      }
      GST_DEBUG
          ("%u number of program in PMT, %u actual number of program without NIT",
          pat->len, nb_programs);

      if (nb_programs >= 2)
        return;
    }

    GST_DEBUG ("program %d started", program->program_number);
    demux->program_number = program->program_number;
    demux->program = program;
    base->is_program_started = TRUE;

    /* Increment the program_generation counter */
    demux->program_generation = (demux->program_generation + 1) & 0xf;

    /* Emit collection message */
    gst_element_post_message ((GstElement *) base,
        gst_message_new_stream_collection ((GstObject *) base,
            program->collection));

    /* If this is not the initial program, we need to calculate
     * a new segment */
    gst_event_replace (&demux->segment_event, NULL);

    /* DRAIN ALL STREAMS FIRST ! */
    if (demux->previous_program) {
      GList *tmp;
      GST_DEBUG_OBJECT (demux, "Draining previous program");
      for (tmp = demux->previous_program->stream_list; tmp; tmp = tmp->next) {
        TSDemuxStream *stream = (TSDemuxStream *) tmp->data;
        if (stream->pad)
          gst_ts_demux_push_pending_data (demux, stream,
              demux->previous_program);
      }
    }

    /* Add all streams, then fire no-more-pads */
    for (tmp = program->stream_list; tmp; tmp = tmp->next) {
      TSDemuxStream *stream = (TSDemuxStream *) tmp->data;
      activate_pad_for_stream (demux, stream);
      if (stream->pad)
        have_pads = TRUE;
    }

    /* If there was a previous program, now is the time to deactivate it
     * and remove old pads (including pushing EOS) */
    if (demux->previous_program) {
      GST_DEBUG ("Deactivating previous program");
      mpegts_base_deactivate_and_free_program (base, demux->previous_program);
      demux->previous_program = NULL;
    }

    if (!have_pads) {
      /* If we had no pads, this stream is likely corrupted or unsupported and
       * there's not much we can do at this point */
      GST_ELEMENT_ERROR (demux, STREAM, WRONG_TYPE,
          ("This stream contains no valid or supported streams."),
          ("activating program but got no pads"));
      return;
    }

    /* If any of the stream is sparse, push a GAP event before anything else
     * This is done here, and not in activate_pad_for_stream() because pushing
     * a GAP event *is* considering data, and we want to ensure the (potential)
     * old pads are all removed before we push any data on the new ones */
    for (tmp = program->stream_list; tmp; tmp = tmp->next) {
      TSDemuxStream *stream = (TSDemuxStream *) tmp->data;
      if (stream->sparse) {
        /* force sending of pending sticky events which have been stored on the
         * pad already and which otherwise would only be sent on the first buffer
         * or serialized event (which means very late in case of subtitle streams),
         * and playsink waits for stream-start or another serialized event */
        GST_DEBUG_OBJECT (stream->pad, "sparse stream, pushing GAP event");
        gst_pad_push_event (stream->pad, gst_event_new_gap (0, 0));
      }
    }

    gst_element_no_more_pads ((GstElement *) demux);
  }
}

static void
gst_ts_demux_program_stopped (MpegTSBase * base, MpegTSBaseProgram * program)
{
  GstTSDemux *demux = GST_TS_DEMUX (base);

  if (demux->program == program) {
    demux->program = NULL;
    demux->program_number = -1;
  }
}

static inline void
gst_ts_demux_record_pts (GstTSDemux * demux, TSDemuxStream * stream,
    guint64 pts, guint64 offset)
{
  MpegTSBaseStream *bs = (MpegTSBaseStream *) stream;
  MpegTSBase *base = (MpegTSBase *) demux;

  stream->raw_pts = pts;
  if (pts == -1) {
    stream->pts = GST_CLOCK_TIME_NONE;
    return;
  }

  GST_INFO ("pid 0x%04x raw pts:%" G_GUINT64_FORMAT " at offset %"
      G_GUINT64_FORMAT, bs->pid, pts, offset);

  /* Compute PTS in GstClockTime */
  GST_INFO ("Record PTS VALUE!!!");
  if (base->real_time)
    stream->pts =
        mpegts_packetizer_calculate_ts (MPEG_TS_BASE_PACKETIZER (demux),
        MPEGTIME_TO_GSTTIME (pts), &stream->last_valid_pts,
        &stream->ts_base_offset, &stream->ts_wrap_count,
        demux->program->pcr_pid);
  else
    stream->pts =
        mpegts_packetizer_pts_to_ts (MPEG_TS_BASE_PACKETIZER (demux),
        MPEGTIME_TO_GSTTIME (pts), demux->program->pcr_pid);

  GST_INFO ("pid 0x%04x Stored PTS %" G_GUINT64_FORMAT, bs->pid, stream->pts);

  if (base->mheg_ics) {
    if (GST_CLOCK_TIME_IS_VALID (demux->last_pts) &&
        GST_CLOCK_DIFF (stream->pts + demux->rollover_pts,
            demux->last_pts) > (GST_SECOND * 60)) {
      if (demux->rollover_stream == NULL)
        demux->rollover_stream = bs;
      if (demux->rollover_stream == bs) {
        demux->rollover_pts = demux->last_pts;
        GST_INFO_OBJECT (stream->pad,
            "#### rollover detected: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (demux->rollover_pts));
      }
    } else {
      demux->last_pts = stream->pts;
      if (demux->rollover_pts)
        demux->last_pts += demux->rollover_pts;
    }

    if (GST_CLOCK_TIME_IS_VALID (stream->pts) && demux->rollover_pts)
      stream->pts += demux->rollover_pts;
  }

  if (G_UNLIKELY (demux->emit_statistics)) {
    GstStructure *st;
    st = gst_structure_new_id_empty (QUARK_TSDEMUX);
    gst_structure_id_set (st,
        QUARK_PID, G_TYPE_UINT, bs->pid,
        QUARK_OFFSET, G_TYPE_UINT64, offset, QUARK_PTS,
        G_TYPE_UINT64, pts, NULL);
    gst_element_post_message (GST_ELEMENT_CAST (demux),
        gst_message_new_element (GST_OBJECT (demux), st));
  }
}

static inline void
gst_ts_demux_record_dts (GstTSDemux * demux, TSDemuxStream * stream,
    guint64 dts, guint64 offset)
{
  MpegTSBaseStream *bs = (MpegTSBaseStream *) stream;
  MpegTSBase *base = (MpegTSBase *) demux;

  stream->raw_dts = dts;
  if (dts == -1) {
    stream->dts = GST_CLOCK_TIME_NONE;
    return;
  }

  GST_LOG ("pid 0x%04x raw dts:%" G_GUINT64_FORMAT " at offset %"
      G_GUINT64_FORMAT, bs->pid, dts, offset);

  /* Compute DTS in GstClockTime */
  GST_INFO ("Record DTS VALUE!!!");
  if (base->real_time)
    stream->dts =
        mpegts_packetizer_calculate_ts (MPEG_TS_BASE_PACKETIZER (demux),
        MPEGTIME_TO_GSTTIME (dts), &stream->last_valid_dts,
        &stream->ts_base_offset, &stream->ts_wrap_count,
        demux->program->pcr_pid);
  else
    stream->dts =
        mpegts_packetizer_pts_to_ts (MPEG_TS_BASE_PACKETIZER (demux),
        MPEGTIME_TO_GSTTIME (dts), demux->program->pcr_pid);

  GST_LOG ("pid 0x%04x Stored DTS %" G_GUINT64_FORMAT, bs->pid, stream->dts);

  if (base->mheg_ics) {
    if (GST_CLOCK_TIME_IS_VALID (stream->dts) && demux->rollover_pts)
      stream->dts += demux->rollover_pts;
  }

  if (G_UNLIKELY (demux->emit_statistics)) {
    GstStructure *st;
    st = gst_structure_new_id_empty (QUARK_TSDEMUX);
    gst_structure_id_set (st,
        QUARK_PID, G_TYPE_UINT, bs->pid,
        QUARK_OFFSET, G_TYPE_UINT64, offset, QUARK_DTS,
        G_TYPE_UINT64, dts, NULL);
    gst_element_post_message (GST_ELEMENT_CAST (demux),
        gst_message_new_element (GST_OBJECT (demux), st));
  }
}

/* This is called when we haven't got a valid initial PTS/DTS on all streams */
static gboolean
check_pending_buffers (GstTSDemux * demux)
{
  gboolean have_observation = FALSE;
  /* The biggest offset */
  guint64 offset = 0;
  GList *tmp;
  gboolean have_only_sparse = TRUE;

  /* 0. Do we only have sparse stream */
  for (tmp = demux->program->stream_list; tmp; tmp = tmp->next) {
    TSDemuxStream *tmpstream = (TSDemuxStream *) tmp->data;

    if (!tmpstream->sparse) {
      have_only_sparse = FALSE;
      break;
    }
  }

  /* 1. Go over all streams */
  for (tmp = demux->program->stream_list; tmp; tmp = tmp->next) {
    TSDemuxStream *tmpstream = (TSDemuxStream *) tmp->data;
    /* 1.1 check if at least one stream got a valid DTS */
    if (have_only_sparse || !tmpstream->sparse) {
      if ((tmpstream->raw_dts != -1 && tmpstream->dts != GST_CLOCK_TIME_NONE) ||
          (tmpstream->raw_pts != -1 && tmpstream->pts != GST_CLOCK_TIME_NONE)) {
        have_observation = TRUE;
        break;
      }
    }
  }

  /* 2. If we don't have a valid value yet, break out */
  if (have_observation == FALSE)
    return FALSE;

  /* 3. Go over all streams that have current/pending data */
  for (tmp = demux->program->stream_list; tmp; tmp = tmp->next) {
    TSDemuxStream *tmpstream = (TSDemuxStream *) tmp->data;
    PendingBuffer *pend;
    guint64 firstval, lastval, ts;

    /* 3.1 Calculate the offset between current DTS and first DTS */
    if (tmpstream->pending == NULL || tmpstream->state == PENDING_PACKET_EMPTY)
      continue;
    /* If we don't have any pending data, the offset is 0 for this stream */
    if (tmpstream->pending == NULL)
      break;
    if (tmpstream->raw_dts != -1)
      lastval = tmpstream->raw_dts;
    else if (tmpstream->raw_pts != -1)
      lastval = tmpstream->raw_pts;
    else {
      GST_WARNING ("Don't have a last DTS/PTS to use for offset recalculation");
      continue;
    }
    pend = tmpstream->pending->data;
    if (pend->dts != -1)
      firstval = pend->dts;
    else if (pend->pts != -1)
      firstval = pend->pts;
    else {
      GST_WARNING
          ("Don't have a first DTS/PTS to use for offset recalculation");
      continue;
    }
    /* 3.2 Add to the offset the report TS for the current DTS */
    ts = mpegts_packetizer_pts_to_ts (MPEG_TS_BASE_PACKETIZER (demux),
        MPEGTIME_TO_GSTTIME (lastval), demux->program->pcr_pid);
    if (ts == GST_CLOCK_TIME_NONE) {
      GST_WARNING ("THIS SHOULD NOT HAPPEN !");
      continue;
    }
    ts += MPEGTIME_TO_GSTTIME (lastval - firstval);
    /* 3.3 If that offset is bigger than the current offset, store it */
    if (ts > offset)
      offset = ts;
  }

  GST_DEBUG ("New initial pcr_offset %" GST_TIME_FORMAT,
      GST_TIME_ARGS (offset));

  /* 4. Set the offset on the packetizer */
  mpegts_packetizer_set_current_pcr_offset (MPEG_TS_BASE_PACKETIZER (demux),
      offset, demux->program->pcr_pid);

  /* 4. Go over all streams */
  for (tmp = demux->program->stream_list; tmp; tmp = tmp->next) {
    TSDemuxStream *stream = (TSDemuxStream *) tmp->data;

    stream->pending_ts = FALSE;
    /* 4.1 Set pending_ts for FALSE */

    /* 4.2 Recalculate PTS/DTS (in running time) for pending data */
    if (stream->pending) {
      GList *tmp2;
      for (tmp2 = stream->pending; tmp2; tmp2 = tmp2->next) {
        PendingBuffer *pend = (PendingBuffer *) tmp2->data;
        if (pend->pts != -1)
          GST_BUFFER_PTS (pend->buffer) =
              mpegts_packetizer_pts_to_ts (MPEG_TS_BASE_PACKETIZER (demux),
              MPEGTIME_TO_GSTTIME (pend->pts), demux->program->pcr_pid);
        if (pend->dts != -1)
          GST_BUFFER_DTS (pend->buffer) =
              mpegts_packetizer_pts_to_ts (MPEG_TS_BASE_PACKETIZER (demux),
              MPEGTIME_TO_GSTTIME (pend->dts), demux->program->pcr_pid);
        /* 4.2.2 Set first_pts to TS of lowest PTS (for segment) */
        if (stream->first_pts == GST_CLOCK_TIME_NONE) {
          if (GST_BUFFER_PTS (pend->buffer) != GST_CLOCK_TIME_NONE)
            stream->first_pts = GST_BUFFER_PTS (pend->buffer);
          else if (GST_BUFFER_DTS (pend->buffer) != GST_CLOCK_TIME_NONE)
            stream->first_pts = GST_BUFFER_DTS (pend->buffer);
        }
      }
    }
    /* Recalculate PTS/DTS (in running time) for current data */
    if (stream->state != PENDING_PACKET_EMPTY) {
      if (stream->raw_pts != -1) {
        stream->pts =
            mpegts_packetizer_pts_to_ts (MPEG_TS_BASE_PACKETIZER (demux),
            MPEGTIME_TO_GSTTIME (stream->raw_pts), demux->program->pcr_pid);
        if (stream->first_pts == GST_CLOCK_TIME_NONE)
          stream->first_pts = stream->pts;
      }
      if (stream->raw_dts != -1) {
        stream->dts =
            mpegts_packetizer_pts_to_ts (MPEG_TS_BASE_PACKETIZER (demux),
            MPEGTIME_TO_GSTTIME (stream->raw_dts), demux->program->pcr_pid);
        if (stream->first_pts == GST_CLOCK_TIME_NONE)
          stream->first_pts = stream->dts;
      }
    }
  }

  return TRUE;
}

static inline gboolean
gst_ts_demux_parse_private_data_for_hdcp (HDCPInfo * hdcp_info,
    const guint8 * private_data)
{
  gint i = 0;
  guint32 temp32 = 0;

  /* reserved bits check. It has a value of '0' */
  if ((GST_READ_UINT16_BE (private_data) & 0xFFF8) ||
      (GST_READ_UINT16_BE (private_data) & 0xFFE0))
    return FALSE;

  /* marker bits check. It has a value of '1' */
  for (i = 0; i < 16; i += 2) {
    if (!(GST_READ_UINT16_BE (private_data + i) & 0x0001))
      return FALSE;
  }

  /* get input and stream counter from hdcp private data of pes header */
  temp32 = GST_READ_UINT32_BE (private_data);
  hdcp_info->stream_counter = ((temp32 & 0x00060000) << 13);
  hdcp_info->stream_counter |= ((temp32 & 0x0000FFFE) << 14);

  temp32 = GST_READ_UINT32_BE (private_data + 4);
  hdcp_info->stream_counter |= ((temp32 & 0xFFFE0000) >> 17);
  hdcp_info->input_counter = ((guint64) (temp32 & 0x0000001E) << 59);

  temp32 = GST_READ_UINT32_BE (private_data + 8);
  hdcp_info->input_counter |= ((guint64) (temp32 & 0xFFFE0000) << 28);
  hdcp_info->input_counter |= ((guint64) (temp32 & 0x0000FFFE) << 29);

  temp32 = GST_READ_UINT32_BE (private_data + 12);
  hdcp_info->input_counter |= ((guint64) (temp32 & 0xFFFE0000) >> 2);
  hdcp_info->input_counter |= ((guint64) (temp32 & 0x0000FFFE) >> 1);

  GST_DEBUG ("hdcp stream counter: %" G_GUINT32_FORMAT,
      hdcp_info->stream_counter);
  GST_DEBUG ("hdcp input counter: %" G_GUINT64_FORMAT,
      hdcp_info->input_counter);

  return TRUE;
}

static void
gst_ts_demux_parse_pes_header (GstTSDemux * demux, TSDemuxStream * stream,
    guint8 * data, guint32 length, guint64 bufferoffset)
{
  MpegTSBase *base = (MpegTSBase *) demux;
  PESHeader header;
  PESParsingResult parseres;
  const GstDvrMpegtsDescriptor *descr = NULL;
  MpegTSBaseStream *bs = (MpegTSBaseStream *) stream;

  GST_MEMDUMP ("Header buffer", data, MIN (length, 32));

  parseres = mpegts_parse_pes_header (data, length, &header);
  if (G_UNLIKELY (parseres == PES_PARSING_NEED_MORE))
    goto discont;

  if (G_UNLIKELY (parseres == PES_PARSING_BAD)) {
    GST_WARNING ("Error parsing PES header. pid: 0x%x stream_type: 0x%x",
        stream->stream.pid, stream->stream.stream_type);
    if (!base->mheg_ics) {
      stream->error_count++;
      if (stream->error_count >= 100) {
        GST_ELEMENT_ERROR (base, STREAM, DEMUX,
            ("PES parsing error"), ("PES error count %d", stream->error_count));
      }
    }
    goto discont;
  } else if (G_UNLIKELY (parseres == PES_PARSING_OK))
    stream->error_count = 0;

  if (stream->target_pes_substream != 0
      && header.stream_id_extension != stream->target_pes_substream) {
    GST_DEBUG ("Skipping unwanted substream");
    goto discont;
  }

  gst_ts_demux_record_dts (demux, stream, header.DTS, bufferoffset);
  gst_ts_demux_record_pts (demux, stream, header.PTS, bufferoffset);
  if (G_UNLIKELY (stream->pending_ts &&
          (stream->pts != GST_CLOCK_TIME_NONE
              || stream->dts != GST_CLOCK_TIME_NONE))) {
    GST_DEBUG ("Got pts/dts update, rechecking all streams");
    check_pending_buffers (demux);
  } else if (stream->first_pts == GST_CLOCK_TIME_NONE) {
    if (GST_CLOCK_TIME_IS_VALID (stream->pts))
      stream->first_pts = stream->pts;
    else if (GST_CLOCK_TIME_IS_VALID (stream->dts))
      stream->first_pts = stream->dts;
  }

  GST_DEBUG_OBJECT (demux,
      "stream PTS %" GST_TIME_FORMAT " DTS %" GST_TIME_FORMAT,
      GST_TIME_ARGS (stream->pts), GST_TIME_ARGS (stream->dts));

  /* Set hdcp private data for stream & input counter */
  if (header.private_data) {
    stream->hdcp_info.private_data =
        gst_ts_demux_parse_private_data_for_hdcp (&stream->hdcp_info,
        header.private_data);

    if (stream->hdcp_info.private_data
        && demux->program->registration_id != DRF_ID_HDCP)
      GST_WARNING
          ("The stream is encrypted by HDCP. "
          "But the HDCP Registration Descriptor is NOT represented.");
  } else
    stream->hdcp_info.private_data = FALSE;

  /* Remove PES headers */
  GST_DEBUG ("Moving data forward by %d bytes (packet_size:%d, have:%d)",
      header.header_size, header.packet_length, length);
  stream->expected_size = header.packet_length;

  if (bs->stream_type == GST_MPEGTS_STREAM_TYPE_PRIVATE_PES_PACKETS) {
    descr =
        mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
        GST_MTS_DESC_DVB_SUBTITLING);
    if (!descr)
      descr =
          mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
          GST_MTS_DESC_DVB_TELETEXT);
    if (!descr)
      descr =
          mpegts_get_descriptor_from_stream ((MpegTSBaseStream *) stream,
          GST_MTS_DESC_ISDB_DATA_COMPONENT);
  }

  if (!descr) {
    if (stream->expected_size) {
      if (G_LIKELY (stream->expected_size > header.header_size)) {
        stream->expected_size -= header.header_size;
      } else {
        /* next packet will have to complete this one */
        GST_WARNING
            ("invalid header and packet size combination, empty packet");
        stream->expected_size = 0;
      }
    }
    data += header.header_size;
    length -= header.header_size;
  }

  /* Create the output buffer */
  if (stream->expected_size)
    stream->allocated_size = MAX (stream->expected_size, length);
  else
    stream->allocated_size = MAX (8192, length);

  g_assert (stream->data == NULL);
  stream->data = g_malloc (stream->allocated_size);
  memcpy (stream->data, data, length);
  stream->current_size = length;

  stream->state = PENDING_PACKET_BUFFER;

  return;

discont:
  stream->state = PENDING_PACKET_DISCONT;
  return;
}

 /* ONLY CALL THIS:
  * * WITH packet->payload != NULL
  * * WITH pending/current flushed out if beginning of new PES packet
  */
static inline void
gst_ts_demux_queue_data (GstTSDemux * demux, TSDemuxStream * stream,
    MpegTSPacketizerPacket * packet)
{
  guint8 *data;
  guint size;

  GST_LOG ("pid: 0x%04x state:%d", stream->stream.pid, stream->state);

  size = packet->data_end - packet->payload;
  data = packet->payload;

  /* FIXME: This is blocked by CJ E&M Live TV and HLS
     if (stream->continuity_counter == CONTINUITY_UNSET) {
     GST_DEBUG ("CONTINUITY: Initialize to %d", cc);
     } else if ((cc == stream->continuity_counter + 1 ||
     (stream->continuity_counter == MAX_CONTINUITY && cc == 0))) {
     GST_LOG ("CONTINUITY: Got expected %d", cc);
     } else {
     GST_WARNING ("CONTINUITY: Mismatch packet %d, stream %d",
     cc, stream->continuity_counter);
     if (stream->state != PENDING_PACKET_EMPTY)
     stream->state = PENDING_PACKET_DISCONT;
     }
     stream->continuity_counter = cc;
   */

  if (stream->state == PENDING_PACKET_EMPTY) {
    if (G_UNLIKELY (!packet->payload_unit_start_indicator)) {
      stream->state = PENDING_PACKET_DISCONT;
      GST_DEBUG ("Didn't get the first packet of this PES");
    } else {
      GST_LOG ("EMPTY=>HEADER");
      stream->state = PENDING_PACKET_HEADER;
      /* geunil.jung. For high speed trick */
      stream->last_scan_offset = 0;
    }
  }

  switch (stream->state) {
    case PENDING_PACKET_HEADER:
    {
      GST_LOG ("HEADER: Parsing PES header");

      /* parse the header */
      gst_ts_demux_parse_pes_header (demux, stream, data, size, packet->offset);
      break;
    }
    case PENDING_PACKET_BUFFER:
    {
      GST_LOG ("BUFFER: appending data");
      if (G_UNLIKELY (stream->current_size + size > stream->allocated_size)) {
        GST_LOG ("resizing buffer");
        do {
          stream->allocated_size *= 2;
        }
        while (stream->current_size + size > stream->allocated_size);
        stream->data = g_realloc (stream->data, stream->allocated_size);
      }
      memcpy (stream->data + stream->current_size, data, size);
      stream->current_size += size;
      break;
    }
    case PENDING_PACKET_DISCONT:
    {
      GST_LOG ("DISCONT: not storing/pushing");
      if (G_UNLIKELY (stream->data)) {
        g_free (stream->data);
        stream->data = NULL;
      }
      stream->continuity_counter = CONTINUITY_UNSET;
      break;
    }
    default:
      break;
  }

  return;
}

static void
calculate_and_push_newsegment (GstTSDemux * demux, TSDemuxStream * stream,
    MpegTSBaseProgram * target_program)
{
  MpegTSBase *base = (MpegTSBase *) demux;
  GstClockTime lowest_pts = GST_CLOCK_TIME_NONE;
  GstClockTime firstts = 0;
  GList *tmp;

  GST_INFO ("Creating new newsegment for stream %p", stream);

  if (target_program == NULL)
    target_program = demux->program;

  /* Speedup : if we don't need to calculate anything, go straight to pushing */
  if (demux->segment_event)
    goto push_new_segment;

  /* Calculate the 'new_start' value, used for newsegment */
  for (tmp = target_program->stream_list; tmp; tmp = tmp->next) {
    TSDemuxStream *pstream = (TSDemuxStream *) tmp->data;

    if (GST_CLOCK_TIME_IS_VALID (pstream->first_pts)) {
      if (!GST_CLOCK_TIME_IS_VALID (lowest_pts)
          || pstream->first_pts < lowest_pts)
        lowest_pts = pstream->first_pts;
    }
  }
  if (GST_CLOCK_TIME_IS_VALID (lowest_pts)) {
    firstts = lowest_pts;
    GST_INFO ("lowest_pts %" G_GUINT64_FORMAT " => clocktime %"
        GST_TIME_FORMAT, lowest_pts, GST_TIME_ARGS (firstts));
  } else {
    /* we could not determine first ts */
    return;
  }

  // For specific contents with high fluctuation bitrate.
  // MFTEVENTFT-48656, MFTEVENTFT-48670, WEBOSLCD13-87932
  if (demux->segment.rate < 0.0 && lowest_pts > demux->segment.stop) {
    demux->segment.stop = firstts;
  }

  /* It will happen only if it's first program or after flushes. */
  GST_DEBUG ("Calculating actual segment");
  if (base->segment.format == GST_FORMAT_TIME) {
    if (base->custom_seek_mode) {
      /* Start from the first ts/pts */
      gst_segment_init (&demux->segment, GST_FORMAT_TIME);
      if (demux->rate > 0) {
        demux->segment.start = firstts;
        demux->segment.stop = GST_CLOCK_TIME_NONE;
      } else {
        demux->segment.start = 0;
        demux->segment.stop = firstts;
      }
      demux->segment.position = firstts;
      demux->segment.time = firstts;
      demux->segment.rate = demux->rate;
      base->custom_seek_mode = FALSE;
    } else {
      /* Try to recover segment info from base if it's in TIME format */
      demux->segment = base->segment;
      if (base->dlna_opval == DLNA_ORG_OP_TIME_RANGE && base->segment.rate < 0) {
        demux->segment.start = 0;
        demux->segment.stop = GST_CLOCK_TIME_NONE;
        demux->segment.position = firstts;
        demux->segment.time = 0;
      } else if (base->segment.start <= firstts && base->real_time) {
        demux->segment.start = firstts;
        demux->segment.position = firstts;
        demux->segment.stop = GST_CLOCK_TIME_NONE;

        demux->segment.time =
            gst_segment_to_stream_time (&base->segment, GST_FORMAT_TIME,
            firstts);
        demux->segment.base =
            gst_segment_to_running_time (&base->segment, GST_FORMAT_TIME,
            firstts);
        demux->segment.format = GST_FORMAT_TIME;
      }
    }
  } else if (demux->segment.rate > 0 && demux->reset_segment) {
    /* Start from the first ts/pts, adding base for accumulation */
    GstClockTime base =
        demux->segment.base + demux->segment.position - demux->segment.start;
    gst_segment_init (&demux->segment, GST_FORMAT_TIME);
    demux->segment.start = firstts;
    demux->segment.stop = GST_CLOCK_TIME_NONE;
    demux->segment.position = firstts;
    demux->segment.time = firstts;
    demux->segment.rate = demux->rate;
    demux->segment.base = base;
  } else if (demux->segment.start < firstts) {
    /* Take into account the offset to the first buffer timestamp */
    if (demux->segment.rate > 0) {
      demux->segment.start = firstts;

      if (GST_CLOCK_TIME_IS_VALID (demux->segment.stop))
        demux->segment.stop += firstts - demux->segment.start;
      demux->segment.position = firstts;
    }
  } else if (demux->segment.rate < 0) {
    demux->segment.stop = firstts;
  } else {
    /* Start from the first ts/pts */
    gst_segment_init (&demux->segment, GST_FORMAT_TIME);
    demux->segment.start = firstts;
    demux->segment.stop = GST_CLOCK_TIME_NONE;
    demux->segment.position = firstts;
    demux->segment.time = firstts;
    demux->segment.rate = demux->rate;
  }

  if (!demux->segment_event) {
    demux->segment_event = gst_event_new_segment (&demux->segment);

    if (base->last_seek_seqnum != GST_SEQNUM_INVALID)
      gst_event_set_seqnum (demux->segment_event, base->last_seek_seqnum);
  }

push_new_segment:
  for (tmp = target_program->stream_list; tmp; tmp = tmp->next) {
    stream = (TSDemuxStream *) tmp->data;
    if (stream->pad == NULL)
      continue;

    if (base->dlna_opval == DLNA_ORG_OP_NONE && base->dlna_flagval == 0x1000) {
      gst_pad_push_event (stream->pad, gst_event_new_flush_start ());
      gst_pad_push_event (stream->pad, gst_event_new_flush_stop (TRUE));
    }

    if (demux->segment_event) {
      GST_DEBUG_OBJECT (stream->pad, "Pushing newsegment event");
      gst_event_ref (demux->segment_event);
      gst_pad_push_event (stream->pad, demux->segment_event);
    }

    if (demux->global_tags) {
      gst_pad_push_event (stream->pad,
          gst_event_new_tag (gst_tag_list_ref (demux->global_tags)));
    }

    /* Push pending tags */
    if (stream->taglist) {
      GST_DEBUG_OBJECT (stream->pad, "Sending tags %" GST_PTR_FORMAT,
          stream->taglist);
      gst_pad_push_event (stream->pad, gst_event_new_tag (stream->taglist));
      stream->taglist = NULL;
    }

    /* Send GAP event to audio path in case of serverside trick mode
     * because audio packet was not delivered by SERVER during trick play.
     * USE-CASE: HikariTV
     * FIXME: Define proper value for starttime and duration of GAP event */
    if (base->serverside_trick
        && g_strrstr (GST_PAD_NAME (stream->pad), "audio"))
      gst_pad_push_event (stream->pad, gst_event_new_gap (0,
              GST_CLOCK_TIME_NONE));

    stream->need_newsegment = FALSE;
  }
  base->serverside_trick = FALSE;
}

static void
gst_ts_demux_check_and_sync_streams (GstTSDemux * demux, GstClockTime time)
{
  GList *tmp;

  GST_DEBUG_OBJECT (demux,
      "Recheck streams and sync to at least: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (time));

  if (G_UNLIKELY (demux->program == NULL))
    return;

  /* Go over each stream and update it to at least 'time' time.
   * For each stream, the pad stores the buffer counter the last time
   * a gap check occurred (gap_ref_buffers) and a gap_ref_pts timestamp
   * that is either the PTS from the stream or the PCR the pad was updated
   * to.
   *
   * We can check nb_out_buffers to see if any buffers were pushed since then.
   * This means we can detect buffers passing without PTSes fine and still generate
   * gaps.
   *
   * If there haven't been any buffers pushed on this stream since the last
   * gap check, push a gap event updating to the indicated input PCR time
   * and update the pad's tracking.
   *
   * If there have been buffers pushed, update the reference buffer count
   * and but don't push a gap event
   */
  for (tmp = demux->program->stream_list; tmp; tmp = tmp->next) {
    TSDemuxStream *ps = (TSDemuxStream *) tmp->data;
    GST_DEBUG_OBJECT (ps->pad,
        "0x%04x, PTS:%" GST_TIME_FORMAT " REFPTS:%" GST_TIME_FORMAT " Gap:%"
        GST_TIME_FORMAT " nb_buffers: %d (ref:%d)",
        ((MpegTSBaseStream *) ps)->pid, GST_TIME_ARGS (ps->pts),
        GST_TIME_ARGS (ps->gap_ref_pts),
        GST_TIME_ARGS (ps->pts - ps->gap_ref_pts), ps->nb_out_buffers,
        ps->gap_ref_buffers);
    if (ps->pad == NULL)
      continue;

    if (ps->nb_out_buffers == ps->gap_ref_buffers && ps->gap_ref_pts != ps->pts) {
      /* Do initial setup of pad if needed - segment etc */
      GST_DEBUG_OBJECT (ps->pad,
          "Stream needs update. Pushing GAP event to TS %"
          GST_TIME_FORMAT, GST_TIME_ARGS (time));
      if (G_UNLIKELY (ps->need_newsegment))
        calculate_and_push_newsegment (demux, ps, NULL);

      /* Now send gap event */
      gst_pad_push_event (ps->pad, gst_event_new_gap (time, 0));
    }

    /* Update GAP tracking vars so we don't re-check this stream for a while */
    ps->gap_ref_pts = time;
    if (ps->pts != GST_CLOCK_TIME_NONE && ps->pts > time)
      ps->gap_ref_pts = ps->pts;
    ps->gap_ref_buffers = ps->nb_out_buffers;
  }
}

static GstBufferList *
parse_opus_access_unit (TSDemuxStream * stream)
{
  GstByteReader reader;
  GstBufferList *buffer_list = NULL;

  buffer_list = gst_buffer_list_new ();
  gst_byte_reader_init (&reader, stream->data, stream->current_size);

  do {
    GstBuffer *buffer;
    guint16 id;
    guint au_size = 0;
    guint8 b;
    gboolean start_trim_flag, end_trim_flag, control_extension_flag;
    guint16 start_trim = 0, end_trim = 0;
    guint8 *packet_data;
    guint packet_size;

    if (!gst_byte_reader_get_uint16_be (&reader, &id))
      goto error;

    /* No control header */
    if ((id >> 5) != 0x3ff)
      goto error;

    do {
      if (!gst_byte_reader_get_uint8 (&reader, &b))
        goto error;
      au_size += b;
    } while (b == 0xff);

    start_trim_flag = (id >> 4) & 0x1;
    end_trim_flag = (id >> 3) & 0x1;
    control_extension_flag = (id >> 2) & 0x1;

    if (start_trim_flag) {
      if (!gst_byte_reader_get_uint16_be (&reader, &start_trim))
        goto error;
    }

    if (end_trim_flag) {
      if (!gst_byte_reader_get_uint16_be (&reader, &end_trim))
        goto error;
    }

    if (control_extension_flag) {
      if (!gst_byte_reader_get_uint8 (&reader, &b))
        goto error;

      if (!gst_byte_reader_skip (&reader, b))
        goto error;
    }

    packet_size = au_size;

    /* FIXME: this should be
     *   packet_size = au_size - gst_byte_reader_get_pos (&reader);
     * but ffmpeg and the only available sample stream from obe.tv
     * are not including the control header size in au_size
     */
    if (gst_byte_reader_get_remaining (&reader) < packet_size)
      goto error;
    if (!gst_byte_reader_dup_data (&reader, packet_size, &packet_data))
      goto error;

    buffer = gst_buffer_new_wrapped (packet_data, packet_size);

    if (start_trim != 0 || end_trim != 0) {
      gst_buffer_add_audio_clipping_meta (buffer, GST_FORMAT_DEFAULT,
          start_trim, end_trim);
    }

    gst_buffer_list_add (buffer_list, buffer);
  } while (gst_byte_reader_get_remaining (&reader) > 0);

  g_free (stream->data);
  stream->data = NULL;
  stream->current_size = 0;

  return buffer_list;

error:
  {
    GST_ERROR ("Failed to parse Opus access unit");
    g_free (stream->data);
    stream->data = NULL;
    stream->current_size = 0;
    if (buffer_list)
      gst_buffer_list_unref (buffer_list);
    return NULL;
  }
}

/* interlaced mode is disabled at the moment */
/*#define TSDEMUX_JP2K_SUPPORT_INTERLACE */
static GstBuffer *
parse_jp2k_access_unit (TSDemuxStream * stream)
{
  GstByteReader reader;
  /* header tag */
  guint32 header_tag;
  /* Framerate box */
  guint16 den G_GNUC_UNUSED;
  guint16 num G_GNUC_UNUSED;
  /* Maximum bitrate box */
  guint32 MaxBr G_GNUC_UNUSED;
  guint32 AUF[2] = { 0, 0 };
#ifdef TSDEMUX_JP2K_SUPPORT_INTERLACE
  /* Field Coding Box */
  guint8 Fic G_GNUC_UNUSED = 1;
  guint8 Fio G_GNUC_UNUSED = 0;
  /* header size equals 38 for non-interlaced, and 48 for interlaced */
  guint header_size = stream->jp2kInfos.interlace ? 48 : 38;
#else
  /* header size equals 38 for non-interlaced, and 48 for interlaced */
  guint header_size = 38;
#endif
  /* Time Code box */
  guint32 HHMMSSFF G_GNUC_UNUSED;
  /* Broadcast color box */
  guint8 CollC G_GNUC_UNUSED;
  guint8 b G_GNUC_UNUSED;

  guint data_location;
  GstBuffer *retbuf = NULL;

  if (stream->current_size < header_size) {
    GST_ERROR_OBJECT (stream->pad, "Not enough data for header");
    goto error;
  }

  gst_byte_reader_init (&reader, stream->data, stream->current_size);

  /* Check for the location of the jp2k magic */
  data_location =
      gst_byte_reader_masked_scan_uint32 (&reader, 0xffffffff, 0xff4fff51, 0,
      stream->current_size);
  GST_DEBUG_OBJECT (stream->pad, "data location %d", data_location);
  if (data_location == -1) {
    GST_ERROR_OBJECT (stream->pad, "Stream does not contain jp2k magic header");
    goto error;
  }

  /* Elementary stream header box 'elsm' == 0x656c736d */
  header_tag = gst_byte_reader_get_uint32_be_unchecked (&reader);
  if (header_tag != 0x656c736d) {
    GST_ERROR_OBJECT (stream->pad, "Expected ELSM box but found box %x instead",
        header_tag);
    goto error;
  }
  /* Frame rate box 'frat' == 0x66726174 */
  header_tag = gst_byte_reader_get_uint32_be_unchecked (&reader);
  if (header_tag != 0x66726174) {
    GST_ERROR_OBJECT (stream->pad,
        "Expected frame rate box, but found box %x instead", header_tag);
    goto error;

  }
  den = gst_byte_reader_get_uint16_be_unchecked (&reader);
  num = gst_byte_reader_get_uint16_be_unchecked (&reader);
  /* Maximum bit rate box 'brat' == 0x62726174 */
  header_tag = gst_byte_reader_get_uint32_be_unchecked (&reader);
  if (header_tag != 0x62726174) {
    GST_ERROR_OBJECT (stream->pad, "Expected brat box but read box %x instead",
        header_tag);
    goto error;

  }
  MaxBr = gst_byte_reader_get_uint32_be_unchecked (&reader);
  AUF[0] = gst_byte_reader_get_uint32_be_unchecked (&reader);
  if (stream->jp2kInfos.interlace) {
#ifdef TSDEMUX_JP2K_SUPPORT_INTERLACE
    AUF[1] = gst_byte_reader_get_uint32_be_unchecked (&reader);
    /*  Field Coding Box 'fiel' == 0x6669656c */
    header_tag = gst_byte_reader_get_uint32_be_unchecked (&reader);
    if (header_tag != 0x6669656c) {
      GST_ERROR_OBJECT (stream->pad,
          "Expected Field Coding box but found box %x instead", header_tag);
      goto error;
    }
    Fic = gst_byte_reader_get_uint8_unchecked (&reader);
    Fio = gst_byte_reader_get_uint8_unchecked (&reader);
#else
    GST_ERROR_OBJECT (stream->pad, "interlaced mode not supported");
    goto error;
#endif
  }

  /* Time Code Box 'tcod' == 0x74636f64 */
  /* Some progressive streams might have a AUF[1] of value 0 present */
  header_tag = gst_byte_reader_get_uint32_be_unchecked (&reader);
  if (header_tag == 0 && !stream->jp2kInfos.interlace) {
    AUF[1] = header_tag;
    header_tag = gst_byte_reader_get_uint32_be_unchecked (&reader);
    /* Bump up header size and recheck */
    header_size += 4;
    if (stream->current_size < header_size) {
      GST_ERROR_OBJECT (stream->pad, "Not enough data for header");
      goto error;
    }
  }
  if (header_tag != 0x74636f64) {
    GST_ERROR_OBJECT (stream->pad,
        "Expected Time code box but found %d box instead", header_tag);
    goto error;
  }
  HHMMSSFF = gst_byte_reader_get_uint32_be_unchecked (&reader);
  /* Broadcast Color Box 'bcol' == 0x6263686c */
  header_tag = gst_byte_reader_get_uint32_be_unchecked (&reader);
  if (header_tag != 0x62636f6c) {
    GST_ERROR_OBJECT (stream->pad,
        "Expected Broadcast color box but found %x box instead", header_tag);
    goto error;
  }
  CollC = gst_byte_reader_get_uint8_unchecked (&reader);
  b = gst_byte_reader_get_uint8_unchecked (&reader);

  /* Check if we have enough data to create a valid buffer */
  if ((stream->current_size - data_location) < (AUF[0] + AUF[1])) {
    GST_ERROR ("Required size (%d) greater than remaining size in buffer (%d)",
        AUF[0] + AUF[1], (stream->current_size - data_location));
    goto error;
  }

  retbuf = gst_buffer_new_wrapped_full (0, stream->data, stream->current_size,
      data_location, stream->current_size - data_location,
      stream->data, g_free);
  stream->data = NULL;
  stream->current_size = 0;
  return retbuf;

error:
  GST_ERROR ("Failed to parse JP2K access unit");
  g_free (stream->data);
  stream->data = NULL;
  stream->current_size = 0;
  return NULL;
}

static GstFlowReturn
gst_ts_demux_push_pending_data (GstTSDemux * demux, TSDemuxStream * stream,
    MpegTSBaseProgram * target_program)
{
  GstFlowReturn res = GST_FLOW_OK;
  MpegTSBaseStream *bs = (MpegTSBaseStream *) stream;
  MpegTSBase *base = (MpegTSBase *) demux;
  HDCPInfo *hdcp_info = &stream->hdcp_info;
  GstBuffer *buffer = NULL;
  GstBufferList *buffer_list = NULL;


  GST_DEBUG_OBJECT (stream->pad,
      "stream:%p, pid:0x%04x stream_type:%d state:%d", stream, bs->pid,
      bs->stream_type, stream->state);

  if (G_UNLIKELY (stream->data == NULL)) {
    GST_LOG ("stream->data == NULL");
    goto beach;
  }

  if (G_UNLIKELY (stream->state == PENDING_PACKET_EMPTY)) {
    GST_LOG ("EMPTY: returning");
    goto beach;
  }

  if (G_UNLIKELY (stream->state != PENDING_PACKET_BUFFER)) {
    GST_LOG ("state:%d, returning", stream->state);
    goto beach;
  }

  if (G_UNLIKELY (demux->program == NULL)) {
    GST_LOG_OBJECT (demux, "No program");
    g_free (stream->data);
    goto beach;
  }

  /* decryption PES payload by HDCP */
  if (hdcp_info->private_data) {
    gst_ts_demux_hdcp_decryption (GUINT64_TO_BE (hdcp_info->input_counter),
        GUINT32_TO_BE (hdcp_info->stream_counter),
        stream->data, stream->current_size, &stream->data);
  }

  if (stream->needs_keyframe) {
    MpegTSBase *base = (MpegTSBase *) demux;

    if ((gst_ts_demux_adjust_seek_offset_for_keyframe (stream, stream->data,
                stream->current_size)) || demux->last_seek_offset == 0) {
      GST_DEBUG_OBJECT (stream->pad,
          "Got Keyframe, ready to go at %" GST_TIME_FORMAT,
          GST_TIME_ARGS (stream->pts));

      if (bs->stream_type == GST_MPEGTS_STREAM_TYPE_PRIVATE_PES_PACKETS &&
          bs->registration_id == DRF_ID_OPUS) {
        buffer_list = parse_opus_access_unit (stream);
        if (!buffer_list) {
          res = GST_FLOW_ERROR;
          goto beach;
        }

        if (gst_buffer_list_length (buffer_list) == 1) {
          buffer = gst_buffer_ref (gst_buffer_list_get (buffer_list, 0));
          gst_buffer_list_unref (buffer_list);
          buffer_list = NULL;
        }
      } else if (bs->stream_type == GST_MPEGTS_STREAM_TYPE_VIDEO_JP2K) {
        buffer = parse_jp2k_access_unit (stream);
        if (!buffer) {
          res = GST_FLOW_ERROR;
          goto beach;
        }
      } else {
        buffer = gst_buffer_new_wrapped (stream->data, stream->current_size);
      }

      stream->seeked_pts = stream->pts;
      stream->seeked_dts = stream->dts;
      stream->needs_keyframe = FALSE;
    } else {
      base->seek_offset = demux->last_seek_offset - 200 * base->packetsize;
      if (demux->last_seek_offset < 200 * base->packetsize)
        base->seek_offset = 0;
      demux->last_seek_offset = base->seek_offset;
      mpegts_packetizer_flush (base->packetizer, FALSE);
      base->mode = BASE_MODE_SEEKING;

      stream->continuity_counter = CONTINUITY_UNSET;
      res = GST_FLOW_REWINDING;
      g_free (stream->data);
      goto beach;
    }
  } else {
    if (bs->stream_type == GST_MPEGTS_STREAM_TYPE_PRIVATE_PES_PACKETS &&
        bs->registration_id == DRF_ID_OPUS) {
      buffer_list = parse_opus_access_unit (stream);
      if (!buffer_list) {
        res = GST_FLOW_ERROR;
        goto beach;
      }

      if (gst_buffer_list_length (buffer_list) == 1) {
        buffer = gst_buffer_ref (gst_buffer_list_get (buffer_list, 0));
        gst_buffer_list_unref (buffer_list);
        buffer_list = NULL;
      }
    } else if (bs->stream_type == GST_MPEGTS_STREAM_TYPE_VIDEO_JP2K) {
      buffer = parse_jp2k_access_unit (stream);
      if (!buffer) {
        res = GST_FLOW_ERROR;
        goto beach;
      }
    } else {
      buffer = gst_buffer_new_wrapped (stream->data, stream->current_size);
    }

    if (G_UNLIKELY (stream->pending_ts && !check_pending_buffers (demux))) {
      if (buffer) {
        PendingBuffer *pend;
        pend = g_slice_new0 (PendingBuffer);
        pend->buffer = buffer;
        pend->pts = stream->raw_pts;
        pend->dts = stream->raw_dts;
        stream->pending = g_list_append (stream->pending, pend);
      } else {
        guint i, n;

        n = gst_buffer_list_length (buffer_list);
        for (i = 0; i < n; i++) {
          PendingBuffer *pend;
          pend = g_slice_new0 (PendingBuffer);
          pend->buffer = gst_buffer_ref (gst_buffer_list_get (buffer_list, i));
          pend->pts = i == 0 ? stream->raw_pts : -1;
          pend->dts = i == 0 ? stream->raw_dts : -1;
          stream->pending = g_list_append (stream->pending, pend);
        }
        gst_buffer_list_unref (buffer_list);
      }
      GST_DEBUG ("Not enough information to push buffers yet, storing buffer");
      goto beach;
    }
  }

  /*DLNA Forward Stalling */
  if (G_UNLIKELY (stream->need_newsegment))
    calculate_and_push_newsegment (demux, stream, target_program);

  if (G_UNLIKELY (stream->need_newsegment) && buffer) {
    /* Pusing Buffer before SEGMENT event does not make sense,
     * If we still need newsegment, push this buffer to pending list */
    PendingBuffer *pend;
    pend = g_slice_new0 (PendingBuffer);
    pend->buffer = buffer;
    pend->pts = stream->raw_pts;
    pend->dts = stream->raw_dts;
    stream->pending = g_list_append (stream->pending, pend);

    GST_DEBUG_OBJECT (stream->pad,
        "Still need new segment, keeping buffer PTS:%" GST_TIME_FORMAT " DTS:%"
        GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_PTS (pend->buffer)),
        GST_TIME_ARGS (GST_BUFFER_DTS (pend->buffer)));

    goto beach;
  }

  /* FIXME : Push pending buffers if any */
  if (G_UNLIKELY (stream->pending)) {
    GList *tmp;
    for (tmp = stream->pending; tmp; tmp = tmp->next) {
      PendingBuffer *pend = (PendingBuffer *) tmp->data;

      GST_DEBUG_OBJECT (stream->pad,
          "Pushing pending buffer PTS:%" GST_TIME_FORMAT " DTS:%"
          GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_PTS (pend->buffer)),
          GST_TIME_ARGS (GST_BUFFER_DTS (pend->buffer)));

      if (stream->discont)
        GST_BUFFER_FLAG_SET (pend->buffer, GST_BUFFER_FLAG_DISCONT);
      stream->discont = FALSE;

      res = gst_pad_push (stream->pad, pend->buffer);
      stream->nb_out_buffers += 1;
      g_slice_free (PendingBuffer, pend);
    }
    g_list_free (stream->pending);
    stream->pending = NULL;
  }

  if ((GST_CLOCK_TIME_IS_VALID (stream->seeked_pts)
          && stream->pts < stream->seeked_pts) ||
      (GST_CLOCK_TIME_IS_VALID (stream->seeked_dts) &&
          stream->pts < stream->seeked_dts)) {
    GST_INFO_OBJECT (stream->pad,
        "Droping with PTS: %" GST_TIME_FORMAT " DTS: %" GST_TIME_FORMAT
        " after seeking as other stream needed to be seeked further"
        "(seeked PTS: %" GST_TIME_FORMAT " DTS: %" GST_TIME_FORMAT ")",
        GST_TIME_ARGS (stream->pts), GST_TIME_ARGS (stream->dts),
        GST_TIME_ARGS (stream->seeked_pts), GST_TIME_ARGS (stream->seeked_dts));
    if (buffer)
      gst_buffer_unref (buffer);
    if (buffer_list)
      gst_buffer_list_unref (buffer_list);
    goto beach;
  }

  GST_DEBUG_OBJECT (stream->pad, "stream->pts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (stream->pts));

  /* Decorate buffer or first buffer of the buffer list */
  if (buffer_list)
    buffer = gst_buffer_list_get (buffer_list, 0);

  if (GST_CLOCK_TIME_IS_VALID (stream->pts))
    GST_BUFFER_PTS (buffer) = stream->pts;
  if (GST_CLOCK_TIME_IS_VALID (stream->dts))
    GST_BUFFER_DTS (buffer) = stream->dts;

  /* Set valid DTS */
  if (GST_CLOCK_TIME_IS_VALID (stream->pts) &&
      !GST_CLOCK_TIME_IS_VALID (stream->dts)) {
    GST_BUFFER_DTS (buffer) = stream->pts;
  }

  if (stream->discont)
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
  stream->discont = FALSE;

  if (buffer_list)
    buffer = NULL;

  GST_DEBUG_OBJECT (stream->pad,
      "Pushing buffer%s with PTS: %" GST_TIME_FORMAT " , DTS: %"
      GST_TIME_FORMAT, (buffer_list ? "list" : ""), GST_TIME_ARGS (stream->pts),
      GST_TIME_ARGS (stream->dts));

  if (GST_CLOCK_TIME_IS_VALID (stream->dts))
    demux->segment.position = stream->dts;
  else if (GST_CLOCK_TIME_IS_VALID (stream->pts))
    demux->segment.position = stream->pts;

  /* for DLNA time mode rewind */
  if (base->dlna_opval == DLNA_ORG_OP_TIME_RANGE && base->segment.rate < 0
      && base->is_iframe_in_cur_pes) {

    if (stream->discont)
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
    stream->discont = FALSE;

    GST_INFO_OBJECT (stream->pad, "Pushing buffer with PTS: %" GST_TIME_FORMAT
        " , DTS: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_PTS (buffer)),
        GST_TIME_ARGS (GST_BUFFER_DTS (buffer)));
    if (buffer) {
      if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DTS (buffer)))
        demux->segment.position = GST_BUFFER_DTS (buffer);
      else if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_PTS (buffer)))
        demux->segment.position = GST_BUFFER_PTS (buffer);

      res = gst_pad_push (stream->pad, buffer);
      /* Record that a buffer was pushed */
      stream->nb_out_buffers += 1;
#ifdef DUMP_TS
      if (dumpFp) {
        size_t written =
            fwrite (stream->data, sizeof (guint8), stream->current_size,
            dumpFp);
        if (written != stream->current_size)
          printf
              ("\n\n#######################################DUMP_TS ERROR : cannot write file \n\n");
      }
#endif
    } else {
      guint n = gst_buffer_list_length (buffer_list);
      res = gst_pad_push_list (stream->pad, buffer_list);
      /* Record that a buffer was pushed */
      stream->nb_out_buffers += n;
    }
    GST_INFO_OBJECT (stream->pad, "Returned %s", gst_flow_get_name (res));
  } else if ((base->dlna_opval == DLNA_ORG_OP_TIME_RANGE
          || base->dlna_opval == DLNA_ORG_OP_BOTH_RANGE)
      && (base->segment.rate == 2)
      && (base->dlna_duration != -1)
      && (GST_BUFFER_PTS (buffer) > base->dlna_duration)) {
    /* for DLNA time based seek device (ARIB) */
    GST_INFO_OBJECT (stream->pad,
        "Drop: dlna_duration: %" GST_TIME_FORMAT ", buffer_pts: %"
        GST_TIME_FORMAT, GST_TIME_ARGS (base->dlna_duration),
        GST_TIME_ARGS (GST_BUFFER_PTS (buffer)));
    GST_INFO_OBJECT (stream->pad,
        "Dropping frame because the timestamp is over the duration");
    gst_buffer_unref (buffer);
    res = GST_FLOW_OK;
  } else {
    if (!base->real_time && stream->need_newsegment && base->segment.rate > 0) {
      GST_INFO_OBJECT (stream->pad,
          "Dropping frame prior to new segment event");
      gst_buffer_unref (buffer);
      res = GST_FLOW_OK;
    } else {
      if (stream->discont)
        GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
      stream->discont = FALSE;

      GST_INFO_OBJECT (stream->pad,
          "Pushing buffer with PTS: %" GST_TIME_FORMAT " , DTS: %"
          GST_TIME_FORMAT " , DISCONT_flag: %d",
          GST_TIME_ARGS (GST_BUFFER_PTS (buffer)),
          GST_TIME_ARGS (GST_BUFFER_DTS (buffer)),
          GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT));

      if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DTS (buffer)))
        demux->segment.position = GST_BUFFER_DTS (buffer);
      else if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_PTS (buffer)))
        demux->segment.position = GST_BUFFER_PTS (buffer);

      res = gst_pad_push (stream->pad, buffer);
      /* Record that a buffer was pushed */
      stream->nb_out_buffers += 1;

#ifdef DUMP_TS
      if (dumpFp) {
        size_t written =
            fwrite (stream->data, sizeof (guint8), stream->current_size,
            dumpFp);
        if (written != stream->current_size)
          printf
              ("\n\n#######################################DUMP_TS ERROR : cannot write file \n\n");
      }
#endif
      GST_INFO_OBJECT (stream->pad, "Returned %s", gst_flow_get_name (res));
    }
  }
  res = gst_flow_combiner_update_flow (demux->flowcombiner, res);
  GST_INFO_OBJECT (stream->pad, "combined %s", gst_flow_get_name (res));

  /* GAP / sparse stream tracking */
  if (G_UNLIKELY (stream->gap_ref_pts == GST_CLOCK_TIME_NONE))
    stream->gap_ref_pts = stream->pts;
  else {
    /* Look if the stream PTS has advanced 2 seconds since the last
     * gap check, and sync streams if it has. The first stream to
     * hit this will trigger a gap check */
    if (G_UNLIKELY (stream->pts != GST_CLOCK_TIME_NONE &&
            stream->pts > stream->gap_ref_pts + 2 * GST_SECOND)) {
      if (demux->program->pcr_pid != 0x1fff) {
        GstClockTime curpcr =
            mpegts_packetizer_get_current_time (MPEG_TS_BASE_PACKETIZER (demux),
            demux->program->pcr_pid);
        if (curpcr == GST_CLOCK_TIME_NONE || curpcr < 800 * GST_MSECOND)
          goto beach;
        curpcr -= 800 * GST_MSECOND;
        /* Use the current PCR (with a safety margin) to sync against */
        gst_ts_demux_check_and_sync_streams (demux, curpcr);
      } else {
        /* If we don't have a PCR track, just use the current stream PTS */
        if (base->real_time)
          goto beach;
        gst_ts_demux_check_and_sync_streams (demux, stream->pts);
      }
    }
  }

beach:
  /* Reset everything */
  GST_LOG ("Resetting to EMPTY, returning %s", gst_flow_get_name (res));
  stream->state = PENDING_PACKET_EMPTY;
  stream->data = NULL;
  stream->expected_size = 0;
  stream->current_size = 0;

  /* geunil.jung. For high speed trick */
  stream->last_scan_offset = 0;
  stream->frame_scan_done = FALSE;
  stream->is_iframe = FALSE;
  stream->is_first_iframe_in_interlace = FALSE;

  return res;
}

/* geunil.jung. For high speed trick */
static guint
scan_for_start_code_prefix (TSDemuxStream * stream, guint offset)
{
  const guint8 *data;

  data = stream->data;
#if 1
  while (offset < stream->current_size - 2) {
    if (data[offset] == 0x00 && data[offset + 1] == 0x00
        && data[offset + 2] == 0x01) {
      /* ok..now we search SCP */
      break;
    }
    offset++;
  }

#else
  while (offset <= (stream->current_size - 4)) {
    if (data[offset + 2] > 1) {
      offset += 3;
    } else if (data[offset + 1]) {
      offset += 2;
    } else if (data[offset] || data[offset + 2] != 1) {
      offset++;
    } else {
      break;
    }
  }
#endif
  return offset;
}

/* geunil.jung. For high speed trick */
static void
gst_ts_demux_parse_mpeg2_video (GstTSDemux * demux, TSDemuxStream * stream)
{
  MpegTSBase *base = (MpegTSBase *) demux;
  GstCaps *src_caps, *caps;
  GstNalParser buf;
  guint offset = 0;
  guint8 start_code = -1;
  guint8 picture_coding_type = -1;
  guint16 width, height;

  if (stream->current_size < stream->last_scan_offset + 6)
    /* need more data */
    return;

  do {
    offset = scan_for_start_code_prefix (stream, stream->last_scan_offset);
    stream->last_scan_offset = offset;

    if (offset < stream->current_size - 5) {
      start_code = GST_READ_UINT8 (stream->data + offset + 3);
      stream->last_scan_offset += 3;

      if (start_code == 0x00) { /* picture header */
        picture_coding_type = GST_READ_UINT8 (stream->data + offset + 5) & 0x38;

        if (picture_coding_type == 0x08) {      /* I-frame */
          base->is_iframe_in_cur_pes = TRUE;
          stream->is_iframe = TRUE;
        } else {
          base->is_iframe_in_cur_pes = FALSE;
          stream->is_iframe = FALSE;
        }

        stream->frame_scan_done = TRUE;
        break;
      }
      if (start_code == 0xB3 && !stream->is_update_video_caps) {
        gst_nal_parser_init (&buf, stream->data + offset + 4, 24);
        READ_UINT16 (&buf, width, 12);
        READ_UINT16 (&buf, height, 12);
        src_caps = gst_pad_query_caps (stream->pad, NULL);
        if (src_caps) {
          caps = gst_caps_copy (src_caps);
          gst_caps_set_simple (caps, "width", G_TYPE_INT, width,
              "height", G_TYPE_INT, height, NULL);
          stream->is_update_video_caps = TRUE;
          gst_pad_set_caps (stream->pad, caps);
          gst_caps_unref (src_caps);
          gst_caps_unref (caps);
        }

      }
    } else
      /* we need more data */
      break;

  } while (stream->current_size > stream->last_scan_offset + 2);
  return;
error:
  GST_DEBUG ("sequence header unit parsing error");
}

static void
gst_ts_demux_parse_mpeg4_video (GstTSDemux * demux, TSDemuxStream * stream)
{
  MpegTSBase *base = (MpegTSBase *) demux;
  guint offset = 0;
  guint8 start_code = -1;
  guint8 vop_coding_type = -1;

  if (stream->current_size < stream->last_scan_offset + 5)
    /* need more data */
    return;

  do {
    offset = scan_for_start_code_prefix (stream, stream->last_scan_offset);
    stream->last_scan_offset = offset;

    if (offset < stream->current_size - 4) {
      start_code = GST_READ_UINT8 (stream->data + offset + 3);
      stream->last_scan_offset += 3;

      if (start_code == 0xB6) { /* VideoObjectPlane start code */
        vop_coding_type = GST_READ_UINT8 (stream->data + offset + 4) & 0xC0;

        if (vop_coding_type == 0x00) {  /* Intra-coded(I) */
          base->is_iframe_in_cur_pes = TRUE;
          stream->is_iframe = TRUE;
        } else {
          base->is_iframe_in_cur_pes = FALSE;
          stream->is_iframe = FALSE;
        }

        stream->frame_scan_done = TRUE;
        break;
      }
    } else
      /* we need more data */
      break;

  } while (stream->current_size > stream->last_scan_offset + 2);
}

/*arun.s for calculating height,width from sps for h265*/
static gboolean
gst_ts_demux_parse_h265_sps (TSDemuxStream * stream, guint offset,
    guint16 * wid, guint16 * ht)
{
  GstNalParser buf;
  H265ProfileTierLevel ptl;
  guint i, j;
  guint8 maxNumSubLayersMinus1, vps_id;
  guint8 temporal_id_nesting_flag, separate_colour_plane_flag;
  guint16 width, height;
  guint32 G_GNUC_UNUSED id = 0, chroma_format_idc = 0;

  gst_nal_parser_init (&buf, stream->data + offset, stream->current_size - 1);
  memset (&ptl, 0, sizeof (ptl));

  READ_UINT8 (&buf, vps_id, 4);
  READ_UINT8 (&buf, maxNumSubLayersMinus1, 3);
  READ_UINT8 (&buf, temporal_id_nesting_flag, 1);
  GST_DEBUG ("parsing \"ProfileTierLevel parameters\"");

  for (i = 0; i < maxNumSubLayersMinus1; i++)
    ptl.sub_layer_tier_flag[i] = 0;

  READ_UINT8 (&buf, ptl.profile_space, 2);
  READ_UINT8 (&buf, ptl.tier_flag, 1);
  READ_UINT8 (&buf, ptl.profile_idc, 5);

  for (j = 0; j < 32; j++)
    READ_UINT8 (&buf, ptl.profile_compatibility_flag[j], 1);

  READ_UINT8 (&buf, ptl.progressive_source_flag, 1);
  READ_UINT8 (&buf, ptl.interlaced_source_flag, 1);
  READ_UINT8 (&buf, ptl.non_packed_constraint_flag, 1);
  READ_UINT8 (&buf, ptl.frame_only_constraint_flag, 1);

  /* skip the reserved zero bits */
  if (!gst_nal_parser_skip (&buf, 44))
    goto error;

  READ_UINT8 (&buf, ptl.level_idc, 8);
  for (j = 0; j < maxNumSubLayersMinus1; j++) {
    READ_UINT8 (&buf, ptl.sub_layer_profile_present_flag[j], 1);
    READ_UINT8 (&buf, ptl.sub_layer_level_present_flag[j], 1);
  }

  if (maxNumSubLayersMinus1 > 0) {
    for (i = maxNumSubLayersMinus1; i < 8; i++)
      if (!gst_nal_parser_skip (&buf, 2))
        goto error;
  }

  for (i = 0; i < maxNumSubLayersMinus1; i++) {
    if (ptl.sub_layer_profile_present_flag[i]) {
      READ_UINT8 (&buf, ptl.sub_layer_profile_space[i], 2);
      READ_UINT8 (&buf, ptl.sub_layer_tier_flag[i], 1);
      READ_UINT8 (&buf, ptl.sub_layer_profile_idc[i], 5);

      for (j = 0; j < 32; j++)
        READ_UINT8 (&buf, ptl.sub_layer_profile_compatibility_flag[i][j], 1);

      READ_UINT8 (&buf, ptl.sub_layer_progressive_source_flag[i], 1);
      READ_UINT8 (&buf, ptl.sub_layer_interlaced_source_flag[i], 1);
      READ_UINT8 (&buf, ptl.sub_layer_non_packed_constraint_flag[i], 1);
      READ_UINT8 (&buf, ptl.sub_layer_frame_only_constraint_flag[i], 1);

      if (!gst_nal_parser_skip (&buf, 44))
        goto error;
    }

    if (ptl.sub_layer_level_present_flag[i])
      READ_UINT8 (&buf, ptl.sub_layer_level_idc[i], 8);
  }

  READ_UE_ALLOWED (&buf, id, 0, 16 - 1);

  READ_UE_ALLOWED (&buf, chroma_format_idc, 0, 3);
  if (chroma_format_idc == 3)
    READ_UINT8 (&buf, separate_colour_plane_flag, 1);

  READ_UE_ALLOWED (&buf, width, 1, 16888);
  READ_UE_ALLOWED (&buf, height, 1, 16888);

  *wid = width;
  *ht = height;

  GST_INFO ("width = %d,height = %d", width, height);

  return TRUE;
error:
  GST_WARNING ("SPS parsing error");
  return FALSE;

}

/*arun.s for calculating height,width from sps for h265*/
static void
gst_ts_demux_parse_h265_video (GstTSDemux * demux, TSDemuxStream * stream)
{
  MpegTSBase *base = (MpegTSBase *) demux;
  GstCaps *src_caps, *caps;
  GstNalParser buf;
  guint offset = 0;
  guint offset_next = 0;
  guint offset_prev = 0;
  gboolean need_more_sps_data = FALSE;
  guint8 nal_unit_type = -1;
  guint8 tmp;
  guint16 width, height;
  GST_INFO (" gst_ts_demux_parse_h265_video");

  if (stream->current_size < stream->last_scan_offset + 4)
    /* need more data */
    return;

  do {
    offset = scan_for_start_code_prefix (stream, stream->last_scan_offset);
    stream->last_scan_offset = offset;

    if (offset < stream->current_size - 3) {
      gst_nal_parser_init (&buf, stream->data + offset + 3,
          stream->current_size - 1);
      offset_prev = offset;
      stream->last_scan_offset += 3;
      READ_UINT8 (&buf, tmp, 1);
      READ_UINT8 (&buf, nal_unit_type, 6);
      READ_UINT8 (&buf, tmp, 6);
      READ_UINT8 (&buf, tmp, 3);
      GST_INFO ("nal_unit_type = %d", nal_unit_type);

      if (nal_unit_type == 33 && !stream->is_update_video_caps) {
        stream->is_iframe = FALSE;
        offset_next =
            scan_for_start_code_prefix (stream, stream->last_scan_offset);
        if (offset_next != 0 && (stream->data[offset_next] == 0x00
                && stream->data[offset_next + 1] == 0x00
                && stream->data[offset_next + 2] == 0x01)) {
          GST_INFO
              ("Found SPS NAL!!! CUR_OFFSET: [%u], NEXT_OFFSET: [%u], current_size: [%u]",
              offset, offset_next, stream->current_size);
          if (gst_ts_demux_parse_h265_sps (stream, offset + 5, &width, &height)) {
            GST_INFO ("stream->pic_width =  %d,stream->pic_height = %d", width,
                height);
            if (width > 0 && height > 0) {
              if ((width / 16 * height / 16) > 8704)
                base->is_higher_than_FHD = TRUE;
              else
                base->is_higher_than_FHD = FALSE;

              src_caps = gst_pad_query_caps (stream->pad, NULL);
              if (src_caps) {
                caps = gst_caps_copy (src_caps);
                gst_caps_set_simple (caps, "width", G_TYPE_INT, width,
                    "height", G_TYPE_INT, height, NULL);
                stream->is_update_video_caps = TRUE;
                gst_pad_set_caps (stream->pad, caps);
                gst_caps_unref (src_caps);
                gst_caps_unref (caps);
              }
            }
          }
        } else {
          GST_INFO
              ("need more SPS data. last_scan: %d, offset_next: %d, size: %d",
              stream->last_scan_offset, offset_next, stream->current_size);
          need_more_sps_data = TRUE;
        }
      } else if (nal_unit_type >= 16 && nal_unit_type <= 21) {
        /* IDR picture */
        GST_DEBUG ("IDR is founded!");
        base->is_iframe_in_cur_pes = TRUE;
        stream->is_iframe = TRUE;
        stream->frame_scan_done = TRUE;
        break;
      } else {
        base->is_iframe_in_cur_pes = FALSE;
        stream->is_iframe = FALSE;
      }
    } else
      /* need more data */
      break;

  } while (stream->current_size > stream->last_scan_offset + 2);

  if (need_more_sps_data) {
    stream->last_scan_offset = offset_prev;
    GST_LOG ("Start parsing with last_scan_offset: %d",
        stream->last_scan_offset);
  }


  return;
error:
  GST_DEBUG ("SPS nal unit parsing error");

}

static gboolean
gst_ts_demux_parse_h264_parse_scaling_list (GstNalParser * reader,
    guint8 scaling_lists_4x4[6][16], guint8 scaling_lists_8x8[6][64],
    const guint8 fallback_4x4_inter[16], const guint8 fallback_4x4_intra[16],
    const guint8 fallback_8x8_inter[64], const guint8 fallback_8x8_intra[64],
    guint8 n_lists)
{
  guint i;

  GST_DEBUG ("parsing scaling lists");

  for (i = 0; i < 12; i++) {
    gboolean use_default = FALSE;

    if (i < n_lists) {
      guint8 scaling_list_present_flag;

      READ_UINT8 (reader, scaling_list_present_flag, 1);
      if (scaling_list_present_flag) {
        guint8 *scaling_list;
        const guint8 *scan;
        guint size;
        guint j;
        guint8 last_scale, next_scale;

        if (i < 6) {
          scaling_list = scaling_lists_4x4[i];
          scan = zigzag_4x4;
          size = 16;
        } else {
          scaling_list = scaling_lists_8x8[i - 6];
          scan = zigzag_8x8;
          size = 64;
        }

        last_scale = 8;
        next_scale = 8;
        for (j = 0; j < size; j++) {
          if (next_scale != 0) {
            gint32 delta_scale;

            READ_SE (reader, delta_scale);
            next_scale = (last_scale + delta_scale) & 0xff;
          }
          if (j == 0 && next_scale == 0) {
            use_default = TRUE;
            break;
          }
          last_scale = scaling_list[scan[j]] =
              (next_scale == 0) ? last_scale : next_scale;
        }
      } else
        use_default = TRUE;
    } else
      use_default = TRUE;

    if (use_default) {
      switch (i) {
        case 0:
          memcpy (scaling_lists_4x4[0], fallback_4x4_intra, 16);
          break;
        case 1:
          memcpy (scaling_lists_4x4[1], scaling_lists_4x4[0], 16);
          break;
        case 2:
          memcpy (scaling_lists_4x4[2], scaling_lists_4x4[1], 16);
          break;
        case 3:
          memcpy (scaling_lists_4x4[3], fallback_4x4_inter, 16);
          break;
        case 4:
          memcpy (scaling_lists_4x4[4], scaling_lists_4x4[3], 16);
          break;
        case 5:
          memcpy (scaling_lists_4x4[5], scaling_lists_4x4[4], 16);
          break;
        case 6:
          memcpy (scaling_lists_8x8[0], fallback_8x8_intra, 64);
          break;
        case 7:
          memcpy (scaling_lists_8x8[1], fallback_8x8_inter, 64);
          break;
        case 8:
          memcpy (scaling_lists_8x8[2], scaling_lists_8x8[0], 64);
          break;
        case 9:
          memcpy (scaling_lists_8x8[3], scaling_lists_8x8[1], 64);
          break;
        case 10:
          memcpy (scaling_lists_8x8[4], scaling_lists_8x8[2], 64);
          break;
        case 11:
          memcpy (scaling_lists_8x8[5], scaling_lists_8x8[3], 64);
          break;

        default:
          break;
      }
    }
  }

  return TRUE;

error:

  GST_WARNING ("error parsing scaling lists");
  return FALSE;
}

static gboolean
gst_ts_demux_parse_h264_parse_vui_parameters (H264SPS * sps, GstNalParser * nr)
{
  H264VUIParams *vui = &sps->vui_parameters;

  GST_DEBUG ("parsing \"VUI Parameters\"");

  /* set default values for fields that might not be present in the bitstream
     and have valid defaults */
  vui->aspect_ratio_idc = 0;
  vui->video_format = 5;
  vui->video_full_range_flag = 0;
  vui->colour_primaries = 2;
  vui->transfer_characteristics = 2;
  vui->matrix_coefficients = 2;
  vui->chroma_sample_loc_type_top_field = 0;
  vui->chroma_sample_loc_type_bottom_field = 0;
  vui->par_n = 0;
  vui->par_d = 0;

  READ_UINT8 (nr, vui->aspect_ratio_info_present_flag, 1);
  if (vui->aspect_ratio_info_present_flag) {
    READ_UINT8 (nr, vui->aspect_ratio_idc, 8);
    if (vui->aspect_ratio_idc == EXTENDED_SAR) {
      READ_UINT16 (nr, vui->sar_width, 16);
      READ_UINT16 (nr, vui->sar_height, 16);
      vui->par_n = vui->sar_width;
      vui->par_d = vui->sar_height;
    } else if (vui->aspect_ratio_idc <= 16) {
      vui->par_n = aspect_ratios[vui->aspect_ratio_idc].par_n;
      vui->par_d = aspect_ratios[vui->aspect_ratio_idc].par_d;
    }
  }

  READ_UINT8 (nr, vui->overscan_info_present_flag, 1);
  if (vui->overscan_info_present_flag)
    READ_UINT8 (nr, vui->overscan_appropriate_flag, 1);

  READ_UINT8 (nr, vui->video_signal_type_present_flag, 1);
  if (vui->video_signal_type_present_flag) {

    READ_UINT8 (nr, vui->video_format, 3);
    READ_UINT8 (nr, vui->video_full_range_flag, 1);
    READ_UINT8 (nr, vui->colour_description_present_flag, 1);
    if (vui->colour_description_present_flag) {
      READ_UINT8 (nr, vui->colour_primaries, 8);
      READ_UINT8 (nr, vui->transfer_characteristics, 8);
      READ_UINT8 (nr, vui->matrix_coefficients, 8);
    }
  }

  READ_UINT8 (nr, vui->chroma_loc_info_present_flag, 1);
  if (vui->chroma_loc_info_present_flag) {
    READ_UE_ALLOWED (nr, vui->chroma_sample_loc_type_top_field, 0, 5);
    READ_UE_ALLOWED (nr, vui->chroma_sample_loc_type_bottom_field, 0, 5);
  }

  READ_UINT8 (nr, vui->timing_info_present_flag, 1);
  if (vui->timing_info_present_flag) {
    READ_UINT32 (nr, vui->num_units_in_tick, 32);
    if (vui->num_units_in_tick == 0)
      GST_WARNING ("num_units_in_tick = 0 detected in stream "
          "(incompliant to H.264 E.2.1).");

    READ_UINT32 (nr, vui->time_scale, 32);
    if (vui->time_scale == 0)
      GST_WARNING ("time_scale = 0 detected in stream "
          "(incompliant to H.264 E.2.1).");

    READ_UINT8 (nr, vui->fixed_frame_rate_flag, 1);
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"VUI Parameters\"");
  return FALSE;
}

/*arun.s for calculating height,width from sps*/
static gboolean
gst_ts_demux_parse_h264_sps (TSDemuxStream * stream, guint offset,
    guint16 * wid, guint16 * ht, gint * num, gint * den)
{
  GstNalParser buf;
  H264SPS sps;
  guint8 frame_cropping_flag;
  gint width, height;
  guint subwc[] = { 1, 2, 2, 1 };
  guint subhc[] = { 1, 2, 1, 1 };
  H264VUIParams *vui = NULL;


  GST_DEBUG ("parsing SPS");
  gst_nal_parser_init (&buf, stream->data + offset, stream->current_size - 1);
  sps.chroma_format_idc = 1;
  sps.separate_colour_plane_flag = 0;
  sps.bit_depth_luma_minus8 = 0;
  sps.bit_depth_chroma_minus8 = 0;
  sps.mb_adaptive_frame_field_flag = 0;
  sps.frame_crop_left_offset = 0;
  sps.frame_crop_right_offset = 0;
  sps.frame_crop_top_offset = 0;
  sps.frame_crop_bottom_offset = 0;
  sps.delta_pic_order_always_zero_flag = 0;
  memset (sps.scaling_lists_4x4, 16, 96);
  memset (sps.scaling_lists_8x8, 16, 384);
  READ_UINT8 (&buf, sps.profile_idc, 8);
  READ_UINT8 (&buf, sps.constraint_set0_flag, 1);
  READ_UINT8 (&buf, sps.constraint_set1_flag, 1);
  READ_UINT8 (&buf, sps.constraint_set2_flag, 1);
  READ_UINT8 (&buf, sps.constraint_set3_flag, 1);

  /* skip reserved_zero_4bits */
  if (!gst_nal_parser_skip (&buf, 4))
    goto error;

  READ_UINT8 (&buf, sps.level_idc, 8);

  READ_UE_ALLOWED (&buf, sps.id, 0, 32 - 1);

  if (sps.profile_idc == 100 || sps.profile_idc == 110 ||
      sps.profile_idc == 122 || sps.profile_idc == 244 ||
      sps.profile_idc == 44 || sps.profile_idc == 83 || sps.profile_idc == 86) {
    READ_UE_ALLOWED (&buf, sps.chroma_format_idc, 0, 3);
    if (sps.chroma_format_idc == 3)
      READ_UINT8 (&buf, sps.separate_colour_plane_flag, 1);

    READ_UE_ALLOWED (&buf, sps.bit_depth_luma_minus8, 0, 6);
    READ_UE_ALLOWED (&buf, sps.bit_depth_chroma_minus8, 0, 6);
    READ_UINT8 (&buf, sps.qpprime_y_zero_transform_bypass_flag, 1);

    READ_UINT8 (&buf, sps.scaling_matrix_present_flag, 1);
    if (sps.scaling_matrix_present_flag) {
      guint8 n_lists;
      n_lists = (sps.chroma_format_idc != 3) ? 8 : 12;
      if (!gst_ts_demux_parse_h264_parse_scaling_list (&buf,
              sps.scaling_lists_4x4, sps.scaling_lists_8x8,
              default_4x4_inter, default_4x4_intra,
              default_8x8_inter, default_8x8_intra, n_lists))
        goto error;
    }
  }

  READ_UE_ALLOWED (&buf, sps.log2_max_frame_num_minus4, 0, 12);
  sps.max_frame_num = 1 << (sps.log2_max_frame_num_minus4 + 4);

  READ_UE_ALLOWED (&buf, sps.pic_order_cnt_type, 0, 2);
  if (sps.pic_order_cnt_type == 0) {
    READ_UE_ALLOWED (&buf, sps.log2_max_pic_order_cnt_lsb_minus4, 0, 12);
  } else if (sps.pic_order_cnt_type == 1) {
    guint i;

    READ_UINT8 (&buf, sps.delta_pic_order_always_zero_flag, 1);
    READ_SE (&buf, sps.offset_for_non_ref_pic);
    READ_SE (&buf, sps.offset_for_top_to_bottom_field);
    READ_UE_ALLOWED (&buf, sps.num_ref_frames_in_pic_order_cnt_cycle, 0, 255);

    for (i = 0; i < sps.num_ref_frames_in_pic_order_cnt_cycle; i++)
      READ_SE (&buf, sps.offset_for_ref_frame[i]);
  }

  READ_UE (&buf, sps.num_ref_frames);
  READ_UINT8 (&buf, sps.gaps_in_frame_num_value_allowed_flag, 1);
  READ_UE (&buf, sps.pic_width_in_mbs_minus1);
  READ_UE (&buf, sps.pic_height_in_map_units_minus1);
  READ_UINT8 (&buf, sps.frame_mbs_only_flag, 1);

  if (!sps.frame_mbs_only_flag)
    READ_UINT8 (&buf, sps.mb_adaptive_frame_field_flag, 1);

  READ_UINT8 (&buf, sps.direct_8x8_inference_flag, 1);
  READ_UINT8 (&buf, frame_cropping_flag, 1);
  if (frame_cropping_flag) {
    READ_UE (&buf, sps.frame_crop_left_offset);
    READ_UE (&buf, sps.frame_crop_right_offset);
    READ_UE (&buf, sps.frame_crop_top_offset);
    READ_UE (&buf, sps.frame_crop_bottom_offset);
  }

  READ_UINT8 (&buf, sps.vui_parameters_present_flag, 1);
  if (sps.vui_parameters_present_flag) {
    if (!gst_ts_demux_parse_h264_parse_vui_parameters (&sps, &buf))
      goto error;
    vui = &sps.vui_parameters;
  }

  /* Calculate  width and height */
  width = (sps.pic_width_in_mbs_minus1 + 1);
  width *= 16;
  height = (sps.pic_height_in_map_units_minus1 + 1);
  height *= 16 * (2 - sps.frame_mbs_only_flag);
  GST_INFO ("initial width=%d, height=%d", width, height);

  if (frame_cropping_flag) {
    width -= (sps.frame_crop_left_offset + sps.frame_crop_right_offset)
        * subwc[sps.chroma_format_idc];
    height -= (sps.frame_crop_top_offset + sps.frame_crop_bottom_offset
        * subhc[sps.chroma_format_idc] * (2 - sps.frame_mbs_only_flag));
  }

  if (width < 0 || height < 0) {
    GST_WARNING ("invalid width/height in SPS");
    return FALSE;
  }
  GST_LOG ("final width=%u, height=%u", width, height);

  *wid = width;
  *ht = height;

  if (vui && vui->timing_info_present_flag && vui->fixed_frame_rate_flag
      && sps.frame_mbs_only_flag) {
    sps.fps_num = vui->time_scale;
    sps.fps_den = vui->num_units_in_tick;
    /* picture is a frame = 2 fields */
    sps.fps_den *= 2;

    *num = sps.fps_num;
    *den = sps.fps_den;

    GST_LOG ("framerate %d/%d", sps.fps_num, sps.fps_den);
  } else {
    *num = 0;
    *den = 0;
  }

  return TRUE;
error:
  GST_WARNING ("SPS parsing error");
  return FALSE;

}

/* geunil.jung. For high speed trick */
static void
gst_ts_demux_parse_h264_video (GstTSDemux * demux, TSDemuxStream * stream)
{
  //  MpegTSBaseStream *bs = (MpegTSBaseStream *) stream;
  MpegTSBase *base = (MpegTSBase *) demux;
  GstCaps *src_caps, *caps;
  guint offset = 0;
  guint offset_next = 0;
  guint offset_prev = 0;
  gboolean need_more_sps_data = FALSE;
  guint8 nal_unit_type = -1;
  guint16 width, height;
  gint fps_num, fps_den;

  if (stream->current_size < stream->last_scan_offset + 4)
    /* need more data */
    return;

  do {
    offset = scan_for_start_code_prefix (stream, stream->last_scan_offset);
    stream->last_scan_offset = offset;

    if (offset < stream->current_size - 3) {
      nal_unit_type = GST_READ_UINT8 (stream->data + offset + 3) & 0x1F;
      offset_prev = offset;
      stream->last_scan_offset += 3;

      if (nal_unit_type == 0x05) {
        /* IDR picture */
        GST_DEBUG ("IDR is founded!");
        base->is_iframe_in_cur_pes = TRUE;
        stream->is_iframe = TRUE;
        stream->is_first_iframe_in_interlace = FALSE;
        stream->frame_scan_done = TRUE;
        break;
      } else if (nal_unit_type == 0x01) {
        /* Non-IDR nal_unit_type */
        /* kimky. FIXME: The '16' is not the magic number */
        if (stream->current_size > 16) {
          GstNalParser buf;
          guint32 first_mb_in_slice;
          guint32 slice_type;
          guint16 frame_num;
          gint G_GNUC_UNUSED pps_id;
          GST_INFO
              ("Found Non-IDR NAL!!! CUR_OFFSET: [%u], NEXT_OFFSET: [%u], current_size: [%u]",
              offset, offset_next, stream->current_size);

          GST_DEBUG ("parsing Slice");
          gst_nal_parser_init (&buf,
              stream->data + stream->last_scan_offset + 1,
              stream->current_size - 1);
          first_mb_in_slice = -1;
          slice_type = -1;
          READ_UE (&buf, first_mb_in_slice);
          READ_UE (&buf, slice_type);

          READ_UE_ALLOWED (&buf, pps_id, 0, 255);
          READ_UINT16 (&buf, frame_num, stream->log2_max_frame_num_minus4 + 4);
          GST_INFO ("first_mb_in_slice: %u, slice_type: %u, frame_num: %u",
              first_mb_in_slice, slice_type, frame_num);

          if (slice_type != -1 && ((slice_type % 5) == 2)) {
            /* I slice */
            base->is_iframe_in_cur_pes = TRUE;
            stream->is_iframe = TRUE;
            stream->is_first_iframe_in_interlace = TRUE;
            stream->prev_frame_num = frame_num;
          } else if (slice_type != -1 && ((slice_type % 5) == 0)
              && stream->prev_frame_num >= 0
              && stream->prev_frame_num == frame_num
              && stream->is_first_iframe_in_interlace == TRUE) {
            /* P slice combined the previous I slice */
            base->is_iframe_in_cur_pes = TRUE;
            stream->is_iframe = TRUE;
            stream->is_first_iframe_in_interlace = FALSE;
            stream->prev_frame_num = frame_num;
            GST_INFO ("P slice combined the previout I slice");
          } else {
            base->is_iframe_in_cur_pes = FALSE;
            stream->is_iframe = FALSE;
            stream->is_first_iframe_in_interlace = FALSE;
            stream->prev_frame_num = -1;
          }
          stream->frame_scan_done = TRUE;
          continue;
        error:
          GST_WARNING ("Slice parsing error");
          return;
        }
      } else if (nal_unit_type != 0 && nal_unit_type < 0x05) {
        base->is_iframe_in_cur_pes = FALSE;
        stream->is_iframe = FALSE;
        stream->is_first_iframe_in_interlace = FALSE;
        stream->frame_scan_done = TRUE;
        break;
      } else if (nal_unit_type == 0x07 && !stream->is_update_video_caps) {
        stream->is_iframe = FALSE;
        stream->is_first_iframe_in_interlace = FALSE;
        /*SPS NAL Unit */
        offset_next =
            scan_for_start_code_prefix (stream, stream->last_scan_offset);
        if (offset_next != 0 && (stream->data[offset_next] == 0x00
                && stream->data[offset_next + 1] == 0x00
                && stream->data[offset_next + 2] == 0x01)) {
          GST_INFO
              ("Found SPS NAL!!! CUR_OFFSET: [%u], NEXT_OFFSET: [%u], current_size: [%u]",
              offset, offset_next, stream->current_size);
          if (gst_ts_demux_parse_h264_sps (stream,
                  stream->last_scan_offset + 1, &width, &height, &fps_num,
                  &fps_den)) {
            if (width > 0 && height > 0) {
              if ((width / 16 * height / 16) > 8704)
                base->is_higher_than_FHD = TRUE;
              else
                base->is_higher_than_FHD = FALSE;

              src_caps = gst_pad_query_caps (stream->pad, NULL);
              if (src_caps) {
                caps = gst_caps_copy (src_caps);
                if (fps_den != 0) {
                  gst_caps_set_simple (caps, "width", G_TYPE_INT, width,
                      "height", G_TYPE_INT, height, "framerate",
                      GST_TYPE_FRACTION, fps_num, fps_den, NULL);
                } else {
                  gst_caps_set_simple (caps, "width", G_TYPE_INT, width,
                      "height", G_TYPE_INT, height, NULL);
                }
                stream->is_update_video_caps = TRUE;
                gst_pad_set_caps (stream->pad, caps);
                gst_caps_unref (src_caps);
                gst_caps_unref (caps);
              }
            }
          }
        } else {
          GST_INFO
              ("need more SPS data. last_scan: %d, offset_next: %d, size: %d",
              stream->last_scan_offset, offset_next, stream->current_size);
          need_more_sps_data = TRUE;
        }
      }
    } else
      /* need more data */
      break;
  } while (stream->current_size > stream->last_scan_offset + 2);

  if (need_more_sps_data) {
    stream->last_scan_offset = offset_prev;
    GST_LOG ("Start parsing with last_scan_offset: %d",
        stream->last_scan_offset);
  }
}

/* geunil.jung. For high speed trick */
static void
gst_ts_demux_parse_video_es (GstTSDemux * demux, TSDemuxStream * stream)
{
  MpegTSBaseStream *bs = (MpegTSBaseStream *) stream;
  GstCaps *src_caps;
  gint i;
  switch (bs->stream_type) {
    case GST_MPEGTS_STREAM_TYPE_VIDEO_MPEG1:
    case GST_MPEGTS_STREAM_TYPE_VIDEO_MPEG2:
      gst_ts_demux_parse_mpeg2_video (demux, stream);
      break;
    case GST_MPEGTS_STREAM_TYPE_VIDEO_MPEG4:
      gst_ts_demux_parse_mpeg4_video (demux, stream);
      break;
    case GST_MPEGTS_STREAM_TYPE_VIDEO_H264:
      gst_ts_demux_parse_h264_video (demux, stream);
      break;
    case GST_MPEGTS_STREAM_TYPE_VIDEO_HEVC_H265:
    case GST_MPEGTS_STREAM_TYPE_VIDEO_HEVC:
      /*arun.s for calculating height,width from sps for h265 */
      gst_ts_demux_parse_h265_video (demux, stream);
      break;
    case GST_MPEGTS_STREAM_TYPE_PRIVATE_PES_PACKETS:
      // For Dolby Vision stream
      if (bs->registration_id == DRF_ID_DOVI) {
        src_caps = gst_pad_query_caps (stream->pad, NULL);
        for (i = 0; i < gst_caps_get_size (src_caps); i++) {
          gint dv_profile;

          GstStructure *structure = gst_caps_get_structure (src_caps, i);

          if (gst_structure_get_int (structure, "dolby-vision-profile",
                  &dv_profile)) {
            switch (dv_profile) {
              case 0:
              case 1:
                gst_ts_demux_parse_h264_video (demux, stream);
                break;
              case 2:
              case 3:
              case 4:
              case 5:
              case 6:
              case 7:
                gst_ts_demux_parse_h265_video (demux, stream);
                break;
              default:
                break;
            }
            break;
          }
        }
        gst_caps_unref (src_caps);
      }
      break;
    default:
      break;
  }

  return;
}

static GstFlowReturn
gst_ts_demux_handle_packet (GstTSDemux * demux, TSDemuxStream * stream,
    MpegTSPacketizerPacket * packet, GstMpegtsSection * section)
{
  GstFlowReturn res = GST_FLOW_OK;
  /* geunil.jung. For high speed trick */
  MpegTSBaseStream *bs = (MpegTSBaseStream *) stream;
  MpegTSBase *base = (MpegTSBase *) demux;

  GST_LOG ("pid 0x%04x pusi:%d, afc:%d, cont:%d, payload:%p",
      packet->pid, packet->payload_unit_start_indicator,
      packet->scram_afc_cc & 0x30,
      FLAGS_CONTINUITY_COUNTER (packet->scram_afc_cc), packet->payload);
  if (section) {
    GST_DEBUG ("buffer size %d", section->section_length);
    return res;
  }

  if (G_UNLIKELY (packet->payload_unit_start_indicator) &&
      FLAGS_HAS_PAYLOAD (packet->scram_afc_cc)) {
    /* geunil.jung. For high speed trick */
    if (base->high_speed_trick && base->video_pid == bs->pid) {
      GST_INFO ("base->video_pid 0x%04x pusi:%d, is_iframe:%d",
          base->video_pid, packet->payload_unit_start_indicator,
          stream->is_iframe);
      if (stream->is_iframe) {
        res = gst_ts_demux_push_pending_data (demux, stream, NULL);
        GST_INFO ("push_pending_data in handle_packet: Return %s",
            gst_flow_get_name (res));
        if (res == GST_FLOW_OK)
          base->iframe_push_done = TRUE;
//        if (base->segment.rate < 0)
        return res;
      } else
        gst_ts_demux_stream_reset (stream);
    } else
      /* Flush previous data */
      res = gst_ts_demux_push_pending_data (demux, stream, NULL);
  }

  if (packet->payload && (res == GST_FLOW_OK || res == GST_FLOW_NOT_LINKED)
      && stream->pad) {
    gst_ts_demux_queue_data (demux, stream, packet);
    /* geunil.jung. For high speed trick */
    /* now let's search i-frame!! */
    if ((base->iframe_interval == -1 && !stream->frame_scan_done) ||
        (base->high_speed_trick
            && stream->state == PENDING_PACKET_BUFFER
            && base->video_pid == bs->pid && !stream->frame_scan_done)) {
      GST_INFO ("Start searching I frame with last_scan_offset:%d",
          stream->last_scan_offset);
      gst_ts_demux_parse_video_es (demux, stream);
      if (stream->is_iframe) {
        GST_INFO ("I frame is founded!!");
        GST_INFO ("iframe_offset %" G_GUINT64_FORMAT
            ", iframe_interval %" G_GUINT32_FORMAT ", packetizer->offset %"
            G_GUINT64_FORMAT, base->iframe_offset, base->iframe_interval,
            base->packetizer->offset);

        if (base->iframe_interval == -1 && base->iframe_offset != -1
            && !base->happen_seek_event) {
          if (base->packetizer->offset >= base->iframe_offset)
            base->iframe_interval =
                base->packetizer->offset - base->iframe_offset;
          else
            base->iframe_interval =
                base->iframe_offset - base->packetizer->offset;
//          base->trick_seek_size = base->iframe_interval * base->seek_size_ratio;
        }

        if (!base->iframe_push_done)
          base->iframe_offset = base->packetizer->offset;

        if (base->happen_seek_event)
          base->happen_seek_event = FALSE;
      }
    }

    /* Test for DLNA MPEG_TS_HD REW */
    if (base->high_speed_trick && (base->segment.rate < 0) &&
        ((base->video_pid != bs->pid) && stream->current_size) &&
        (stream->current_size < stream->expected_size)) {
//      if (!base->audio_pushed) {
      res = gst_ts_demux_push_pending_data (demux, stream, NULL);
      if (res == GST_FLOW_OK) {
        GST_INFO ("audio data pushed!");
        base->audio_pushed = TRUE;
      }
//      }
    }

    GST_LOG ("current_size:%d, expected_size:%d, audio_pushed %d",
        stream->current_size, stream->expected_size, base->audio_pushed);
    /* Finally check if the data we queued completes a packet */
    if (stream->expected_size && stream->current_size == stream->expected_size) {
      GST_LOG ("pushing complete packet");
      /* geunil.jung. For high speed trick */
      GST_INFO ("pid 0x%04x, video_pid 0x%04x", bs->pid, base->video_pid);
      if (base->high_speed_trick && base->video_pid == bs->pid) {
        if (stream->is_iframe) {
          res = gst_ts_demux_push_pending_data (demux, stream, NULL);
          if (res == GST_FLOW_OK) {
            base->iframe_push_done = TRUE;
          }
        } else
          gst_ts_demux_stream_reset (stream);
      } else if (!base->audio_pushed) {
        res = gst_ts_demux_push_pending_data (demux, stream, NULL);
        if (res == GST_FLOW_OK) {
          GST_INFO ("audio data pushed!");
          base->audio_pushed = TRUE;
        }
      } else {
        res = gst_ts_demux_push_pending_data (demux, stream, NULL);
      }
    }
  }

  /* We are rewinding to find a keyframe,
   * and didn't want the data to be queued
   */
  if (res == GST_FLOW_REWINDING)
    res = GST_FLOW_OK;

  return res;
}

static void
gst_ts_demux_flush (MpegTSBase * base, gboolean hard)
{
  GstTSDemux *demux = GST_TS_DEMUX_CAST (base);

  gst_ts_demux_flush_streams (demux, hard);

  if (demux->segment_event) {
    gst_event_unref (demux->segment_event);
    demux->segment_event = NULL;
  }
  if (demux->global_tags) {
    gst_tag_list_unref (demux->global_tags);
    demux->global_tags = NULL;
  }
  if (hard) {
    /* For pull mode seeks the current segment needs to be preserved */
    demux->rate = 1.0;
#if 0                           // In case of FastForward, Do not run below
    gst_segment_init (&demux->segment, GST_FORMAT_UNDEFINED);
#endif
  }
}

static GstFlowReturn
gst_ts_demux_drain (MpegTSBase * base)
{
  GstTSDemux *demux = GST_TS_DEMUX_CAST (base);
  GList *tmp;
  GstFlowReturn res = GST_FLOW_OK;

  if (!demux->program)
    return res;

  for (tmp = demux->program->stream_list; tmp; tmp = tmp->next) {
    TSDemuxStream *stream = (TSDemuxStream *) tmp->data;
    if (stream->pad) {
      res = gst_ts_demux_push_pending_data (demux, stream, NULL);
      if (G_UNLIKELY (res != GST_FLOW_OK))
        break;
    }
  }

  return res;
}

static GstFlowReturn
gst_ts_demux_push (MpegTSBase * base, MpegTSPacketizerPacket * packet,
    GstMpegtsSection * section)
{
  GstTSDemux *demux = GST_TS_DEMUX_CAST (base);
  TSDemuxStream *stream = NULL;
  GstFlowReturn res = GST_FLOW_OK;
  if (G_LIKELY (demux->program)) {
    stream = (TSDemuxStream *) demux->program->streams[packet->pid];
    if (stream) {
      res = gst_ts_demux_handle_packet (demux, stream, packet, section);
    }
  }
  return res;
}

gboolean
gst_ts_demux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (ts_demux_debug, "dvrtsdemux", 0,
      "MPEG transport stream demuxer");
  init_pes_parser ();
  return gst_element_register (plugin, "dvrtsdemux",
      GST_RANK_NONE, GST_TYPE_TS_DEMUX);
}
