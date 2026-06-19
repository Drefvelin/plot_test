# Placement Frontiers

## Purpose

Maintain distance-sorted buckets of eligible frontage slots so placement pulls closest-first without scanning all roads each attempt.

## What it does

All state on `Town.frontierManager`:

| Bucket | Source | Sort key |
|--------|--------|----------|
| `plot[0..2]` | Plot segments ≥ min width | Road midpoint dist (core/suburban/rural band) |
| `wall[0..2]` | Wall segments ≥ min gap | Same bands |
| `alley` | Core developed-bank wall gaps not in `checkedAlleyGaps` | Core dist |
| `border[]` | Segments hitting terrain outline inward ray | Per `TerrainId`; road midpoint |
| `scanPlains/Forest/Hills` | Bootstrap terrain-scan segments | Terrain score + zone |

Zone/band not stored on segments — computed O(1) via `roadCenterDist` / `roadFrontierBand`.

## How it works

**Single entry:** `notifyPlacementFrontier` — no direct rebuild from carve code.

| Event | Effect |
|-------|--------|
| `FullRebuild` | Town bootstrap after frontage init |
| `PlotCarved` | Refresh plot/wall/alley bank + scan + border bank |
| `RingExtended` | `frontierExtendBands` — re-bucket on band change, wall resync |
| `TopologyChanged` | Bulk secondary replay rebuild |
| `InstanceRemoved` | Border bank refresh if border building removed |

**Peek/consume APIs** — `PlacementFrontier.cpp`: `peekClosestPlotSlot`, `peekClosestWallGapSlot`, `peekNextTerrainScanSlot`, `peekNextBorderSlot`.

Incremental alley: `applySecondaryRoadRecord` → `notifyRoadFrontierRefresh` on affected roads (not full topology notify).

Per-bank exhaustion (`RoadExhaustion.h`): `PlotDone`, `GapDone`, `AlleyDone` — updated on carve, not full rescan each sync.

Files: [`FrontierManager.cpp`](../../app/core/FrontierManager.cpp), [`BorderFrontier.cpp`](../../app/core/BorderFrontier.cpp), [`PlacementFrontier.cpp`](../../app/core/PlacementFrontier.cpp).

## Interactions

- Sync pipeline: [sync-pipeline.md](sync-pipeline.md)
- Terrain border placement: [terrain-buildings.md](terrain-buildings.md)
- Rules (frontier table): [rules.md](rules.md)
- Hop bands: [zones-and-rings.md](zones-and-rings.md)
