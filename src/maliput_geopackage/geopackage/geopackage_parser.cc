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
#include "maliput_geopackage/geopackage/geopackage_parser.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include <maliput/common/logger.h>
#include <maliput_sparse/geometry/line_string.h>

#include "maliput_geopackage/geopackage/wkt_parser.h"

namespace maliput_geopackage {
namespace geopackage {

namespace {

/// Converts a vector of maliput::math::Vector3 to a maliput_sparse::geometry::LineString3d
maliput_sparse::geometry::LineString3d ToLineString3d(const std::vector<maliput::math::Vector3>& points) {
  return maliput_sparse::geometry::LineString3d(points);
}

/// Converts LaneEnd::Which from string
maliput_sparse::parser::LaneEnd::Which LaneEndWhichFromString(const std::string& end_str) {
  if (end_str == "start") {
    return maliput_sparse::parser::LaneEnd::Which::kStart;
  } else if (end_str == "finish") {
    return maliput_sparse::parser::LaneEnd::Which::kFinish;
  }
  throw std::runtime_error("Invalid lane_end value: " + end_str);
}

}  // namespace

GeoPackageParser::GeoPackageParser(const std::string& gpkg_file_path) {
  maliput::log()->trace("Opening GeoPackage: ", gpkg_file_path);
  OpenDatabase(gpkg_file_path);

  maliput::log()->trace("Parsing metadata...");
  ParseMetadata();

  maliput::log()->trace("Parsing junctions...");
  ParseJunctions();

  maliput::log()->trace("Parsing segments and lanes...");
  ParseSegmentsAndLanes();

  maliput::log()->trace("Parsing connections...");
  ParseConnections();

  maliput::log()->info("GeoPackage parsing complete. Found ", junctions_.size(), " junctions and ", connections_.size(),
                       " connections.");
}

GeoPackageParser::~GeoPackageParser() { CloseDatabase(); }

void GeoPackageParser::OpenDatabase(const std::string& gpkg_file_path) {
  const int rc = sqlite3_open_v2(gpkg_file_path.c_str(), &db_, SQLITE_OPEN_READONLY, nullptr);
  if (rc != SQLITE_OK) {
    const std::string error_msg = sqlite3_errmsg(db_);
    sqlite3_close(db_);
    db_ = nullptr;
    throw std::runtime_error("Failed to open GeoPackage file '" + gpkg_file_path + "': " + error_msg);
  }
}

void GeoPackageParser::CloseDatabase() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

void GeoPackageParser::ParseMetadata() {
  // Parse maliput_metadata table for configuration values
  const char* sql = "SELECT key, value FROM maliput_metadata";
  sqlite3_stmt* stmt;

  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    maliput::log()->warn("No maliput_metadata table found, using defaults.");
    return;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const char* value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    if (key && value) {
      maliput::log()->trace("Metadata: ", key, " = ", value);
    }
  }

  sqlite3_finalize(stmt);
}

void GeoPackageParser::ParseJunctions() {
  const char* sql = "SELECT junction_id, name FROM junctions";
  sqlite3_stmt* stmt;

  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to query junctions table: " + std::string(sqlite3_errmsg(db_)));
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* junction_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    // const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

    if (junction_id) {
      maliput_sparse::parser::Junction junction;
      junction.id = junction_id;
      junctions_[junction.id] = junction;
      maliput::log()->trace("Parsed junction: ", junction_id);
    }
  }

  sqlite3_finalize(stmt);
}

