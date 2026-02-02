##############################################################################
# Examples
##############################################################################

add_executable(geopackage_query_example
  geopackage_query_example.cc
)

target_link_libraries(geopackage_query_example
  PRIVATE
    maliput::api
    maliput::common
    maliput_geopackage::builder
)

install(TARGETS geopackage_query_example
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

# Install example resources
install(
  FILES
    ${CMAKE_SOURCE_DIR}/resources/two_lane_road.gpkg
  DESTINATION share/${PROJECT_NAME}/examples
)
