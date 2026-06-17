# Terrain & Placement Integration

How baked terrain connects to the existing town growth and placement pipeline.

**Authoritative placement rules** (plots, zones, alleys, gap-fill): [`placement-model.md`](placement-model.md). This file covers terrain hooks and hop-ring **operational** notes only.

## Current placement flow

1. `TownBuilder::build` — Voronoi → road graph; corridors, bridges, splits, culls, junctions; `ensureTownFrontageInitialized` once
2. `BuildingPlacer::sync` — **one building per call** via `placementQueueCursor`; no per-sync full carve replay or frontier rebuild
3. Slot collection — `PlacementFrontier` pull; legacy collectors for per-road `roadFilter >= 0` only
4. Scoring — `scoreSegmentForZone` in `FrontageZones.cpp` (urban / residential / rural)
5. Depth cap — inward ray to nearest other road, divided by two
6. Hard rejects — zone score `1e9f`; disc, terrain (corners + edge samples), overlap, road crossing, alley checks in `PlotGeometry.cpp`
7. Modes — `PlotLot`, `SegmentGapFill`; alley logic in `SecondaryRoadPlacement.cpp`

Terrain adds **hard rejects**, **score terms**, and eventually **new placement modes**.

## Hard constraints (must not place)

Implemented in [`PlotGeometry.cpp`](../app/core/PlotGeometry.cpp) via `polygonBuildable()` (corner + edge sampling) and threaded from [`BuildingPlacer::sync`](../app/core/BuildingPlacer.cpp) through `tryPlaceRoadPlot` / `tryPlaceSegmentMain`.

Apply when validating plot corners and building footprints (same layer as overlap / protected plot checks):

```cpp
bool polygonBuildable(const Vec2 corners[4], const TerrainAtlas& terrain, float edgeStep = 0.5f);
// rejects if any corner or any edge sample is not terrain.isBuildable()
```

Reject entire candidate if **any** plot or footprint corner/edge fails. After `layoutBuildingsOnPlot`, each footprint is re-checked with `footprintPlacementValid(..., terrain, ...)`.

Water-facing road banks are disabled at town build and during secondary-road rebuild: [`assignRoadSideInwards`](../app/core/Town.cpp) probes buildability on each side of the road and zeroes `inward` on forbidden banks (no frontage segments on that side).

Logging: `terrain_forbidden` in `probe.log`; `terrain_reject: queueIndex=…` in `layout.log`.

## Soft constraints (prefer / score)

Extend or complement `scoreSegmentForZone` in `FrontageZones.cpp`:

```cpp
float terrainScore = terrainAffinity(buildingType, midpoint, terrain);
if (terrainScore >= 1e8f) return 1e9f;  // hard reject via affinity
return existingZoneScore + terrainScore * weight;
```

Examples:

| Building | Rule |
|----------|------|
| `farm` | Require enough nearby plains coverage; prefer higher coverage |
| `mine` | Minimize `distToRegionEdge(p, Hills)` under cap |
| `lumber_camp` | Minimize distance to forest edge; allow up to ~20 units |
| `house` / `workshop` | Prefer buildable plains; no special anchor |

Keep hard water/river rejection separate from soft biome preference.

## Feature-anchor placement (new mode)

Some buildings should not use road frontage as primary logic:

| Building | Anchor | Selection |
|----------|--------|-----------|
| Watermill | River centerline point | On river; near road/junction within max distance; segment not already used |
| Fisherman / dock | Shoreline point | On shore; road/frontage within inland distance |
| Mine | Hills polygon edge | Near edge; road access |

Proposed enum extension on `BuildingInstance`:

```cpp
enum class BuildingPlacementMode {
    PlotLot,
    SegmentGapFill,
    FeatureAnchor,  // river | shore | hills edge
};
```

`BuildingPlacer::sync` routes queue entries with `terrain.anchor` set to a dedicated placer before or instead of generic frontage search.

## Roads, bridges, and following terrain

### Bridges

When a `Road` segment intersects a forbidden river polygon:

- Record a `Bridge` at intersection
- Allow road to cross (render bridge; no building on crossing)
- Detect at town build by segment–polygon intersection test

### Roads following rivers / shore

Do not replace Voronoi graph initially. **Bias** candidate roads:

- In `SecondaryRoadPlacement` / alley probing: score candidates parallel to nearby river centerline or shoreline within a corridor (~5–10 units)
- Optional later: snap one coastal primary edge to simplified shoreline polyline

