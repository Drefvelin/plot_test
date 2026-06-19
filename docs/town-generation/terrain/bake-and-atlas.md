# Terrain Bake and Atlas

## Purpose

Convert terrain art into static geometry and rasters used for buildability, outlines, corridor graphs, and debug overlays.

## What it does

One-time bake at startup (`TerrainBake.cpp` → `TerrainAtlas`):

1. **Label image** — RGB → `TerrainKind` (exact match, then nearest colour, max delta 32; river wins ties over sea).
2. **Forbidden zones** — Sea+river mask, dilate (`water_inset`), trace contours → `forbiddenPolygons`; separate `seaOutlines` / `riverOutlines` with `shore_inset` / `river_inset`.
3. **Land biome outlines** — Count non-water pixels → `majorityLandKind`; trace boundary for **minority** kinds only (forest, hills, plains).
4. **Corridor graphs** — `shoreRoadGraph`, `riverRoadGraph` with corridor insets/spacing.
5. **Coarse grid** (optional) — `grid_size` e.g. 128×128 for fast dominant-kind sampling.

Bake logs (`terrain.log`): component counts, `majority_land=`, `river_nodes=`, `border_frontier: sea_slots=… river_slots=…` after town build.

## How it works

| Input | Config key |
|-------|------------|
| PNG | `terrain.image` |
| Colour map | `terrain.colors` → `app/config/terrain.txt` |
| Simplify | `terrain.simplify_tolerance` |
| Insets | `water_inset`, `shore_inset`, `river_inset` |
| Grid | `terrain.grid_size` |

`TerrainAtlas` structure ([`TerrainAtlas.h`](../../../app/core/TerrainAtlas.h)):

- `majorityLandKind`
- `forbiddenPolygons`, `*Outlines` (open polylines)
- `raster`, `forbiddenDilated`
- Corridor graphs, overlay texture, debug meshes

Validation: log outline node counts; minority kinds get non-zero counts when present on map.

Avoid: keeping full-res PNG for per-candidate queries; one global polygon per biome type.

## Interactions

- Runtime queries: [queries.md](queries.md)
- Corridors emitted as roads: [corridors.md](corridors.md)
- Waterside raster probe uses baked raster: [../water-and-bridges.md](../water-and-bridges.md)
- Config keys: [../../config/reference.md](../../config/reference.md)
