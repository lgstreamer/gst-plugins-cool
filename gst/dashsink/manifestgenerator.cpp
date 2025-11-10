/*
 * GStreamer dashsink element
 *
 * Copyright 2017 LG Electronics, Inc.
 *  @author: Seoungil Kang <seoungil.kang@lge.com>
 *
 * manifestgenerator.cpp:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */
#include "manifestgenerator.hpp"

GST_DEBUG_CATEGORY_EXTERN (dashsink_debug);
#define GST_CAT_DEFAULT dashsink_debug
#define SEC(t)  ((gint64) ((((GstClockTime)(t)) / GST_SECOND)))
#define MSEC(t) ((gint64) ((((GstClockTime)(t)) / GST_MSECOND)))
static guint _getTimeVal (const char *val)
{
  char *ptr = NULL;
  return strtoul(val+2, &ptr, 10);
}

static guint _getNumberVal (const char *val)
{
  char *ptr = NULL;
  return strtoul(val, &ptr, 10);
}

ManifestGenerator::ManifestGenerator (void)
  : mpd_(NULL), durationCurrentPeriod_(0), durationOverallPeriods_(0), presentationTimeOffset_(0)
{
}

ManifestGenerator::~ManifestGenerator (void)
{
  if (mpd_ != NULL) {
    XmlElementFree (mpd_);
    mpd_ = NULL;
  }
}

bool ManifestGenerator::loadManifest (std::string location)
{
  bool result = false;
  do {
    if (mpd_ != NULL) XmlElementFree (mpd_);
    if ((mpd_ = XmlElementLoad (location)) == NULL) break;
    durationOverallPeriods_ = _getTimeVal (XmlElementAttrGet (mpd_, "mediaPresentationDuration").c_str ());
    location_ = location;
    result = true;
  } while (0);
  return result;
}

bool ManifestGenerator::storeManifest (void)
{
  bool result = false;
  XmlElement * period = NULL;
  std::string durationOverall = "PT0S";
  std::string durationCurrent = "PT0S";
  std::string dump = "";

  do {
    if (location_ == "") break;
    if (mpd_ == NULL) break;
    if (durationCurrentPeriod_ == 0) break;
    if ((period = XmlElementElemsGetLastOne (mpd_, "Period")) == NULL) break;
    durationOverall = string_format ("PT%" G_GINT64_FORMAT "S", durationOverallPeriods_ + durationCurrentPeriod_);
    if (!XmlElementAttrSet (mpd_, "mediaPresentationDuration", durationOverall)) break;
    durationCurrent = string_format ("PT%" G_GINT64_FORMAT "S", durationCurrentPeriod_);
    if (!XmlElementAttrSet (period, "duration", durationCurrent)) break;
    if ((dump = XmlElementDump (mpd_)) == "") break;
    g_file_set_contents (location_.c_str (), dump.c_str (), -1, NULL);
    result = true;
  } while (0);
  return result;
}

bool ManifestGenerator::updateCurrentDuration (gint64 curRunningTime)
{
  if (durationCurrentPeriod_ != SEC (curRunningTime)) {
    durationCurrentPeriod_ = SEC (curRunningTime);
  }
  return true;
}


gint64 ManifestGenerator::getOverallDuration (void)
{
  return (durationOverallPeriods_ + durationCurrentPeriod_) * 1000;
}

int ManifestGenerator::getCurrentPeriodId (void)
{
  int result = -1;
  XmlElement * period = NULL;
  std::string period_id = "";

  do {
    if (mpd_ == NULL) break;
    if ((period = XmlElementElemsGetLastOne (mpd_, "Period")) == NULL) break;
    if ((period_id = XmlElementAttrGet (period, "id")) == "") break;
    result = _getNumberVal (period_id.c_str ());
  } while (0);
  return result;
}