### Voronoi site bias (optional)

When placing Voronoi sites in `TownBuilder`, bias generators toward plains grid locations and away from forbidden terrain so fewer initial roads cross water.

## Alley / gap-fill interaction

Gap-fill is **core-only densification** for `fill_in` types; all buildings use plots in normal placement. See [`placement-model.md`](placement-model.md).

When a gap-fill main building is placed, `removeSecondaryRecordsBlockedByMainFootprint` removes conflicting alley **records** incrementally (then `rebuildSecondaryRoadsFromRecords` if needed). There is **no** per-sync town-wide alley cleanup scan in `BuildingPlacer::sync` (debug helper `removeAlleysThroughSecondaryBuildings` retained for recovery only).

Gap-fill probing uses per-bank `mainOccupancyT` spans for `clipGapToMainWalls`, `Town.secondaryRoadIds` for alley corridor tests, and **full** `footprintOverlapsMains` for overlap rejection (town-wide; required for correctness across banks and roads).

Terrain hard rejects apply to plot corners and gap-fill footprints alike. `collectFrontageSlots` includes alley roads the same as primary roads.

**Alley creation quality** (`town.yml`): `min_alley_side_road_dist`, `min_alley_crossing_angle_deg`, `min_alley_endpoint_spacing` — see [`road-only-model.md`](road-only-model.md) alleys section.

## Hop-ring town growth (operational)

Full rules: [`placement-model.md`](placement-model.md) (plain-English summary + technical reference).

Growth uses junction-hop rings (`GrowthRings.cpp`, `BuildingPlacer.cpp`). Summary:

| Field | Role |
|-------|------|
| `suburbanMaxHop` | Town ring for urban/residential |
| `urbanCoreMaxHop` | Core subset (−1 until first bump) |
| `ringPhase` | `Normal` / `DensifyCore` |

**Bump:** last road in town-ring sweep fails → expand rings → retry index. Roads whose rural/suburban/core band changes get `restoreRoadWallFromInstances` (wall segments, exhaustion, alley checked-state, wall/alley frontier; plot segments unchanged). Interactive: max 3 bumps per UI frame per index.

**Per road (urban/residential):** `tryPlaceOnTownRoad` — plot first; gap-fill only when plot fails and `mayGapFillOnRoad` (`fill_in` + hop ≤ `urbanCoreMaxHop` + `DensifyCore`). See [`placement-model.md`](placement-model.md).

**Rural:** `tryPlaceRuralOnRoads`, plot only, `hop > suburbanMaxHop`.

Layout trace: `ring_attempt`, `ring_road_list`, `ring_road_fail`, `ring_band_exhausted`, `ring_summary`, `placement_skipped`.

### Auto-grow (unattended testing)

CLI (from build output dir):

```
PlotApp.exe --auto-grow 50 --auto-exit --profile
```

- `--auto-grow [N]` — target count (bare flag defaults to **200**, or `town.yml` total if higher)
- `--auto-exit` — quit after target count is requested **and** placement cursor has caught up; print `ring_summary` to stdout; exit code = failure count
- `--auto-grow-ms MS` — milliseconds between +1 target requests (default from `config.yml`, usually 50)
- `--profile` — enable scoped timing buckets; summary on `--auto-exit` (stdout + log). Top-level: `PlacerSync`, `GrowthLoop`, `PlaceGapFill`, `MeshRebuild`. Child scopes (overlap parents): `SyncAlleyCleanup`, `PlacementPrep`, `RingBump`, `GapFillCollect`, `GapFillTrySlot`, `PlotTrySegment`, `PlotLayout`, `FrontageCarve`, `FrontageWallCarve`, `MeshOutline`, `MeshFrontageSeg`, `MeshAlleyProbe`, `MeshRoad`, `MeshHopDebug`, `MeshJunction`.
- `growth.verbose_placement_logs: true` — per-sync segment inventory and segment probes (`segments` / `probe` channels)

**Growth model:** one building request per step — same as slider +1 or a future external “new building” ping. `BuildingPlacer::sync` places at most one queue index per call.

