# Data Model

## Purpose

Define the runtime objects that represent town geometry and buildings after Voronoi generation.

## What it does

| Type | Role |
|------|------|
| `Town` | Root: `roads`, `junctions`, `buildingInstances`, `frontierManager`, hop rings, bridge buckets, render meshes |
| `Road` | Segment `a`–`b`; flags `isSecondary`, `isBridge`, `isTerrainCorridor`; banks `sideA` / `sideB` |
| `RoadSideFrontage` | Per bank: `segments` (plot slots), `wallSegments` (alley/gap gaps), `inward`, `exhausted`, depth/occupancy caches |
| `RoadFrontageSegment` | Span along road edge: `startT`, `endT`, `width`, `centerDist` |
| `Junction` | `pos` + incident `roadIds` |
| `Plot` | Four corners; `roadId`, `roadBank`; created at placement on `BuildingInstance` |
| `BuildingInstance` | `typeId`, queue `id`, `plot`, `footprints`, `placementMode` |
| `SecondaryRoadRecord` | Alley provenance: `hostRoadId`, `hostBankIndex`, geometry for rebuild |
| `TerrainAtlas` | Baked forbidden mask, outlines, raster; see [terrain queries](../town-generation/terrain/queries.md) |

Bank **0** is left of `a→b`; bank **1** is right.

Plots exist only after placement — there is no pre-generated lot mesh.

## How it works

**Frontage bootstrap** (`ensureTownFrontageInitialized` in `Town.cpp`):

- Each buildable bank gets one plot segment `[setback, len−setback]` and matching wall segment.
- Carving splits segments when plots or main footprints are placed.
- Wall segments are stored incrementally; not recomputed from instances each frame.

**Depth cap** for plot candidates (`maxPlotDepthToRoadHit`):

```text
syncMinPlotDepth = min depth for smallest plot_sizes band (both orientations)

road corridor width D to nearest other road:
  if D/2 >= syncMinPlotDepth     → roadCap = D/2
  else if D >= syncMinPlotDepth  → narrow strip: lower (roadId, bankIndex) gets D, facing bank gets 0
  else                           → roadCap = 0

outlineCap = nearest sea/river outline hit (full distance, no halving)

maxDepth = min(roadCap, outlineCap)   — omit a term when that hit is missing
```

Host road excluded from road probes. Outline uses `Town.syncTerrainProbes.borderIds` and `syncTerrainAtlas`. `syncMinPlotDepth` and `syncMinPlotFrontage` set from [`PlacementFloors`](../../app/core/PlacementFloors.cpp) at sync.

**Building footprints** must be orthogonal rectangles (`footprintHasRightAngles` in `PlotGeometry.cpp`).

Source: [`app/core/Town.h`](../../app/core/Town.h).

## Interactions

- Build pipeline: [overview.md](overview.md)
- Frontage and carving: [../placement/frontage-and-plots.md](../placement/frontage-and-plots.md)
- Alley records: [../placement/alleys-and-gap-fill.md](../placement/alleys-and-gap-fill.md)
- Config building defs: [../config/reference.md](../config/reference.md)
