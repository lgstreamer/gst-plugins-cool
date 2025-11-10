/*
 * GStreamer dashsink element
 *
 * Copyright 2017 LG Electronics, Inc.
 *  @author: Seoungil Kang <seoungil.kang@lge.com>
 *
 * manifestgenerator.hpp:
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
#pragma once
#include <gst/gst.h>
#include <string>
#include "xmlelement.hpp"


class ManifestGenerator {
public:
  enum StreamType { AUDIO, VIDEO, SUBTITLE, LAST };
public:
  ManifestGenerator (void);
  virtual ~ManifestGenerator (void);
public:
  bool loadManifest (std::string location);
  bool storeManifest (void);
public:
  bool updateCurrentDuration (gint64 curRunningTime);
  gint64 getOverallDuration (void);
  int getCurrentPeriodId (void);
public:
  int addPeriod (void);
  int addEvent (void);
  int addAdaptationSet (StreamType type, std::string media, int duration, int startNumber);
  int addRepresentationAudio (XmlElement * adaptationset, std::string media, int duration, int startNumber);
  int addRepresentationVideo (XmlElement * adaptationset, std::string media, int duration, int startNumber);
  int addRepresentationSubtitle (XmlElement * adaptationset, std::string media, int duration, int startNumber);
  int addSegmentTemplate (XmlElement * representation, std::string media, int duration, int startNumber);
  bool updateAdaptationSet (int adaptationset_id, std::string key, std::string value);
  bool updateRepresentation (int adaptationset_id, std::string key, std::string value);
  bool updateAudioChannelConfiguration (int adaptationset_id, std::string key, std::string value);
  bool updateSegmentTemplate (int adaptationset_id, std::string key, std::string value);
  bool updatePresentationTimeOffset (int adaptationset_id, gint64 presentationTimeOffset);
  bool updateAdaptationSetRole (int adaptationset_id, std::string role);
private:
  XmlElement * mpd_;
  std::string location_;
  gint64 durationCurrentPeriod_;
  gint64 durationOverallPeriods_;
  gint64 presentationTimeOffset_;
};
