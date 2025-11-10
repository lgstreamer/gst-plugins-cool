/*
 * gstmpegtssection.c -
 * Copyright (C) 2013 Edward Hervey
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 * Copyright (C) 2007 Alessandro Decina
 *               2010 Edward Hervey
 *  Author: Youness Alaoui <youness.alaoui@collabora.co.uk>, Collabora Ltd.
 *  Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 *  Author: Edward Hervey <bilboed@bilboed.com>, Collabora Ltd.
 *
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
 *   Zaheer Abbas Merali <zaheerabbas at merali dot org>
 *   Edward Hervey <edward@collabora.com>
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
 * SECTION:gst-dvb-section
 * @title: DVB variants of MPEG-TS sections
 * @short_description: Sections for the various DVB specifications
 * @include: gst/mpegts/mpegts.h
 *
 */


/*
 * TODO
 *
 * * Check minimum size for section parsing in the various 
 *   gst_mpegts_section_get_<tabld>() methods
 *
 * * Implement parsing code for
 *   * BAT
 *   * CAT
 *   * TSDT
 */

static inline GstDateTime *
_parse_utc_time (guint8 * data)
{
  guint year, month, day, hour, minute, second;
  guint16 mjd;
  guint8 *utc_ptr;

  mjd = GST_READ_UINT16_BE (data);

  if (mjd == G_MAXUINT16)
    return NULL;

  /* See EN 300 468 Annex C */
  year = (guint32) (((mjd - 15078.2) / 365.25));
  month = (guint8) ((mjd - 14956.1 - (guint) (year * 365.25)) / 30.6001);
  day = mjd - 14956 - (guint) (year * 365.25) - (guint) (month * 30.6001);
  if (month == 14 || month == 15) {
    year++;
    month = month - 1 - 12;
  } else {
    month--;
  }
  year += 1900;

  utc_ptr = data + 2;

  /* First digit of hours cannot exceed 1 (max: 23 hours) */
  hour = ((utc_ptr[0] & 0x30) >> 4) * 10 + (utc_ptr[0] & 0x0F);
  /* First digit of minutes cannot exced 5 (max: 59 mins) */
  minute = ((utc_ptr[1] & 0x70) >> 4) * 10 + (utc_ptr[1] & 0x0F);
  /* first digit of seconds cannot exceed 5 (max: 59 seconds) */
  second = ((utc_ptr[2] & 0x70) >> 4) * 10 + (utc_ptr[2] & 0x0F);

  /* Time is UTC */
  if (hour < 24 && minute < 60 && second < 60) {
    return gst_date_time_new (0.0, year, month, day, hour, minute,
        (gdouble) second);
  } else if (utc_ptr[0] == 0xFF && utc_ptr[1] == 0xFF && utc_ptr[2] == 0xFF) {
    return gst_date_time_new (0.0, year, month, day, -1, -1, -1);
  }

  return NULL;
}

/* Event Information Table */
static GstDvrMpegtsEITEvent *
_gst_dvrmpegts_eit_event_copy (GstDvrMpegtsEITEvent * eit)
{
  GstDvrMpegtsEITEvent *copy;

  copy = g_slice_dup (GstDvrMpegtsEITEvent, eit);
  copy->start_time = gst_date_time_ref (eit->start_time);
  copy->descriptors = g_ptr_array_ref (eit->descriptors);

  return copy;
}

static void
_gst_dvrmpegts_eit_event_free (GstDvrMpegtsEITEvent * eit)
{
  if (eit->start_time)
    gst_date_time_unref (eit->start_time);
  if (eit->descriptors)
    g_ptr_array_unref (eit->descriptors);
  g_slice_free (GstDvrMpegtsEITEvent, eit);
}

G_DEFINE_BOXED_TYPE (GstDvrMpegtsEITEvent, gst_dvrmpegts_eit_event,
    (GBoxedCopyFunc) _gst_dvrmpegts_eit_event_copy,
    (GFreeFunc) _gst_dvrmpegts_eit_event_free);

static GstDvrMpegtsEIT *
_gst_dvrmpegts_eit_copy (GstDvrMpegtsEIT * eit)
{
  GstDvrMpegtsEIT *copy;

  copy = g_slice_dup (GstDvrMpegtsEIT, eit);
  copy->events = g_ptr_array_ref (eit->events);

  return copy;
}

static void
_gst_dvrmpegts_eit_free (GstDvrMpegtsEIT * eit)
{
  g_ptr_array_unref (eit->events);
  g_slice_free (GstDvrMpegtsEIT, eit);
}

G_DEFINE_BOXED_TYPE (GstDvrMpegtsEIT, gst_dvrmpegts_eit,
    (GBoxedCopyFunc) _gst_dvrmpegts_eit_copy, (GFreeFunc) _gst_dvrmpegts_eit_free);

