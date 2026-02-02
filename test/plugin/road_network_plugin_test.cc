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
#include <cstdlib>
#include <map>
#include <memory>
#include <string>

#include <gtest/gtest.h>
#include <maliput/common/filesystem.h>
#include <maliput/plugin/maliput_plugin.h>
#include <maliput/plugin/maliput_plugin_manager.h>
#include <maliput/plugin/maliput_plugin_type.h>
#include <maliput/plugin/road_network_loader.h>

namespace maliput_geopackage {
namespace test {
namespace {

class RoadNetworkPluginTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Save the current MALIPUT_PLUGIN_PATH
    back_up_env_ = maliput::common::Filesystem::get_env_path(kEnvName);
    // Set the plugin path to where our plugin library is built
    ASSERT_EQ(0, setenv(kEnvName.c_str(), kPluginPath.c_str(), 1 /*replace*/));
  }

  void TearDown() override {
    // Restore the original MALIPUT_PLUGIN_PATH
    ASSERT_EQ(0, setenv(kEnvName.c_str(), back_up_env_.c_str(), 1 /*replace*/));
  }

  const std::string kEnvName{"MALIPUT_PLUGIN_PATH"};
  const std::string kPluginPath{TEST_PLUGIN_LIBDIR};
  const std::string kGpkgFile{TEST_RESOURCES_DIR "two_lane_road.gpkg"};

 private:
  std::string back_up_env_;
};

GTEST_TEST(RoadNetworkLoader, VerifyPluginLoads) {
  // RoadNetworkLoader plugin id.
  const maliput::plugin::MaliputPlugin::Id kPluginId{"maliput_geopackage"};

  // Check MaliputPlugin existence.
  maliput::plugin::MaliputPluginManager manager{};
  const maliput::plugin::MaliputPlugin* rn_plugin{manager.GetPlugin(kPluginId)};
  ASSERT_NE(nullptr, rn_plugin);

  // Check maliput_geopackage plugin is obtained.
  EXPECT_EQ(kPluginId.string(), rn_plugin->GetId());
  EXPECT_EQ(maliput::plugin::MaliputPluginType::kRoadNetworkLoader, rn_plugin->GetType());

  // Check plugin can be loaded
  maliput::plugin::RoadNetworkLoaderPtr rn_loader_ptr{nullptr};
  EXPECT_NO_THROW(rn_loader_ptr = rn_plugin->ExecuteSymbol<maliput::plugin::RoadNetworkLoaderPtr>(
                      maliput::plugin::RoadNetworkLoader::GetEntryPoint()));
  ASSERT_NE(nullptr, rn_loader_ptr);
}

TEST_F(RoadNetworkPluginTest, VerifyRoadNetworkCreation) {
  // RoadNetworkLoader plugin id.
  const maliput::plugin::MaliputPlugin::Id kPluginId{"maliput_geopackage"};

  // maliput_geopackage properties needed for loading a road geometry.
  const std::map<std::string, std::string> rg_properties{
      {"road_geometry_id", "maliput_geopackage road geometry"},
      {"gpkg_file", kGpkgFile},
      {"linear_tolerance", "1e-2"},
      {"angular_tolerance", "1e-2"},
  };

  // Get plugin
  maliput::plugin::MaliputPluginManager manager{};
  const maliput::plugin::MaliputPlugin* rn_plugin{manager.GetPlugin(kPluginId)};
  ASSERT_NE(nullptr, rn_plugin);

  maliput::plugin::RoadNetworkLoaderPtr rn_loader_ptr = rn_plugin->ExecuteSymbol<maliput::plugin::RoadNetworkLoaderPtr>(
      maliput::plugin::RoadNetworkLoader::GetEntryPoint());
  ASSERT_NE(nullptr, rn_loader_ptr);

  std::unique_ptr<maliput::plugin::RoadNetworkLoader> rn_loader{
      reinterpret_cast<maliput::plugin::RoadNetworkLoader*>(rn_loader_ptr)};

  // Check maliput_geopackage RoadNetwork is constructible.
  std::unique_ptr<const maliput::api::RoadNetwork> rn = (*rn_loader)(rg_properties);
  ASSERT_NE(nullptr, rn);
  ASSERT_NE(nullptr, rn->road_geometry());

  // Verify basic structure
  EXPECT_EQ(1, rn->road_geometry()->num_junctions());
}

TEST_F(RoadNetworkPluginTest, GetDefaultParameters) {
  // RoadNetworkLoader plugin id.
  const maliput::plugin::MaliputPlugin::Id kPluginId{"maliput_geopackage"};

  // Get plugin
  maliput::plugin::MaliputPluginManager manager{};
  const maliput::plugin::MaliputPlugin* rn_plugin{manager.GetPlugin(kPluginId)};
  ASSERT_NE(nullptr, rn_plugin);

  maliput::plugin::RoadNetworkLoaderPtr rn_loader_ptr = rn_plugin->ExecuteSymbol<maliput::plugin::RoadNetworkLoaderPtr>(
      maliput::plugin::RoadNetworkLoader::GetEntryPoint());
  ASSERT_NE(nullptr, rn_loader_ptr);

  std::unique_ptr<maliput::plugin::RoadNetworkLoader> rn_loader{
      reinterpret_cast<maliput::plugin::RoadNetworkLoader*>(rn_loader_ptr)};

  // Check default parameters are returned
  const auto default_params = rn_loader->GetDefaultParameters();
  EXPECT_FALSE(default_params.empty());

  // Check that expected keys exist
  EXPECT_TRUE(default_params.find("road_geometry_id") != default_params.end());
  EXPECT_TRUE(default_params.find("linear_tolerance") != default_params.end());
  EXPECT_TRUE(default_params.find("angular_tolerance") != default_params.end());
}

}  // namespace
}  // namespace test
}  // namespace maliput_geopackage
