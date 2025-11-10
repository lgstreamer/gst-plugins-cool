/*
 * Copyright (C) 2014 Stefan Ringel
 *
 * Authors:
 *   Stefan Ringel <linuxtv@stefanringel.de>
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

#include <string.h>
#include <stdlib.h>

#include "mpegts.h"
#include "gstmpegts-private.h"

/**
 * SECTION:gst-atsc-section
 * @title: ATSC variants of MPEG-TS sections
 * @short_description: Sections for the various ATSC specifications
 * @include: gst/mpegts/mpegts.h
 *
 */

/* Terrestrial/Cable Virtual Channel Table TVCT/CVCT */
static GstDvrMpegtsAtscVCTSource *
_gst_dvrmpegts_atsc_vct_source_copy (GstDvrMpegtsAtscVCTSource * source)
{
  GstDvrMpegtsAtscVCTSource *copy;

  copy = g_slice_dup (GstDvrMpegtsAtscVCTSource, source);
  copy->descriptors = g_ptr_array_ref (source->descriptors);

  return copy;
}

static void
_gst_dvrmpegts_atsc_vct_source_free (GstDvrMpegtsAtscVCTSource * source)
{
  g_free (source->short_name);
  if (source->descriptors)
    g_ptr_array_unref (source->descriptors);
  g_slice_free (GstDvrMpegtsAtscVCTSource, source);
}

G_DEFINE_BOXED_TYPE (GstDvrMpegtsAtscVCTSource, gst_dvrmpegts_atsc_vct_source,
    (GBoxedCopyFunc) _gst_dvrmpegts_atsc_vct_source_copy,
    (GFreeFunc) _gst_dvrmpegts_atsc_vct_source_free);

static GstDvrMpegtsAtscVCT *
_gst_dvrmpegts_atsc_vct_copy (GstDvrMpegtsAtscVCT * vct)
{
  GstDvrMpegtsAtscVCT *copy;

  copy = g_slice_dup (GstDvrMpegtsAtscVCT, vct);
  copy->sources = g_ptr_array_ref (vct->sources);
  copy->descriptors = g_ptr_array_ref (vct->descriptors);

  return copy;
}

static void
_gst_dvrmpegts_atsc_vct_free (GstDvrMpegtsAtscVCT * vct)
{
  if (vct->sources)
    g_ptr_array_unref (vct->sources);
  if (vct->descriptors)
    g_ptr_array_unref (vct->descriptors);
  g_slice_free (GstDvrMpegtsAtscVCT, vct);
}

G_DEFINE_BOXED_TYPE (GstDvrMpegtsAtscVCT, gst_dvrmpegts_atsc_vct,
    (GBoxedCopyFunc) _gst_dvrmpegts_atsc_vct_copy,
    (GFreeFunc) _gst_dvrmpegts_atsc_vct_free);

