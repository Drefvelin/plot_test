# Terrain Implementation Roadmap

Phased rollout. Update **Status** as work lands.

## Phase 1 — Foundation

Split into **Step 1** (visual + bake) and **Step 1.5 / 1b** (topology + placement). Detailed task list: [terrain-step1-implementation.md](terrain-step1-implementation.md).

### Step 1 — Bake, overlay, debug, visual road clip

**Goal:** Load terrain art, bake geometry, toggle overlay, see outlines and roads stopping at water (mesh only).

| Task | Status |
|------|--------|
| Config `terrain.image` + assets copy | Done |
| Load `terrain.txt` colour map | Done |
| `TerrainBake` → `TerrainAtlas` (forbidden, shoreline, forest/hills polygons, raster) | Done |
| Terrain overlay toggle (terrain+debug / debug / off) | Done |
| Debug outlines from simplified polygons (magenta / cyan / yellow / green / orange) | Done |
| Water-safe contour graphs + inset config | Done |
| Visual road mesh clip at water | Done |

**Exit criteria:** Overlay and debug toggles work; roads visibly stop at water; bake logs sane counts.

### Step 1.5 / Phase 1b — Topology and placement

**Goal:** Real road splits, buildability queries, and road-only placement validation.

| Task | Status |
|------|--------|
| Split `Road` at water + re-index junctions | Done — `sanitizeRoadGraphAtWater` at load for Voronoi + corridor primaries ([`terrain-bridges.md`](terrain-bridges.md)) |
| Cell boundary rebuild after split | Obsolete — road-only model removed cells |
| `isBuildable()` in footprint/plot validation | Done — `polygonBuildable` corners + edges; terrain threaded from `BuildingPlacer::sync` |
| Land-aware road bank inwards | Done — `assignRoadSideInwards(town, terrain)` disables water-facing banks |
| Road-hit depth cap for plots | Done — nearest inward road hit / 2 |

**Exit criteria:** No building footprints inside water or rivers on test map.

## Phase 2 — Biome scoring

**Goal:** Farms, mines, and logging camps respect terrain softly.

| Task | Status |
|------|--------|
| Forest / hills / plains minority outlines + `majorityLandKind` in bake | Done |
| `buildings.yml` terrain fields + `DefCache` parsing | Done |
| `FrontierManager` border + terrain-scan frontiers | Done |
| Rural terrain-first (`terrain_scan` / `frontier_loose_fallback`) | Done |
| Border placement hug + band styles | Done |
| `type: any` routing (`tryPlaceAnyOnRoads`) | Done |
| Road/segment terrain coverage from grid | Not started |
| Terrain term in `scoreSegmentForZone` | Not started |

**Exit criteria:** Farms cluster on plains; resource types score near correct biomes.

## Phase 3 — Feature anchors

**Goal:** Watermills, docks, mines on features.

| River / shoreline outlines in bake | Done |
| Border frontier (frontage-first) + `tryPlaceBorderPlot` (hug + band) | Done |
| Fisher hut / watermill `type: any` border placement | Done (strict river may fail if no slots) |
| Mine hills proximity via terrain-scan frontier | Done |
| `FeatureAnchor` placement mode | Not started |

**Exit criteria:** At least one of each anchor type places correctly on the test map.

## Phase 4 — Roads & corridors

**Goal:** Rivers and coast shape connectivity; corridor roads shape the road graph.

Detailed pipeline: [terrain-corridor-roads.md](terrain-corridor-roads.md). Bridges: [terrain-bridges.md](terrain-bridges.md).

| Task | Status |
|------|--------|
| Shore + river corridor graphs on atlas (config insets/spacing) | Done |
| Emit corridor `Road`s; split at interior intersections | Done |
| Road-only model removes `town.cells` and face extraction | Done ([road-only-model.md](road-only-model.md)) |
| Split at forbidden boundary; detect legal crossings → `isBridge` | Done |
| Bridge rendering (brown) + snap chord | Done |
| Parallel-to-river bias in secondary road probe | Not started |
| Parallel-to-shore bias (optional) | Not started |
| Voronoi site bias away from water (optional) | Not started |

**Exit criteria:** Corridor roads on map with junctions at Voronoi crosses; buildings use road-bank frontage and terrain validation.

## Phase 5 — Polish

| Task | Status |
|------|--------|
| Serialized `TerrainAtlas` cache | Not started |
| Bake validation asserts + unit tests | Not started |
| Tune simplification tolerance and margins in `town.yml` | Not started |
| Documentation sync with code (`plans/` + `AGENTS.md`) | Ongoing |

## Dependencies

```
Phase 1 ──► Phase 2 ──► Phase 3
                │
                └──► Phase 4 (bridges need forbidden + river geometry)
Phase 5 anytime after Phase 1
```

## Related work already in codebase

| Feature | Location | Notes |
|---------|----------|-------|
| Zone scoring | `FrontageZones.cpp` | urban / residential / rural; hop gates via `suburbanMaxHop` |
| Hop-ring growth | `GrowthRings.cpp`, `BuildingPlacer.cpp` | See [`placement-model.md`](placement-model.md); code drift: gap-fill order/band gating |
| Gap-fill + alleys | `FrontageGapFill.cpp`, `SecondaryRoadPlacement.cpp` | road-bank gap-fill; alleys on primary+secondary in core hop range |
| Protected plots | `PlotGeometry.cpp`, `buildings.yml` | `allow_plot_fill: false` |

When terrain phases touch these files, update this table if behaviour changes.
