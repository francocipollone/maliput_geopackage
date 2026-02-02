// BSD 3-Clause License
//
// Copyright (c) 2026, Maliput Contributors
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#include "maliput_geopackage/geopackage/wkt_parser.h"

#include <algorithm>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace maliput_geopackage {
namespace geopackage {

namespace {

// Trims whitespace from both ends of a string.
std::string Trim(const std::string& str) {
  const auto start = str.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) return "";
  const auto end = str.find_last_not_of(" \t\n\r");
  return str.substr(start, end - start + 1);
}

// Extracts the content between parentheses from a WKT string.
std::string ExtractParenthesesContent(const std::string& wkt) {
  const auto open_paren = wkt.find('(');
  const auto close_paren = wkt.rfind(')');
  if (open_paren == std::string::npos || close_paren == std::string::npos || open_paren >= close_paren) {
    throw std::runtime_error("Malformed WKT: missing or mismatched parentheses in '" + wkt + "'");
  }
  return wkt.substr(open_paren + 1, close_paren - open_paren - 1);
}

// Parses a single point from a space-separated string "x y z".
maliput::math::Vector3 ParseSinglePoint(const std::string& point_str) {
  std::istringstream iss(Trim(point_str));
  double x, y, z;
  if (!(iss >> x >> y >> z)) {
    throw std::runtime_error("Malformed WKT point: '" + point_str + "'");
  }
  return maliput::math::Vector3(x, y, z);
}

}  // namespace

std::vector<maliput::math::Vector3> ParseLineStringZ(const std::string& wkt) {
  // Check for LINESTRINGZ or LINESTRING Z prefix
  std::string upper_wkt = wkt;
  std::transform(upper_wkt.begin(), upper_wkt.end(), upper_wkt.begin(), ::toupper);

  if (upper_wkt.find("LINESTRING") == std::string::npos) {
    throw std::runtime_error("WKT string is not a LINESTRING: '" + wkt + "'");
  }

  const std::string content = ExtractParenthesesContent(wkt);

  std::vector<maliput::math::Vector3> points;
  std::istringstream iss(content);
  std::string point_str;

  // Split by comma
  while (std::getline(iss, point_str, ',')) {
    points.push_back(ParseSinglePoint(point_str));
  }

  if (points.size() < 2) {
    throw std::runtime_error("LINESTRING must have at least 2 points, got " + std::to_string(points.size()));
  }

  return points;
}

maliput::math::Vector3 ParsePointZ(const std::string& wkt) {
  // Check for POINTZ or POINT Z prefix
  std::string upper_wkt = wkt;
  std::transform(upper_wkt.begin(), upper_wkt.end(), upper_wkt.begin(), ::toupper);

  if (upper_wkt.find("POINT") == std::string::npos) {
    throw std::runtime_error("WKT string is not a POINT: '" + wkt + "'");
  }

  const std::string content = ExtractParenthesesContent(wkt);
  return ParseSinglePoint(content);
}

}  // namespace geopackage
}  // namespace maliput_geopackage