static gpointer
_parse_atsc_vct (GstMpegtsSection * section)
{
  GstDvrMpegtsAtscVCT *vct = NULL;
  guint8 *data, *end, source_nb;
  guint32 tmp32;
  guint16 descriptors_loop_length, tmp16;
  guint i;
  GError *err = NULL;

  vct = g_slice_new0 (GstDvrMpegtsAtscVCT);

  data = section->data;
  end = data + section->section_length;

  vct->transport_stream_id = section->subtable_extension;

  /* Skip already parsed data */
  data += 8;

  /* minimum size */
  if (end - data < 2 + 2 + 4)
    goto error;

  vct->protocol_version = *data;
  data += 1;

  source_nb = *data;
  data += 1;

  vct->sources = g_ptr_array_new_full (source_nb,
      (GDestroyNotify) _gst_dvrmpegts_atsc_vct_source_free);

  for (i = 0; i < source_nb; i++) {
    GstDvrMpegtsAtscVCTSource *source;

    /* minimum 32 bytes for a entry, 2 bytes second descriptor
       loop-length, 4 bytes crc */
    if (end - data < 32 + 2 + 4)
      goto error;

    source = g_slice_new0 (GstDvrMpegtsAtscVCTSource);
    g_ptr_array_add (vct->sources, source);

    source->short_name =
        g_convert ((gchar *) data, 14, "utf-8", "utf-16be", NULL, NULL, &err);
    if (err) {
      GST_WARNING ("Failed to convert VCT Source short_name to utf-8: %d %s",
          err->code, err->message);
      GST_MEMDUMP ("UTF-16 string", data, 14);
      g_error_free (err);
    }
    data += 14;

    tmp32 = GST_READ_UINT32_BE (data);
    source->major_channel_number = (tmp32 >> 18) & 0x03FF;
    source->minor_channel_number = (tmp32 >> 8) & 0x03FF;
    source->modulation_mode = tmp32 & 0xF;
    data += 4;

    source->carrier_frequency = GST_READ_UINT32_BE (data);
    data += 4;

    source->channel_TSID = GST_READ_UINT16_BE (data);
    data += 2;

    source->program_number = GST_READ_UINT16_BE (data);
    data += 2;

    tmp16 = GST_READ_UINT16_BE (data);
    source->ETM_location = (tmp16 >> 14) & 0x3;
    source->access_controlled = (tmp16 >> 13) & 0x1;
    source->hidden = (tmp16 >> 12) & 0x1;

    /* only used in CVCT */
    source->path_select = (tmp16 >> 11) & 0x1;
    source->out_of_band = (tmp16 >> 10) & 0x1;

    source->hide_guide = (tmp16 >> 9) & 0x1;
    source->service_type = tmp16 & 0x3f;
    data += 2;

    source->source_id = GST_READ_UINT16_BE (data);
    data += 2;

    descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x03FF;
    data += 2;

    if (end - data < descriptors_loop_length + 6)
      goto error;

    source->descriptors =
        gst_mpegts_parse_descriptors (data, descriptors_loop_length);
    if (source->descriptors == NULL)
      goto error;
    data += descriptors_loop_length;
  }

  descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x03FF;
  data += 2;

  if (end - data < descriptors_loop_length + 4)
    goto error;

  vct->descriptors =
      gst_mpegts_parse_descriptors (data, descriptors_loop_length);
  if (vct->descriptors == NULL)
    goto error;

  return (gpointer) vct;

error:
  _gst_dvrmpegts_atsc_vct_free (vct);

  return NULL;
}

/**
 * gst_mpegts_section_get_atsc_tvct:
 * @section: a #GstMpegtsSection of type %GST_MPEGTS_SECTION_ATSC_TVCT
 *
 * Returns the #GstDvrMpegtsAtscVCT contained in the @section
 *
 * Returns: The #GstDvrMpegtsAtscVCT contained in the section, or %NULL if an error
 * happened.
 */
const GstDvrMpegtsAtscVCT *
gst_mpegts_section_get_atsc_tvct (GstMpegtsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_ATSC_TVCT,
      NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed =
        __common_section_checks (section, 16, _parse_atsc_vct,
        (GDestroyNotify) _gst_dvrmpegts_atsc_vct_free);

  return (const GstDvrMpegtsAtscVCT *) section->cached_parsed;
}

/**
 * gst_mpegts_section_get_atsc_cvct:
 * @section: a #GstMpegtsSection of type %GST_MPEGTS_SECTION_ATSC_CVCT
 *
 * Returns the #GstDvrMpegtsAtscVCT contained in the @section
 *
 * Returns: The #GstDvrMpegtsAtscVCT contained in the section, or %NULL if an error
 * happened.
 */
const GstDvrMpegtsAtscVCT *
gst_mpegts_section_get_atsc_cvct (GstMpegtsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_ATSC_CVCT,
      NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed =
        __common_section_checks (section, 16, _parse_atsc_vct,
        (GDestroyNotify) _gst_dvrmpegts_atsc_vct_free);

  return (const GstDvrMpegtsAtscVCT *) section->cached_parsed;
}

