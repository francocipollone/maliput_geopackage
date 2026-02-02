#!/usr/bin/env python3
"""
Generate a T-shape road GeoPackage file for maliput_geopackage.

This script creates a T-intersection based on TShapeRoad.xodr with driving lanes only.

T-Intersection Layout (Top View):
                                    
    y=3.5   ═══════════════════════════════════════════════════════════════
            │     West L1 (backward ←)    │ Junction │  East L1 (backward ←)   │
    y=0     ───────────────────────────────┼──────────┼─────────────────────────
            │     West L2 (forward →)     │          │  East L2 (forward →)    │
    y=-3.5  ═══════════════════════╗      │          │      ╔═══════════════════
                                   ║      │          │      ║
                            x=46   ║      │  x=50    │      ║  x=54
                                   ║      │          │      ║
                            y=-3.5 ╚══════╝          ╚══════╝
                                          │          │
                                   South  │          │ South
                                   L2 (↓) │          │ L1 (↑)
                                          │          │
                                  x=46.5  │  x=50    │  x=53.5
                                          │          │
    y=-50                         ════════╧══════════╧════════

The junction area:
- West road ends at x=46
- East road starts at x=54  
- South road ends at y=-3.5 (the south edge of the east-west road)
- Junction box is from (46,-3.5) to (54,0)

Turn connections:
- West L2 → South L2: Right turn from (46,-1.75) curving to (48.25,-3.5)
- South L1 → East L2: Right turn from (51.75,-3.5) curving to (54,-1.75)
- East L1 → South L2: Left turn from (54,1.75) curving to (48.25,-3.5)
- South L1 → West L1: Left turn from (51.75,-3.5) curving to (46,1.75)

Usage:
    python3 generate_tshape_gpkg.py [output_path]
"""

import sqlite3
import sys
import os
import math


