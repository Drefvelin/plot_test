# Placement Sync Pipeline

## Purpose

Document one `BuildingPlacer::sync` call: at most one building request, minimal global rescans, carve and frontier refresh on success only.

## What it does

Each sync:

1. Ensures placement floor mins (`syncMinPlotFrontage`, etc.) from defs.
2. If cursor < target, attempts **one** queue index.
3. Routes by building `type`: rural → any → urban/residential (with alley pending fills, core densify, ring sweep, bump loop).
4. On success: push `BuildingInstance`, carve frontage/wall, `notifyPlacementFrontier(PlotCarved)`, rebuild meshes.
5. On failure: record in `placementFailedIndices`; HUD **Failures** count.

No per-sync full carve replay, exhaustion rescan, or town-wide alley cleanup.

## How it works

Entry: [`BuildingPlacer.cpp`](../../app/core/placement/orchestration/BuildingPlacer.cpp) `sync`.

**Urban/residential order** (see [rules.md](rules.md)):

1. Pending alley plot fills (`tryFillBlockingPendingAlleys`) — same `tryPlaceOnTownRoad`.
2. If `DensifyCore`: `tryPlaceInUrbanCore`.
3. Town-ring sweep: plot per road, then gap-fill when gated.
4. Bump rings (max 3 per UI frame) on exhaustion.

**Per road** (`tryPlaceOnTownRoad` in `GrowthRings.cpp`): plot first (`tryPlaceRoadPlot`); gap-fill only if `mayGapFillOnRoad`.

**Performance** — `PlacementPrep` hoists per-index setup; `RoadAttemptMemo` skips roads that failed plot+gap; junction hop cache on `Town`; depth memo on `RoadSideFrontage`; `invalidateRoadTopologyCaches` on alley rebuild.

Profiling scopes: `PlacerSync`, `GrowthLoop`, `PlaceGapFill`, `MeshRebuild`, `TerrainBorderPlace`, `TerrainScanPeek`, `TerrainScanTrySlot`.

Logs: `layout.log` — `ring_*`, `placement_skipped`, `gap_fill`.

## Interactions

- Authoritative rules: [rules.md](rules.md)
- Frontiers: [frontiers.md](frontiers.md)
- Growth queue: [../growth/queue-and-controls.md](../growth/queue-and-controls.md)
- Frontage carving: [frontage-and-plots.md](frontage-and-plots.md)