/* MGT */

static GstDvrMpegtsAtscMGTTable *
_gst_dvrmpegts_atsc_mgt_table_copy (GstDvrMpegtsAtscMGTTable * mgt_table)
{
  GstDvrMpegtsAtscMGTTable *copy;

  copy = g_slice_dup (GstDvrMpegtsAtscMGTTable, mgt_table);
  copy->descriptors = g_ptr_array_ref (mgt_table->descriptors);

  return copy;
}

static void
_gst_dvrmpegts_atsc_mgt_table_free (GstDvrMpegtsAtscMGTTable * mgt_table)
{
  g_ptr_array_unref (mgt_table->descriptors);
  g_slice_free (GstDvrMpegtsAtscMGTTable, mgt_table);
}

G_DEFINE_BOXED_TYPE (GstDvrMpegtsAtscMGTTable, gst_dvrmpegts_atsc_mgt_table,
    (GBoxedCopyFunc) _gst_dvrmpegts_atsc_mgt_table_copy,
    (GFreeFunc) _gst_dvrmpegts_atsc_mgt_table_free);

static GstDvrMpegtsAtscMGT *
_gst_dvrmpegts_atsc_mgt_copy (GstDvrMpegtsAtscMGT * mgt)
{
  GstDvrMpegtsAtscMGT *copy;

  copy = g_slice_dup (GstDvrMpegtsAtscMGT, mgt);
  copy->tables = g_ptr_array_ref (mgt->tables);
  copy->descriptors = g_ptr_array_ref (mgt->descriptors);

  return copy;
}

static void
_gst_dvrmpegts_atsc_mgt_free (GstDvrMpegtsAtscMGT * mgt)
{
  g_ptr_array_unref (mgt->tables);
  g_ptr_array_unref (mgt->descriptors);
  g_slice_free (GstDvrMpegtsAtscMGT, mgt);
}

G_DEFINE_BOXED_TYPE (GstDvrMpegtsAtscMGT, gst_dvrmpegts_atsc_mgt,
    (GBoxedCopyFunc) _gst_dvrmpegts_atsc_mgt_copy,
    (GFreeFunc) _gst_dvrmpegts_atsc_mgt_free);

static gpointer
_parse_atsc_mgt (GstMpegtsSection * section)
{
  GstDvrMpegtsAtscMGT *mgt = NULL;
  guint i = 0;
  guint8 *data, *end;
  guint16 descriptors_loop_length;

  mgt = g_slice_new0 (GstDvrMpegtsAtscMGT);

  data = section->data;
  end = data + section->section_length;

  /* Skip already parsed data */
  data += 8;

  mgt->protocol_version = GST_READ_UINT8 (data);
  data += 1;
  mgt->tables_defined = GST_READ_UINT16_BE (data);
  data += 2;
  mgt->tables = g_ptr_array_new_full (mgt->tables_defined,
      (GDestroyNotify) _gst_dvrmpegts_atsc_mgt_table_free);
  for (i = 0; i < mgt->tables_defined && data + 11 < end; i++) {
    GstDvrMpegtsAtscMGTTable *mgt_table;

    if (data + 11 >= end) {
      GST_WARNING ("MGT data too short to parse inner table num %d", i);
      goto error;
    }

    mgt_table = g_slice_new0 (GstDvrMpegtsAtscMGTTable);
    g_ptr_array_add (mgt->tables, mgt_table);

    mgt_table->table_type = GST_READ_UINT16_BE (data);
    data += 2;
    mgt_table->pid = GST_READ_UINT16_BE (data) & 0x1FFF;
    data += 2;
    mgt_table->version_number = GST_READ_UINT8 (data) & 0x1F;
    data += 1;
    mgt_table->number_bytes = GST_READ_UINT32_BE (data);
    data += 4;
    descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x0FFF;
    data += 2;

    if (data + descriptors_loop_length >= end) {
      GST_WARNING ("MGT data too short to parse inner table descriptors (table "
          "num %d", i);
      goto error;
    }
    mgt_table->descriptors =
        gst_mpegts_parse_descriptors (data, descriptors_loop_length);
    data += descriptors_loop_length;
  }

  descriptors_loop_length = GST_READ_UINT16_BE (data) & 0xFFF;
  data += 2;
  if (data + descriptors_loop_length >= end) {
    GST_WARNING ("MGT data too short to parse descriptors");
    goto error;
  }
  mgt->descriptors =
      gst_mpegts_parse_descriptors (data, descriptors_loop_length);

  return (gpointer) mgt;

error:
  _gst_dvrmpegts_atsc_mgt_free (mgt);

  return NULL;
}


