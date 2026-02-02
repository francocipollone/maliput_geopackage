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

Defines lanes with their geometric boundaries. Each lane has left and right boundaries stored as 3D LineStrings.

```sql
CREATE TABLE lanes (
    lane_id TEXT PRIMARY KEY,
    segment_id TEXT NOT NULL,
    lane_type TEXT DEFAULT 'driving',
    direction TEXT DEFAULT 'forward',
    left_boundary TEXT NOT NULL,  -- WKT LINESTRINGZ
    right_boundary TEXT NOT NULL, -- WKT LINESTRINGZ
    FOREIGN KEY (segment_id) REFERENCES segments(segment_id)
);
```

| Column | Type | Description |
|--------|------|-------------|
| `lane_id` | TEXT | Unique identifier for the lane |
| `segment_id` | TEXT | Parent segment ID |
| `lane_type` | TEXT | Lane type: `driving`, `shoulder`, `parking`, etc. |
| `direction` | TEXT | Travel direction: `forward`, `backward`, `bidirectional` |
| `left_boundary` | TEXT | Left boundary as WKT LINESTRINGZ |
| `right_boundary` | TEXT | Right boundary as WKT LINESTRINGZ |

**Geometry Format:**

Boundaries are stored as Well-Known Text (WKT) 3D LineStrings:

```
LINESTRINGZ(x1 y1 z1, x2 y2 z2, x3 y3 z3, ...)
```

The coordinates represent points in the inertial frame (typically ENU - East-North-Up).

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

-- Insert lanes (100m straight road, 3.5m lane width each)
-- Lane 1: y = 0 to y = 3.5
INSERT INTO lanes (lane_id, segment_id, lane_type, direction, left_boundary, right_boundary)
VALUES (
    'j1_s1_lane1',
    'j1_s1',
    'driving',
    'forward',
    'LINESTRINGZ(0 3.5 0, 25 3.5 0, 50 3.5 0, 75 3.5 0, 100 3.5 0)',
    'LINESTRINGZ(0 0 0, 25 0 0, 50 0 0, 75 0 0, 100 0 0)'
);

-- Lane 2: y = 3.5 to y = 7.0
INSERT INTO lanes (lane_id, segment_id, lane_type, direction, left_boundary, right_boundary)
VALUES (
    'j1_s1_lane2',
    'j1_s1',
    'driving',
    'forward',
    'LINESTRINGZ(0 7.0 0, 25 7.0 0, 50 7.0 0, 75 7.0 0, 100 7.0 0)',
    'LINESTRINGZ(0 3.5 0, 25 3.5 0, 50 3.5 0, 75 3.5 0, 100 3.5 0)'
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