int ManifestGenerator::addPeriod (void)
{
  int result = -1;
  XmlElement * period = NULL;
  std::string period_id = "";
  do {
    if (mpd_ == NULL) break;
    if (!XmlElementElemsSet (mpd_, "Period")) break;
    if ((period = XmlElementElemsGetLastOne (mpd_, "Period")) == NULL) break;
    if ((period_id = XmlElementAttrGet (period, "id")) == "") break;
    if (!XmlElementElemsSet (period, "EventStream")) break;
    result = _getNumberVal (period_id.c_str ());
  } while (0);
  return result;
}

int ManifestGenerator::addEvent (void)
{
  int result = -1;
  int time_scale = 0;
  XmlElement * period = NULL;
  XmlElement * event_stream = NULL;
  XmlElement * event = NULL;
  std::string period_id = "";
  std::string timescale = "";
  std::string event_id = "";
  std::string presentation_time = "";

  do {
    if (mpd_ == NULL) break;
    if ((period = XmlElementElemsGetLastOne (mpd_, "Period")) == NULL) break;
    if ((event_stream = XmlElementElemsGetLastOne (period, "EventStream")) == NULL) break;
    if (!XmlElementElemsSet (event_stream, "Event")) break;
    if ((event = XmlElementElemsGetLastOne (event_stream, "Event")) == NULL) break;
    if ((period_id = XmlElementAttrGet (period, "id")) == "") break;
    if ((timescale = XmlElementAttrGet (event_stream, "timescale")) == "") break;
    if ((event_id = XmlElementAttrGet (event, "id")) == "") break;

    time_scale = _getNumberVal (timescale.c_str ());
    presentation_time = string_format ("%" G_GINT64_FORMAT, durationCurrentPeriod_ * time_scale);

    if (period_id != "0")
        event_id = string_format ("%s%04d", period_id.c_str (),
            _getNumberVal (event_id.c_str ()));

    if (!XmlElementAttrSet (event, "id", event_id)) break;
    if (!XmlElementAttrSet (event, "presentationTime", presentation_time)) break;
    if (!XmlElementAttrSet (event, "duration", "0")) break;

    result = _getNumberVal(event_id.c_str ());
  } while (0);
  return result;
}

int ManifestGenerator::addAdaptationSet (StreamType type, std::string media, int duration, int startNumber)
{
  int result = -1;
  int representation_id = -1;
  XmlElement * period = NULL;
  XmlElement * adaptationset = NULL;
  std::string adaptationset_id = "";

  do {
    if (type >= LAST ) break;
    if (mpd_ == NULL) break;
    if ((period = XmlElementElemsGetLastOne (mpd_, "Period")) == NULL) break;
    if (!XmlElementElemsSet (period, "AdaptationSet")) break;
    if ((adaptationset = XmlElementElemsGetLastOne (period, "AdaptationSet")) == NULL) break;
    if ((adaptationset_id = XmlElementAttrGet (adaptationset, "id")) == "") break;
    switch (type) {
      case AUDIO:
        if (!XmlElementAttrSet (adaptationset, "contentType", "audio")) break;
        representation_id = addRepresentationAudio (adaptationset, media, duration, startNumber);
        break;
      case VIDEO:
        if (!XmlElementAttrSet (adaptationset, "contentType", "video")) break;
        representation_id = addRepresentationVideo (adaptationset, media, duration, startNumber);
        break;
      case SUBTITLE:
        if (!XmlElementAttrSet (adaptationset, "contentType", "subtitle")) break;
        representation_id = addRepresentationSubtitle (adaptationset, media, duration, startNumber);
        break;
      default: break;
    }
    if (representation_id >= 0)
      result = _getNumberVal (adaptationset_id.c_str ());
  } while (0);
  return result;
}