/**
 * gst_mpegts_section_get_atsc_mgt:
 * @section: a #GstMpegtsSection of type %GST_MPEGTS_SECTION_ATSC_MGT
 *
 * Returns the #GstDvrMpegtsAtscMGT contained in the @section.
 *
 * Returns: The #GstDvrMpegtsAtscMGT contained in the section, or %NULL if an error
 * happened.
 */
const GstDvrMpegtsAtscMGT *
gst_mpegts_section_get_atsc_mgt (GstMpegtsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_ATSC_MGT,
      NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed =
        __common_section_checks (section, 17, _parse_atsc_mgt,
        (GDestroyNotify) _gst_dvrmpegts_atsc_mgt_free);

  return (const GstDvrMpegtsAtscMGT *) section->cached_parsed;
}

/* Multi string structure */

static GstDvrMpegtsAtscStringSegment *
_gst_dvrmpegts_atsc_string_segment_copy (GstDvrMpegtsAtscStringSegment * seg)
{
  GstDvrMpegtsAtscStringSegment *copy;

  copy = g_slice_dup (GstDvrMpegtsAtscStringSegment, seg);

  return copy;
}

static void
_gst_dvrmpegts_atsc_string_segment_free (GstDvrMpegtsAtscStringSegment * seg)
{
  g_free (seg->cached_string);
  g_slice_free (GstDvrMpegtsAtscStringSegment, seg);
}

static void
_gst_mpegts_atsc_string_segment_decode_string (GstDvrMpegtsAtscStringSegment * seg)
{
  const gchar *from_encoding;

  g_return_if_fail (seg->cached_string == NULL);

  if (seg->compression_type != 0) {
    GST_FIXME ("Compressed strings not yet supported");
    return;
  }

  /* FIXME add more encodings */
  switch (seg->mode) {
    case 0x3F:
      from_encoding = "UTF-16BE";
      break;
    default:
      from_encoding = NULL;
      break;
  }

  if (from_encoding != NULL && seg->compressed_data_size > 0) {
    GError *err = NULL;

    seg->cached_string =
        g_convert ((gchar *) seg->compressed_data,
        (gssize) seg->compressed_data_size, "UTF-8", from_encoding, NULL, NULL,
        &err);

    if (err) {
      GST_WARNING ("Failed to convert input string from codeset %s",
          from_encoding);
      g_error_free (err);
    }
  } else {
    seg->cached_string =
        g_strndup ((gchar *) seg->compressed_data, seg->compressed_data_size);
  }
}

const gchar *
gst_mpegts_atsc_string_segment_get_string (GstDvrMpegtsAtscStringSegment * seg)
{
  if (!seg->cached_string)
    _gst_mpegts_atsc_string_segment_decode_string (seg);

  return seg->cached_string;
}

G_DEFINE_BOXED_TYPE (GstDvrMpegtsAtscStringSegment, gst_dvrmpegts_atsc_string_segment,
    (GBoxedCopyFunc) _gst_dvrmpegts_atsc_string_segment_copy,
    (GFreeFunc) _gst_dvrmpegts_atsc_string_segment_free);

