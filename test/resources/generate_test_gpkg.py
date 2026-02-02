#!/usr/bin/env python3
"""
Generate a test GeoPackage file for maliput_geopackage.

This script creates a simple 2-lane straight road (100m) following the
maliput_geopackage schema.

Usage:
    python3 generate_test_gpkg.py [output_path]
"""

import sqlite3
import sys
import os


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


def populate_two_lane_road(conn):
    """
    Populate the database with a simple 2-lane straight road (100m).

    Road layout:
        y = 3.5  ════════════════════════════════════  Left edge
                 │              Lane 1               │
                 │         (forward direction)       │  ───►
        y = 0.0  ┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈  Center line
                 │              Lane 2               │
                 │        (backward direction)       │  ◄───
        y = -3.5 ════════════════════════════════════  Right edge

        x = 0                                    x = 100
    """
    cursor = conn.cursor()

    # Insert metadata
    metadata = [
        ('schema_version', '1.0'),
        ('linear_tolerance', '0.01'),
        ('angular_tolerance', '0.01'),
        ('scale_length', '1.0'),
        ('inertial_to_backend_frame_translation', '0.0,0.0,0.0'),
    ]
    cursor.executemany('INSERT INTO maliput_metadata (key, value) VALUES (?, ?)', metadata)

    # Insert junction
    cursor.execute("INSERT INTO junctions (junction_id, name) VALUES ('j1', 'Main Road Junction')")

    # Insert segment
    cursor.execute("INSERT INTO segments (segment_id, junction_id, name) VALUES ('j1_s1', 'j1', 'Main Road Segment')")

    # Insert lanes
    # Lane 1 (Forward direction, y: 0 to 3.5)
    lane1_left = 'LINESTRINGZ(0 3.5 0, 25 3.5 0, 50 3.5 0, 75 3.5 0, 100 3.5 0)'
    lane1_right = 'LINESTRINGZ(0 0 0, 25 0 0, 50 0 0, 75 0 0, 100 0 0)'
    lane1_center = 'LINESTRINGZ(0 1.75 0, 25 1.75 0, 50 1.75 0, 75 1.75 0, 100 1.75 0)'

    cursor.execute('''
        INSERT INTO lanes (lane_id, segment_id, lane_type, direction, speed_limit_mps,
                           left_boundary_type, right_boundary_type,
                           left_boundary, right_boundary, centerline)
        VALUES ('j1_s1_lane1', 'j1_s1', 'driving', 'forward', 13.89,
                'solid_yellow', 'dashed_yellow', ?, ?, ?)
    ''', (lane1_left, lane1_right, lane1_center))

    # Lane 2 (Backward direction, y: -3.5 to 0)
    lane2_left = 'LINESTRINGZ(0 0 0, 25 0 0, 50 0 0, 75 0 0, 100 0 0)'
    lane2_right = 'LINESTRINGZ(0 -3.5 0, 25 -3.5 0, 50 -3.5 0, 75 -3.5 0, 100 -3.5 0)'
    lane2_center = 'LINESTRINGZ(0 -1.75 0, 25 -1.75 0, 50 -1.75 0, 75 -1.75 0, 100 -1.75 0)'

    cursor.execute('''
        INSERT INTO lanes (lane_id, segment_id, lane_type, direction, speed_limit_mps,
                           left_boundary_type, right_boundary_type,
                           left_boundary, right_boundary, centerline)
        VALUES ('j1_s1_lane2', 'j1_s1', 'driving', 'backward', 13.89,
                'dashed_yellow', 'solid_yellow', ?, ?, ?)
    ''', (lane2_left, lane2_right, lane2_center))

    # Insert branch points
    cursor.execute("INSERT INTO branch_points (branch_point_id, location) VALUES ('bp_start', 'POINTZ(0 0 0)')")
    cursor.execute("INSERT INTO branch_points (branch_point_id, location) VALUES ('bp_end', 'POINTZ(100 0 0)')")

    # Insert branch point lane connections
    branch_point_lanes = [
        # Start branch point
        ('bp_start', 'j1_s1_lane1', 'a', 'start'),
        ('bp_start', 'j1_s1_lane2', 'a', 'finish'),
        # End branch point
        ('bp_end', 'j1_s1_lane1', 'a', 'finish'),
        ('bp_end', 'j1_s1_lane2', 'a', 'start'),
    ]
    cursor.executemany('''
        INSERT INTO branch_point_lanes (branch_point_id, lane_id, side, lane_end)
        VALUES (?, ?, ?, ?)
    ''', branch_point_lanes)

    # Insert adjacent lanes
    adjacent_lanes = [
        ('j1_s1_lane1', 'j1_s1_lane2', 'right'),
        ('j1_s1_lane2', 'j1_s1_lane1', 'left'),
    ]
    cursor.executemany('''
        INSERT INTO adjacent_lanes (lane_id, adjacent_lane_id, side)
        VALUES (?, ?, ?)
    ''', adjacent_lanes)

    conn.commit()


def main():
    output_path = sys.argv[1] if len(sys.argv) > 1 else 'two_lane_road.gpkg'

    # Remove existing file
    if os.path.exists(output_path):
        os.remove(output_path)

    print(f"Creating GeoPackage: {output_path}")

    conn = sqlite3.connect(output_path)
    try:
        create_schema(conn)
        populate_two_lane_road(conn)
        print("Successfully created test GeoPackage with 2-lane road (100m)")

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

    finally:
        conn.close()


if __name__ == '__main__':
    main()