static gpointer
_parse_eit (GstMpegtsSection * section)
{
  GstDvrMpegtsEIT *eit = NULL;
  guint i = 0, allocated_events = 12;
  guint8 *data, *end, *duration_ptr;
  guint16 descriptors_loop_length;

  eit = g_slice_new0 (GstDvrMpegtsEIT);

  data = section->data;
  end = data + section->section_length;

  /* Skip already parsed data */
  data += 8;

  eit->transport_stream_id = GST_READ_UINT16_BE (data);
  data += 2;
  eit->original_network_id = GST_READ_UINT16_BE (data);
  data += 2;
  eit->segment_last_section_number = *data++;
  eit->last_table_id = *data++;

  eit->actual_stream = (section->table_id == 0x4E ||
      (section->table_id >= 0x50 && section->table_id <= 0x5F));
  eit->present_following = (section->table_id == 0x4E
      || section->table_id == 0x4F);

  eit->events =
      g_ptr_array_new_full (allocated_events,
      (GDestroyNotify) _gst_dvrmpegts_eit_event_free);

  while (data < end - 4) {
    GstDvrMpegtsEITEvent *event;

    /* 12 is the minimum entry size + CRC */
    if (end - data < 12 + 4) {
      GST_WARNING ("PID %d invalid EIT entry length %d",
          section->pid, (gint) (end - 4 - data));
      goto error;
    }

    event = g_slice_new0 (GstDvrMpegtsEITEvent);
    g_ptr_array_add (eit->events, event);

    event->event_id = GST_READ_UINT16_BE (data);
    data += 2;

    event->start_time = _parse_utc_time (data);
    duration_ptr = data + 5;
    event->duration = (((duration_ptr[0] & 0xF0) >> 4) * 10 +
        (duration_ptr[0] & 0x0F)) * 60 * 60 +
        (((duration_ptr[1] & 0xF0) >> 4) * 10 +
        (duration_ptr[1] & 0x0F)) * 60 +
        ((duration_ptr[2] & 0xF0) >> 4) * 10 + (duration_ptr[2] & 0x0F);

    data += 8;
    event->running_status = *data >> 5;
    event->free_CA_mode = (*data >> 4) & 0x01;

    descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x0FFF;
    data += 2;

    event->descriptors =
        gst_mpegts_parse_descriptors (data, descriptors_loop_length);
    if (event->descriptors == NULL)
      goto error;
    data += descriptors_loop_length;

    i += 1;
  }

  if (data != end - 4) {
    GST_WARNING ("PID %d invalid EIT parsed %d length %d",
        section->pid, (gint) (data - section->data), section->section_length);
    goto error;
  }

  return (gpointer) eit;

error:
  if (eit)
    _gst_dvrmpegts_eit_free (eit);

  return NULL;

}

/**
 * gst_mpegts_section_get_eit:
 * @section: a #GstMpegtsSection of type %GST_MPEGTS_SECTION_EIT
 *
 * Returns the #GstDvrMpegtsEIT contained in the @section.
 *
 * Returns: The #GstDvrMpegtsEIT contained in the section, or %NULL if an error
 * happened.
 */
const GstDvrMpegtsEIT *
gst_mpegts_section_get_eit (GstMpegtsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_EIT, NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed = __common_section_checks (section, 18, _parse_eit,
        (GDestroyNotify) _gst_dvrmpegts_eit_free);

  return (const GstDvrMpegtsEIT *) section->cached_parsed;
}

/* Bouquet Association Table */
static GstDvrMpegtsBATStream *
_gst_dvrmpegts_bat_stream_copy (GstDvrMpegtsBATStream * bat)
{
  GstDvrMpegtsBATStream *copy;

  copy = g_slice_dup (GstDvrMpegtsBATStream, bat);
  copy->descriptors = g_ptr_array_ref (bat->descriptors);

  return copy;
}

static void
_gst_dvrmpegts_bat_stream_free (GstDvrMpegtsBATStream * bat)
{
  if (bat->descriptors)
    g_ptr_array_unref (bat->descriptors);
  g_slice_free (GstDvrMpegtsBATStream, bat);
}

G_DEFINE_BOXED_TYPE (GstDvrMpegtsBATStream, gst_dvrmpegts_bat_stream,
    (GBoxedCopyFunc) _gst_dvrmpegts_bat_stream_copy,
    (GFreeFunc) _gst_dvrmpegts_bat_stream_free);

static GstDvrMpegtsBAT *
_gst_dvrmpegts_bat_copy (GstDvrMpegtsBAT * bat)
{
  GstDvrMpegtsBAT *copy;

  copy = g_slice_dup (GstDvrMpegtsBAT, bat);
  copy->descriptors = g_ptr_array_ref (bat->descriptors);
  copy->streams = g_ptr_array_ref (bat->streams);

  return copy;
}

static void
_gst_dvrmpegts_bat_free (GstDvrMpegtsBAT * bat)
{
  if (bat->descriptors)
    g_ptr_array_unref (bat->descriptors);
  if (bat->streams)
    g_ptr_array_unref (bat->streams);
  g_slice_free (GstDvrMpegtsBAT, bat);
}