static GstDvrMpegtsAtscMultString *
_gst_dvrmpegts_atsc_mult_string_copy (GstDvrMpegtsAtscMultString * mstring)
{
  GstDvrMpegtsAtscMultString *copy;

  copy = g_slice_dup (GstDvrMpegtsAtscMultString, mstring);
  copy->segments = g_ptr_array_ref (mstring->segments);

  return copy;
}

static void
_gst_dvrmpegts_atsc_mult_string_free (GstDvrMpegtsAtscMultString * mstring)
{
  g_ptr_array_unref (mstring->segments);
  g_slice_free (GstDvrMpegtsAtscMultString, mstring);
}

G_DEFINE_BOXED_TYPE (GstDvrMpegtsAtscMultString, gst_dvrmpegts_atsc_mult_string,
    (GBoxedCopyFunc) _gst_dvrmpegts_atsc_mult_string_copy,
    (GFreeFunc) _gst_dvrmpegts_atsc_mult_string_free);

static GPtrArray *
_parse_atsc_mult_string (guint8 * data, guint datasize)
{
  guint8 num_strings;
  GPtrArray *res = NULL;
  guint8 *end = data + datasize;
  gint i;

  if (datasize > 0) {
    /* 1 is the minimum entry size, so no need to check here */
    num_strings = GST_READ_UINT8 (data);
    data += 1;

    res =
        g_ptr_array_new_full (num_strings,
        (GDestroyNotify) _gst_dvrmpegts_atsc_mult_string_free);

    for (i = 0; i < num_strings; i++) {
      GstDvrMpegtsAtscMultString *mstring;
      guint8 num_segments;
      gint j;

      mstring = g_slice_new0 (GstDvrMpegtsAtscMultString);
      g_ptr_array_add (res, mstring);
      mstring->segments =
          g_ptr_array_new_full (num_strings,
          (GDestroyNotify) _gst_dvrmpegts_atsc_string_segment_free);

      /* each entry needs at least 4 bytes (lang code and segments number) */
      if (end - data < 4) {
        GST_WARNING ("Data too short for multstring parsing %d",
            (gint) (end - data));
        goto error;
      }

      mstring->iso_639_langcode[0] = GST_READ_UINT8 (data);
      data += 1;
      mstring->iso_639_langcode[1] = GST_READ_UINT8 (data);
      data += 1;
      mstring->iso_639_langcode[2] = GST_READ_UINT8 (data);
      data += 1;
      num_segments = GST_READ_UINT8 (data);
      data += 1;

      for (j = 0; j < num_segments; j++) {
        GstDvrMpegtsAtscStringSegment *seg;

        seg = g_slice_new0 (GstDvrMpegtsAtscStringSegment);
        g_ptr_array_add (mstring->segments, seg);

        /* each entry needs at least 3 bytes */
        if (end - data < 3) {
          GST_WARNING ("Data too short for multstring parsing %d", datasize);
          goto error;
        }

        seg->compression_type = GST_READ_UINT8 (data);
        data += 1;
        seg->mode = GST_READ_UINT8 (data);
        data += 1;
        seg->compressed_data_size = GST_READ_UINT8 (data);
        data += 1;

        if (end - data < seg->compressed_data_size) {
          GST_WARNING ("Data too short for multstring parsing %d", datasize);
          goto error;
        }

        if (seg->compressed_data_size)
          seg->compressed_data = data;
        data += seg->compressed_data_size;
      }

    }
  }
  return res;

error:
  if (res)
    g_ptr_array_unref (res);
  return NULL;
}

/* EIT */

static GstDvrMpegtsAtscEITEvent *
_gst_dvrmpegts_atsc_eit_event_copy (GstDvrMpegtsAtscEITEvent * event)
{
  GstDvrMpegtsAtscEITEvent *copy;

  copy = g_slice_dup (GstDvrMpegtsAtscEITEvent, event);
  copy->titles = g_ptr_array_ref (event->titles);
  copy->descriptors = g_ptr_array_ref (event->descriptors);

  return copy;
}

