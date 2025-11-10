/*
 * GStreamer dashsink element
 *
 * Copyright 2017 LG Electronics, Inc.
 *  @author: Seoungil Kang <seoungil.kang@lge.com>
 *
 * xmlelement.cpp:
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
#include <string>
#include <map>
#include <vector>
#include <gst/gst.h>
#include <string.h>
#include <stdarg.h>
#include <memory>
#include "libxml/xmlmemory.h"
#include "libxml/xmlreader.h"
#include "libxml/parser.h"
#include "xmlelement.hpp"
GST_DEBUG_CATEGORY_EXTERN (dashsink_debug);
#define GST_CAT_DEFAULT dashsink_debug
static XmlElement * newSegmentBase (void);
static XmlElement * newMultipleSegmentBase (void);
static XmlElement * newRepresentationBase (void);
static XmlElement * newAudioChannelConfiguration (void);
static XmlElement * newSegmentTemplate (void);
static XmlElement * newRepresentation (void);
static XmlElement * newAdaptationSet (void);
static XmlElement * newEvent (void);
static XmlElement * newEventStream (void);
static XmlElement * newPeriod (void);
static XmlElement * newMPD (void);

struct XmlElement {
  typedef XmlElement *PXmlElement;
  typedef std::vector<PXmlElement> XmlElementSequence;
  typedef std::vector<PXmlElement> *PXmlElementSequence;

  std::map<std::string, std::string> attributes_;
  std::map<std::string, PXmlElementSequence> composite_elements_;
  std::string name_;
  std::string head_;
  int depth_;

  XmlElement (void)
    : name_(""), head_(""), depth_(0)
  {}

  virtual ~XmlElement (void)
  {
    for (auto &kv: composite_elements_) {
      if (kv.second != NULL) {
        for (auto *p: *kv.second) {
          delete p;
        }
        delete kv.second;
      }
    }
  }

  bool is_valid_attribute (std::string attribute)
  {
    return attributes_.count (attribute) ? true : false;
  }

  bool is_valid_element (std::string element)
  {
    return composite_elements_.count (element) ? true : false;
  }

  void attr_reg (std::string name, std::string value)
  {
    attributes_[name] = value;
  }

  bool attr_set (std::string name, std::string value)
  {
    bool result = is_valid_attribute (name);
    if (result)
      attributes_[name] = value;
    return result;
  }

  std::string attr_get (std::string name)
  {
    std::string result = "";
    if (is_valid_attribute (name)) {
      result = attributes_[name];
    }
    return result;
  }

  void elems_reg (std::string name, XmlElementSequence * p)
  {
    composite_elements_[name] = p;
  }

  bool elems_set (std::string name)
  {
    bool result = false;
    XmlElementSequence * p = elems_get (name);
    if (p != NULL) {
      XmlElement * e = XmlElementNew (name);
      if (e != NULL) {
        e->attr_set ("id", string_format("%d", p->size()));
        p->push_back (e);
        result = true;
      }
    }
    return result;
  }

  XmlElementSequence* elems_get (std::string name)
  {
    if (is_valid_element (name)) {
      return composite_elements_[name];
    }
    return NULL;
  }

  std::string tag_start (void)
  {
    std::string result (depth_, ' ');
    result += "<" + name_;
    for (auto &kv: attributes_) {
      if (kv.second != "")
        result += " " + kv.first + "=\"" + kv.second + "\"";
    }
    result += ">";
    return result;
  }

  std::string tag_end (void)
  {
    std::string result (depth_, ' ');
    result += "</" + name_ + ">";
    return result;
  }

  std::string dump (void)
  {
    std::string result = "";
    if (head_ != "")
      result += head_;
    result += tag_start (); result += "\n";
    for (auto &kv: composite_elements_) {
      if (kv.second != NULL) {
        for (auto *p: *kv.second) {
          result += p->dump (); result += "\n";
        }
      }
    }
    result += tag_end ();
    return result;
  }

  void parse (xmlNodePtr c)
  {
    xmlAttrPtr attr;
    attr = c->properties;
    while (attr) {
      xmlChar *val = xmlNodeListGetString (c->doc, attr->children, 1);
      attr_set ((const char*)attr->name, (const char*)val);
      xmlFree(val);
      attr = attr->next;
    }
    for (xmlNode *n = c->children; n; n = n->next) {
      if (n->type == XML_ELEMENT_NODE) {
        XmlElementSequence * p = elems_get ((const char*)n->name);
        if (p != NULL) {
          XmlElement * e = XmlElementNew ((const char *)n->name);
          if (e != NULL) {
            p->push_back (e);
            e->parse (n);
          }
        }
      }
    }
  }

  void load (std::string path)
  {
    xmlDocPtr doc = NULL;
    xmlNodePtr root = NULL;
    do {
      doc = xmlReadFile (path.c_str(), NULL, 0);
      if (doc == NULL) break;
      root = xmlDocGetRootElement (doc);
      if (root == NULL) break;
      parse (root);
    } while (0);
    xmlFreeDoc (doc);
    xmlCleanupParser ();
  }
};

static XmlElement * newSegmentBase (void)
{
  XmlElement * p = new XmlElement ();
  p->attr_reg ("timescale", "1000");                   // xs:unsignedInt
  p->attr_reg ("presentationTimeOffset", "");          // xs:unsignedLong
  p->attr_reg ("indexRange", "");                      // xs:string
  p->attr_reg ("indexRangeExact", "");                 // xs:boolean
  p->attr_reg ("availabilityTimeOffset", "");          // xs:double
  p->attr_reg ("availabilityTimeComplete", "");        // xs:boolean
  p->elems_reg ("FramePacking", NULL);                 // DescriptorType,Min(0),Max(unbounde)
  p->elems_reg ("Initialization", NULL);               // URLType,Min(0)
  p->elems_reg ("RepresentationIndex", NULL);          // URLType,Min(0)

  return p;
}

static XmlElement * newMultipleSegmentBase (void)
{
  XmlElement * p = newSegmentBase ();
  p->attr_reg ("duration", "90000");                   // xs:unsignedInt
  p->attr_reg ("startNumber", "0");                    // xs:unsignedInt
  p->elems_reg ("SegmentTimeline", NULL);              // SegmentTimelineType,Min(0)
  p->elems_reg ("BitstreamSwitching", NULL);           // URLType,Min(0)

  return p;
}

static XmlElement * newRepresentationBase (void)
{
  XmlElement * p = new XmlElement ();
  p->attr_reg ("profiles", "");                        // xs:string
  p->attr_reg ("width", "");                           // xs:unsignedInt
  p->attr_reg ("height", "");                          // xs:unsignedInt
  p->attr_reg ("sar", "");                             // ratioType
  p->attr_reg ("frameRate", "");                       // FrameRateType
  p->attr_reg ("audioSamplingRate", "");               // xs:string
  p->attr_reg ("mimeType", "");                        // xs:string
  p->attr_reg ("segmentProfiles", "");                 // xs:string
  p->attr_reg ("codecs", "");                          // xs:string
  p->attr_reg ("maximumSAPPeriod", "");                // xs:double
  p->attr_reg ("startWithSAP", "");                    // SAPType
  p->attr_reg ("maxPlayoutRate", "");                  // xs:double
  p->attr_reg ("codingDependency", "");                // xs:boolean
  p->attr_reg ("scanType", "");                        // VideoScanType
  p->elems_reg ("FramePacking", NULL);                 // DescriptorType,Min(0),Max(unbounde)
  p->elems_reg ("AudioChannelConfiguration", NULL);    // DescriptorType,Min(0),Max(unbounde)
  p->elems_reg ("ContentProtection", NULL);            // DescriptorType,Min(0),Max(unbounde)
  p->elems_reg ("EssentialProperty", NULL);            // DescriptorType,Min(0),Max(unbounde)
  p->elems_reg ("SupplementalProperty", NULL);         // DescriptorType,Min(0),Max(unbounde)
  p->elems_reg ("InbandEventStream", NULL);            // DescriptorType,Min(0),Max(unbounde)

  return p;
}

static XmlElement * newAudioChannelConfiguration (void)
{
  XmlElement * p = newRepresentationBase ();
  p->name_ = "AudioChannelConfiguration";
  p->depth_ = 4;
  p->attr_reg ("schemeIdUri", "urn:mpeg:dash:23003:3:audio_channel_configuration:2011"); // xs:anyURI,REQUIRED
  p->attr_reg ("value", "");                           // xs:string

  return p;
}

static XmlElement * newSegmentTemplate (void)
{
  XmlElement * p = newMultipleSegmentBase ();
  p->name_ = "SegmentTemplate";
  p->depth_ = 4;
  p->attr_reg ("media", "$Number%8d$");                // xs:string
  p->attr_reg ("index", "");                           // xs:string
  p->attr_reg ("initialization", "");                  // xs:string
  p->attr_reg ("bitstreamSwitching", "");              // xs:string

  return p;
}

static XmlElement * newRepresentation (void)
{
  XmlElement * p = newRepresentationBase ();
  p->name_ = "Representation";
  p->depth_ = 3;
  p->attr_reg ("id", "unknown");                       // StringNoWhitespaceType,REQUIRED
  p->attr_reg ("bandwidth", "0");                      // xs:unsignedInt,REQUIRED
  p->attr_reg ("qualityRanking", "");                  // xs:unsignedInt
  p->attr_reg ("dependencyId", "");                    // StringVectorType
  p->attr_reg ("mediaStreamStructureId", "");          // StringVectorType
  p->elems_reg ("BaseURL", NULL);                      // BaseURLType,Min(0),Max(unbounded)
  p->elems_reg ("SubRepresentation", NULL);            // SubRepresentationType,Min(0),Max(unbounded)
  p->elems_reg ("SegmentBase", NULL);                  // SegmentBaseType,Min(0)
  p->elems_reg ("SegmentList", NULL);                  // SegmentListType,Min(0)
  p->elems_reg ("AudioChannelConfiguration", new XmlElement::XmlElementSequence()); // AudioChannelConfigurationType,Min(0),Max(unbounded)
  p->elems_reg ("SegmentTemplate", new XmlElement::XmlElementSequence()); // SegmentTemplateType,Min(0)

  return p;
}

static XmlElement * newRole (void)
{
  XmlElement * p = new XmlElement ();
  p->name_ = "Role";
  p->depth_ = 3;
  p->attr_reg ("schemeIdUri", "urn:mpeg:dash:role:2011");// xs:anyURI,REQUIRED
  p->attr_reg ("value", "");                           // xs:string

  return p;
}

static XmlElement * newAdaptationSet (void)
{
  XmlElement * p = newRepresentationBase ();
  p->name_ = "AdaptationSet";
  p->depth_ = 2;
  p->attr_reg ("id", "0");                             // xs:unsignedInt
  p->attr_reg ("group", "");                           // xs:unsignedInt
  p->attr_reg ("lang", "");                            // xs:language
  p->attr_reg ("contentType", "");                     // xs:string
  p->attr_reg ("par", "");                             // ratioType
  p->attr_reg ("minBandwidth", "");                    // xs:unsignedInt
  p->attr_reg ("maxBandwidth", "");                    // xs:unsignedInt
  p->attr_reg ("minHeight", "");                       // xs:unsignedInt
  p->attr_reg ("maxHeight", "");                       // xs:unsignedInt
  p->attr_reg ("minWidth", "");                        // xs:unsignedInt
  p->attr_reg ("maxWidth", "");                        // xs:unsignedInt
  p->attr_reg ("minFrameRate", "");                    // FrameRateType
  p->attr_reg ("maxFrameRate", "");                    // FrameRateType
  p->attr_reg ("segmentAlignment", "");                // ConditionalUintType,default(false)
  p->attr_reg ("subsegmentAlignment", "");             // ConditionalUintType,default(false)
  p->attr_reg ("subsegmentStartsWithSAP", "");         // SAPType,default(0)
  p->attr_reg ("bitstreamSwitching", "");              // xs:boolean
  p->elems_reg ("Accessibility", NULL);                // DescriptorType,Min(0),Max(unbounded)
  p->elems_reg ("Role", new XmlElement::XmlElementSequence ()); // DescriptorType,Min(0),Max(unbounded)
  p->elems_reg ("Rating", NULL);                       // DescriptorType,Min(0),Max(unbounded)
  p->elems_reg ("Viewpoint", NULL);                    // DescriptorType,Min(0),Max(unbounded)
  p->elems_reg ("ContentComponent", NULL);             // ContentComponentType,Min(0),Max(unbounded)
  p->elems_reg ("BaseURL", NULL);                      // BaseURLType,Min(0),Max(unbounded)
  p->elems_reg ("SegmentBase", NULL);                  // SegmentBaseType,Min(0)
  p->elems_reg ("SegmentList", NULL);                  // SegmentListType,Min(0)
  p->elems_reg ("SegmentTemplate", NULL);              // SegmentTemplateType,Min(0)
  p->elems_reg ("Representation", new XmlElement::XmlElementSequence ()); // RepresentationType,Min(0),Max(unbounded)

  p->attr_set ("mimeType", "");

  return p;
}

static XmlElement * newEvent (void)
{
  XmlElement * p = new XmlElement ();
  p->name_ = "Event";
  p->depth_ = 3;
  p->attr_reg ("id", "0");                             // xs:unsignedInt
  p->attr_reg ("presentationTime", "0");               // xs:unsignedLong,specifies the presentation time of the event relative to the start of the Period.
  p->attr_reg ("duration", "0");                       // xs:unsignedLong

  return p;
}

static XmlElement * newEventStream (void)
{
  XmlElement * p = new XmlElement ();
  p->name_ = "EventStream";
  p->depth_ = 2;
  p->attr_reg ("schemeIdUri", "urn:com:lge:dvr:2017"); // xs:anyURI,REQUIRED
  p->attr_reg ("value", "");                           // xs:string
  p->attr_reg ("timescale", "1000");                   // xs:unsignedInt
  p->elems_reg ("Event", new XmlElement::XmlElementSequence());// EventType,Min(0),Max(unbounded)

  return p;
}

static XmlElement * newPeriod (void)
{
  XmlElement * p = new XmlElement ();
  p->name_ = "Period";
  p->depth_ = 1;
  p->attr_reg ("id", "0");                             // xs:string
  p->attr_reg ("start", "");                           // xs:duration
  p->attr_reg ("duration", "PT0S");                    // xs:duration
  p->attr_reg ("bitstreamSwitching", "");              // xs:boolean
  p->elems_reg ("BaseURL", NULL);                      // BaseURLType,Min(0),Max(unbounded)
  p->elems_reg ("SegmentBase", NULL);                  // SegmentBaseType,Min(0)
  p->elems_reg ("SegmentList", NULL);                  // SegmentListType,Min(0)
  p->elems_reg ("SegmentTemplate", NULL);              // SegmentTemplateType,Min(0)
  p->elems_reg ("AssetIdentifier", NULL);              // DescriptorType,Min(0)
  p->elems_reg ("EventStream", new XmlElement::XmlElementSequence ());  // EventStreamType,Min(0),Max(unbounded)
  p->elems_reg ("AdaptationSet", new XmlElement::XmlElementSequence ());// AdaptationSetType,Min(0),Max(unbounded)
  p->elems_reg ("Subset", NULL);                       // SubsetType,Min(0),Max(unbounded)

  return p;
}

static XmlElement * newMPD (void)
{
  XmlElement * p = new XmlElement ();
  p->name_ = "MPD";
  p->depth_ = 0;
  p->head_ = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
  p->attr_reg ("id", "");                              // xs:string
  p->attr_reg ("profiles", "urn:mpeg:dash:profile:isoff-on-demand:2011"); // xs:string,REQUIRED
  p->attr_reg ("type", "");                            // xs:PresentationType,default(static)
  p->attr_reg ("availabilityStartTime", "");           // xs:dateTime
  p->attr_reg ("availabilityEndTime", "");             // xs:dateTime
  p->attr_reg ("publishTime", "");                     // xs:dateTime
  p->attr_reg ("mediaPresentationDuration", "PT0S");   // xs:duration
  p->attr_reg ("minimumUpdatePeriod", "");             // xs:duration
  p->attr_reg ("minBufferTime", "PT2S");               // xs:duration,REQUIRED
  p->attr_reg ("timeShiftBufferDepth", "");            // xs:duration
  p->attr_reg ("suggestedPresentationDelay", "");      // xs:duration
  p->attr_reg ("maxSegmentDuration", "");              // xs:duration
  p->attr_reg ("maxSubSegmentDuration", "");           // xs:duration
  p->elems_reg ("ProgramInformation", NULL);           // ProgramInformationType,Min(0),Max(unbounded)
  p->elems_reg ("BaseURL", NULL);                      // BaseURLType,Min(0),Max(unbounded)
  p->elems_reg ("Location", NULL);                     // xs:anyURI,Min(0),Max(unbounded)
  p->elems_reg ("Period", new XmlElement::XmlElementSequence ());     // PeriodType,Max(unbounded)
  p->elems_reg ("Metrics", NULL);                      // MetricsType,Min(0),Max(unbounded)

  p->attr_reg ("xmlns", "urn:mpeg:dash:schema:mpd:2011");

  return p;
}

///////////////////////////////////////////////////////////////////////////////
// APIs
XmlElement * XmlElementLoad (std::string path)
{
  XmlElement *element = XmlElementNew ("MPD");
  if (element != NULL) {
    element->load (path);
  }
  return element;
}

std::string XmlElementDump (XmlElement * element)
{
  std::string result = "";
  if (element != NULL)
    result = element->dump ();
  return result;
}

XmlElement * XmlElementNew (std::string name)
{
  if (name == "MPD") {
    return newMPD ();
  } else if (name == "Period") {
    return newPeriod ();
  } else if (name == "AdaptationSet") {
    return newAdaptationSet ();
  } else if (name == "Representation") {
    return newRepresentation ();
  } else if (name == "AudioChannelConfiguration") {
    return newAudioChannelConfiguration ();
  } else if (name == "Role") {
    return newRole ();
  } else if (name == "SegmentTemplate") {
    return newSegmentTemplate ();
  } else if (name == "EventStream") {
    return newEventStream ();
  } else if (name == "Event") {
    return newEvent ();
  } else {
    return NULL;
  }
}

void XmlElementFree (XmlElement * element)
{
  if (element != NULL)
    delete element;
}

bool XmlElementAttrSet (XmlElement * element, std::string name, std::string value)
{
  bool result = false;
  if (element != NULL)
    result = element->attr_set (name, value);
  return result;
}

std::string XmlElementAttrGet (XmlElement * element, std::string name)
{
  std::string result = "";
  if (element != NULL)
    result = element->attr_get (name);
  return result;
}

bool XmlElementElemsSet (XmlElement * element, std::string name)
{
  bool result = false;
  if (element != NULL)
    result = element->elems_set (name);
  return result;
}

XmlElement * XmlElementElemsGet (XmlElement * element, std::string name, std::string id)
{
  XmlElement * result = NULL;
  XmlElement::XmlElementSequence * s = NULL;

  if (element != NULL) {
    if (((s = element->elems_get (name)) != NULL) && (s->size () > 0)) {
      for (auto *e: *s) {
        if (e->attr_get ("id") == id) {
          result = e;
          break;
        }
      }
    }
  }

  return result;
}

XmlElement * XmlElementElemsGetLastOne (XmlElement * element, std::string name)
{
  XmlElement * result = NULL;
  XmlElement::XmlElementSequence * s = NULL;

  if (element != NULL) {
    if (((s = element->elems_get (name)) != NULL) && (s->size () > 0)) {
      result = s->back ();
    }
  }

  return result;
}

std::string string_format(const std::string fmt_str, ...)
{
  int final_n, n = ((int)fmt_str.size()) * 2;
  std::string str;
  std::unique_ptr<char[]> formatted;
  va_list ap;
  while(1) {
    formatted.reset(new char[n]);
    strcpy(&formatted[0], fmt_str.c_str());
    va_start(ap, fmt_str);
    final_n = vsnprintf(&formatted[0], n, fmt_str.c_str(), ap);
    va_end(ap);
    if (final_n < 0 || final_n >= n)
      n += abs(final_n - n + 1);
    else
      break;
  }
  return std::string(formatted.get());
}