G_DEFINE_BOXED_TYPE (GstDvrMpegtsBAT, gst_dvrmpegts_bat,
    (GBoxedCopyFunc) _gst_dvrmpegts_bat_copy, (GFreeFunc) _gst_dvrmpegts_bat_free);

static gpointer
_parse_bat (GstMpegtsSection * section)
{
  GstDvrMpegtsBAT *bat = NULL;
  guint i = 0, allocated_streams = 12;
  guint8 *data, *end, *entry_begin;
  guint16 descriptors_loop_length, transport_stream_loop_length;

  GST_DEBUG ("BAT");

  bat = g_slice_new0 (GstDvrMpegtsBAT);

  data = section->data;
  end = data + section->section_length;

  /* Skip already parsed data */
  data += 8;

  descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;

  /* see if the buffer is large enough */
  if (descriptors_loop_length && (data + descriptors_loop_length > end - 4)) {
    GST_WARNING ("PID %d invalid BAT descriptors loop length %d",
        section->pid, descriptors_loop_length);
    goto error;
  }
  bat->descriptors =
      gst_mpegts_parse_descriptors (data, descriptors_loop_length);
  if (bat->descriptors == NULL)
    goto error;
  data += descriptors_loop_length;

  transport_stream_loop_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;
  if (G_UNLIKELY (transport_stream_loop_length > (end - 4 - data))) {
    GST_WARNING
        ("PID 0x%04x invalid BAT (transport_stream_loop_length too big)",
        section->pid);
    goto error;
  }

  bat->streams =
      g_ptr_array_new_full (allocated_streams,
      (GDestroyNotify) _gst_dvrmpegts_bat_stream_free);

  /* read up to the CRC */
  while (transport_stream_loop_length - 4 > 0) {
    GstDvrMpegtsBATStream *stream = g_slice_new0 (GstDvrMpegtsBATStream);

    g_ptr_array_add (bat->streams, stream);

    if (transport_stream_loop_length < 6) {
      /* each entry must be at least 6 bytes (+ 4 bytes CRC) */
      GST_WARNING ("PID %d invalid BAT entry size %d",
          section->pid, transport_stream_loop_length);
      goto error;
    }

    entry_begin = data;

    stream->transport_stream_id = GST_READ_UINT16_BE (data);
    data += 2;

    stream->original_network_id = GST_READ_UINT16_BE (data);
    data += 2;

    descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x0FFF;
    data += 2;

    GST_DEBUG ("descriptors_loop_length %d", descriptors_loop_length);

    if (descriptors_loop_length && (data + descriptors_loop_length > end - 4)) {
      GST_WARNING
          ("PID %d invalid BAT entry %d descriptors loop length %d (only have %"
          G_GSIZE_FORMAT ")", section->pid, section->subtable_extension,
          descriptors_loop_length, (gsize) (end - 4 - data));
      goto error;
    }
    stream->descriptors =
        gst_mpegts_parse_descriptors (data, descriptors_loop_length);
    if (stream->descriptors == NULL)
      goto error;

    data += descriptors_loop_length;

    i += 1;
    transport_stream_loop_length -= data - entry_begin;
  }

  if (data != end - 4) {
    GST_WARNING ("PID %d invalid BAT parsed %d length %d",
        section->pid, (gint) (data - section->data), section->section_length);
    goto error;
  }

  return (gpointer) bat;

error:
  if (bat)
    _gst_dvrmpegts_bat_free (bat);

  return NULL;
}

/**
 * gst_mpegts_section_get_bat:
 * @section: a #GstMpegtsSection of type %GST_MPEGTS_SECTION_BAT
 *
 * Returns the #GstDvrMpegtsBAT contained in the @section.
 *
 * Returns: The #GstDvrMpegtsBAT contained in the section, or %NULL if an error
 * happened.
 */
const GstDvrMpegtsBAT *
gst_mpegts_section_get_bat (GstMpegtsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_BAT, NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed =
        __common_section_checks (section, 16, _parse_bat,
        (GDestroyNotify) _gst_dvrmpegts_bat_free);

  return (const GstDvrMpegtsBAT *) section->cached_parsed;
}


/* Network Information Table */

static GstDvrMpegtsNITStream *
_gst_dvrmpegts_nit_stream_copy (GstDvrMpegtsNITStream * nit)
{
  GstDvrMpegtsNITStream *copy;

  copy = g_slice_dup (GstDvrMpegtsNITStream, nit);
  copy->descriptors = g_ptr_array_ref (nit->descriptors);

  return copy;
}

static void
_gst_dvrmpegts_nit_stream_free (GstDvrMpegtsNITStream * nit)
{
  if (nit->descriptors)
    g_ptr_array_unref (nit->descriptors);
  g_slice_free (GstDvrMpegtsNITStream, nit);
}