static void
_gst_dvrmpegts_atsc_eit_event_free (GstDvrMpegtsAtscEITEvent * event)
{
  if (event->titles)
    g_ptr_array_unref (event->titles);
  if (event->descriptors)
    g_ptr_array_unref (event->descriptors);
  g_slice_free (GstDvrMpegtsAtscEITEvent, event);
}

G_DEFINE_BOXED_TYPE (GstDvrMpegtsAtscEITEvent, gst_dvrmpegts_atsc_eit_event,
    (GBoxedCopyFunc) _gst_dvrmpegts_atsc_eit_event_copy,
    (GFreeFunc) _gst_dvrmpegts_atsc_eit_event_free);

static GstDvrMpegtsAtscEIT *
_gst_dvrmpegts_atsc_eit_copy (GstDvrMpegtsAtscEIT * eit)
{
  GstDvrMpegtsAtscEIT *copy;

  copy = g_slice_dup (GstDvrMpegtsAtscEIT, eit);
  copy->events = g_ptr_array_ref (eit->events);

  return copy;
}

static void
_gst_dvrmpegts_atsc_eit_free (GstDvrMpegtsAtscEIT * eit)
{
  if (eit->events)
    g_ptr_array_unref (eit->events);
  g_slice_free (GstDvrMpegtsAtscEIT, eit);
}

G_DEFINE_BOXED_TYPE (GstDvrMpegtsAtscEIT, gst_dvrmpegts_atsc_eit,
    (GBoxedCopyFunc) _gst_dvrmpegts_atsc_eit_copy,
    (GFreeFunc) _gst_dvrmpegts_atsc_eit_free);

static gpointer
_parse_atsc_eit (GstMpegtsSection * section)
{
  GstDvrMpegtsAtscEIT *eit = NULL;
  guint i = 0;
  guint8 *data, *end;
  guint8 num_events;

  eit = g_slice_new0 (GstDvrMpegtsAtscEIT);

  data = section->data;
  end = data + section->section_length;

  eit->source_id = section->subtable_extension;

  /* Skip already parsed data */
  data += 8;

  eit->protocol_version = GST_READ_UINT8 (data);
  data += 1;
  num_events = GST_READ_UINT8 (data);
  data += 1;

  eit->events = g_ptr_array_new_with_free_func ((GDestroyNotify)
      _gst_dvrmpegts_atsc_eit_event_free);

  for (i = 0; i < num_events; i++) {
    GstDvrMpegtsAtscEITEvent *event;
    guint32 tmp;
    guint8 text_length;
    guint16 descriptors_loop_length;

    if (end - data < 12) {
      GST_WARNING ("PID %d invalid EIT entry length %d with %u events",
          section->pid, (gint) (end - 4 - data), num_events);
      goto error;
    }

    event = g_slice_new0 (GstDvrMpegtsAtscEITEvent);
    g_ptr_array_add (eit->events, event);

    event->event_id = GST_READ_UINT16_BE (data) & 0x3FFF;
    data += 2;
    event->start_time = GST_READ_UINT32_BE (data);
    data += 4;

    tmp = GST_READ_UINT32_BE (data);
    data += 4;
    event->etm_location = (tmp >> 28) & 0x3;
    event->length_in_seconds = (tmp >> 8) & 0x0FFFFF;
    text_length = tmp & 0xFF;

    if (text_length > end - data - 4 - 2) {
      GST_WARNING ("PID %d invalid EIT entry length %d with %u events",
          section->pid, (gint) (end - 4 - data), num_events);
      goto error;
    }
    event->titles = _parse_atsc_mult_string (data, text_length);
    data += text_length;

    descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x0FFF;
    data += 2;

    if (end - data - 4 < descriptors_loop_length) {
      GST_WARNING ("PID %d invalid EIT entry length %d with %u events",
          section->pid, (gint) (end - 4 - data), num_events);
      goto error;
    }

    event->descriptors =
        gst_mpegts_parse_descriptors (data, descriptors_loop_length);
    data += descriptors_loop_length;
  }

  if (data != end - 4) {
    GST_WARNING ("PID %d invalid EIT parsed %d length %d",
        section->pid, (gint) (data - section->data), section->section_length);
    goto error;
  }

  return (gpointer) eit;

error:
  _gst_dvrmpegts_atsc_eit_free (eit);

  return NULL;

}

