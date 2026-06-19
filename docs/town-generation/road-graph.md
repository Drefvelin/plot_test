# Road Graph

## Purpose

Turn a Voronoi diagram into a clean road network: primary Voronoi segments, terrain corridors, junctions, and culled duplicates.

## What it does

At town load:

1. Generate Voronoi sites inside the diagram disc (`voronoi.scale`, `voronoi.seed`).
2. Copy finite diagram edges into `Road` segments (primaries).
3. Append shore/river **corridor roads** (`isTerrainCorridor`) from baked graphs.
4. Split all roads at interior intersections.
5. Water sanitize, bridges, parallel cull (see linked docs).
6. Index junctions at shared endpoints; assign stable road ids.

Secondary/alley roads do not exist at load — they are added during growth.

## How it works

| Step | Function | File |
|------|----------|------|
| Voronoi generate | `TownBuilder::build` | `TownBuilder.cpp` |
| Corridor append | `appendCorridorRoads` | `RoadNetwork.cpp` |
| Intersection split | `splitRoadsAtIntersections` | `RoadNetwork.cpp` |
| Parallel cull | `cullVoronoiRoadsParallelToCorridors` | `RoadNetwork.cpp` |
| Junction index | `indexJunctions` | `Town.cpp` |

**Intersection split** uses `segmentCrossingParams` (`PlotGeometry.cpp`); segments shorter than 0.5 u are dropped; road ids re-indexed after splits.

**Parallel cull** walks corridor chains, probes perpendicular offsets, marks non-corridor roads parallel to the chain (and not incident to chain endpoints), deletes whole roads.

Logs: channel `voronoi` — `corridor_roads`, `roads_after_split`, `culled_parallel_voronoi`.

Config: `terrain.corridor_*` keys in [config reference](../config/reference.md).

## Interactions

- Corridors: [terrain/corridors.md](../town-generation/terrain/corridors.md)
- Water sanitize and bridges: [water-and-bridges.md](water-and-bridges.md)
- Placement uses roads as blockers: [../placement/frontage-and-plots.md](../placement/frontage-and-plots.md)
- Data model: [../architecture/data-model.md](../architecture/data-model.md)
