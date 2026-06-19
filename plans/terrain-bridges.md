# Terrain Bridges

River/sea crossings become **bridge** roads in the graph: a brown line between two inset junctions.

## Pipeline

```
Voronoi + disc roads
  → appendCorridorRoads
  → splitRoadsAtIntersections
  → sanitizeRoadGraphAtWater (once at load; all non-bridge primaries incl. corridors)
  → mergeWatersideJunctions (cluster nearby shore junctions; collapse fork stubs)
  → resolveBridges (waterside junctions from sanitize → pair → snap → create)
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

1. **Collect waterside junctions** — only IDs in `Town.watersideJunctionIds` (junctions within `bridge_waterside_max_dist` of a forbidden terrain outline); must still have a land road (`junctionHasLandRoad`).
2. **Cluster** — union-find on pairs within `shore_junction_merge_dist` (default **2.0** u; **0** disables).
3. **Merge** — move each cluster to its centroid via `updateJunctionPosition`; `indexJunctions` collapses co-located junctions.
4. **Compact** — remove degenerate segments and duplicate roads between the same junction pair (keeps longest).

Log: `shore_junction_merge clusters=… merged_junctions=… removed_roads=…`.

### 2 — Waterside junction set (terrain image raster)

After sanitize (and refreshed after merge / bridge resolve), `collectWatersideJunctionIds` probes the **baked terrain image raster** at each junction:

1. Sample `TerrainAtlas::sample(junction.pos)` — world units map to terrain image pixels (`10 px = 1 u`).
2. Sample `TerrainAtlas::sample` on a disc of radius `bridge_waterside_max_dist` (default **4.0** u; step **0.25** u) around the junction.
3. If any sample classifies as a **forbidden** terrain kind (`sea`, `river` from `terrains.yml`), the junction is bridge-eligible.

No outline geometry is used. Logs: `waterside j=… raster_dist=… hit_kind=… hit_point=… junction_sample=…`.

These IDs are stored on `Town.watersideJunctionIds` and drawn **purple** in `buildJunctionMesh` and the **B** bridge debug view (other junctions red).

For pairing, each waterside junction gets a water body via `nearestWatersideWaterBody` (same raster probe — nearest forbidden kind within radius).

### 3 — Pair candidates (same water body)

For each unordered shore pair `(ja, jb)`:

| Rule | Check |
|------|--------|
| Same water body | same `TerrainId` kind (sea or river) |
| Max span | chord length ≤ `bridge_max_span` |
| Min span | chord length ≥ 0.5 u |
| Same bank | no land path ≤ **8 road hops** between junctions (non-bridge roads only) |
| Pairable shores | same water kind, **or** opposite banks (allows river↔sea at mouths) |

Snap validation (`findBestBridgeChord`) still requires a mostly-water interior on the chosen chord; pairing does not pre-check the unsnapped segment.

**Matching** — valid pairs sorted by chord length (shortest first). Before snap, defer when either endpoint:

1. Has a **closer valid** bridge to another free shore junction (cross-water).
2. Has a **same-bank** shore junction that is **closer to the same opposite partner** (≤8 non-bridge hops) — let the nearer fork on that bank take the crossing.

Repeat passes until no new bridges are placed. Snap runs only on accept; failed snap leaves both junctions free for the next pass.

### 4 — Snap and create

For each matched pair:

1. Optional snap: slide ±`bridge_snap_search_radius` along inset tangent; shortest valid water chord.
2. Update both junction positions; rewrite incident land road endpoints.
3. Append new `isBridge` road between the two junctions (brown render).

Unmatched shore junctions stay dead-ends at the bank.

### 5 — Bridge buckets and growth reveal

After `resolveBridges` and road cull, `buildBridgeBuckets` runs once:

1. **Bucket** — for each bridge, junction-hop BFS from **both** endpoints along non-bridge roads (stops at other bridges); collects roads within `bridge_bucket_hops` (default **2**) of each end.
2. **Hidden by default** — unrevealed bridges are omitted from `roadMesh` (normal view).
3. **Seed reveal** — the bridge whose midpoint is closest to `Town.center` starts revealed (always at least one).
4. **Building reveal** — when any `BuildingInstance` is placed on a road in a bucket (`instance.roadId` or `plot.roadId`), that bridge is revealed on the next `rebuildRoadMesh`.

Bridge debug (**B**) still draws all bridges regardless of reveal state.

Logs: `bridge_buckets count=… seed_b=…`, `bridge_revealed b=… trigger_road=…`.

## Config (`config.yml`)

```yaml
terrain:
  bridges_enabled: true
  bridge_max_span: 180.0
  bridge_waterside_max_dist: 4.0
  shore_junction_merge_dist: 2.0   # 0 = off
  bridge_snap_enabled: true
  bridge_snap_search_radius: 8.0
  bridge_bucket_hops: 2

colors:
  bridge: [139, 90, 43]
```

Skipped from sanitize/bridge match: secondary roads, bridge roads, and degenerate road segments. Corridor roads are **included** in sanitize (same as Voronoi primaries).

## Logging

Channel `voronoi`:

- `water_sanitize boundary_splits=N removed=M split_multiland=P junctions_snapped=Q`
- `shore_junction_merge clusters=…`
- `bridge_candidates=N …` (summary)

Channel **`bridge`** (`logs/bridge.log`) — full pipeline:

- `sanitize …` — water sanitize summary
- `merge_cluster` / `merge_summary` — waterside junction merge
- `waterside_collect radius=…` — raster probe setup
- `waterside j=… raster_dist=… hit_kind=… hit_point=… junction_sample=…` — accepted junction
- `waterside_count=…` — total marked junctions
- `candidate j=… pos=… roads=…` — each waterside junction used for pairing
- `pair_ok ja=… jb=… span=…` — valid crossing pairs
- `defer …` — skipped pair with reason (`closer_partner`, `same_bank`)
- `snap_fail …` — chord snap rejected
- `placed road_id=… ja=… jb=… roads_ja=… roads_jb=…` — bridge created
- `resolve_summary …` — totals

## Downstream

- Bridges skip frontage assignment (`assignRoadSideInwards`).
- `clip_roads_at_water` does not clip bridge segments.
- Road-only placement treats bridges as road blockers for depth rays; bridges do not create frontage.

See also: [terrain-roadmap.md](terrain-roadmap.md) Phase 4.