void GeoPackageParser::ParseSegmentsAndLanes() {
  // First, parse segments and associate them with junctions
  const char* segment_sql = "SELECT segment_id, junction_id, name FROM segments";
  sqlite3_stmt* stmt;

  std::unordered_map<std::string, std::string> segment_to_junction;

  if (sqlite3_prepare_v2(db_, segment_sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to query segments table: " + std::string(sqlite3_errmsg(db_)));
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* segment_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const char* junction_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

    if (segment_id && junction_id) {
      segment_to_junction[segment_id] = junction_id;

      // Create segment in the corresponding junction
      auto junction_it = junctions_.find(junction_id);
      if (junction_it != junctions_.end()) {
        maliput_sparse::parser::Segment segment;
        segment.id = segment_id;
        junction_it->second.segments[segment.id] = segment;
        maliput::log()->trace("Parsed segment: ", segment_id, " in junction: ", junction_id);
      }
    }
  }
  sqlite3_finalize(stmt);

  // Load `boundaries` table and require it for parsing lanes (new schema only)
  std::unordered_map<std::string, maliput_sparse::geometry::LineString3d> boundaries_map;
  {
    const char* boundary_sql = "SELECT boundary_id, geometry FROM boundaries";
    sqlite3_stmt* bstmt = nullptr;
    if (sqlite3_prepare_v2(db_, boundary_sql, -1, &bstmt, nullptr) != SQLITE_OK) {
      throw std::runtime_error("GeoPackage missing required 'boundaries' table: " + std::string(sqlite3_errmsg(db_)));
    }

    bool has_boundaries = false;
    while (sqlite3_step(bstmt) == SQLITE_ROW) {
      has_boundaries = true;
      const char* boundary_id = reinterpret_cast<const char*>(sqlite3_column_text(bstmt, 0));
      const char* geometry_wkt = reinterpret_cast<const char*>(sqlite3_column_text(bstmt, 1));
      if (!boundary_id || !geometry_wkt) {
        sqlite3_finalize(bstmt);
        throw std::runtime_error("Invalid entry in 'boundaries' table: missing id or geometry");
      }
      const auto pts = ParseLineStringZ(geometry_wkt);
      boundaries_map.emplace(boundary_id, ToLineString3d(pts));
      maliput::log()->trace("Parsed boundary: ", boundary_id);
    }

    sqlite3_finalize(bstmt);

    if (!has_boundaries) {
      throw std::runtime_error("'boundaries' table exists but contains no rows; at least one boundary is required");
    }
  }

  // Parse lanes by referencing boundary IDs. This parser requires the new schema.
  const char* lane_sql =
      "SELECT lane_id, segment_id, lane_type, direction, left_boundary_id, right_boundary_id "
      "FROM lanes";

  if (sqlite3_prepare_v2(db_, lane_sql, -1, &stmt, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to query lanes table: " + std::string(sqlite3_errmsg(db_)));
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* lane_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const char* segment_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    const char* left_boundary_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    const char* right_boundary_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));

    if (!lane_id || !segment_id || !left_boundary_id || !right_boundary_id) {
      sqlite3_finalize(stmt);
      throw std::runtime_error(
          "Lane row missing required fields (expected 'left_boundary_id' and 'right_boundary_id')");
    }

    auto left_it = boundaries_map.find(left_boundary_id);
    auto right_it = boundaries_map.find(right_boundary_id);
    if (left_it == boundaries_map.end() || right_it == boundaries_map.end()) {
      sqlite3_finalize(stmt);
      throw std::runtime_error("Lane '" + std::string(lane_id) + "' references unknown boundary id(s)");
    }

    maliput_sparse::parser::Lane lane{
        lane_id,           // id
        left_it->second,   // left
        right_it->second,  // right
        std::nullopt,      // left_lane_id
        std::nullopt,      // right_lane_id
        {},                // successors
        {}                 // predecessors
    };

    // Find the junction for this segment
    auto seg_junc_it = segment_to_junction.find(segment_id);
    if (seg_junc_it == segment_to_junction.end()) {
      maliput::log()->warn("Lane ", lane_id, " references unknown segment ", segment_id);
      continue;
    }

    const std::string& junction_id = seg_junc_it->second;

    // Add lane to the segment
    auto junction_it = junctions_.find(junction_id);
    if (junction_it != junctions_.end()) {
      auto segment_it = junction_it->second.segments.find(segment_id);
      if (segment_it != junction_it->second.segments.end()) {
        segment_it->second.lanes.push_back(lane);
        lane_to_junction_[lane_id] = junction_id;
        lane_to_segment_[lane_id] = segment_id;
        maliput::log()->trace("Parsed lane (via boundary ids): ", lane_id, " in segment: ", segment_id);
      }
    }
  }
  sqlite3_finalize(stmt);
}

void GeoPackageParser::ParseConnections() {
  BuildBranchPointConnections();
  BuildLaneAdjacency();
}

