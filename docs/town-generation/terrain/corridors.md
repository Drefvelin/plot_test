# Terrain Corridor Roads

## Purpose

Add shore and river roads that follow terrain contours and remove redundant parallel Voronoi segments.

## What it does

Pipeline position:

```
Voronoi roads
  → appendCorridorRoads
  → splitRoadsAtIntersections
  → sanitizeRoadGraphAtWater (corridors included)
  → resolveBridges
  → cullVoronoiRoadsParallelToCorridors
```

Corridor segments are ordinary `Road` entries with `isTerrainCorridor = true`. They count as primaries for sanitize and frontage (where buildable).

## How it works

**Atlas bake** — On `bakeTerrain`:

- `shoreRoadGraph` — `buildWaterContourGraph(sea, shore_road_inset, corridor_edge_spacing)`
- `riverRoadGraph` — same for river mask

**Emit** — `appendCorridorRoads` adds all graph edges; logs `corridor_roads emitted=N`.

**Parallel cull** — Sample corridor chains (~5 u steps); perpendicular probes at ±`corridor_parallel_probe_offset`; delete non-corridor roads with `|dot| ≥ corridor_parallel_cos` parallel to local tangent, unless incident to chain endpoint junction (preserves radial crosses).

Config (`config.yml` → `terrain`, no code defaults for corridor keys):

```yaml
corridor_roads_enabled: true
shore_road_inset: 20.0
river_road_inset: 20.0
corridor_edge_spacing: 80.0
corridor_parallel_probe_offset: 4.0
corridor_parallel_cos: 0.98
```

Debug-only overlay keys: `contour_width`, etc. — do not affect corridor graph insets.

Logs: `voronoi` — `roads_before_corridors`, `culled_parallel_voronoi`.

## Interactions

- Road graph: [../road-graph.md](../road-graph.md)
- Bake graphs: [bake-and-atlas.md](bake-and-atlas.md)
- Water sanitize treats corridors like Voronoi primaries: [../water-and-bridges.md](../water-and-bridges.md)
- Placement on corridor frontage: same as any primary road

## Out of scope

Partial Voronoi span clip, endpoint snap before split, serialized atlas cache, runtime water sanitize during growth.