G_DEFINE_BOXED_TYPE (GstDvrMpegtsNITStream, gst_dvrmpegts_nit_stream,
    (GBoxedCopyFunc) _gst_dvrmpegts_nit_stream_copy,
    (GFreeFunc) _gst_dvrmpegts_nit_stream_free);

static GstDvrMpegtsNIT *
_gst_dvrmpegts_nit_copy (GstDvrMpegtsNIT * nit)
{
  GstDvrMpegtsNIT *copy = g_slice_dup (GstDvrMpegtsNIT, nit);

  copy->descriptors = g_ptr_array_ref (nit->descriptors);
  copy->streams = g_ptr_array_ref (nit->streams);

  return copy;
}

static void
_gst_dvrmpegts_nit_free (GstDvrMpegtsNIT * nit)
{
  if (nit->descriptors)
    g_ptr_array_unref (nit->descriptors);
  g_ptr_array_unref (nit->streams);
  g_slice_free (GstDvrMpegtsNIT, nit);
}

G_DEFINE_BOXED_TYPE (GstDvrMpegtsNIT, gst_dvrmpegts_nit,
    (GBoxedCopyFunc) _gst_dvrmpegts_nit_copy, (GFreeFunc) _gst_dvrmpegts_nit_free);


static gpointer
_parse_nit (GstMpegtsSection * section)
{
  GstDvrMpegtsNIT *nit = NULL;
  guint i = 0, allocated_streams = 12;
  guint8 *data, *end, *entry_begin;
  guint16 descriptors_loop_length, transport_stream_loop_length;

  GST_DEBUG ("NIT");

  nit = g_slice_new0 (GstDvrMpegtsNIT);

  data = section->data;
  end = data + section->section_length;

  /* Set network id, and skip the rest of what is already parsed */
  nit->network_id = section->subtable_extension;
  data += 8;

  nit->actual_network = section->table_id == 0x40;

  descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;

  /* see if the buffer is large enough */
  if (descriptors_loop_length && (data + descriptors_loop_length > end - 4)) {
    GST_WARNING ("PID %d invalid NIT descriptors loop length %d",
        section->pid, descriptors_loop_length);
    goto error;
  }
  nit->descriptors =
      gst_mpegts_parse_descriptors (data, descriptors_loop_length);
  if (nit->descriptors == NULL)
    goto error;
  data += descriptors_loop_length;

  transport_stream_loop_length = GST_READ_UINT16_BE (data) & 0x0FFF;
  data += 2;
  if (G_UNLIKELY (transport_stream_loop_length > (end - 4 - data))) {
    GST_WARNING
        ("PID 0x%04x invalid NIT (transport_stream_loop_length too big)",
        section->pid);
    goto error;
  }

  nit->streams =
      g_ptr_array_new_full (allocated_streams,
      (GDestroyNotify) _gst_dvrmpegts_nit_stream_free);

  /* read up to the CRC */
  while (transport_stream_loop_length - 4 > 0) {
    GstDvrMpegtsNITStream *stream = g_slice_new0 (GstDvrMpegtsNITStream);

    g_ptr_array_add (nit->streams, stream);

    if (transport_stream_loop_length < 6) {
      /* each entry must be at least 6 bytes (+ 4 bytes CRC) */
      GST_WARNING ("PID %d invalid NIT entry size %d",
          section->pid, transport_stream_loop_length);
      goto error;
    }

    entry_begin = data;

    stream->transport_stream_id = GST_READ_UINT16_BE (data);
    data += 2;

    stream->original_network_id = GST_READ_UINT16_BE (data);
    data += 2;

    descriptors_loop_length = GST_READ_UINT16_BE (data) & 0x0FFF;
    data += 2;

    GST_DEBUG ("descriptors_loop_length %d", descriptors_loop_length);

    if (descriptors_loop_length && (data + descriptors_loop_length > end - 4)) {
      GST_WARNING
          ("PID %d invalid NIT entry %d descriptors loop length %d (only have %"
          G_GSIZE_FORMAT ")", section->pid, section->subtable_extension,
          descriptors_loop_length, (gsize) (end - 4 - data));
      goto error;
    }
    stream->descriptors =
        gst_mpegts_parse_descriptors (data, descriptors_loop_length);
    if (stream->descriptors == NULL)
      goto error;

    data += descriptors_loop_length;

    i += 1;
    transport_stream_loop_length -= data - entry_begin;
  }

  if (data != end - 4) {
    GST_WARNING ("PID %d invalid NIT parsed %d length %d",
        section->pid, (gint) (data - section->data), section->section_length);
    goto error;
  }

  return (gpointer) nit;

error:
  if (nit)
    _gst_dvrmpegts_nit_free (nit);

  return NULL;
}