**Performance:** junction hop distances cached on `Town`. **Placement frontiers** hold plot/wall/alley candidates in distance buckets; **incremental** bank refresh on carve (no per-sync full carve replay, town-wide frontier rebuild, or alley-record reconcile). Ring bump uses `frontierExtendBands`: per-road wall geometry resync when the road's zone band changes, plus `frontierRefreshPlotBank` so plot frontier refs move to the correct bucket (e.g. Rural→Suburban). Gap-fill uses `RoadSideFrontage.mainOccupancyT` + scoped overlap checks. Mesh rebuild in sync is PlotApp visual only.

**Stable IDs:** road/segment ids are monotonic (`next` counters); carve keeps left segment id; `assignRoadSideInwards` updates inward only (does not wipe segments).

**Per-bank exhaustion flags** (`RoadExhaustion.h`) — incremental on carve; bootstrap once:

| Bit | Set when | Skip in |
|-----|----------|---------|
| `PlotDone` | No frontage segment on bank ≥ global min plot frontage | legacy collectors |
| `GapDone` | No stored wall segment on bank ≥ global min gap width | legacy gap collectors |
| `AlleyDone` | Alley probe exhausted on bank | alley collectors |

Global floors from [`PlacementFloors::fromDefs`](app/core/PlacementFloors.cpp); copied to `Town.syncMin*` each sync via `ensurePlacementSyncMins`. Incremental alley apply uses `applySecondaryRoadRecord` (append road, local `frontierRefreshRoad`); full `rebuildSecondaryRoadsFromRecords` on record trim/removal or when gap-fill blocks a record. PlotApp slider-down does not replay carves or alley records (debug limitation).

**Placement perf caches** (`PlacementPrep.h`, `PlotGeometry.cpp`):

| Cache | Storage | Invalidation |
|-------|---------|--------------|
| Depth memo | `RoadSideFrontage.depthCacheEntries` — lazy `(t, frontage, setback) → maxPlotDepthToRoadHit` | `Town::roadTopologyGeneration` bump (secondary rebuild, alley push) |
| Road attempt memo | Transient `RoadAttemptMemo` per queue index in growth loop | Ring bump, topology gen, new building instance |
| Setup hoist | `PlacementPrep` built once per index attempt | N/A (rebuilt each index) |

`invalidateRoadTopologyCaches(town)` increments `roadTopologyGeneration` and clears depth memos (lazy on next query). Called from `rebuildSecondaryRoadsFromRecords` and direct alley apply in `SecondaryRoadPlacement.cpp`.

Config fallback in `config.yml` `growth:` (`auto_grow`, `auto_grow_ms`, `auto_exit`, `profile`). In-app: **G** toggles auto-grow to max buildings.

### Debug

- Layout log: `ring_state`, `ring_bump`, `ring_place`, `ring_attempt`, `ring_road_list`, `ring_road_try`, `ring_road_fail`, `ring_band_exhausted`, `ring_band_cap`, `ring_summary`, `placement_skipped` (`rural_out_of_range`, `urban_core_exhausted`, `ring_no_slot`)
- **Zone tint:** HUD **Zones** button or **Z** — placement-band road/junction coloring (`hopDebugRoadMesh` / `hopDebugJunctionMesh`); separate from terrain toggle (**T**).

## Growth failures (skip-and-continue)

`BuildingPlacer::sync` does **not** stop on the first failed placement. Each queue index is attempted once per sync pass:

- Success → `buildingInstances.push_back` with `BuildingInstance.id` = queue index
- Failure → `placement_skipped` in `layout.log` (reasons: `rural_out_of_range`, `urban_core_exhausted`, `ring_no_slot`), index stored in `placementFailedIndices`

The HUD shows **Failures: N** under the growth slider (red when N > 0). Target healthy runs: N = 0.

## Config surface

| File | Keys |
|------|------|
| `app/config/terrain.txt` | Colour → kind (exists) |
| `app/config/config.yml` | `terrain.image_path`, bake grid size (proposed) |
| `app/config/buildings.yml` | Per-type `terrain.prefer`, `anchor`, thresholds (proposed) |
| `app/config/config.yml` | `growth.auto_grow*`, `growth.profile`, terrain bake, `world.pixels_per_unit` |

## Logging

Add a `terrain` log channel (or use `layout`) for:

- Bake summary (counts, timing)
- Rejected placements (`terrain_forbidden`, `terrain_biome`)
- Anchor placements (`watermill`, `dock`, `mine`)

Match existing patterns in `Logger` / `PlacementLogging.cpp`.
