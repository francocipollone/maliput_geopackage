##############################################################################
# Tests
##############################################################################

ament_add_gtest(wkt_parser_test wkt_parser_test.cc)
target_link_libraries(wkt_parser_test
  maliput_geopackage::geopackage
)

ament_add_gtest(geopackage_parser_test geopackage_parser_test.cc)
target_link_libraries(geopackage_parser_test
  maliput_geopackage::geopackage
)
target_compile_definitions(geopackage_parser_test
  PRIVATE
    TEST_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/"
)

##############################################################################
# Plugin Tests
##############################################################################
add_subdirectory(plugin)