/**
 * gst_mpegts_section_get_nit:
 * @section: a #GstMpegtsSection of type %GST_MPEGTS_SECTION_NIT
 *
 * Returns the #GstDvrMpegtsNIT contained in the @section.
 *
 * Returns: The #GstDvrMpegtsNIT contained in the section, or %NULL if an error
 * happened.
 */
const GstDvrMpegtsNIT *
gst_mpegts_section_get_nit (GstMpegtsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_NIT, NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed =
        __common_section_checks (section, 16, _parse_nit,
        (GDestroyNotify) _gst_dvrmpegts_nit_free);

  return (const GstDvrMpegtsNIT *) section->cached_parsed;
}

/**
 * gst_mpegts_nit_new:
 *
 * Allocates and initializes a #GstDvrMpegtsNIT.
 *
 * Returns: (transfer full): A newly allocated #GstDvrMpegtsNIT
 */
GstDvrMpegtsNIT *
gst_mpegts_nit_new (void)
{
  GstDvrMpegtsNIT *nit;

  nit = g_slice_new0 (GstDvrMpegtsNIT);

  nit->descriptors = g_ptr_array_new_with_free_func ((GDestroyNotify)
      gst_dvrmpegts_descriptor_free);
  nit->streams = g_ptr_array_new_with_free_func ((GDestroyNotify)
      _gst_dvrmpegts_nit_stream_free);

  return nit;
}

/**
 * gst_mpegts_nit_stream_new:
 *
 * Allocates and initializes a #GstDvrMpegtsNITStream
 *
 * Returns: (transfer full): A newly allocated #GstDvrMpegtsNITStream
 */
GstDvrMpegtsNITStream *
gst_mpegts_nit_stream_new (void)
{
  GstDvrMpegtsNITStream *stream;

  stream = g_slice_new0 (GstDvrMpegtsNITStream);

  stream->descriptors = g_ptr_array_new_with_free_func (
      (GDestroyNotify) gst_dvrmpegts_descriptor_free);

  return stream;
}

static gboolean
_packetize_nit (GstMpegtsSection * section)
{
  gsize length, network_length, loop_length;
  const GstDvrMpegtsNIT *nit;
  GstDvrMpegtsNITStream *stream;
  GstDvrMpegtsDescriptor *descriptor;
  guint i, j;
  guint8 *data, *pos;

  nit = gst_mpegts_section_get_nit (section);

  if (nit == NULL)
    return FALSE;

  /* 8 byte common section fields
     2 byte network_descriptors_length
     2 byte transport_stream_loop_length
     4 byte CRC */
  length = 16;

  /* Find length of network descriptors */
  network_length = 0;
  if (nit->descriptors) {
    for (i = 0; i < nit->descriptors->len; i++) {
      descriptor = g_ptr_array_index (nit->descriptors, i);
      network_length += descriptor->length + 2;
    }
  }

  /* Find length of loop */
  loop_length = 0;
  if (nit->streams) {
    for (i = 0; i < nit->streams->len; i++) {
      stream = g_ptr_array_index (nit->streams, i);
      loop_length += 6;
      if (stream->descriptors) {
        for (j = 0; j < stream->descriptors->len; j++) {
          descriptor = g_ptr_array_index (stream->descriptors, j);
          loop_length += descriptor->length + 2;
        }
      }
    }
  }

  length += network_length + loop_length;

  /* Max length of NIT section is 1024 bytes */
  g_return_val_if_fail (length <= 1024, FALSE);

  _packetize_common_section (section, length);

  data = section->data + 8;
  /* reserved                         - 4  bit
     network_descriptors_length       - 12 bit uimsbf */
  GST_WRITE_UINT16_BE (data, network_length | 0xF000);
  data += 2;

  _packetize_descriptor_array (nit->descriptors, &data);

  /* reserved                         - 4  bit
     transport_stream_loop_length     - 12 bit uimsbf */
  GST_WRITE_UINT16_BE (data, loop_length | 0xF000);
  data += 2;

  if (nit->streams) {
    for (i = 0; i < nit->streams->len; i++) {
      stream = g_ptr_array_index (nit->streams, i);
      /* transport_stream_id          - 16 bit uimsbf */
      GST_WRITE_UINT16_BE (data, stream->transport_stream_id);
      data += 2;

      /* original_network_id          - 16 bit uimsbf */
      GST_WRITE_UINT16_BE (data, stream->original_network_id);
      data += 2;

      /* reserved                     -  4 bit
         transport_descriptors_length - 12 bit uimsbf

         Set length to zero, and update in loop */
      pos = data;
      *data++ = 0xF0;
      *data++ = 0x00;

      _packetize_descriptor_array (stream->descriptors, &data);

      /* Go back and update the descriptor length */
      GST_WRITE_UINT16_BE (pos, (data - pos - 2) | 0xF000);
    }
  }

  return TRUE;
}

/**
 * gst_mpegts_section_from_nit:
 * @nit: (transfer full): a #GstDvrMpegtsNIT to create the #GstMpegtsSection from
 *
 * Ownership of @nit is taken. The data in @nit is managed by the #GstMpegtsSection
 *
 * Returns: (transfer full): the #GstMpegtsSection
 */
