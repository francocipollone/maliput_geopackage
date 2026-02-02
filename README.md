# maliput_geopackage

## Description

`maliput_geopackage` is a [Maliput](https://github.com/maliput/maliput) backend implementation that loads road network data from [GeoPackage](https://www.geopackage.org/) files.

GeoPackage is an OGC standard format that uses SQLite as its container, providing:
- Spatial indexing (R-tree) for efficient queries
- Schema enforcement at database level
- Compatibility with GIS tools (QGIS, GDAL, etc.)
- Single-file distribution

This backend relies on [maliput_sparse](https://github.com/maliput/maliput_sparse) for building the road geometry from sampled lane boundaries.

## Documentation

- **[GeoPackage Schema](docs/geopackage_schema.md)**: Complete schema specification with examples

## Schema Overview

The GeoPackage schema is designed to map directly to maliput concepts:

| Table | Purpose |
|-------|---------|
| `maliput_metadata` | Road network configuration (tolerances, scale, etc.) |
| `junctions` | Junction definitions |
| `segments` | Segment definitions (groups of lanes) |
| `lanes` | Lane geometries (left/right boundaries as LINESTRINGZ) |
| `branch_point_lanes` | Lane-to-BranchPoint relationships |
| `adjacent_lanes` | Lane adjacency for lane changes |

See [docs/geopackage_schema.md](docs/geopackage_schema.md) for the complete schema specification with SQL examples.

## Build

```bash
colcon build --packages-select maliput_geopackage
```

## Usage

### Basic Example

```cpp
#include <maliput_geopackage/builder/road_network_builder.h>

const std::map<std::string, std::string> builder_config {
  {"gpkg_file", "/path/to/road_network.gpkg"},
  {"road_geometry_id", "my_road_network"},
  {"linear_tolerance", "0.01"},
  {"angular_tolerance", "0.01"},
};

auto road_network = maliput_geopackage::builder::RoadNetworkBuilder(builder_config)();
```

### Running the Query Example

The package includes an example that demonstrates common road network queries:

```bash
# After building, run from the workspace
source install/setup.bash
./install/maliput_geopackage/lib/maliput_geopackage/geopackage_query_example \
  ./install/maliput_geopackage/share/maliput_geopackage/examples/two_lane_road.gpkg
```

The example demonstrates:
- Loading a GeoPackage road network
- Querying road network statistics
- Traversing junctions, segments, and lanes
- Coordinate transformations (lane ↔ inertial)
- Finding the nearest lane to a point
- Branch point connectivity queries

## Dependencies

- [maliput](https://github.com/maliput/maliput)
- [maliput_sparse](https://github.com/maliput/maliput_sparse)
- SQLite3

## License

BSD 3-Clause License. See [LICENSE](LICENSE) file.
