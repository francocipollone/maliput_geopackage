# Maliput GeoPackage Schema

This document describes the GeoPackage schema used by `maliput_geopackage` to store HD-map data compatible with the maliput road network abstraction.

## Overview

The maliput GeoPackage format stores road network data in a SQLite database following the [OGC GeoPackage](https://www.geopackage.org/) standard. This format provides:

- **Portability**: Single-file database that works across platforms
- **Spatial indexing**: Efficient geometric queries via SpatiaLite extensions
- **Self-documenting**: Schema is queryable and human-readable
- **Tooling support**: Compatible with QGIS, GDAL, and other GIS tools

## Schema Definition

### Core Tables

#### `maliput_metadata`

Stores key-value pairs for road network configuration.

```sql
CREATE TABLE maliput_metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
```

**Common metadata keys:**

| Key | Description | Example |
|-----|-------------|---------|
| `linear_tolerance` | Tolerance for linear operations (meters) | `0.01` |
| `angular_tolerance` | Tolerance for angular operations (radians) | `0.01` |
| `scale_length` | Scale length for road geometry | `1.0` |
| `inertial_to_backend_frame_translation` | Translation vector `{x, y, z}` | `{0.0, 0.0, 0.0}` |

---

#### `junctions`

Defines junctions in the road network. A junction is a collection of segments that share common branch points.

```sql
CREATE TABLE junctions (
    junction_id TEXT PRIMARY KEY,
    name TEXT
);
```

| Column | Type | Description |
|--------|------|-------------|
| `junction_id` | TEXT | Unique identifier for the junction |
| `name` | TEXT | Human-readable name (optional) |

---

#### `segments`

Defines segments within junctions. A segment is a collection of parallel lanes.

```sql
CREATE TABLE segments (
    segment_id TEXT PRIMARY KEY,
    junction_id TEXT NOT NULL,
    name TEXT,
    FOREIGN KEY (junction_id) REFERENCES junctions(junction_id)
);
```

| Column | Type | Description |
|--------|------|-------------|
| `segment_id` | TEXT | Unique identifier for the segment |
| `junction_id` | TEXT | Parent junction ID |
| `name` | TEXT | Human-readable name (optional) |

---

#### `lanes`

Defines lanes which reference left and right boundary geometries stored in the `boundaries` table. Boundaries are shared 3D LineStrings (WKT LINESTRINGZ) that can be reused across multiple lanes to avoid duplication.

```sql
-- Boundaries table: shared LINESTRINGZ geometries referenced by lanes
CREATE TABLE IF NOT EXISTS boundaries (
    boundary_id TEXT PRIMARY KEY,
    geometry TEXT NOT NULL  -- WKT LINESTRINGZ(x1 y1 z1, x2 y2 z2, ...)
);

-- Lanes now reference boundary IDs instead of storing WKT directly
CREATE TABLE lanes (
    lane_id TEXT PRIMARY KEY,
    segment_id TEXT NOT NULL,
    lane_type TEXT DEFAULT 'driving',
    direction TEXT DEFAULT 'forward',
    left_boundary_id TEXT NOT NULL,   -- FOREIGN KEY -> boundaries(boundary_id)
    right_boundary_id TEXT NOT NULL,  -- FOREIGN KEY -> boundaries(boundary_id)
    FOREIGN KEY (segment_id) REFERENCES segments(segment_id)
    FOREIGN KEY (left_boundary_id) REFERENCES boundaries(boundary_id),
    FOREIGN KEY (right_boundary_id) REFERENCES boundaries(boundary_id)
);
```

| Column | Type | Description |
|--------|------|-------------|
| `lane_id` | TEXT | Unique identifier for the lane |
| `segment_id` | TEXT | Parent segment ID |
| `lane_type` | TEXT | Lane type: `driving`, `shoulder`, `parking`, etc. |
| `direction` | TEXT | Travel direction: `forward`, `backward`, `bidirectional` |
| `left_boundary_id` | TEXT | Reference to a `boundaries.boundary_id` containing the left boundary WKT LINESTRINGZ |
| `right_boundary_id` | TEXT | Reference to a `boundaries.boundary_id` containing the right boundary WKT LINESTRINGZ |

**Note:** For backwards compatibility some GeoPackages may still include `left_boundary` and `right_boundary` WKT columns; both formats are supported by the parser.

**Geometry Format:**

Boundaries are stored as Well-Known Text (WKT) 3D LineStrings:

```
LINESTRINGZ(x1 y1 z1, x2 y2 z2, x3 y3 z3, ...)
```

The coordinates represent points in the inertial frame (typically ENU - East-North-Up).
#### `boundaries`

Stores shared boundary geometries as WKT LINESTRINGZ. These are referenced from `lanes` by ID to avoid duplicating identical boundary geometry when adjacent lanes share a common edge.

```sql
CREATE TABLE IF NOT EXISTS boundaries (
    boundary_id TEXT PRIMARY KEY,
    geometry TEXT NOT NULL  -- WKT LINESTRINGZ(x1 y1 z1, x2 y2 z2, ...)
);
```

| Column | Type | Description |
|--------|------|-------------|
| `boundary_id` | TEXT | Unique identifier for the boundary |
| `geometry` | TEXT | WKT LINESTRINGZ geometry string |

**Geometry Format:**

Boundaries are stored as Well-Known Text (WKT) 3D LineStrings:

```
LINESTRINGZ(x1 y1 z1, x2 y2 z2, x3 y3 z3, ...)
```

The coordinates represent points in the inertial frame (typically ENU - East-North-Up).

---

#### `lanes`

Defines lanes which reference left and right boundary geometries stored in the `boundaries` table.

```sql
CREATE TABLE lanes (
    lane_id TEXT PRIMARY KEY,
    segment_id TEXT NOT NULL,
    lane_type TEXT DEFAULT 'driving',
    direction TEXT DEFAULT 'forward',
    left_boundary_id TEXT NOT NULL,
    left_inverted BOOLEAN DEFAULT FALSE,
    right_boundary_id TEXT NOT NULL,
    right_inverted BOOLEAN DEFAULT FALSE,
    FOREIGN KEY (segment_id) REFERENCES segments(segment_id),
    FOREIGN KEY (left_boundary_id) REFERENCES boundaries(boundary_id),
    FOREIGN KEY (right_boundary_id) REFERENCES boundaries(boundary_id)
);
```

| Column | Type | Description |
|--------|------|-------------|
| `lane_id` | TEXT | Unique identifier for the lane |
| `segment_id` | TEXT | Parent segment ID |
| `lane_type` | TEXT | Lane type: `driving`, `shoulder`, `parking`, etc. |
| `direction` | TEXT | Travel direction: `forward`, `backward`, `bidirectional` |
| `left_boundary_id` | TEXT | Reference to a `boundaries.boundary_id` |
| `left_inverted` | BOOLEAN | If TRUE, iterate left boundary points in reverse order |
| `right_boundary_id` | TEXT | Reference to a `boundaries.boundary_id` |
| `right_inverted` | BOOLEAN | If TRUE, iterate right boundary points in reverse order |

**Note:** For backwards compatibility some GeoPackages may still include `left_boundary` and `right_boundary` WKT columns; both formats are supported by the parser.

---

##### Boundary Inversion Flags: Rationale

The `left_inverted` and `right_inverted` flags solve a critical problem when working with boundary geometries from various sources (GIS tools, automated extraction, format conversions like Lanelet2).

**The Problem:**

Boundary geometries may be stored with inconsistent point ordering. For example, when drawing lanes in a GIS tool, a user might draw:
- Left boundary from south to north
- Right boundary from north to south

This results in "twisted" lane geometry where the boundaries cross each other, which is geometrically invalid.

```
❌ WITHOUT inversion flags - Twisted lane:

  Left:   L1 -----> L2 -----> L3      (points go left-to-right)
  Right:  R3 <----- R2 <----- R1      (points go right-to-left)
  
  At "start": Left is at x=0, Right is at x=100 → INVALID
```

**The Solution:**

The inversion flags allow boundaries to remain stored as-is while indicating how they should be interpreted:

```
✓ WITH inversion flags - Consistent lane:

  Stored boundaries (unchanged):
    Left (b1):  L1 → L2 → L3         (x=0 to x=100)
    Right (b2): R3 → R2 → R1         (x=100 to x=0)
  
  Lane definition:
    left_boundary_id = 'b1', left_inverted = FALSE
    right_boundary_id = 'b2', right_inverted = TRUE   ← fixes orientation!
  
  After applying inversions:
    Left:  L1 → L2 → L3   (x=0 to x=100)
    Right: R1 → R2 → R3   (x=0 to x=100)  ✓ Consistent!
```

**Processing Order:**

```
Step 1: Get raw boundary points from storage
        ↓
Step 2: Apply per-boundary inversion flags (left_inverted, right_inverted)
        → This makes both boundaries geometrically consistent
        ↓
Step 3: The `direction` field indicates travel semantics (forward/backward)
        → This is metadata for routing, not geometry processing
```

**Benefits:**

1. **Boundaries stay immutable** - no need to modify stored geometry
2. **Maximum sharing** - same boundary can be used normally in one lane, inverted in another
3. **Explicit** - clear what's happening, easy to debug
4. **Source compatibility** - matches Lanelet2 semantics where inverted references are common

**Relationship with `direction`:**

| Field | Purpose | Affects |
|-------|---------|---------|
| `left_inverted` / `right_inverted` | **Geometric**: Make boundary points consistent | Point iteration order |
| `direction` | **Semantic**: Which way does traffic flow? | Routing, s-coordinate direction |

The inversion flags fix geometry; `direction` describes traffic semantics on that now-consistent geometry.

---

### Connectivity Tables

#### `branch_point_lanes`

Defines how lanes connect at branch points. Branch points are the start and end points of lanes where they can connect to other lanes.

```sql
CREATE TABLE branch_point_lanes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    branch_point_id TEXT NOT NULL,
    lane_id TEXT NOT NULL,
    side TEXT NOT NULL CHECK (side IN ('a', 'b')),
    lane_end TEXT NOT NULL CHECK (lane_end IN ('start', 'finish')),
    FOREIGN KEY (lane_id) REFERENCES lanes(lane_id)
);
```

| Column | Type | Description |
|--------|------|-------------|
| `branch_point_id` | TEXT | Identifier for the branch point |
| `lane_id` | TEXT | Lane connected to this branch point |
| `side` | TEXT | Side of the branch point: `a` or `b` |
| `lane_end` | TEXT | Which end of the lane: `start` or `finish` |

**Branch Point Semantics:**

- A branch point connects lane ends that meet at the same physical location
- Side `a` lanes can transition to side `b` lanes (and vice versa)
- A straight road has two branch points: one at the start, one at the end
- An intersection may have multiple lanes on each side

Example for a simple 2-lane road:

```
Lane 1: start ──────────────────> finish
Lane 2: start ──────────────────> finish

BranchPoint "bp_start":
  - a-side: lane1/start, lane2/start
  - b-side: (empty, or connected upstream lanes)

BranchPoint "bp_end":
  - a-side: lane1/finish, lane2/finish  
  - b-side: (empty, or connected downstream lanes)
```

---

#### `adjacent_lanes`

Defines lateral adjacency between parallel lanes in the same segment.

```sql
CREATE TABLE adjacent_lanes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    lane_id TEXT NOT NULL,
    adjacent_lane_id TEXT NOT NULL,
    side TEXT NOT NULL CHECK (side IN ('left', 'right')),
    FOREIGN KEY (lane_id) REFERENCES lanes(lane_id),
    FOREIGN KEY (adjacent_lane_id) REFERENCES lanes(lane_id)
);
```

| Column | Type | Description |
|--------|------|-------------|
| `lane_id` | TEXT | The reference lane |
| `adjacent_lane_id` | TEXT | The adjacent lane |
| `side` | TEXT | Which side: `left` or `right` |

**Note:** Adjacency should be defined bidirectionally. If lane A has lane B on its left, then lane B should have lane A on its right.

---

## Complete Example

Here's a complete SQL script to create a simple 2-lane straight road:

```sql
-- Create tables
CREATE TABLE maliput_metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE junctions (
    junction_id TEXT PRIMARY KEY,
    name TEXT
);

CREATE TABLE segments (
    segment_id TEXT PRIMARY KEY,
    junction_id TEXT NOT NULL,
    name TEXT,
    FOREIGN KEY (junction_id) REFERENCES junctions(junction_id)
);

CREATE TABLE lanes (
    lane_id TEXT PRIMARY KEY,
    segment_id TEXT NOT NULL,
    lane_type TEXT DEFAULT 'driving',
    direction TEXT DEFAULT 'forward',
    left_boundary TEXT NOT NULL,
    right_boundary TEXT NOT NULL,
    FOREIGN KEY (segment_id) REFERENCES segments(segment_id)
);

CREATE TABLE branch_point_lanes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    branch_point_id TEXT NOT NULL,
    lane_id TEXT NOT NULL,
    side TEXT NOT NULL CHECK (side IN ('a', 'b')),
    lane_end TEXT NOT NULL CHECK (lane_end IN ('start', 'finish')),
    FOREIGN KEY (lane_id) REFERENCES lanes(lane_id)
);

CREATE TABLE adjacent_lanes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    lane_id TEXT NOT NULL,
    adjacent_lane_id TEXT NOT NULL,
    side TEXT NOT NULL CHECK (side IN ('left', 'right')),
    FOREIGN KEY (lane_id) REFERENCES lanes(lane_id),
    FOREIGN KEY (adjacent_lane_id) REFERENCES lanes(lane_id)
);

-- Insert metadata
INSERT INTO maliput_metadata (key, value) VALUES ('linear_tolerance', '0.01');
INSERT INTO maliput_metadata (key, value) VALUES ('angular_tolerance', '0.01');
INSERT INTO maliput_metadata (key, value) VALUES ('scale_length', '1.0');

-- Insert junction
INSERT INTO junctions (junction_id, name) VALUES ('j1', 'Main Road Junction');

-- Insert segment
INSERT INTO segments (segment_id, junction_id, name) VALUES ('j1_s1', 'j1', 'Main Road Segment');

-- Insert shared boundaries (100m straight road, 3.5m lane width each)
INSERT INTO boundaries (boundary_id, geometry) VALUES ('b_right', 'LINESTRINGZ(0 0 0, 25 0 0, 50 0 0, 75 0 0, 100 0 0)');
INSERT INTO boundaries (boundary_id, geometry) VALUES ('b_between', 'LINESTRINGZ(0 3.5 0, 25 3.5 0, 50 3.5 0, 75 3.5 0, 100 3.5 0)');
INSERT INTO boundaries (boundary_id, geometry) VALUES ('b_left', 'LINESTRINGZ(0 7.0 0, 25 7.0 0, 50 7.0 0, 75 7.0 0, 100 7.0 0)');

-- Insert lanes by referencing boundary IDs
-- Lane 1: y = 0 to y = 3.5
INSERT INTO lanes (lane_id, segment_id, lane_type, direction, left_boundary_id, right_boundary_id)
VALUES (
    'j1_s1_lane1',
    'j1_s1',
    'driving',
    'forward',
    'b_between',
    'b_right'
);

-- Lane 2: y = 3.5 to y = 7.0
INSERT INTO lanes (lane_id, segment_id, lane_type, direction, left_boundary_id, right_boundary_id)
VALUES (
    'j1_s1_lane2',
    'j1_s1',
    'driving',
    'forward',
    'b_left',
    'b_between'
);

-- Define branch points
-- Start branch point
INSERT INTO branch_point_lanes (branch_point_id, lane_id, side, lane_end)
VALUES ('bp_start', 'j1_s1_lane1', 'a', 'start');
INSERT INTO branch_point_lanes (branch_point_id, lane_id, side, lane_end)
VALUES ('bp_start', 'j1_s1_lane2', 'a', 'start');

-- End branch point
INSERT INTO branch_point_lanes (branch_point_id, lane_id, side, lane_end)
VALUES ('bp_end', 'j1_s1_lane1', 'a', 'finish');
INSERT INTO branch_point_lanes (branch_point_id, lane_id, side, lane_end)
VALUES ('bp_end', 'j1_s1_lane2', 'a', 'finish');

-- Define lane adjacency
INSERT INTO adjacent_lanes (lane_id, adjacent_lane_id, side)
VALUES ('j1_s1_lane1', 'j1_s1_lane2', 'left');
INSERT INTO adjacent_lanes (lane_id, adjacent_lane_id, side)
VALUES ('j1_s1_lane2', 'j1_s1_lane1', 'right');
```

---

## Visual Representation

```
                    y = 7.0
    ┌────────────────────────────────────────┐
    │              Lane 2                     │
    │  (j1_s1_lane2)                         │
    │                                        │
    ├────────────────────────────────────────┤ y = 3.5 (shared boundary)
    │              Lane 1                     │
    │  (j1_s1_lane1)                         │
    │                                        │
    └────────────────────────────────────────┘
   x=0                                      x=100
                    y = 0

   Direction: ─────────────────────────────────>
              (forward along positive x-axis)
```

---

## Best Practices

### Naming Conventions

- **Junction IDs**: `j1`, `j2`, ... or descriptive names like `intersection_main_oak`
- **Segment IDs**: `{junction_id}_s{n}`, e.g., `j1_s1`
- **Lane IDs**: `{segment_id}_lane{n}`, e.g., `j1_s1_lane1`
- **Branch Point IDs**: `bp_{location}` or `{junction_id}_bp_{n}`

### Geometry Guidelines

1. **Consistent direction**: All lane boundaries should follow the same direction (typically in the direction of travel)
2. **Sufficient sampling**: Include enough points to capture curves accurately
3. **Matching endpoints**: Left and right boundaries should have corresponding start/end points
4. **Z-coordinates**: Include elevation data when available; use 0 for flat roads

### Connectivity Guidelines

1. **Complete branch points**: Ensure all lane ends are associated with a branch point
2. **Bidirectional adjacency**: Define both directions of lane adjacency
3. **Consistent sides**: Use `a`/`b` sides consistently across branch points

---

## Tools and Utilities

### Viewing GeoPackage Files

- **QGIS**: Open source GIS that can visualize GeoPackage layers
- **SQLite Browser**: View and edit the database structure
- **ogr2ogr (GDAL)**: Convert between formats

### Creating GeoPackage Files

The `maliput_geopackage` package includes a Python utility for generating test files:

```bash
python3 test/resources/generate_test_gpkg.py
```

### Validating Schema

```bash
sqlite3 your_map.gpkg ".schema"
```

---

## Complete Schema Reference

The following is the complete SQL schema for creating a maliput-compatible GeoPackage. This can be used as a reference or executed directly to create an empty database ready for data insertion.

### Schema SQL

```sql
-- =============================================================================
-- Maliput GeoPackage Schema v1.0
-- 
-- This schema defines the table structure for storing HD-map road network data
-- compatible with the maliput road network abstraction.
--
-- Usage:
--   sqlite3 my_road_network.gpkg < maliput_schema.sql
--
-- Or in Python:
--   import sqlite3
--   conn = sqlite3.connect('my_road_network.gpkg')
--   conn.executescript(open('maliput_schema.sql').read())
-- =============================================================================

-- -----------------------------------------------------------------------------
-- Metadata Table
-- Stores configuration parameters for the road network
-- -----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS maliput_metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

-- -----------------------------------------------------------------------------
-- Junctions Table
-- A junction is a collection of segments that share common branch points
-- -----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS junctions (
    junction_id TEXT PRIMARY KEY,
    name TEXT
);

-- -----------------------------------------------------------------------------
-- Segments Table
-- A segment is a collection of parallel lanes within a junction
-- -----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS segments (
    segment_id TEXT PRIMARY KEY,
    junction_id TEXT NOT NULL,
    name TEXT,
    FOREIGN KEY (junction_id) REFERENCES junctions(junction_id)
        ON DELETE CASCADE
);

-- -----------------------------------------------------------------------------
-- Boundaries Table
-- Shared boundary geometries stored as WKT LINESTRINGZ. These are referenced
-- from `lanes` by ID to avoid duplicating identical boundary geometry.
-- -----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS boundaries (
    boundary_id TEXT PRIMARY KEY,
    geometry TEXT NOT NULL  -- WKT LINESTRINGZ(x1 y1 z1, x2 y2 z2, ...)
);

-- -----------------------------------------------------------------------------
-- Lanes Table
-- Defines lanes referencing boundary IDs in `boundaries` table
-- -----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS lanes (
    lane_id TEXT PRIMARY KEY,
    segment_id TEXT NOT NULL,
    lane_type TEXT DEFAULT 'driving' 
        CHECK (lane_type IN ('driving', 'shoulder', 'parking', 'biking', 'sidewalk', 'restricted')),
    direction TEXT DEFAULT 'forward'
        CHECK (direction IN ('forward', 'backward', 'bidirectional')),
    left_boundary_id TEXT NOT NULL,   -- FOREIGN KEY -> boundaries(boundary_id)
    right_boundary_id TEXT NOT NULL,  -- FOREIGN KEY -> boundaries(boundary_id)
    FOREIGN KEY (segment_id) REFERENCES segments(segment_id)
        ON DELETE CASCADE,
    FOREIGN KEY (left_boundary_id) REFERENCES boundaries(boundary_id)
        ON DELETE CASCADE,
    FOREIGN KEY (right_boundary_id) REFERENCES boundaries(boundary_id)
        ON DELETE CASCADE
);

-- -----------------------------------------------------------------------------
-- Branch Point Lanes Table
-- Defines how lanes connect at branch points (longitudinal connectivity)
-- -----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS branch_point_lanes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    branch_point_id TEXT NOT NULL,
    lane_id TEXT NOT NULL,
    side TEXT NOT NULL CHECK (side IN ('a', 'b')),
    lane_end TEXT NOT NULL CHECK (lane_end IN ('start', 'finish')),
    FOREIGN KEY (lane_id) REFERENCES lanes(lane_id)
        ON DELETE CASCADE,
    UNIQUE (branch_point_id, lane_id, lane_end)
);

-- -----------------------------------------------------------------------------
-- Adjacent Lanes Table
-- Defines lateral adjacency between parallel lanes (lane change connectivity)
-- -----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS adjacent_lanes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    lane_id TEXT NOT NULL,
    adjacent_lane_id TEXT NOT NULL,
    side TEXT NOT NULL CHECK (side IN ('left', 'right')),
    FOREIGN KEY (lane_id) REFERENCES lanes(lane_id)
        ON DELETE CASCADE,
    FOREIGN KEY (adjacent_lane_id) REFERENCES lanes(lane_id)
        ON DELETE CASCADE,
    UNIQUE (lane_id, adjacent_lane_id)
);

-- -----------------------------------------------------------------------------
-- Indexes for Performance
-- -----------------------------------------------------------------------------
CREATE INDEX IF NOT EXISTS idx_segments_junction ON segments(junction_id);
CREATE INDEX IF NOT EXISTS idx_lanes_segment ON lanes(segment_id);
CREATE INDEX IF NOT EXISTS idx_branch_point_lanes_bp ON branch_point_lanes(branch_point_id);
CREATE INDEX IF NOT EXISTS idx_branch_point_lanes_lane ON branch_point_lanes(lane_id);
CREATE INDEX IF NOT EXISTS idx_adjacent_lanes_lane ON adjacent_lanes(lane_id);
CREATE INDEX IF NOT EXISTS idx_adjacent_lanes_adjacent ON adjacent_lanes(adjacent_lane_id);

-- -----------------------------------------------------------------------------
-- Default Metadata Values
-- These can be overwritten with actual values for your road network
-- -----------------------------------------------------------------------------
INSERT OR IGNORE INTO maliput_metadata (key, value) VALUES ('schema_version', '1.0');
INSERT OR IGNORE INTO maliput_metadata (key, value) VALUES ('linear_tolerance', '0.01');
INSERT OR IGNORE INTO maliput_metadata (key, value) VALUES ('angular_tolerance', '0.01');
INSERT OR IGNORE INTO maliput_metadata (key, value) VALUES ('scale_length', '1.0');
INSERT OR IGNORE INTO maliput_metadata (key, value) VALUES ('inertial_to_backend_frame_translation', '{0.0, 0.0, 0.0}');
```

---

## Publishing and Distribution

### Schema Provider vs Data Provider

The GeoPackage workflow separates **schema definition** from **data population**:

| Responsibility | Schema Provider (maliput_geopackage) | Data Provider (User) |
|----------------|--------------------------------------|----------------------|
| **What they provide** | Table structure, constraints, indexes | Road network content |
| **SQL operations** | `CREATE TABLE`, `CREATE INDEX`, `CHECK` | `INSERT INTO` |
| **Examples** | Table definitions, default metadata | Junctions, lanes, geometry |
| **Distributed as** | `.sql` file or template `.gpkg` | Populated `.gpkg` file |

**Schema Provider distributes:**
```sql
-- Structure (tables, constraints)
CREATE TABLE lanes (...);
CREATE INDEX idx_lanes_segment ON lanes(segment_id);

-- Default configuration
INSERT INTO maliput_metadata (key, value) VALUES ('linear_tolerance', '0.01');
```

**Data Provider populates:**
```sql
-- Their road network data
INSERT INTO junctions (junction_id, name) VALUES ('downtown', 'Downtown Area');
-- Using `boundaries` table for shared geometries
INSERT INTO boundaries (boundary_id, geometry) VALUES ('b1', 'LINESTRINGZ(...)');
INSERT INTO boundaries (boundary_id, geometry) VALUES ('b2', 'LINESTRINGZ(...)');
INSERT INTO lanes (lane_id, segment_id, left_boundary_id, right_boundary_id)
VALUES ('lane_1', 'seg_1', 'b2', 'b1');

-- Override defaults if needed
UPDATE maliput_metadata SET value = '0.001' WHERE key = 'linear_tolerance';
```

This separation allows:
- **Standardization**: All maliput GeoPackages have the same structure
- **Validation**: Constraints catch invalid data at insertion time
- **Tooling**: Schema can be versioned and validated independently
- **Flexibility**: Users focus on their road network, not database design

---

### Schema Distribution Options

#### 1. SQL File in Package

Include the schema as a `.sql` file in your package:

```
maliput_geopackage/
├── share/
│   └── maliput_geopackage/
│       └── schema/
│           └── maliput_schema.sql    <-- Complete schema
```

Install via CMake:

```cmake
install(
    FILES schema/maliput_schema.sql
    DESTINATION share/${PROJECT_NAME}/schema
)
```

Users can then find it:

```cpp
#include <ament_index_cpp/get_package_share_directory.hpp>

std::string schema_path = ament_index_cpp::get_package_share_directory("maliput_geopackage") 
                        + "/schema/maliput_schema.sql";
```

#### 2. Programmatic Schema Creation

Provide a function that creates the schema:

```cpp
namespace maliput_geopackage {

/// Creates an empty GeoPackage database with the maliput schema.
/// @param filepath Path where the .gpkg file will be created
/// @throws std::runtime_error if file creation fails
void CreateEmptyGeoPackage(const std::string& filepath);

}  // namespace maliput_geopackage
```

#### 3. Template GeoPackage

Distribute an empty `.gpkg` file with the schema already created:

```
maliput_geopackage/
├── share/
│   └── maliput_geopackage/
│       └── templates/
│           └── empty_road_network.gpkg   <-- Empty DB with schema
```

Users copy and populate:

```bash
cp $(ros2 pkg prefix maliput_geopackage)/share/maliput_geopackage/templates/empty_road_network.gpkg my_map.gpkg
```

#### 4. Python Package for Map Creation

Provide a Python utility for creating maps:

```python
#!/usr/bin/env python3
"""maliput_geopackage.create - Create maliput GeoPackage files."""

import sqlite3
from pathlib import Path

SCHEMA_SQL = """
-- (complete schema here)
"""

def create_geopackage(filepath: str, metadata: dict = None) -> None:
    """Create a new maliput GeoPackage with the standard schema.
    
    Args:
        filepath: Path for the new .gpkg file
        metadata: Optional dict of metadata key-value pairs
    """
    conn = sqlite3.connect(filepath)
    conn.executescript(SCHEMA_SQL)
    
    if metadata:
        for key, value in metadata.items():
            conn.execute(
                "INSERT OR REPLACE INTO maliput_metadata (key, value) VALUES (?, ?)",
                (key, str(value))
            )
    
    conn.commit()
    conn.close()

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Create empty maliput GeoPackage")
    parser.add_argument("output", help="Output .gpkg filepath")
    args = parser.parse_args()
    
    create_geopackage(args.output)
    print(f"Created: {args.output}")
```

### Versioning the Schema

Include a schema version in metadata:

```sql
INSERT INTO maliput_metadata (key, value) VALUES ('schema_version', '1.0');
```

Check compatibility at load time:

```cpp
std::string version = QueryMetadata(db, "schema_version");
if (version != SUPPORTED_SCHEMA_VERSION) {
    throw std::runtime_error("Unsupported schema version: " + version);
}
```

### Documentation Publishing

1. **README in package**: Quick start guide
2. **This document**: Full schema reference (install to `share/doc/`)
3. **Online docs**: Host on GitHub Pages or Read the Docs
4. **JSON Schema**: For validation tooling (optional)

```cmake
install(
    FILES 
        docs/geopackage_schema.md
        docs/partial_loading.md
    DESTINATION share/doc/${PROJECT_NAME}
)
```
