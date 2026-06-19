# Alleys and Gap-Fill

## Purpose

Increase core density: gap-fill places footprint-only buildings in wall gaps; alleys add secondary roads from those gaps for further plot placement.

## What it does

**Gap-fill** — Core-only, `fill_in` types, during `DensifyCore`. After plot fails on a core road: `tryPlaceSegmentMain` pulls wall-gap frontier slot, places main footprint without plot (`SegmentGapFill`). Not used in suburban-only placement or before first plot attempt.

**Alleys** — Created from wall gaps on **developed** primary banks (≥1 building on that road+bank). New `Road` with `isSecondary = true`. After creation, plots use the **same** `tryPlaceRoadPlot` as primaries.

## How it works

### Gap-fill

- Gated by `mayGapFillOnRoad` (core hop, `DensifyCore`, `fill_in`).
- Uses `mainOccupancyT` spans for clip; `footprintOverlapsMains` for overlap.
- Success: `removeSecondaryRecordsBlockedByMainFootprint` if alley record blocked.

File: [`FrontageGapFill.cpp`](../../app/core/FrontageGapFill.cpp).

### Alley creation

File: [`SecondaryRoadPlacement.cpp`](../../app/core/SecondaryRoadPlacement.cpp).

Probe order per `(gapT, angle)`:

1. Straight inward ray to dest road (may chain through existing alleys).
2. On failure: depth stub to `maxPlotDepthToRoadHit`.
3. Valid stub → 180° turn fan (≥19 dirs, seeded shuffle); prefer validated road-to-road turn, else best partial.
4. Dead-end stub if no turn; water stop allowed if land segment ≥ `min_alley_length`.

**Placed alley** starts at gap on host centerline (host split at `gapPoint`). Probe origin uses inset `max(frontage_setback, min_alley_side_road_dist)`.

Quality gates (`town.yml`): `min_alley_side_road_dist`, `min_alley_crossing_angle_deg`, `min_alley_bank_angle_sep_deg`, `min_alley_endpoint_spacing`. Set `0` to disable.

**Differences from primaries**

| Aspect | Primary | Alley |
|--------|---------|-------|
| Created at load | Yes | During growth |
| Pass-through when spawning | N/A | Yes |
| Plot API | `tryPlaceRoadPlot` | Same |
| Render colour | Black | Green (`secondary_edges`) |

Logs: `alley_diag`, `layout.log` (`gap_fill`, `blocked_by_main`, `bank_parallel`, `water_stop`).

## Interactions

- Rules: [rules.md](rules.md)
- Frontage wall segments: [frontage-and-plots.md](frontage-and-plots.md)
- Frontier alley bucket: [frontiers.md](frontiers.md)
- Config alley keys: [../config/reference.md](../config/reference.md)