void GeoPackageParser::BuildBranchPointConnections() {
  // Query branch_point_lanes to build connections
  // Group by branch_point_id, then create connections between a-side and b-side lanes
  const char* sql =
      "SELECT branch_point_id, lane_id, side, lane_end "
      "FROM branch_point_lanes "
      "ORDER BY branch_point_id, side";

  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    maliput::log()->warn("No branch_point_lanes table found or query failed.");
    return;
  }

  // Group lane ends by branch point and side
  std::unordered_map<std::string, std::vector<maliput_sparse::parser::LaneEnd>> a_side_lanes;
  std::unordered_map<std::string, std::vector<maliput_sparse::parser::LaneEnd>> b_side_lanes;

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* bp_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const char* lane_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    const char* side = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    const char* lane_end = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

    if (!bp_id || !lane_id || !side || !lane_end) continue;

    maliput_sparse::parser::LaneEnd le;
    le.lane_id = lane_id;
    le.end = LaneEndWhichFromString(lane_end);

    if (std::string(side) == "a") {
      a_side_lanes[bp_id].push_back(le);
    } else if (std::string(side) == "b") {
      b_side_lanes[bp_id].push_back(le);
    }
  }
  sqlite3_finalize(stmt);

  // Create connections: each a-side lane connects to each b-side lane
  for (const auto& [bp_id, a_lanes] : a_side_lanes) {
    auto b_it = b_side_lanes.find(bp_id);
    if (b_it == b_side_lanes.end()) continue;

    for (const auto& a_lane : a_lanes) {
      for (const auto& b_lane : b_it->second) {
        maliput_sparse::parser::Connection conn;
        conn.from = a_lane;
        conn.to = b_lane;
        connections_.push_back(conn);
        maliput::log()->trace("Created connection: ", a_lane.lane_id, " -> ", b_lane.lane_id);
      }
    }
  }
}

void GeoPackageParser::BuildLaneAdjacency() {
  // Query adjacent_lanes table to set left_lane_id and right_lane_id
  const char* sql = "SELECT lane_id, adjacent_lane_id, side FROM adjacent_lanes";

  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    maliput::log()->warn("No adjacent_lanes table found or query failed.");
    return;
  }

  // Build adjacency map
  std::unordered_map<std::string, std::string> left_adjacent;
  std::unordered_map<std::string, std::string> right_adjacent;

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* lane_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const char* adjacent_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    const char* side = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

    if (!lane_id || !adjacent_id || !side) continue;

    if (std::string(side) == "left") {
      left_adjacent[lane_id] = adjacent_id;
    } else if (std::string(side) == "right") {
      right_adjacent[lane_id] = adjacent_id;
    }
  }
  sqlite3_finalize(stmt);

  // Update lanes with adjacency information and reorder lanes in each segment
  for (auto& [junction_id, junction] : junctions_) {
    for (auto& [segment_id, segment] : junction.segments) {
      // First, update adjacency info
      for (auto& lane : segment.lanes) {
        auto left_it = left_adjacent.find(lane.id);
        if (left_it != left_adjacent.end()) {
          lane.left_lane_id = left_it->second;
        }

        auto right_it = right_adjacent.find(lane.id);
        if (right_it != right_adjacent.end()) {
          lane.right_lane_id = right_it->second;
        }
      }

      // Now reorder lanes so that the rightmost lane (no right_lane_id) is first
      // and each subsequent lane is to the left
      if (segment.lanes.size() > 1) {
        std::vector<maliput_sparse::parser::Lane> ordered_lanes;
        ordered_lanes.reserve(segment.lanes.size());

        // Build a map from lane_id to lane
        std::unordered_map<std::string, const maliput_sparse::parser::Lane*> lane_map;
        for (const auto& lane : segment.lanes) {
          lane_map[lane.id] = &lane;
        }

        // Find the rightmost lane (no right_lane_id)
        const maliput_sparse::parser::Lane* current = nullptr;
        for (const auto& lane : segment.lanes) {
          if (!lane.right_lane_id.has_value()) {
            current = &lane;
            break;
          }
        }

        if (current) {
          // Chain from right to left
          while (current) {
            ordered_lanes.push_back(*current);
            if (current->left_lane_id.has_value()) {
              auto it = lane_map.find(current->left_lane_id.value());
              current = (it != lane_map.end()) ? it->second : nullptr;
            } else {
              current = nullptr;
            }
          }

          // If we successfully ordered all lanes, use the new order
          if (ordered_lanes.size() == segment.lanes.size()) {
            segment.lanes = std::move(ordered_lanes);
            maliput::log()->trace("Reordered lanes in segment: ", segment_id);
          }
        }
      }
    }
  }
}

const std::unordered_map<maliput_sparse::parser::Junction::Id, maliput_sparse::parser::Junction>&
GeoPackageParser::DoGetJunctions() const {
  return junctions_;
}

const std::vector<maliput_sparse::parser::Connection>& GeoPackageParser::DoGetConnections() const {
  return connections_;
}

}  // namespace geopackage
}  // namespace maliput_geopackage