def create_schema(conn):
    """Create the maliput_geopackage schema tables."""
    cursor = conn.cursor()

    # Metadata table
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS maliput_metadata (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        )
    ''')

    # Junctions table
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS junctions (
            junction_id TEXT PRIMARY KEY,
            name TEXT
        )
    ''')

    # Segments table
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS segments (
            segment_id TEXT PRIMARY KEY,
            junction_id TEXT NOT NULL,
            name TEXT,
            FOREIGN KEY (junction_id) REFERENCES junctions(junction_id)
        )
    ''')

    # Lanes table (geometry stored as WKT text for simplicity)
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS lanes (
            lane_id TEXT PRIMARY KEY,
            segment_id TEXT NOT NULL,
            lane_type TEXT DEFAULT 'driving',
            direction TEXT DEFAULT 'forward',
            speed_limit_mps REAL,
            left_boundary_type TEXT DEFAULT 'solid_white',
            right_boundary_type TEXT DEFAULT 'dashed_white',
            left_boundary TEXT NOT NULL,
            right_boundary TEXT NOT NULL,
            centerline TEXT,
            FOREIGN KEY (segment_id) REFERENCES segments(segment_id)
        )
    ''')

    # Branch points table
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS branch_points (
            branch_point_id TEXT PRIMARY KEY,
            location TEXT NOT NULL
        )
    ''')

    # Branch point lanes table
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS branch_point_lanes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            branch_point_id TEXT NOT NULL,
            lane_id TEXT NOT NULL,
            side TEXT NOT NULL CHECK(side IN ('a', 'b')),
            lane_end TEXT NOT NULL CHECK(lane_end IN ('start', 'finish')),
            FOREIGN KEY (branch_point_id) REFERENCES branch_points(branch_point_id),
            FOREIGN KEY (lane_id) REFERENCES lanes(lane_id),
            UNIQUE (branch_point_id, lane_id, lane_end)
        )
    ''')

    # Adjacent lanes table
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS adjacent_lanes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            lane_id TEXT NOT NULL,
            adjacent_lane_id TEXT NOT NULL,
            side TEXT NOT NULL CHECK(side IN ('left', 'right')),
            FOREIGN KEY (lane_id) REFERENCES lanes(lane_id),
            FOREIGN KEY (adjacent_lane_id) REFERENCES lanes(lane_id),
            UNIQUE (lane_id, side)
        )
    ''')

    conn.commit()


def generate_linestring_z(points):
    """Generate WKT LINESTRINGZ from a list of (x, y, z) tuples."""
    coords = ', '.join(f"{x} {y} {z}" for x, y, z in points)
    return f"LINESTRINGZ({coords})"


def generate_pointz(x, y, z):
    """Generate WKT POINTZ."""
    return f"POINTZ({x} {y} {z})"


def sample_line(x1, y1, x2, y2, num_points=10):
    """Sample points along a line segment."""
    points = []
    for i in range(num_points):
        t = i / (num_points - 1)
        x = x1 + t * (x2 - x1)
        y = y1 + t * (y2 - y1)
        points.append((x, y, 0.0))
    return points


def sample_arc(center_x, center_y, radius, start_angle, end_angle, num_points=10):
    """Sample points along an arc."""
    points = []
    for i in range(num_points):
        t = i / (num_points - 1)
        angle = start_angle + t * (end_angle - start_angle)
        x = center_x + radius * math.cos(angle)
        y = center_y + radius * math.sin(angle)
        points.append((x, y, 0.0))
    return points


def populate_tshape_road(conn):
    """
    Populate the database with a T-shape intersection.
    
    Following maliput_malidrive TShapeRoad.xodr structure with only driving lanes.
    Lane width = 3.5m
    
    Coordinate system:
    - X increases to the East
    - Y increases to the North
    - West road: x = 0 to 46, y = 0 (centerline)
    - East road: x = 54 to 100, y = 0 (centerline)
    - South road: y = -50 to -3.5, x = 50 (centerline)
    - Junction box: x = 46 to 54, y = -3.5 to 3.5
    """
    cursor = conn.cursor()

    LANE_WIDTH = 3.5
    HALF_LANE = LANE_WIDTH / 2  # 1.75
    
    # Junction boundaries
    WEST_END = 46.0
    EAST_START = 54.0
    SOUTH_END = -3.5  # Where south road meets the junction
    SOUTH_START_Y = -50.0  # Far end of south road
    
    # South road centerline is at x=50
    SOUTH_CENTER_X = 50.0

    # Insert metadata
    metadata = [
        ('schema_version', '1.0'),
        ('linear_tolerance', '0.01'),
        ('angular_tolerance', '0.01'),
        ('scale_length', '1.0'),
        ('inertial_to_backend_frame_translation', '0.0,0.0,0.0'),
    ]
    cursor.executemany('INSERT INTO maliput_metadata (key, value) VALUES (?, ?)', metadata)

    # =====================================================
    # JUNCTIONS
    # =====================================================
    junctions = [
        ('j_west', 'West Road Junction'),
        ('j_east', 'East Road Junction'),
        ('j_south', 'South Road Junction'),
        ('j_intersection', 'T-Intersection Junction'),
    ]
    cursor.executemany('INSERT INTO junctions (junction_id, name) VALUES (?, ?)', junctions)

    # =====================================================
    # SEGMENTS
    # Each turn lane gets its own segment since they don't share boundaries
    # =====================================================
    segments = [
        ('j_west_s1', 'j_west', 'West Road Segment'),
        ('j_east_s1', 'j_east', 'East Road Segment'),
        ('j_south_s1', 'j_south', 'South Road Segment'),
        # Junction connection segments - one per connection
        ('j_int_straight', 'j_intersection', 'Straight West-East'),
        ('j_int_south_east', 'j_intersection', 'Turn South to East'),
        ('j_int_east_south', 'j_intersection', 'Turn East to South'),
        ('j_int_west_south', 'j_intersection', 'Turn West to South'),
        ('j_int_south_west', 'j_intersection', 'Turn South to West'),
    ]
    cursor.executemany('INSERT INTO segments (segment_id, junction_id, name) VALUES (?, ?, ?)', segments)

    # =====================================================
    # WEST ROAD LANES (x: 0 to 46, centerline at y=0)
    # Lane 1 (left of centerline): y = 0 to 3.5, backward (traveling West)
    # Lane 2 (right of centerline): y = -3.5 to 0, forward (traveling East)
    # =====================================================
    west_l1_left = generate_linestring_z(sample_line(0, LANE_WIDTH, WEST_END, LANE_WIDTH))
    west_l1_right = generate_linestring_z(sample_line(0, 0, WEST_END, 0))
    west_l1_center = generate_linestring_z(sample_line(0, HALF_LANE, WEST_END, HALF_LANE))
    
    west_l2_left = generate_linestring_z(sample_line(0, 0, WEST_END, 0))
    west_l2_right = generate_linestring_z(sample_line(0, -LANE_WIDTH, WEST_END, -LANE_WIDTH))
    west_l2_center = generate_linestring_z(sample_line(0, -HALF_LANE, WEST_END, -HALF_LANE))

    cursor.execute('''
        INSERT INTO lanes (lane_id, segment_id, lane_type, direction, speed_limit_mps,
                           left_boundary_type, right_boundary_type,
                           left_boundary, right_boundary, centerline)
        VALUES ('west_l1', 'j_west_s1', 'driving', 'backward', 17.88,
                'solid_white', 'solid_yellow', ?, ?, ?)
    ''', (west_l1_left, west_l1_right, west_l1_center))

    cursor.execute('''
        INSERT INTO lanes (lane_id, segment_id, lane_type, direction, speed_limit_mps,
                           left_boundary_type, right_boundary_type,
                           left_boundary, right_boundary, centerline)
        VALUES ('west_l2', 'j_west_s1', 'driving', 'forward', 17.88,
                'solid_yellow', 'solid_white', ?, ?, ?)
    ''', (west_l2_left, west_l2_right, west_l2_center))

    # =====================================================
    # EAST ROAD LANES (x: 54 to 100, centerline at y=0)
    # Same structure as west road
    # =====================================================
    east_l1_left = generate_linestring_z(sample_line(EAST_START, LANE_WIDTH, 100, LANE_WIDTH))
    east_l1_right = generate_linestring_z(sample_line(EAST_START, 0, 100, 0))
    east_l1_center = generate_linestring_z(sample_line(EAST_START, HALF_LANE, 100, HALF_LANE))
    
    east_l2_left = generate_linestring_z(sample_line(EAST_START, 0, 100, 0))
    east_l2_right = generate_linestring_z(sample_line(EAST_START, -LANE_WIDTH, 100, -LANE_WIDTH))
    east_l2_center = generate_linestring_z(sample_line(EAST_START, -HALF_LANE, 100, -HALF_LANE))

    cursor.execute('''
        INSERT INTO lanes (lane_id, segment_id, lane_type, direction, speed_limit_mps,
                           left_boundary_type, right_boundary_type,
                           left_boundary, right_boundary, centerline)
        VALUES ('east_l1', 'j_east_s1', 'driving', 'backward', 17.88,
                'solid_white', 'solid_yellow', ?, ?, ?)
    ''', (east_l1_left, east_l1_right, east_l1_center))

    cursor.execute('''
        INSERT INTO lanes (lane_id, segment_id, lane_type, direction, speed_limit_mps,
                           left_boundary_type, right_boundary_type,
                           left_boundary, right_boundary, centerline)
        VALUES ('east_l2', 'j_east_s1', 'driving', 'forward', 17.88,
                'solid_yellow', 'solid_white', ?, ?, ?)
    ''', (east_l2_left, east_l2_right, east_l2_center))

    # =====================================================
    # SOUTH ROAD LANES (centerline at x=50, y: -50 to -3.5)
    # Road goes North (increasing Y), reference line at x=50
    # Lane 1 (right of ref line going North): x = 50 to 53.5, forward (traveling North)
    # Lane 2 (left of ref line going North): x = 46.5 to 50, backward (traveling South)
    # 
    # For maliput: lanes are ordered right-to-left when looking in direction of travel
    # South_l1 travels North, so its right boundary is at x=50+3.5=53.5
    # South_l2 travels South, so its right boundary is at x=50-3.5=46.5
    # =====================================================
    
    # Lane 1: Traveling North (forward), centerline at x=50+1.75=51.75
    south_l1_left = generate_linestring_z(sample_line(SOUTH_CENTER_X, SOUTH_START_Y, SOUTH_CENTER_X, SOUTH_END))
    south_l1_right = generate_linestring_z(sample_line(SOUTH_CENTER_X + LANE_WIDTH, SOUTH_START_Y, SOUTH_CENTER_X + LANE_WIDTH, SOUTH_END))
    south_l1_center = generate_linestring_z(sample_line(SOUTH_CENTER_X + HALF_LANE, SOUTH_START_Y, SOUTH_CENTER_X + HALF_LANE, SOUTH_END))
    
    # Lane 2: Traveling South (backward), centerline at x=50-1.75=48.25
    south_l2_left = generate_linestring_z(sample_line(SOUTH_CENTER_X - LANE_WIDTH, SOUTH_START_Y, SOUTH_CENTER_X - LANE_WIDTH, SOUTH_END))
    south_l2_right = generate_linestring_z(sample_line(SOUTH_CENTER_X, SOUTH_START_Y, SOUTH_CENTER_X, SOUTH_END))
    south_l2_center = generate_linestring_z(sample_line(SOUTH_CENTER_X - HALF_LANE, SOUTH_START_Y, SOUTH_CENTER_X - HALF_LANE, SOUTH_END))

    cursor.execute('''
        INSERT INTO lanes (lane_id, segment_id, lane_type, direction, speed_limit_mps,
                           left_boundary_type, right_boundary_type,
                           left_boundary, right_boundary, centerline)
        VALUES ('south_l1', 'j_south_s1', 'driving', 'forward', 17.88,
                'solid_yellow', 'solid_white', ?, ?, ?)
    ''', (south_l1_left, south_l1_right, south_l1_center))

    cursor.execute('''
        INSERT INTO lanes (lane_id, segment_id, lane_type, direction, speed_limit_mps,
                           left_boundary_type, right_boundary_type,
                           left_boundary, right_boundary, centerline)
        VALUES ('south_l2', 'j_south_s1', 'driving', 'backward', 17.88,
                'solid_white', 'solid_yellow', ?, ?, ?)
    ''', (south_l2_left, south_l2_right, south_l2_center))

    # =====================================================
    # JUNCTION - STRAIGHT LANES (West <-> East)
    # These connect west_l1 to east_l1 and west_l2 to east_l2
    # =====================================================
    
    int_straight_l1_left = generate_linestring_z(sample_line(WEST_END, LANE_WIDTH, EAST_START, LANE_WIDTH, 5))
    int_straight_l1_right = generate_linestring_z(sample_line(WEST_END, 0, EAST_START, 0, 5))
    int_straight_l1_center = generate_linestring_z(sample_line(WEST_END, HALF_LANE, EAST_START, HALF_LANE, 5))

    cursor.execute('''
        INSERT INTO lanes (lane_id, segment_id, lane_type, direction, speed_limit_mps,
                           left_boundary_type, right_boundary_type,
                           left_boundary, right_boundary, centerline)
        VALUES ('int_straight_l1', 'j_int_straight', 'driving', 'backward', 17.88,
                'none', 'none', ?, ?, ?)
    ''', (int_straight_l1_left, int_straight_l1_right, int_straight_l1_center))

    int_straight_l2_left = generate_linestring_z(sample_line(WEST_END, 0, EAST_START, 0, 5))
    int_straight_l2_right = generate_linestring_z(sample_line(WEST_END, -LANE_WIDTH, EAST_START, -LANE_WIDTH, 5))
    int_straight_l2_center = generate_linestring_z(sample_line(WEST_END, -HALF_LANE, EAST_START, -HALF_LANE, 5))

    cursor.execute('''
        INSERT INTO lanes (lane_id, segment_id, lane_type, direction, speed_limit_mps,
                           left_boundary_type, right_boundary_type,
                           left_boundary, right_boundary, centerline)
        VALUES ('int_straight_l2', 'j_int_straight', 'driving', 'forward', 17.88,
                'none', 'none', ?, ?, ?)
    ''', (int_straight_l2_left, int_straight_l2_right, int_straight_l2_center))

    # =====================================================
    # JUNCTION - TURN LANES (South <-> East)
    # 
    # Turn South->East (int_south_east): south_l1 finish -> east_l2 start
    #   south_l1 ends at (51.75, -3.5) going North
    #   east_l2 starts at (54, -1.75) going East
    #   This is a right turn, arc center at (54, -3.5), radius ~2.25
    #
    # Turn East->South (int_east_south): east_l1 start -> south_l2 finish  
    #   east_l1 starts at (54, 1.75) going West
    #   south_l2 ends at (48.25, -3.5) going South
    #   This is a left turn, arc center at (54, -3.5), going around
    # =====================================================
    
    # South->East turn (right turn for south_l1 -> east_l2)
    # Centerline goes from (51.75, -3.5) to (54, -1.75)
    # Arc center at intersection of perpendiculars: at (54, -3.5)
    # Radius from center to (51.75, -3.5) = 54 - 51.75 = 2.25
    turn_se_radius = EAST_START - (SOUTH_CENTER_X + HALF_LANE)  # 54 - 51.75 = 2.25
    turn_se_center = (EAST_START, SOUTH_END)  # (54, -3.5)
    
    # Arc from 180° (pointing West, i.e., at south_l1 end) to 90° (pointing North, i.e., at east_l2 start)
    turn_se_center_pts = sample_arc(turn_se_center[0], turn_se_center[1], turn_se_radius, 
                                     math.pi, math.pi/2, 8)
    turn_se_left_pts = sample_arc(turn_se_center[0], turn_se_center[1], turn_se_radius + HALF_LANE, 
                                   math.pi, math.pi/2, 8)
    turn_se_right_pts = sample_arc(turn_se_center[0], turn_se_center[1], turn_se_radius - HALF_LANE, 
                                    math.pi, math.pi/2, 8)

    cursor.execute('''
        INSERT INTO lanes (lane_id, segment_id, lane_type, direction, speed_limit_mps,
                           left_boundary_type, right_boundary_type,
                           left_boundary, right_boundary, centerline)
        VALUES ('int_south_east', 'j_int_south_east', 'driving', 'forward', 17.88,
                'none', 'none', ?, ?, ?)
    ''', (generate_linestring_z(turn_se_left_pts),
          generate_linestring_z(turn_se_right_pts),
          generate_linestring_z(turn_se_center_pts)))

    # East->South turn (left turn for east_l1 -> south_l2)
    # east_l1 starts at (54, 1.75) going West
    # south_l2 ends at (48.25, -3.5) going South
    # Use straight line for simplicity (in a real map this would be a curve)
    turn_es_center_pts = sample_line(EAST_START, HALF_LANE, SOUTH_CENTER_X - HALF_LANE, SOUTH_END, 8)
    turn_es_left_pts = sample_line(EAST_START, 0, SOUTH_CENTER_X, SOUTH_END, 8)
    turn_es_right_pts = sample_line(EAST_START, LANE_WIDTH, SOUTH_CENTER_X - LANE_WIDTH, SOUTH_END, 8)

    cursor.execute('''
        INSERT INTO lanes (lane_id, segment_id, lane_type, direction, speed_limit_mps,
                           left_boundary_type, right_boundary_type,
                           left_boundary, right_boundary, centerline)
        VALUES ('int_east_south', 'j_int_east_south', 'driving', 'backward', 17.88,
                'none', 'none', ?, ?, ?)
    ''', (generate_linestring_z(turn_es_left_pts),
          generate_linestring_z(turn_es_right_pts),
          generate_linestring_z(turn_es_center_pts)))

    # =====================================================
    # JUNCTION - TURN LANES (West <-> South)
    #
    # Turn West->South (int_west_south): west_l2 finish -> south_l2 start
    #   west_l2 ends at (46, -1.75) going East
    #   south_l2 starts at (48.25, -3.5) going South (backward direction means start is at y=-50, but
    #   for connectivity we connect to the y=-3.5 end which is 'finish' for the backward lane)
    #   Actually, for a backward lane, 'start' is at y=-50 and 'finish' is at y=-3.5
    #   So west_l2 -> south_l2: connects at (48.25, -3.5)
    #   This is a right turn, center at (46, -3.5)
    #
    # Turn South->West (int_south_west): south_l1 finish -> west_l1 start
    #   south_l1 ends at (51.75, -3.5) going North
    #   west_l1 starts at (46, 1.75) going West
    #   This is a left turn
    # =====================================================
    
    # West->South turn (right turn for west_l2 -> south_l2)
    # west_l2 ends at (46, -1.75), south_l2 junction end at (48.25, -3.5)
    turn_ws_radius = (SOUTH_CENTER_X - HALF_LANE) - WEST_END  # 48.25 - 46 = 2.25
    turn_ws_center = (WEST_END, SOUTH_END)  # (46, -3.5)
    
    # Arc from 90° (pointing North, at west_l2 end) to 0° (pointing East toward south_l2)
    turn_ws_center_pts = sample_arc(turn_ws_center[0], turn_ws_center[1], turn_ws_radius, 
                                     math.pi/2, 0, 8)
    turn_ws_left_pts = sample_arc(turn_ws_center[0], turn_ws_center[1], turn_ws_radius - HALF_LANE, 
                                   math.pi/2, 0, 8)
    turn_ws_right_pts = sample_arc(turn_ws_center[0], turn_ws_center[1], turn_ws_radius + HALF_LANE, 
                                    math.pi/2, 0, 8)

    cursor.execute('''
        INSERT INTO lanes (lane_id, segment_id, lane_type, direction, speed_limit_mps,
                           left_boundary_type, right_boundary_type,
                           left_boundary, right_boundary, centerline)
        VALUES ('int_west_south', 'j_int_west_south', 'driving', 'forward', 17.88,
                'none', 'none', ?, ?, ?)
    ''', (generate_linestring_z(turn_ws_left_pts),
          generate_linestring_z(turn_ws_right_pts),
          generate_linestring_z(turn_ws_center_pts)))

    # South->West turn (left turn for south_l1 -> west_l1)
    # south_l1 ends at (51.75, -3.5), west_l1 starts at (46, 1.75)
    # Use straight line for simplicity (in a real map this would be a curve)
    turn_sw_center_pts = sample_line(SOUTH_CENTER_X + HALF_LANE, SOUTH_END, WEST_END, HALF_LANE, 8)
    turn_sw_left_pts = sample_line(SOUTH_CENTER_X + LANE_WIDTH, SOUTH_END, WEST_END, 0, 8)
    turn_sw_right_pts = sample_line(SOUTH_CENTER_X, SOUTH_END, WEST_END, LANE_WIDTH, 8)

    cursor.execute('''
        INSERT INTO lanes (lane_id, segment_id, lane_type, direction, speed_limit_mps,
                           left_boundary_type, right_boundary_type,
                           left_boundary, right_boundary, centerline)
        VALUES ('int_south_west', 'j_int_south_west', 'driving', 'backward', 17.88,
                'none', 'none', ?, ?, ?)
    ''', (generate_linestring_z(turn_sw_left_pts),
          generate_linestring_z(turn_sw_right_pts),
          generate_linestring_z(turn_sw_center_pts)))

    # =====================================================
    # BRANCH POINTS
    # =====================================================
    branch_points = [
        # Dead ends
        ('bp_west_start', generate_pointz(0, 0, 0)),
        ('bp_east_end', generate_pointz(100, 0, 0)),
        ('bp_south_start', generate_pointz(SOUTH_CENTER_X, SOUTH_START_Y, 0)),
        # Junction connections
        ('bp_west_jct', generate_pointz(WEST_END, 0, 0)),
        ('bp_east_jct', generate_pointz(EAST_START, 0, 0)),
        ('bp_south_jct', generate_pointz(SOUTH_CENTER_X, SOUTH_END, 0)),
    ]
    cursor.executemany('INSERT INTO branch_points (branch_point_id, location) VALUES (?, ?)', branch_points)

    # =====================================================
    # BRANCH POINT LANES
    # 
    # For maliput_sparse, lanes have 'start' and 'finish' ends.
    # For forward lanes: start is at low coordinate, finish at high
    # For backward lanes: start is at high coordinate, finish at low
    # 
    # west_l1 (backward): start at x=46, finish at x=0
    # west_l2 (forward): start at x=0, finish at x=46
    # east_l1 (backward): start at x=100, finish at x=54
    # east_l2 (forward): start at x=54, finish at x=100
    # south_l1 (forward): start at y=-50, finish at y=-3.5
    # south_l2 (backward): start at y=-3.5, finish at y=-50
    # =====================================================
    branch_point_lanes = [
        # West start (dead end at x=0)
        ('bp_west_start', 'west_l1', 'a', 'finish'),
        ('bp_west_start', 'west_l2', 'a', 'start'),
        
        # East end (dead end at x=100)
        ('bp_east_end', 'east_l1', 'a', 'start'),
        ('bp_east_end', 'east_l2', 'a', 'finish'),
        
        # South start (dead end at y=-50)
        ('bp_south_start', 'south_l1', 'a', 'start'),
        ('bp_south_start', 'south_l2', 'a', 'finish'),
        
        # West-Junction connection (at x=46)
        ('bp_west_jct', 'west_l1', 'a', 'start'),
        ('bp_west_jct', 'west_l2', 'a', 'finish'),
        ('bp_west_jct', 'int_straight_l1', 'b', 'start'),
        ('bp_west_jct', 'int_straight_l2', 'b', 'start'),
        ('bp_west_jct', 'int_west_south', 'b', 'start'),
        ('bp_west_jct', 'int_south_west', 'b', 'finish'),
        
        # East-Junction connection (at x=54)
        ('bp_east_jct', 'east_l1', 'a', 'finish'),
        ('bp_east_jct', 'east_l2', 'a', 'start'),
        ('bp_east_jct', 'int_straight_l1', 'b', 'finish'),
        ('bp_east_jct', 'int_straight_l2', 'b', 'finish'),
        ('bp_east_jct', 'int_south_east', 'b', 'finish'),
        ('bp_east_jct', 'int_east_south', 'b', 'start'),
        
        # South-Junction connection (at y=-3.5)
        ('bp_south_jct', 'south_l1', 'a', 'finish'),
        ('bp_south_jct', 'south_l2', 'a', 'start'),
        ('bp_south_jct', 'int_south_east', 'b', 'start'),
        ('bp_south_jct', 'int_east_south', 'b', 'finish'),
        ('bp_south_jct', 'int_west_south', 'b', 'finish'),
        ('bp_south_jct', 'int_south_west', 'b', 'start'),
    ]
    cursor.executemany('''
        INSERT INTO branch_point_lanes (branch_point_id, lane_id, side, lane_end)
        VALUES (?, ?, ?, ?)
    ''', branch_point_lanes)

    # =====================================================
    # ADJACENT LANES
    # =====================================================
    adjacent_lanes = [
        # West road
        ('west_l1', 'west_l2', 'right'),
        ('west_l2', 'west_l1', 'left'),
        # East road
        ('east_l1', 'east_l2', 'right'),
        ('east_l2', 'east_l1', 'left'),
        # South road
        ('south_l1', 'south_l2', 'left'),
        ('south_l2', 'south_l1', 'right'),
        # Junction straight
        ('int_straight_l1', 'int_straight_l2', 'right'),
        ('int_straight_l2', 'int_straight_l1', 'left'),
        # Junction South-East turns (these are separate segments, no adjacency)
        # Junction West-South turns (these are separate segments, no adjacency)
    ]
    cursor.executemany('''
        INSERT INTO adjacent_lanes (lane_id, adjacent_lane_id, side)
        VALUES (?, ?, ?)
    ''', adjacent_lanes)

    conn.commit()


def main():
    output_path = sys.argv[1] if len(sys.argv) > 1 else 't_shape_road.gpkg'

    # Remove existing file
    if os.path.exists(output_path):
        os.remove(output_path)

    print(f"Creating GeoPackage: {output_path}")

    conn = sqlite3.connect(output_path)
    try:
        create_schema(conn)
        populate_tshape_road(conn)
        print("Successfully created T-shape road GeoPackage")

        # Print summary
        cursor = conn.cursor()
        cursor.execute("SELECT COUNT(*) FROM junctions")
        print(f"  - Junctions: {cursor.fetchone()[0]}")
        cursor.execute("SELECT COUNT(*) FROM segments")
        print(f"  - Segments: {cursor.fetchone()[0]}")
        cursor.execute("SELECT COUNT(*) FROM lanes")
        print(f"  - Lanes: {cursor.fetchone()[0]}")
        cursor.execute("SELECT COUNT(*) FROM branch_points")
        print(f"  - Branch Points: {cursor.fetchone()[0]}")
        cursor.execute("SELECT COUNT(*) FROM branch_point_lanes")
        print(f"  - Branch Point Lane Connections: {cursor.fetchone()[0]}")
        cursor.execute("SELECT COUNT(*) FROM adjacent_lanes")
        print(f"  - Adjacent Lane Relationships: {cursor.fetchone()[0]}")
        
        # Print lane geometries for verification
        print("\n  Lane Centerlines:")
        for row in cursor.execute('SELECT lane_id, centerline FROM lanes'):
            coords = row[1].replace('LINESTRINGZ(', '').replace(')', '').split(',')
            start = coords[0].strip()
            end = coords[-1].strip()
            print(f"    {row[0]}: {start} -> {end}")

    finally:
        conn.close()


if __name__ == '__main__':
    main()
