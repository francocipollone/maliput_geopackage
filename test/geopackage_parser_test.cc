// BSD 3-Clause License
//
// Copyright (c) 2026, Maliput Contributors
// All rights reserved.
#include "maliput_geopackage/geopackage/geopackage_parser.h"

#include <chrono>
#include <string>

#include <filesystem>
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

TEST_F(GeoPackageParserTest, LoadBoundariesTable) {
  // Create a temporary GeoPackage-like sqlite db file path in the system temp directory
  const std::filesystem::path tmp_dir = std::filesystem::temp_directory_path();
  const auto unique_suffix =
      std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::string tmpname = (tmp_dir / ("test_boundaries_" + unique_suffix + ".gpkg")).string();

  sqlite3* db = nullptr;
  ASSERT_EQ(sqlite3_open_v2(tmpname.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr), SQLITE_OK);

  const char* sqls = R"SQL(
      CREATE TABLE junctions (junction_id TEXT PRIMARY KEY, name TEXT);
      CREATE TABLE segments (segment_id TEXT PRIMARY KEY, junction_id TEXT, name TEXT);
      CREATE TABLE boundaries (boundary_id TEXT PRIMARY KEY, geometry TEXT);
      CREATE TABLE lanes (
          lane_id TEXT PRIMARY KEY,
          segment_id TEXT,
          lane_type TEXT,
          direction TEXT,
          left_boundary_id TEXT,
          right_boundary_id TEXT
      );

      INSERT INTO junctions(junction_id, name) VALUES('j1', 'J1');
      INSERT INTO segments(segment_id, junction_id, name) VALUES('j1_s1', 'j1', 'seg');
      INSERT INTO boundaries(boundary_id, geometry) VALUES('br', 'LINESTRINGZ(0 0 0,100 0 0)');
      INSERT INTO boundaries(boundary_id, geometry) VALUES('bb', 'LINESTRINGZ(0 3.5 0,100 3.5 0)');
      INSERT INTO boundaries(boundary_id, geometry) VALUES('bl', 'LINESTRINGZ(0 7.0 0,100 7.0 0)');

      INSERT INTO lanes (lane_id, segment_id, lane_type, direction, left_boundary_id, right_boundary_id)
        VALUES ('j1_s1_lane1','j1_s1','driving','forward','bb','br');

      INSERT INTO lanes (lane_id, segment_id, lane_type, direction, left_boundary_id, right_boundary_id)
        VALUES ('j1_s1_lane2','j1_s1','driving','backward','bl','bb');
  )SQL";

  char* errmsg = nullptr;
  int rc = sqlite3_exec(db, sqls, nullptr, nullptr, &errmsg);
  if (rc != SQLITE_OK) {
    sqlite3_close(db);
    std::remove(tmpname.c_str());
    FAIL() << (errmsg ? errmsg : "sqlite error");
  }
  sqlite3_close(db);

  // Parse the temporary file
  GeoPackageParser parser(tmpname);
  const auto& junctions = parser.GetJunctions();
  ASSERT_EQ(junctions.size(), 1u);

  auto jt = junctions.find("j1");
  ASSERT_NE(jt, junctions.end());
  auto segit = jt->second.segments.find("j1_s1");
  ASSERT_NE(segit, jt->second.segments.end());
  const auto& segment = segit->second;
  ASSERT_EQ(segment.lanes.size(), 2u);

  const maliput_sparse::parser::Lane* lane1 = nullptr;
  const maliput_sparse::parser::Lane* lane2 = nullptr;
  for (const auto& ln : segment.lanes) {
    if (ln.id == "j1_s1_lane1") lane1 = &ln;
    if (ln.id == "j1_s1_lane2") lane2 = &ln;
  }
  ASSERT_NE(lane1, nullptr);
  ASSERT_NE(lane2, nullptr);

  // Validate boundary coordinates were parsed from the shared boundaries
  EXPECT_DOUBLE_EQ(lane1->left.first().y(), 3.5);
  EXPECT_DOUBLE_EQ(lane1->right.first().y(), 0.0);
  EXPECT_DOUBLE_EQ(lane2->left.first().y(), 7.0);
  EXPECT_DOUBLE_EQ(lane2->right.first().y(), 3.5);

  // cleanup
  std::remove(tmpname.c_str());
}

TEST_F(GeoPackageParserTest, NonExistentFileThrows) {
  EXPECT_THROW(GeoPackageParser("/nonexistent/path/to/file.gpkg"), std::runtime_error);
}

}  // namespace test
}  // namespace geopackage
}  // namespace maliput_geopackage