/**
 * gst_mpegts_section_get_atsc_eit:
 * @section: a #GstMpegtsSection of type %GST_MPEGTS_SECTION_ATSC_EIT
 *
 * Returns the #GstDvrMpegtsAtscEIT contained in the @section.
 *
 * Returns: The #GstDvrMpegtsAtscEIT contained in the section, or %NULL if an error
 * happened.
 */
const GstDvrMpegtsAtscEIT *
gst_mpegts_section_get_atsc_eit (GstMpegtsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_ATSC_EIT,
      NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed =
        __common_section_checks (section, 14, _parse_atsc_eit,
        (GDestroyNotify) _gst_dvrmpegts_atsc_eit_free);

  return (const GstDvrMpegtsAtscEIT *) section->cached_parsed;
}


static GstDvrMpegtsAtscETT *
_gst_dvrmpegts_atsc_ett_copy (GstDvrMpegtsAtscETT * ett)
{
  GstDvrMpegtsAtscETT *copy;

  copy = g_slice_dup (GstDvrMpegtsAtscETT, ett);
  copy->messages = g_ptr_array_ref (ett->messages);

  return copy;
}

static void
_gst_dvrmpegts_atsc_ett_free (GstDvrMpegtsAtscETT * ett)
{
  if (ett->messages)
    g_ptr_array_unref (ett->messages);
  g_slice_free (GstDvrMpegtsAtscETT, ett);
}

G_DEFINE_BOXED_TYPE (GstDvrMpegtsAtscETT, gst_dvrmpegts_atsc_ett,
    (GBoxedCopyFunc) _gst_dvrmpegts_atsc_ett_copy,
    (GFreeFunc) _gst_dvrmpegts_atsc_ett_free);

static gpointer
_parse_ett (GstMpegtsSection * section)
{
  GstDvrMpegtsAtscETT *ett = NULL;
  guint8 *data, *end;

  ett = g_slice_new0 (GstDvrMpegtsAtscETT);

  data = section->data;
  end = data + section->section_length;

  ett->ett_table_id_extension = section->subtable_extension;

  /* Skip already parsed data */
  data += 8;

  ett->protocol_version = GST_READ_UINT8 (data);
  data += 1;
  ett->etm_id = GST_READ_UINT32_BE (data);
  data += 4;

  ett->messages = _parse_atsc_mult_string (data, end - data - 4);
  data += end - data - 4;

  if (data != end - 4) {
    GST_WARNING ("PID %d invalid ETT parsed %d length %d",
        section->pid, (gint) (data - section->data), section->section_length);
    goto error;
  }

  return (gpointer) ett;

error:
  _gst_dvrmpegts_atsc_ett_free (ett);

  return NULL;

}

/**
 * gst_mpegts_section_get_atsc_ett:
 * @section: a #GstMpegtsSection of type %GST_MPEGTS_SECTION_ATSC_ETT
 *
 * Returns the #GstDvrMpegtsAtscETT contained in the @section.
 *
 * Returns: The #GstDvrMpegtsAtscETT contained in the section, or %NULL if an error
 * happened.
 */
const GstDvrMpegtsAtscETT *
gst_mpegts_section_get_atsc_ett (GstMpegtsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_ATSC_ETT,
      NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed = __common_section_checks (section, 17, _parse_ett,
        (GDestroyNotify) _gst_dvrmpegts_atsc_ett_free);

  return (const GstDvrMpegtsAtscETT *) section->cached_parsed;
}

