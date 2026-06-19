# Terrain System Overview

## Purpose

Make town generation respond to a colour-coded terrain map so buildings avoid water, roads follow coast/rivers, and rural types prefer biomes.

## What it does

From `app/config/terrain.txt` + PNG:

| Kind | Intended use |
|------|----------------|
| `sea` | Forbidden; shoreline; coastal roads |
| `river` | Forbidden; mills; bridges |
| `plains` | Farms; general buildable land |
| `forest` | Logging camps |
| `hills` | Mines |
| `mountain` | Harder terrain; mines |

At runtime:

- Hard reject non-buildable footprints (dilated sea+river).
- Soft/route placement via terrain scan, border buckets, proximity/inside modes.
- Corridor roads and bridges shape connectivity.

## How it works

**Core constraint:** no per-pixel PNG lookup during placement scoring loops. Bake once → `TerrainAtlas` with raster, dilated forbidden mask, and simplified outlines.

```
Terrain PNG + terrain.txt
        │
        ▼  bake once
   TerrainAtlas
        ├── forbidden (dilated sea+river)
        ├── sea/river/forest/hills/plains outlines
        ├── corridor road graphs
        └── labelled raster + optional coarse grid
        │
        ▼
   Town build + placement hooks
```

Coordinate space matches diagram bounds (`diagram.width/height`, `world.pixels_per_unit: 10`).

## Interactions

- Bake pipeline: [bake-and-atlas.md](bake-and-atlas.md)
- Query API: [queries.md](queries.md)
- Corridor roads: [corridors.md](corridors.md)
- Placement integration: [../../placement/terrain-buildings.md](../../placement/terrain-buildings.md)
- Water/bridges: [../water-and-bridges.md](../water-and-bridges.md)

## Non-goals

Real-time terrain editing, hydrology simulation, pixel-exact biome boundaries, replacing Voronoi with hand-authored roads.
