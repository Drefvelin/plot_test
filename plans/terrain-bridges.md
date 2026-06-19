# Terrain Bridges

River/sea crossings become **bridge** roads in the graph: a brown line between two inset junctions.

## Pipeline

```
Voronoi + disc roads
  → appendCorridorRoads
  → splitRoadsAtIntersections
  → sanitizeRoadGraphAtWater (once at load; all non-bridge primaries incl. corridors)
  → mergeWatersideJunctions (cluster nearby shore junctions; collapse fork stubs)
  → resolveBridges (outline shore filter → same-body pair → snap → create)
  → cullVoronoiRoadsParallelToCorridors
  → buildRoadMesh (bridges brown, skip water clip)
  → buildJunctionMesh (bridge candidates purple, others red)
```

Implementation: [`RoadNetwork.cpp`](../app/core/RoadNetwork.cpp), [`TownBuilder.cpp`](../app/core/TownBuilder.cpp).

## Algorithm

### 1 — Water sanitize (load-time, all primaries)

`sanitizeRoadGraphAtWater` runs once in `TownBuilder::build` when terrain is valid (not during placement):

1. **Boundary split** — all non-bridge primaries (Voronoi + `isTerrainCorridor`) at land/water crossings.
2. **Clip trim / cull** — `clipRoadSegmentToLand` per segment; delete non-buildable spans; emit multiple roads for land–water–land gaps.
3. **Junction re-index** — `indexJunctions`; snap any remaining non-buildable junctions landward along incident roads.

Skipped: `isBridge`, `isSecondary` (none exist at load), degenerate segments.

### 1b — Waterside junction merge

`mergeWatersideJunctions` runs after sanitize, before bridge pairing:

1. **Collect shore junctions** — same outline + land-road filter as bridge candidates (`classifyShoreBridgeCandidate`, `junctionHasLandRoad`).
2. **Cluster** — union-find on pairs within `shore_junction_merge_dist` (default **2.0** u; **0** disables).
3. **Merge** — move each cluster to its centroid via `updateJunctionPosition`; `indexJunctions` collapses co-located junctions.
4. **Compact** — remove degenerate segments and duplicate roads between the same junction pair (keeps longest).

Log: `shore_junction_merge clusters=… merged_junctions=… removed_roads=…`.

### 2 — Collect shore junctions (outline-based)

A junction is a bridge endpoint candidate when:

1. **Near sea or river outline** — `nearestOutlineFrame` on `outlinesByTerrainId`; distance ≤ `bridge_outline_max_dist`. Prefer **river** when both are in range, else sea.
2. **Road-connected** — at least one incident non-bridge/non-secondary road with buildable midpoint (`isLandRoad`).

Junction positions after sanitize sit on buildable land slightly inset from the outline — not on the polyline itself. IDs are stored on `Town.bridgeCandidateJunctionIds` and drawn **purple** in `buildJunctionMesh` (other junctions red).

### 3 — Pair candidates (same water body)

For each unordered shore pair `(ja, jb)`:

| Rule | Check |
|------|--------|
| Same water body | same `TerrainId` kind (sea or river) |
| Max span | chord length ≤ `bridge_max_span` |
| Min span | chord length ≥ 0.5 u |
| Same bank | no land path ≤ **8 road hops** between junctions (non-bridge roads only) |
| Pairable shores | same water kind, **or** opposite banks (allows river↔sea at mouths) |
| Valid crossing | `segmentInteriorMostlyWater` on interior |

**Matching** — valid pairs sorted by chord length (shortest first). For each candidate, before snap:

1. If either endpoint has a **closer** valid partner in a **later** candidate (same junction, shorter endpoint separation, partner still free) → **defer** this pair.
2. Repeat passes until no new bridges are placed.

A junction is reserved only when a pair **passes snap** (`findBestBridgeChord`); failed snap leaves both junctions free for the next pass.

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
  bridge_max_span: 180.0
  bridge_outline_max_dist: 8.0
  shore_junction_merge_dist: 2.0   # 0 = off
  bridge_snap_enabled: true
  bridge_snap_search_radius: 8.0

colors:
  bridge: [139, 90, 43]
```

Skipped from sanitize/bridge match: secondary roads, bridge roads, and degenerate road segments. Corridor roads are **included** in sanitize (same as Voronoi primaries).

## Logging

Channel `voronoi`:

- `water_sanitize boundary_splits=N removed=M split_multiland=P junctions_snapped=Q`
- `bridge_candidates=N matched=M created=K snapped=S max_span=X shore_junctions=J reject_body=… reject_span=… reject_same_side=… reject_not_water=… reject_snap=… deferred=…`

## Downstream

- Bridges skip frontage assignment (`assignRoadSideInwards`).
- `clip_roads_at_water` does not clip bridge segments.
- Road-only placement treats bridges as road blockers for depth rays; bridges do not create frontage.

See also: [terrain-roadmap.md](terrain-roadmap.md) Phase 4.
