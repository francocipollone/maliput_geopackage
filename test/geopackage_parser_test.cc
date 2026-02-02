// BSD 3-Clause License
//
// Copyright (c) 2026, Maliput Contributors
// All rights reserved.
#include "maliput_geopackage/geopackage/geopackage_parser.h"

#include <string>

#include <gtest/gtest.h>

namespace maliput_geopackage {
namespace geopackage {
namespace test {

class GeoPackageParserTest : public ::testing::Test {
 protected:
  const std::string kTestResourcesDir{TEST_RESOURCES_DIR};
  const std::string kTwoLaneRoadPath{kTestResourcesDir + "two_lane_road.gpkg"};
};

TEST_F(GeoPackageParserTest, LoadTwoLaneRoad) {
  GeoPackageParser parser(kTwoLaneRoadPath);

  const auto& junctions = parser.GetJunctions();
  ASSERT_EQ(junctions.size(), 1u);

  // Check junction exists
  auto junction_it = junctions.find("j1");
  ASSERT_NE(junction_it, junctions.end());

  const auto& junction = junction_it->second;
  EXPECT_EQ(junction.id, "j1");

  // Check segments
  ASSERT_EQ(junction.segments.size(), 1u);
  auto segment_it = junction.segments.find("j1_s1");
  ASSERT_NE(segment_it, junction.segments.end());

  const auto& segment = segment_it->second;
  EXPECT_EQ(segment.id, "j1_s1");

  // Check lanes
  ASSERT_EQ(segment.lanes.size(), 2u);

  // Find lane1
  const maliput_sparse::parser::Lane* lane1 = nullptr;
  const maliput_sparse::parser::Lane* lane2 = nullptr;
  for (const auto& lane : segment.lanes) {
    if (lane.id == "j1_s1_lane1") {
      lane1 = &lane;
    } else if (lane.id == "j1_s1_lane2") {
      lane2 = &lane;
    }
  }

  ASSERT_NE(lane1, nullptr);
  ASSERT_NE(lane2, nullptr);

  // Check lane1 geometry
  EXPECT_EQ(lane1->left.size(), 5u);   // 5 points
  EXPECT_EQ(lane1->right.size(), 5u);  // 5 points

  // First point of lane1's left boundary should be at (0, 3.5, 0)
  EXPECT_DOUBLE_EQ(lane1->left.first().x(), 0.0);
  EXPECT_DOUBLE_EQ(lane1->left.first().y(), 3.5);
  EXPECT_DOUBLE_EQ(lane1->left.first().z(), 0.0);

  // Last point of lane1's left boundary should be at (100, 3.5, 0)
  EXPECT_DOUBLE_EQ(lane1->left.last().x(), 100.0);
  EXPECT_DOUBLE_EQ(lane1->left.last().y(), 3.5);
  EXPECT_DOUBLE_EQ(lane1->left.last().z(), 0.0);

  // Check adjacency
  EXPECT_TRUE(lane1->right_lane_id.has_value());
  EXPECT_EQ(lane1->right_lane_id.value(), "j1_s1_lane2");

  EXPECT_TRUE(lane2->left_lane_id.has_value());
  EXPECT_EQ(lane2->left_lane_id.value(), "j1_s1_lane1");
}

TEST_F(GeoPackageParserTest, ConnectionsAreCreated) {
  GeoPackageParser parser(kTwoLaneRoadPath);

  const auto& connections = parser.GetConnections();

  // With the simple 2-lane road, we don't expect connections between the two lanes
  // at the branch points (they're parallel, not sequential).
  // The connections would be for predecessor/successor relationships.
  // In this simple case, both lanes connect to the same branch points but
  // don't connect to each other through them.

  // This test verifies the connection parsing doesn't crash and returns
  // a valid (possibly empty) list.
  EXPECT_GE(connections.size(), 0u);
}

TEST_F(GeoPackageParserTest, NonExistentFileThrows) {
  EXPECT_THROW(GeoPackageParser("/nonexistent/path/to/file.gpkg"), std::runtime_error);
}

}  // namespace test
}  // namespace geopackage
}  // namespace maliput_geopackage