int ManifestGenerator::addRepresentationAudio (XmlElement * adaptationset, std::string media, int duration, int startNumber)
{
  int result = -1;
  XmlElement * representation = NULL;
  std::string mimeType = "audio/mp4";
  std::string bandwidth = "128000";
  std::string representation_id = "";

  do {
    if (adaptationset == NULL) break;
    if (!XmlElementElemsSet (adaptationset, "Representation")) break;
    if ((representation = XmlElementElemsGetLastOne (adaptationset, "Representation")) == NULL) break;
    if ((representation_id = XmlElementAttrGet (representation, "id")) == "") break;
    if (!XmlElementAttrSet (representation, "mimeType", mimeType)) break;
    if (!XmlElementAttrSet (representation, "bandwidth", bandwidth)) break;
    if (addSegmentTemplate (representation, media, duration, startNumber) < 0) break;
    result = _getNumberVal (representation_id.c_str ());
  } while (0);
  return result;
}

int ManifestGenerator::addRepresentationVideo (XmlElement * adaptationset, std::string media, int duration, int startNumber)
{
  int result = -1;
  XmlElement * representation = NULL;
  std::string mimeType = "video/mp4";
  std::string bandwidth = "20051523";
  std::string representation_id = "";

  do {
    if (adaptationset == NULL) break;
    if (!XmlElementElemsSet (adaptationset, "Representation")) break;
    if ((representation = XmlElementElemsGetLastOne (adaptationset, "Representation")) == NULL) break;
    if ((representation_id = XmlElementAttrGet (representation, "id")) == "") break;
    if (!XmlElementAttrSet (representation, "mimeType", mimeType)) break;
    if (!XmlElementAttrSet (representation, "bandwidth", bandwidth)) break;
    if (addSegmentTemplate (representation, media, duration, startNumber) < 0) break;
    result = _getNumberVal (representation_id.c_str ());
  } while (0);
  return result;
}

int ManifestGenerator::addRepresentationSubtitle (XmlElement * adaptationset, std::string media, int duration, int startNumber)
{
  int result = -1;
  XmlElement * representation = NULL;
  std::string mimeType = "application/mp4";
  std::string bandwidth = "12800";
  std::string representation_id = "";

  do {
    if (adaptationset == NULL) break;
    if (!XmlElementElemsSet (adaptationset, "Representation")) break;
    if ((representation = XmlElementElemsGetLastOne (adaptationset, "Representation")) == NULL) break;
    if ((representation_id = XmlElementAttrGet (representation, "id")) == "") break;
    if (!XmlElementAttrSet (representation, "mimeType", mimeType)) break;
    if (!XmlElementAttrSet (representation, "bandwidth", bandwidth)) break;
    if (addSegmentTemplate (representation, media, duration, startNumber) < 0) break;
    result = _getNumberVal (representation_id.c_str ());
  } while (0);
  return result;
}

int ManifestGenerator::addSegmentTemplate (XmlElement * representation, std::string media, int duration, int startNumber)
{
  int result = -1;
  XmlElement * segment_template = NULL;
  do {
    if (representation == NULL) break;
    if (!XmlElementElemsSet (representation, "SegmentTemplate")) break;
    if ((segment_template = XmlElementElemsGetLastOne (representation, "SegmentTemplate")) == NULL) break;
    if (!XmlElementAttrSet (segment_template, "timescale", "1000")) break;
    if (!XmlElementAttrSet (segment_template, "duration", string_format ("%d", duration))) break;
    if (!XmlElementAttrSet (segment_template, "startNumber", string_format ("%d", startNumber))) break;
    if (!XmlElementAttrSet (segment_template, "media", media)) break;
    result = 0;
  } while (0);
  return result;
}

bool ManifestGenerator::updateAdaptationSet (int adaptationset_id, std::string key, std::string value)
{
  bool result = false;
  XmlElement * period = NULL;
  XmlElement * adaptationset = NULL;

  do {
    if (mpd_ == NULL) break;
    if ((period = XmlElementElemsGetLastOne (mpd_, "Period")) == NULL) break;
    if ((adaptationset = XmlElementElemsGet (period, "AdaptationSet", string_format ("%d", adaptationset_id))) == NULL) break;
    if (!XmlElementAttrSet (adaptationset, key, value)) break;
    result = true;
  } while (0);
  return result;
}

