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

/// @file geopackage_query_example.cc
///
/// This example demonstrates how to load a GeoPackage-based road network
/// and perform common queries using the maliput API.
///
/// Usage:
///   geopackage_query_example <path_to_gpkg_file>
///
/// Example:
///   geopackage_query_example ./two_lane_road.gpkg

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

#include <maliput/api/junction.h>
#include <maliput/api/lane.h>
#include <maliput/api/lane_data.h>
#include <maliput/api/road_geometry.h>
#include <maliput/api/road_network.h>
#include <maliput/api/segment.h>
#include <maliput/common/logger.h>

#include "maliput_geopackage/builder/params.h"
#include "maliput_geopackage/builder/road_network_builder.h"

namespace {

/// Prints a horizontal separator line.
void PrintSeparator(char c = '-', int width = 60) { std::cout << std::string(width, c) << std::endl; }

/// Prints a section header.
void PrintHeader(const std::string& title) {
  std::cout << "\n";
  PrintSeparator('=');
  std::cout << "  " << title << std::endl;
  PrintSeparator('=');
}

/// Prints road network statistics.
void PrintRoadNetworkStats(const maliput::api::RoadGeometry* road_geometry) {
  PrintHeader("Road Network Statistics");

  std::cout << "Road Geometry ID: " << road_geometry->id().string() << std::endl;
  std::cout << "Number of Junctions: " << road_geometry->num_junctions() << std::endl;

  int total_segments = 0;
  int total_lanes = 0;
  for (int i = 0; i < road_geometry->num_junctions(); ++i) {
    const auto* junction = road_geometry->junction(i);
    total_segments += junction->num_segments();
    for (int j = 0; j < junction->num_segments(); ++j) {
      total_lanes += junction->segment(j)->num_lanes();
    }
  }

  std::cout << "Total Segments: " << total_segments << std::endl;
  std::cout << "Total Lanes: " << total_lanes << std::endl;
  std::cout << "Linear Tolerance: " << road_geometry->linear_tolerance() << " m" << std::endl;
  std::cout << "Angular Tolerance: " << road_geometry->angular_tolerance() << " rad" << std::endl;
}

/// Prints detailed lane information.
void PrintLaneDetails(const maliput::api::Lane* lane) {
  std::cout << "\n  Lane: " << lane->id().string() << std::endl;
  std::cout << "    Length: " << std::fixed << std::setprecision(2) << lane->length() << " m" << std::endl;

  // Get lane bounds at start, middle, and end
  const double s_start = 0.0;
  const double s_mid = lane->length() / 2.0;
  const double s_end = lane->length();

  auto print_bounds = [&](double s, const std::string& label) {
    const auto lane_bounds = lane->lane_bounds(s);
    const auto segment_bounds = lane->segment_bounds(s);
    std::cout << "    " << label << " (s=" << std::fixed << std::setprecision(1) << s << "):" << std::endl;
    std::cout << "      Lane bounds: [" << lane_bounds.min() << ", " << lane_bounds.max() << "] m" << std::endl;
    std::cout << "      Segment bounds: [" << segment_bounds.min() << ", " << segment_bounds.max() << "] m"
              << std::endl;
  };

  print_bounds(s_start, "Start");
  print_bounds(s_mid, "Middle");
  print_bounds(s_end, "End");

  // Print adjacent lanes
  const auto* left_lane = lane->to_left();
  const auto* right_lane = lane->to_right();
  std::cout << "    Adjacent lanes:" << std::endl;
  std::cout << "      Left: " << (left_lane ? left_lane->id().string() : "none") << std::endl;
  std::cout << "      Right: " << (right_lane ? right_lane->id().string() : "none") << std::endl;
}

/// Demonstrates coordinate transformations.
void DemonstrateCoordinateTransforms(const maliput::api::Lane* lane) {
  PrintHeader("Coordinate Transformations");

  std::cout << "Using lane: " << lane->id().string() << std::endl;

  // Define some lane positions to transform
  struct TestPoint {
    double s;
    double r;
    double h;
    std::string description;
  };

  std::vector<TestPoint> test_points = {
      {0.0, 0.0, 0.0, "Lane start, centerline"},
      {lane->length() / 2.0, 0.0, 0.0, "Lane middle, centerline"},
      {lane->length(), 0.0, 0.0, "Lane end, centerline"},
      {lane->length() / 2.0, 1.0, 0.0, "Lane middle, 1m to the left"},
      {lane->length() / 2.0, -1.0, 0.0, "Lane middle, 1m to the right"},
      {lane->length() / 2.0, 0.0, 1.0, "Lane middle, 1m above"},
  };

  std::cout << "\nLane Position -> Inertial Position:" << std::endl;
  std::cout << std::string(80, '-') << std::endl;
  std::cout << std::setw(35) << std::left << "Description" << std::setw(20) << "Lane (s,r,h)"
            << "Inertial (x,y,z)" << std::endl;
  std::cout << std::string(80, '-') << std::endl;

  for (const auto& pt : test_points) {
    const maliput::api::LanePosition lane_pos(pt.s, pt.r, pt.h);
    const maliput::api::InertialPosition inertial_pos = lane->ToInertialPosition(lane_pos);

    std::cout << std::setw(35) << std::left << pt.description << std::fixed << std::setprecision(1) << "(" << pt.s
              << ", " << pt.r << ", " << pt.h << ")" << std::setw(8) << " "
              << "(" << inertial_pos.x() << ", " << inertial_pos.y() << ", " << inertial_pos.z() << ")" << std::endl;
  }
}

/// Demonstrates finding the nearest lane to a point.
void DemonstrateFindRoadPosition(const maliput::api::RoadGeometry* road_geometry) {
  PrintHeader("Finding Road Position from Inertial Coordinates");

  // Define some inertial positions to query
  struct QueryPoint {
    double x;
    double y;
    double z;
    std::string description;
  };

  std::vector<QueryPoint> query_points = {
      {50.0, 1.75, 0.0, "Middle of lane 1"},   {50.0, 5.25, 0.0, "Middle of lane 2"},
      {25.0, 3.5, 0.0, "On lane boundary"},    {0.0, 0.0, 0.0, "At road start corner"},
      {100.0, 7.0, 0.0, "At road end corner"}, {50.0, 10.0, 0.0, "Outside road (3m from edge)"},
  };

  std::cout << "\nInertial Position -> Nearest Lane:" << std::endl;
  std::cout << std::string(90, '-') << std::endl;
  std::cout << std::setw(25) << std::left << "Description" << std::setw(20) << "Query (x,y,z)" << std::setw(20)
            << "Nearest Lane" << std::setw(25) << "Lane Pos (s,r,h)" << std::endl;
  std::cout << std::string(90, '-') << std::endl;

  for (const auto& pt : query_points) {
    const maliput::api::InertialPosition inertial_pos(pt.x, pt.y, pt.z);
    const maliput::api::RoadPositionResult result = road_geometry->ToRoadPosition(inertial_pos);

    std::cout << std::setw(25) << std::left << pt.description << std::fixed << std::setprecision(1) << "(" << pt.x
              << ", " << pt.y << ", " << pt.z << ")" << std::setw(8) << " " << std::setw(20)
              << result.road_position.lane->id().string() << "(" << result.road_position.pos.s() << ", "
              << std::setprecision(2) << result.road_position.pos.r() << ", " << result.road_position.pos.h() << ")"
              << std::endl;
  }
}

/// Demonstrates lane traversal.
void DemonstrateLaneTraversal(const maliput::api::RoadGeometry* road_geometry) {
  PrintHeader("Lane Traversal");

  std::cout << "Traversing all lanes in the road network:\n" << std::endl;

  for (int i = 0; i < road_geometry->num_junctions(); ++i) {
    const auto* junction = road_geometry->junction(i);
    std::cout << "Junction: " << junction->id().string() << std::endl;

    for (int j = 0; j < junction->num_segments(); ++j) {
      const auto* segment = junction->segment(j);
      std::cout << "  Segment: " << segment->id().string() << std::endl;

      for (int k = 0; k < segment->num_lanes(); ++k) {
        const auto* lane = segment->lane(k);
        PrintLaneDetails(lane);
      }
    }
  }
}

/// Demonstrates branch point queries.
void DemonstrateBranchPoints(const maliput::api::RoadGeometry* road_geometry) {
  PrintHeader("Branch Points");

  std::cout << "Number of Branch Points: " << road_geometry->num_branch_points() << std::endl;

  for (int i = 0; i < road_geometry->num_branch_points(); ++i) {
    const auto* bp = road_geometry->branch_point(i);
    std::cout << "\nBranch Point: " << bp->id().string() << std::endl;

    std::cout << "  A-Side lanes (" << bp->GetASide()->size() << "):" << std::endl;
    for (int j = 0; j < bp->GetASide()->size(); ++j) {
      const auto& lane_end = bp->GetASide()->get(j);
      std::cout << "    - " << lane_end.lane->id().string() << " @ "
                << (lane_end.end == maliput::api::LaneEnd::Which::kStart ? "start" : "finish") << std::endl;
    }

    std::cout << "  B-Side lanes (" << bp->GetBSide()->size() << "):" << std::endl;
    for (int j = 0; j < bp->GetBSide()->size(); ++j) {
      const auto& lane_end = bp->GetBSide()->get(j);
      std::cout << "    - " << lane_end.lane->id().string() << " @ "
                << (lane_end.end == maliput::api::LaneEnd::Which::kStart ? "start" : "finish") << std::endl;
    }
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  // Set up logging (use MALIPUT_LOG_LEVEL env var or default to "info")
  const char* log_level_env = std::getenv("MALIPUT_LOG_LEVEL");
  maliput::common::set_log_level(log_level_env ? log_level_env : "info");

  // Check command line arguments
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <path_to_gpkg_file>" << std::endl;
    std::cerr << "Example: " << argv[0] << " ./two_lane_road.gpkg" << std::endl;
    return 1;
  }

  const std::string gpkg_file_path = argv[1];

  std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
  std::cout << "║        Maliput GeoPackage Query Example                      ║" << std::endl;
  std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;
  std::cout << "\nLoading GeoPackage file: " << gpkg_file_path << std::endl;

  try {
    // Build the road network from the GeoPackage file
    std::map<std::string, std::string> builder_config = {
        {maliput_geopackage::builder::params::kGpkgFile, gpkg_file_path},
        {maliput_geopackage::builder::params::kLinearTolerance, "0.01"},
        {maliput_geopackage::builder::params::kAngularTolerance, "0.01"},
    };

    std::unique_ptr<maliput::api::RoadNetwork> road_network =
        maliput_geopackage::builder::RoadNetworkBuilder(builder_config)();

    const maliput::api::RoadGeometry* road_geometry = road_network->road_geometry();

    // Run demonstrations
    PrintRoadNetworkStats(road_geometry);
    DemonstrateLaneTraversal(road_geometry);
    DemonstrateBranchPoints(road_geometry);

    // Get first lane for coordinate transform demo
    if (road_geometry->num_junctions() > 0) {
      const auto* junction = road_geometry->junction(0);
      if (junction->num_segments() > 0) {
        const auto* segment = junction->segment(0);
        if (segment->num_lanes() > 0) {
          DemonstrateCoordinateTransforms(segment->lane(0));
        }
      }
    }

    DemonstrateFindRoadPosition(road_geometry);

    std::cout << "\n";
    PrintSeparator('=');
    std::cout << "  Example completed successfully!" << std::endl;
    PrintSeparator('=');

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