/* STT */

static GstDvrMpegtsAtscSTT *
_gst_dvrmpegts_atsc_stt_copy (GstDvrMpegtsAtscSTT * stt)
{
  GstDvrMpegtsAtscSTT *copy;

  copy = g_slice_dup (GstDvrMpegtsAtscSTT, stt);
  copy->descriptors = g_ptr_array_ref (stt->descriptors);

  return copy;
}

static void
_gst_dvrmpegts_atsc_stt_free (GstDvrMpegtsAtscSTT * stt)
{
  if (stt->descriptors)
    g_ptr_array_unref (stt->descriptors);
  if (stt->utc_datetime)
    gst_date_time_unref (stt->utc_datetime);

  g_slice_free (GstDvrMpegtsAtscSTT, stt);
}

G_DEFINE_BOXED_TYPE (GstDvrMpegtsAtscSTT, gst_dvrmpegts_atsc_stt,
    (GBoxedCopyFunc) _gst_dvrmpegts_atsc_stt_copy,
    (GFreeFunc) _gst_dvrmpegts_atsc_stt_free);

static gpointer
_parse_atsc_stt (GstMpegtsSection * section)
{
  GstDvrMpegtsAtscSTT *stt = NULL;
  guint8 *data, *end;
  guint16 daylight_saving;

  stt = g_slice_new0 (GstDvrMpegtsAtscSTT);

  data = section->data;
  end = data + section->section_length;

  /* Skip already parsed data */
  data += 8;

  stt->protocol_version = GST_READ_UINT8 (data);
  data += 1;
  stt->system_time = GST_READ_UINT32_BE (data);
  data += 4;
  stt->gps_utc_offset = GST_READ_UINT8 (data);
  data += 1;

  daylight_saving = GST_READ_UINT16_BE (data);
  data += 2;
  stt->ds_status = daylight_saving >> 15;
  stt->ds_dayofmonth = (daylight_saving >> 8) & 0x1F;
  stt->ds_hour = daylight_saving & 0xFF;

  stt->descriptors = gst_mpegts_parse_descriptors (data, end - data - 4);
  if (stt->descriptors == NULL)
    goto error;

  return (gpointer) stt;

error:
  _gst_dvrmpegts_atsc_stt_free (stt);

  return NULL;
}


/**
 * gst_mpegts_section_get_atsc_stt:
 * @section: a #GstMpegtsSection of type %GST_MPEGTS_SECTION_ATSC_STT
 *
 * Returns the #GstDvrMpegtsAtscSTT contained in the @section.
 *
 * Returns: The #GstDvrMpegtsAtscSTT contained in the section, or %NULL if an error
 * happened.
 */
const GstDvrMpegtsAtscSTT *
gst_mpegts_section_get_atsc_stt (GstMpegtsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_ATSC_STT,
      NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed =
        __common_section_checks (section, 20, _parse_atsc_stt,
        (GDestroyNotify) _gst_dvrmpegts_atsc_stt_free);

  return (const GstDvrMpegtsAtscSTT *) section->cached_parsed;
}

#define GPS_TO_UTC_TICKS G_GINT64_CONSTANT(315964800)
static GstDateTime *
_gst_mpegts_atsc_gps_time_to_datetime (guint32 systemtime, guint8 gps_offset)
{
  return gst_date_time_new_from_unix_epoch_utc (systemtime - gps_offset +
      GPS_TO_UTC_TICKS);
}

GstDateTime *
gst_mpegts_atsc_stt_get_datetime_utc (GstDvrMpegtsAtscSTT * stt)
{
  if (stt->utc_datetime == NULL)
    stt->utc_datetime = _gst_mpegts_atsc_gps_time_to_datetime (stt->system_time,
        stt->gps_utc_offset);

  if (stt->utc_datetime)
    return gst_date_time_ref (stt->utc_datetime);
  return NULL;
}