bool ManifestGenerator::updateRepresentation (int adaptationset_id, std::string key, std::string value)
{
  bool result = false;
  XmlElement * period = NULL;
  XmlElement * adaptationset = NULL;
  XmlElement * representation = NULL;
  XmlElement * audiochannelconfiguration = NULL;

  do {
    if (mpd_ == NULL) break;
    if ((period = XmlElementElemsGetLastOne (mpd_, "Period")) == NULL) break;
    if ((adaptationset = XmlElementElemsGet (period, "AdaptationSet", string_format ("%d", adaptationset_id))) == NULL) break;
    if ((representation = XmlElementElemsGetLastOne (adaptationset, "Representation")) == NULL) break;
    if (!XmlElementAttrSet (representation, key, value)) break;
    result = true;
  } while (0);
  return result;
}

bool ManifestGenerator::updateAudioChannelConfiguration (int adaptationset_id, std::string key, std::string value)
{
  bool result = false;
  XmlElement * period = NULL;
  XmlElement * adaptationset = NULL;
  XmlElement * representation = NULL;
  XmlElement * audiochannelconfiguration = NULL;

  do {
    if (mpd_ == NULL) break;
    if ((period = XmlElementElemsGetLastOne (mpd_, "Period")) == NULL) break;
    if ((adaptationset = XmlElementElemsGet (period, "AdaptationSet", string_format ("%d", adaptationset_id))) == NULL) break;
    if ((representation = XmlElementElemsGetLastOne (adaptationset, "Representation")) == NULL) break;
    if (!XmlElementElemsSet (representation, "AudioChannelConfiguration")) break;
    if ((audiochannelconfiguration = XmlElementElemsGetLastOne (representation, "AudioChannelConfiguration")) == NULL) break;
    if (!XmlElementAttrSet (audiochannelconfiguration, key, value)) break;
    result = true;
  } while (0);
  return result;
}

bool ManifestGenerator::updateSegmentTemplate (int adaptationset_id, std::string key, std::string value)
{
  bool result = false;
  XmlElement * period = NULL;
  XmlElement * adaptationset = NULL;
  XmlElement * representation = NULL;
  XmlElement * segment_template = NULL;

  do {
    if (mpd_ == NULL) break;
    if ((period = XmlElementElemsGetLastOne (mpd_, "Period")) == NULL) break;
    if ((adaptationset = XmlElementElemsGet (period, "AdaptationSet", string_format ("%d", adaptationset_id))) == NULL) break;
    if ((representation = XmlElementElemsGetLastOne (adaptationset, "Representation")) == NULL) break;
    if ((segment_template = XmlElementElemsGetLastOne (representation, "SegmentTemplate")) == NULL) break;
    if (!XmlElementAttrSet (segment_template, key, value)) break;
    result = true;
  } while (0);
  return result;
}

bool ManifestGenerator::updatePresentationTimeOffset (int adaptationset_id, gint64 presentationTimeOffset)
{
  presentationTimeOffset_ = presentationTimeOffset;
  return updateSegmentTemplate (adaptationset_id, "presentationTimeOffset",
      string_format ("%" G_GUINT64_FORMAT, MSEC(presentationTimeOffset_)));
}

bool ManifestGenerator::updateAdaptationSetRole (int adaptationset_id, std::string role)
{
  bool result = false;
  XmlElement * period = NULL;
  XmlElement * adaptationset = NULL;
  XmlElement * adaptationset_role = NULL;

  do {
    if (mpd_ == NULL) break;
    if ((period = XmlElementElemsGetLastOne (mpd_, "Period")) == NULL) break;
    if ((adaptationset = XmlElementElemsGet (period, "AdaptationSet", string_format ("%d", adaptationset_id))) == NULL) break;
    if (!XmlElementElemsSet (adaptationset, "Role")) break;
    if ((adaptationset_role = XmlElementElemsGetLastOne (adaptationset, "Role")) == NULL) break;
    if (!XmlElementAttrSet (adaptationset_role, "value", role)) break;
    result = true;
  } while (0);
  return result;
}
