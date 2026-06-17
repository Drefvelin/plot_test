# Terrain Bridges

River/sea crossings become **bridge** roads in the graph: a brown line between two inset junctions.

## Pipeline

```
Voronoi + disc roads
  → appendCorridorRoads
  → splitRoadsAtIntersections
  → splitRoadsAtForbiddenBoundary
  → indexJunctions
  → resolveBridges (strip → match → snap → create)
  → cullVoronoiRoadsParallelToCorridors
  → buildRoadMesh (bridges brown, skip water clip)
```

Implementation: [`RoadNetwork.cpp`](../app/core/RoadNetwork.cpp), [`TownBuilder.cpp`](../app/core/TownBuilder.cpp).

## Algorithm

### 1 — Strip water stubs

After boundary split, remove eligible primary roads that enter or cross water:

- Shore ↔ in-water endpoint
- In-water ↔ in-water
- Any segment with forbidden interior (including old shore-to-shore water spans)

Land segments (buildable midpoint) remain. In-water junctions vanish on re-index.

### 2 — Collect shore junctions

Junctions on the forbidden inset (purple contour) that still have at least one **land** road attached (includes corridor roads for shore detection).

### 3 — Opposite-bank pairing

For each unordered shore pair:

1. **Opposite banks** — from each junction, landward normal from `forbiddenPolygons`; require `dot(ab, inwardA) < -0.1` and `dot(-ab, inwardB) < -0.1`.
2. **Valid chord** — `segmentEntirelyForbidden` on interior samples.

Greedy **one-to-one** matching: sort candidates by chord length, accept shortest pairs first.

### 4 — Snap and create

For each matched pair:

1. Optional snap: slide ±`bridge_snap_search_radius` along inset tangent; shortest valid water chord.
2. Update both junction positions; rewrite incident land road endpoints.
3. Append new `isBridge` road between the two junctions (brown render).

Unmatched shore junctions stay dead-ends at the bank.

## Config (`config.yml`)

```yaml
terrain:
  bridges_enabled: true
  bridge_snap_enabled: true
  bridge_snap_search_radius: 8.0

colors:
  bridge: [139, 90, 43]
```

Skipped from strip/match: secondary roads, terrain corridor roads, bridge roads, and degenerate road segments.

## Logging

Channel `voronoi`:

- `boundary_splits=N`
- `water_stubs_removed=N shore_junctions=N bridge_pairs_matched=M bridges_created=K bridges_snapped=S`

## Downstream

- Bridges skip frontage assignment (`assignRoadSideInwards`).
- `clip_roads_at_water` does not clip bridge segments.
- Road-only placement treats bridges as road blockers for depth rays; bridges do not create frontage.

See also: [terrain-roadmap.md](terrain-roadmap.md) Phase 4.