GstMpegtsSection *
gst_mpegts_section_from_nit (GstDvrMpegtsNIT * nit)
{
  GstMpegtsSection *section;

  g_return_val_if_fail (nit != NULL, NULL);

  if (nit->actual_network)
    section = _gst_mpegts_section_init (0x10,
        GST_MTS_TABLE_ID_NETWORK_INFORMATION_ACTUAL_NETWORK);
  else
    section = _gst_mpegts_section_init (0x10,
        GST_MTS_TABLE_ID_NETWORK_INFORMATION_OTHER_NETWORK);

  section->subtable_extension = nit->network_id;
  section->cached_parsed = (gpointer) nit;
  section->packetizer = _packetize_nit;
  section->destroy_parsed = (GDestroyNotify) _gst_dvrmpegts_nit_free;

  return section;
}

/* Service Description Table (SDT) */

static GstDvrMpegtsSDTService *
_gst_dvrmpegts_sdt_service_copy (GstDvrMpegtsSDTService * sdt)
{
  GstDvrMpegtsSDTService *copy = g_slice_dup (GstDvrMpegtsSDTService, sdt);

  copy->descriptors = g_ptr_array_ref (sdt->descriptors);

  return copy;
}

static void
_gst_dvrmpegts_sdt_service_free (GstDvrMpegtsSDTService * sdt)
{
  if (sdt->descriptors)
    g_ptr_array_unref (sdt->descriptors);
  g_slice_free (GstDvrMpegtsSDTService, sdt);
}

G_DEFINE_BOXED_TYPE (GstDvrMpegtsSDTService, gst_dvrmpegts_sdt_service,
    (GBoxedCopyFunc) _gst_dvrmpegts_sdt_service_copy,
    (GFreeFunc) _gst_dvrmpegts_sdt_service_free);

static GstDvrMpegtsSDT *
_gst_dvrmpegts_sdt_copy (GstDvrMpegtsSDT * sdt)
{
  GstDvrMpegtsSDT *copy = g_slice_dup (GstDvrMpegtsSDT, sdt);

  copy->services = g_ptr_array_ref (sdt->services);

  return copy;
}

static void
_gst_dvrmpegts_sdt_free (GstDvrMpegtsSDT * sdt)
{
  g_ptr_array_unref (sdt->services);
  g_slice_free (GstDvrMpegtsSDT, sdt);
}

G_DEFINE_BOXED_TYPE (GstDvrMpegtsSDT, gst_dvrmpegts_sdt,
    (GBoxedCopyFunc) _gst_dvrmpegts_sdt_copy, (GFreeFunc) _gst_dvrmpegts_sdt_free);


static gpointer
_parse_sdt (GstMpegtsSection * section)
{
  GstDvrMpegtsSDT *sdt = NULL;
  guint i = 0, allocated_services = 8;
  guint8 *data, *end, *entry_begin;
  guint tmp;
  guint sdt_info_length;
  guint descriptors_loop_length;

  GST_DEBUG ("SDT");

  sdt = g_slice_new0 (GstDvrMpegtsSDT);

  data = section->data;
  end = data + section->section_length;

  sdt->transport_stream_id = section->subtable_extension;

  /* Skip common fields */
  data += 8;

  sdt->original_network_id = GST_READ_UINT16_BE (data);
  data += 2;

  /* skip reserved byte */
  data += 1;

  sdt->actual_ts = section->table_id == 0x42;

  sdt_info_length = section->section_length - 11;

  sdt->services = g_ptr_array_new_full (allocated_services,
      (GDestroyNotify) _gst_dvrmpegts_sdt_service_free);

  /* read up to the CRC */
  while (sdt_info_length - 4 > 0) {
    GstDvrMpegtsSDTService *service = g_slice_new0 (GstDvrMpegtsSDTService);
    g_ptr_array_add (sdt->services, service);

    entry_begin = data;

    if (sdt_info_length - 4 < 5) {
      /* each entry must be at least 5 bytes (+4 bytes for the CRC) */
      GST_WARNING ("PID %d invalid SDT entry size %d",
          section->pid, sdt_info_length);
      goto error;
    }

    service->service_id = GST_READ_UINT16_BE (data);
    data += 2;

    service->EIT_schedule_flag = ((*data & 0x02) == 2);
    service->EIT_present_following_flag = (*data & 0x01) == 1;

    data += 1;
    tmp = GST_READ_UINT16_BE (data);

    service->running_status = (*data >> 5) & 0x07;
    service->free_CA_mode = (*data >> 4) & 0x01;

    descriptors_loop_length = tmp & 0x0FFF;
    data += 2;

    if (descriptors_loop_length && (data + descriptors_loop_length > end - 4)) {
      GST_WARNING ("PID %d invalid SDT entry %d descriptors loop length %d",
          section->pid, service->service_id, descriptors_loop_length);
      goto error;
    }
    service->descriptors =
        gst_mpegts_parse_descriptors (data, descriptors_loop_length);
    if (!service->descriptors)
      goto error;
    data += descriptors_loop_length;

    sdt_info_length -= data - entry_begin;
    i += 1;
  }

  if (data != end - 4) {
    GST_WARNING ("PID %d invalid SDT parsed %d length %d",
        section->pid, (gint) (data - section->data), section->section_length);
    goto error;
  }

  return sdt;

error:
  if (sdt)
    _gst_dvrmpegts_sdt_free (sdt);

  return NULL;
}

