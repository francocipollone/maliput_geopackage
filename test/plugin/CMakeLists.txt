##############################################################################
# Plugin Tests
##############################################################################

ament_add_gtest(road_network_plugin_test road_network_plugin_test.cc)
target_link_libraries(road_network_plugin_test
  maliput::api
  maliput::plugin
)
target_compile_definitions(road_network_plugin_test
  PRIVATE
    TEST_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources/"
    TEST_PLUGIN_LIBDIR="${CMAKE_BINARY_DIR}/src/plugin/"
)

# Set the plugin path so the test can find the plugin
set_tests_properties(road_network_plugin_test
  PROPERTIES
    ENVIRONMENT "MALIPUT_PLUGIN_PATH=${CMAKE_BINARY_DIR}/src/plugin"
)
