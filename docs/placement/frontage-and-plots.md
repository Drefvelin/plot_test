# Frontage and Plots

## Purpose

Road banks expose plot slots; placement carves them and creates road-facing lots with depth limits and terrain validation.

## What it does

- Each buildable bank starts with one plot segment `[setback, len−setback]` and matching wall segment.
- Candidates pick a frontage span, set depth inward (capped by ray hit / 2), validate area and buildability, layout footprints on plot.
- Success carves plot segment and main-building wall occupancy; refreshes bank exhaustion flags.

**Rejections:** outside town disc, non-buildable terrain, overlap, road crossing, alley corridor conflict, depth/area out of band.

## How it works

| Concept | Detail |
|---------|--------|
| Setback | `plots.frontage_setback` (config) |
| Area bounds | `plots.min_area` / `max_area` + per-type `plot_sizes` in buildings.yml |
| Depth cap | `maxPlotDepthToRoadHit` — min of nearest **other-road hit / 2** and nearest **terrain outline hit** (full distance, no halving). Outline probes use `syncTerrainProbes.borderIds` (sea/river). Lazy memo on `RoadSideFrontage`. |
| Buildability | `polygonBuildable()` — corners + edge samples |
| Footprints | Orthogonal rectangles only; `footprintPlacementValid` after layout |

Key files: [`FrontagePlacement.cpp`](../../app/core/FrontagePlacement.cpp), [`PlotGeometry.cpp`](../../app/core/PlotGeometry.cpp), [`PlotDimensions.cpp`](../../app/core/PlotDimensions.cpp), [`Town.cpp`](../../app/core/Town.cpp) (`ensureTownFrontageInitialized`, carve helpers).

**Water-facing banks** — `assignRoadSideInwards` zeroes `inward` on forbidden side; no frontage there.

**Stable ids** — Road/segment ids monotonic; carve keeps left segment id.

Logs: `probe.log` (`terrain_forbidden`), `layout.log` (`terrain_reject`).

## Interactions

- Data model: [../architecture/data-model.md](../architecture/data-model.md)
- Rules (plot before gap-fill): [rules.md](rules.md)
- Gap-fill uses wall segments: [alleys-and-gap-fill.md](alleys-and-gap-fill.md)
- Terrain validation: [../town-generation/terrain/queries.md](../town-generation/terrain/queries.md)
