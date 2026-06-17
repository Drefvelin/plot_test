# Terrain System — Overview

## Goal

Make town generation respond to a **terrain map** (colour-coded PNG + `app/config/terrain.txt`) so that:

- Buildings are not placed in **water** or **rivers**
- **Bridges** span rivers where roads cross them
- **Roads** can follow **rivers** and **shorelines**
- **Resources** split by biome: mines near **hills**, logging near **forest**
- **Farms** prefer **plains**
- Special buildings anchor to features: **watermills** on rivers, **fishermen / docks** on the **shoreline**

The map should feel dynamic and location-driven rather than purely Voronoi + zone type.

## Terrain types (config)

From `app/config/terrain.txt`:

| Key | RGB | Intended use |
|-----|-----|--------------|
| `sea` | 50, 56, 200 | Forbidden; shoreline features; coastal roads |
| `river` | 43, 113, 223 | Forbidden for buildings; river centerlines; mills; bridges |
| `plains` | 58, 200, 50 | Farms; general buildable land |
| `forest` | 27, 122, 38 | Logging camps (in or near) |
| `hills` | 147, 155, 137 | Mines (in or near) |
| `mountain` | 203, 206, 198 | Harder terrain; mines; possibly reduced buildability |

## Core constraint: no per-pixel lookup at placement time

Reading the terrain image for every building candidate would be too slow. Instead:

1. **Bake once** at startup (or load a cached binary) from PNG + colour map
2. Store **simplified geometry** and optional **coarse grids**
3. Answer placement queries with point-in-polygon, distance-to-polyline, and grid lookups

Exact pixel fidelity is not required. Approximate outlines are fine as long as we never place inside water or rivers, and “near forest” can mean within a configurable distance of a simplified polygon edge.

## Architecture (mental model)

```
Terrain PNG + terrain.txt
        │
        ▼  (bake once)
   TerrainAtlas (static)
        │
        ├── Forbidden polygons     → hard reject (sea + river + buffer)
        ├── Feature polylines      → rivers (centerline), shoreline
        ├── Region polygons        → forest, hills, mountain (simplified)
        └── Coarse terrain grid    → fast “what biome is here?” sampling
        │
        ▼
   Placement hooks
        ├── Hard reject in footprint validation
        ├── Soft score in zone / segment scoring
        ├── Road/segment terrain sampling for scoring
        └── Feature-anchor placement (mills, docks, mines)
```

## Coordinate space

Same as the rest of the project: **world units**, with `world.pixels_per_unit: 10` (10 diagram pixels = 1 unit). The terrain image should align with the Voronoi diagram bounds (`diagram.width/height` in `app/config/config.yml`, default 1024×1024 units).

See [AGENTS.md](../AGENTS.md) for scale details.

## What exists today

- `app/config/terrain.txt` — colour definitions only; **not loaded by the app**
- Placement uses road-bank frontage + zone types (`urban`, `residential`, `rural`) via `FrontageZones.cpp`
- Terrain hard rejects are in placement validation; feature-anchored buildings are still future work

## Non-goals (for initial phases)

- Real-time terrain editing
- Hydrology simulation
- Exact forest/hill boundaries matching every pixel
- Replacing the Voronoi road network entirely with hand-authored roads
