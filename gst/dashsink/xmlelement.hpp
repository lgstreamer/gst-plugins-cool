/*
 * GStreamer dashsink element
 *
 * Copyright 2017 LG Electronics, Inc.
 *  @author: Seoungil Kang <seoungil.kang@lge.com>
 *
 * xmlelement.hpp:
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
#include <string>
struct XmlElement;

// Load, Store XmlElements.
XmlElement * XmlElementLoad (std::string path);
std::string  XmlElementDump (XmlElement * element);

// Manipluating XmlElements.
XmlElement * XmlElementNew (std::string name);
void         XmlElementFree (XmlElement * element);
bool         XmlElementAttrSet (XmlElement * element, std::string name, std::string value);
std::string  XmlElementAttrGet (XmlElement * element, std::string name);
bool         XmlElementElemsSet (XmlElement * element, std::string name);
XmlElement * XmlElementElemsGet (XmlElement * element, std::string name, std::string id);
XmlElement * XmlElementElemsGetLastOne (XmlElement * element, std::string name);

// Utility functions.
std::string string_format(const std::string fmt_str, ...);