/**
 * gst_mpegts_section_get_sdt:
 * @section: a #GstMpegtsSection of type %GST_MPEGTS_SECTION_SDT
 *
 * Returns the #GstDvrMpegtsSDT contained in the @section.
 *
 * Returns: The #GstDvrMpegtsSDT contained in the section, or %NULL if an error
 * happened.
 */
const GstDvrMpegtsSDT *
gst_mpegts_section_get_sdt (GstMpegtsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_SDT, NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed =
        __common_section_checks (section, 15, _parse_sdt,
        (GDestroyNotify) _gst_dvrmpegts_sdt_free);

  return (const GstDvrMpegtsSDT *) section->cached_parsed;
}

/**
 * gst_mpegts_sdt_new:
 *
 * Allocates and initializes a #GstDvrMpegtsSDT.
 *
 * Returns: (transfer full): A newly allocated #GstDvrMpegtsSDT
 */
GstDvrMpegtsSDT *
gst_mpegts_sdt_new (void)
{
  GstDvrMpegtsSDT *sdt;

  sdt = g_slice_new0 (GstDvrMpegtsSDT);

  sdt->services = g_ptr_array_new_with_free_func ((GDestroyNotify)
      _gst_dvrmpegts_sdt_service_free);

  return sdt;
}

/**
 * gst_mpegts_sdt_service_new:
 *
 * Allocates and initializes a #GstDvrMpegtsSDTService.
 *
 * Returns: (transfer full): A newly allocated #GstDvrMpegtsSDTService
 */
GstDvrMpegtsSDTService *
gst_mpegts_sdt_service_new (void)
{
  GstDvrMpegtsSDTService *service;

  service = g_slice_new0 (GstDvrMpegtsSDTService);

  service->descriptors = g_ptr_array_new_with_free_func ((GDestroyNotify)
      gst_dvrmpegts_descriptor_free);

  return service;
}

static gboolean
_packetize_sdt (GstMpegtsSection * section)
{
  gsize length, service_length;
  const GstDvrMpegtsSDT *sdt;
  GstDvrMpegtsSDTService *service;
  GstDvrMpegtsDescriptor *descriptor;
  guint i, j;
  guint8 *data, *pos;

  sdt = gst_mpegts_section_get_sdt (section);

  if (sdt == NULL)
    return FALSE;

  /* 8 byte common section fields
     2 byte original_network_id
     1 byte reserved
     4 byte CRC */
  length = 15;

  /* Find length of services */
  service_length = 0;
  if (sdt->services) {
    for (i = 0; i < sdt->services->len; i++) {
      service = g_ptr_array_index (sdt->services, i);
      service_length += 5;
      if (service->descriptors) {
        for (j = 0; j < service->descriptors->len; j++) {
          descriptor = g_ptr_array_index (service->descriptors, j);
          service_length += descriptor->length + 2;
        }
      }
    }
  }

  length += service_length;

  /* Max length if SDT section is 1024 bytes */
  g_return_val_if_fail (length <= 1024, FALSE);

  _packetize_common_section (section, length);

  data = section->data + 8;
  /* original_network_id            - 16 bit uimsbf */
  GST_WRITE_UINT16_BE (data, sdt->original_network_id);
  data += 2;
  /* reserved                       -  8 bit */
  *data++ = 0xFF;

  if (sdt->services) {
    for (i = 0; i < sdt->services->len; i++) {
      service = g_ptr_array_index (sdt->services, i);
      /* service_id                 - 16 bit uimsbf */
      GST_WRITE_UINT16_BE (data, service->service_id);
      data += 2;

      /* reserved                   -  6 bit
         EIT_schedule_flag          -  1 bit
         EIT_present_following_flag -  1 bit */
      *data = 0xFC;
      if (service->EIT_schedule_flag)
        *data |= 0x02;
      if (service->EIT_present_following_flag)
        *data |= 0x01;
      data++;

      /* running_status             -  3 bit uimsbf
         free_CA_mode               -  1 bit
         descriptors_loop_length    - 12 bit uimsbf */
      /* Set length to zero for now */
      pos = data;
      *data++ = 0x00;
      *data++ = 0x00;

      _packetize_descriptor_array (service->descriptors, &data);

      /* Go back and update the descriptor length */
      GST_WRITE_UINT16_BE (pos, data - pos - 2);

      *pos |= service->running_status << 5;
      if (service->free_CA_mode)
        *pos |= 0x10;
    }
  }

  return TRUE;
}

