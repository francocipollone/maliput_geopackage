// BSD 3-Clause License
//
// Copyright (c) 2026, Maliput Contributors
// All rights reserved.
#include "maliput_geopackage/geopackage/wkt_parser.h"

#include <gtest/gtest.h>

namespace maliput_geopackage {
namespace geopackage {
namespace test {

TEST(WktParserTest, ParseLineStringZ) {
  const std::string wkt = "LINESTRINGZ(0 0 0, 10 5 1, 20 10 2)";
  const auto points = ParseLineStringZ(wkt);

  ASSERT_EQ(points.size(), 3u);
  EXPECT_DOUBLE_EQ(points[0].x(), 0.0);
  EXPECT_DOUBLE_EQ(points[0].y(), 0.0);
  EXPECT_DOUBLE_EQ(points[0].z(), 0.0);

  EXPECT_DOUBLE_EQ(points[1].x(), 10.0);
  EXPECT_DOUBLE_EQ(points[1].y(), 5.0);
  EXPECT_DOUBLE_EQ(points[1].z(), 1.0);

  EXPECT_DOUBLE_EQ(points[2].x(), 20.0);
  EXPECT_DOUBLE_EQ(points[2].y(), 10.0);
  EXPECT_DOUBLE_EQ(points[2].z(), 2.0);
}

TEST(WktParserTest, ParseLineStringZWithSpaces) {
  // LINESTRING Z format (space between LINESTRING and Z)
  const std::string wkt = "LINESTRING Z (0 0 0, 100 0 0)";
  const auto points = ParseLineStringZ(wkt);

  ASSERT_EQ(points.size(), 2u);
  EXPECT_DOUBLE_EQ(points[0].x(), 0.0);
  EXPECT_DOUBLE_EQ(points[1].x(), 100.0);
}

TEST(WktParserTest, ParseLineStringZManyPoints) {
  const std::string wkt = "LINESTRINGZ(0 3.5 0, 25 3.5 0, 50 3.5 0, 75 3.5 0, 100 3.5 0)";
  const auto points = ParseLineStringZ(wkt);

  ASSERT_EQ(points.size(), 5u);
  for (const auto& p : points) {
    EXPECT_DOUBLE_EQ(p.y(), 3.5);
    EXPECT_DOUBLE_EQ(p.z(), 0.0);
  }
}

TEST(WktParserTest, ParsePointZ) {
  const std::string wkt = "POINTZ(10 5 1)";
  const auto point = ParsePointZ(wkt);

  EXPECT_DOUBLE_EQ(point.x(), 10.0);
  EXPECT_DOUBLE_EQ(point.y(), 5.0);
  EXPECT_DOUBLE_EQ(point.z(), 1.0);
}

TEST(WktParserTest, ParsePointZWithSpace) {
  const std::string wkt = "POINT Z (10 5 1)";
  const auto point = ParsePointZ(wkt);

  EXPECT_DOUBLE_EQ(point.x(), 10.0);
  EXPECT_DOUBLE_EQ(point.y(), 5.0);
  EXPECT_DOUBLE_EQ(point.z(), 1.0);
}

TEST(WktParserTest, InvalidLineStringThrows) {
  EXPECT_THROW(ParseLineStringZ("NOT_A_LINESTRING"), std::runtime_error);
  EXPECT_THROW(ParseLineStringZ("LINESTRINGZ(0 0 0)"), std::runtime_error);  // Only 1 point
  EXPECT_THROW(ParseLineStringZ("LINESTRINGZ()"), std::runtime_error);
}

TEST(WktParserTest, InvalidPointThrows) {
  EXPECT_THROW(ParsePointZ("NOT_A_POINT"), std::runtime_error);
  EXPECT_THROW(ParsePointZ("POINTZ()"), std::runtime_error);
}

}  // namespace test
}  // namespace geopackage
}  // namespace maliput_geopackage
