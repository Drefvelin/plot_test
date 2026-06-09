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

**Goal:** Real road splits, buildability queries, cell tagging.

| Task | Status |
|------|--------|
| Split `Road` at water + re-index junctions | Done (bridges: [`terrain-bridges.md`](terrain-bridges.md)) |
| `rebuildAllCellBoundaries` after split; log failures | Not started |
| `isBuildable()` in footprint validation | Not started |
| `Cell.buildable` + `dominantTerrain` at town build | Not started |

**Exit criteria:** No building footprints inside water or rivers on test map.

## Phase 2 — Biome scoring

**Goal:** Farms, mines, and logging camps respect terrain softly.

| Task | Status |
|------|--------|
| Forest / hills / mountain region polygons in bake | Done (debug + future scoring) |
| Cell `plainsCoverage` from grid | Not started |
| Extend `buildings.yml` with terrain fields | Not started |
| Split `resource` → `mine` + `lumber_camp` (or equivalent) | Not started |
| Terrain term in `scoreSegmentForZone` | Not started |
| Farm hard/soft plains rules | Not started |

**Exit criteria:** Farms cluster on plains; resource types score near correct biomes.

## Phase 3 — Feature anchors

**Goal:** Watermills, docks, mines on features.

| Task | Status |
|------|--------|
| River centerlines in bake | Not started |
| Shoreline polylines in bake | Not started |
| `FeatureAnchor` placement mode | Not started |
| Watermill on river + road proximity | Not started |
| Fisherman / dock on shoreline | Not started |
| Mine on hills edge | Not started |

**Exit criteria:** At least one of each anchor type places correctly on the test map.

## Phase 4 — Roads & corridors

**Goal:** Rivers and coast shape connectivity; corridor roads subdivide cells.

Detailed pipeline: [terrain-corridor-roads.md](terrain-corridor-roads.md). Bridges: [terrain-bridges.md](terrain-bridges.md).

| Task | Status |
|------|--------|
| Shore + river corridor graphs on atlas (config insets/spacing) | Done |
| Emit corridor `Road`s; split at interior intersections | Done |
| Face extraction → replace `town.cells`; `voronoiParentId` | Done |
| Split at forbidden boundary; detect legal crossings → `isBridge` | Done |
| Bridge rendering (brown) + snap chord | Done |
| Parallel-to-river bias in secondary road probe | Not started |
| Parallel-to-shore bias (optional) | Not started |
| Voronoi site bias away from water (optional) | Not started |

**Exit criteria:** Corridor roads on map with junctions at Voronoi crosses; coastal strips subdivide; buildings on subdivided frontage.

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
| Zone scoring | `FrontageZones.cpp` | urban / residential / rural |
| Gap-fill band | `BuildingPlacer.cpp`, `FrontageGapFill.cpp` | center-out by cell centroid |
| Alley fill + pending | `SecondaryRoadPlacement.cpp` | exhausted pending stays non-blocking; general gap fill fills alleys |
| Protected plots | `PlotGeometry.cpp`, `buildings.yml` | `allow_plot_fill: false` |

When terrain phases touch these files, update this table if behaviour changes.