/**
 * gst_mpegts_section_from_sdt:
 * @sdt: (transfer full): a #GstDvrMpegtsSDT to create the #GstMpegtsSection from
 *
 * Ownership of @sdt is taken. The data in @sdt is managed by the #GstMpegtsSection
 *
 * Returns: (transfer full): the #GstMpegtsSection
 */
GstMpegtsSection *
gst_mpegts_section_from_sdt (GstDvrMpegtsSDT * sdt)
{
  GstMpegtsSection *section;

  g_return_val_if_fail (sdt != NULL, NULL);

  if (sdt->actual_ts)
    section = _gst_mpegts_section_init (0x11,
        GST_MTS_TABLE_ID_SERVICE_DESCRIPTION_ACTUAL_TS);
  else
    section = _gst_mpegts_section_init (0x11,
        GST_MTS_TABLE_ID_SERVICE_DESCRIPTION_OTHER_TS);

  section->subtable_extension = sdt->transport_stream_id;
  section->cached_parsed = (gpointer) sdt;
  section->packetizer = _packetize_sdt;
  section->destroy_parsed = (GDestroyNotify) _gst_dvrmpegts_sdt_free;

  return section;
}

/* Time and Date Table (TDT) */
static gpointer
_parse_tdt (GstMpegtsSection * section)
{
  return (gpointer) _parse_utc_time (section->data + 3);
}

/**
 * gst_mpegts_section_get_tdt:
 * @section: a #GstMpegtsSection of type %GST_MPEGTS_SECTION_TDT
 *
 * Returns the #GstDateTime of the TDT
 *
 * Returns: The #GstDateTime contained in the section, or %NULL
 * if an error happened. Release with #gst_date_time_unref when done.
 */
GstDateTime *
gst_mpegts_section_get_tdt (GstMpegtsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_TDT, NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed =
        __common_section_checks (section, 8, _parse_tdt,
        (GDestroyNotify) gst_date_time_unref);

  if (section->cached_parsed)
    return gst_date_time_ref ((GstDateTime *) section->cached_parsed);
  return NULL;
}


/* Time Offset Table (TOT) */
static GstDvrMpegtsTOT *
_gst_dvrmpegts_tot_copy (GstDvrMpegtsTOT * tot)
{
  GstDvrMpegtsTOT *copy = g_slice_dup (GstDvrMpegtsTOT, tot);

  if (tot->utc_time)
    copy->utc_time = gst_date_time_ref (tot->utc_time);
  copy->descriptors = g_ptr_array_ref (tot->descriptors);

  return copy;
}

static void
_gst_dvrmpegts_tot_free (GstDvrMpegtsTOT * tot)
{
  if (tot->utc_time)
    gst_date_time_unref (tot->utc_time);
  if (tot->descriptors)
    g_ptr_array_unref (tot->descriptors);
  g_slice_free (GstDvrMpegtsTOT, tot);
}

G_DEFINE_BOXED_TYPE (GstDvrMpegtsTOT, gst_dvrmpegts_tot,
    (GBoxedCopyFunc) _gst_dvrmpegts_tot_copy, (GFreeFunc) _gst_dvrmpegts_tot_free);

static gpointer
_parse_tot (GstMpegtsSection * section)
{
  guint8 *data;
  GstDvrMpegtsTOT *tot;
  guint16 desc_len;

  GST_DEBUG ("TOT");

  tot = g_slice_new0 (GstDvrMpegtsTOT);

  tot->utc_time = _parse_utc_time (section->data + 3);

  /* Skip 5 bytes from utc_time (+3 of initial offset) */
  data = section->data + 8;

  desc_len = GST_READ_UINT16_BE (data) & 0xFFF;
  data += 2;
  tot->descriptors = gst_mpegts_parse_descriptors (data, desc_len);

  return (gpointer) tot;
}

/**
 * gst_mpegts_section_get_tot:
 * @section: a #GstMpegtsSection of type %GST_MPEGTS_SECTION_TOT
 *
 * Returns the #GstDvrMpegtsTOT contained in the @section.
 *
 * Returns: The #GstDvrMpegtsTOT contained in the section, or %NULL if an error
 * happened.
 */
const GstDvrMpegtsTOT *
gst_mpegts_section_get_tot (GstMpegtsSection * section)
{
  g_return_val_if_fail (section->section_type == GST_MPEGTS_SECTION_TOT, NULL);
  g_return_val_if_fail (section->cached_parsed || section->data, NULL);

  if (!section->cached_parsed)
    section->cached_parsed =
        __common_section_checks (section, 14, _parse_tot,
        (GDestroyNotify) _gst_dvrmpegts_tot_free);

  return (const GstDvrMpegtsTOT *) section->cached_parsed;
}
