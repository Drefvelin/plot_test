# Plot Test — Agent Notes

Short cheat sheet for agents. Subsystem detail lives in [`docs/README.md`](docs/README.md).

## Documentation

| Topic | Doc |
|-------|-----|
| Index + status | [`docs/README.md`](docs/README.md) |
| **Placement rules (authoritative)** | [`docs/placement/rules.md`](docs/placement/rules.md) |
| Architecture | [`docs/architecture/overview.md`](docs/architecture/overview.md) |
| Config keys | [`docs/config/reference.md`](docs/config/reference.md) |

Update docs in the **same change** when modifying behaviour (see `.cursor/rules/docs-documentation.mdc`).

## Coordinate scale

**10 pixels = 1 world unit** (`world.pixels_per_unit: 10`).

- Simulation geometry in **world units** (`Vec2` / `float`).
- Rendering multiplies by `pixels_per_unit`.
- Helpers: [`app/core/Units.h`](app/core/Units.h).

## Voronoi library boundary (mandatory)

**[jc_voronoi](app/core/third_party/jc_voronoi.h)** only in [`TownBuilder.cpp`](app/core/TownBuilder.cpp) at initial build.

After build: no `jcv_*` anywhere else; all geometry from stored `Town` data and our math.

## Core data model

[`app/core/Town.h`](app/core/Town.h):

| Type | Role |
|------|------|
| `Town` | Root: roads, junctions, instances, frontiers, meshes |
| `Road` | Segment `a`–`b`; primary, secondary, corridor, or bridge |
| `RoadSideFrontage` | Bank segments + wall gaps + exhaustion |
| `Junction` | `pos` + incident roads |
| `Plot` | Road-facing lot on `BuildingInstance` |
| `BuildingInstance` | `typeId`, queue `id`, `plot`, footprints |

Detail: [`docs/architecture/data-model.md`](docs/architecture/data-model.md).

## Build pipeline (one line)

`TownBuilder::build` → Voronoi roads, corridors, water sanitize, bridge resolve, buckets, bank inwards, frontage init → `BuildingPlacer::sync` (one building/sync) → `App` render.

Detail: [`docs/architecture/overview.md`](docs/architecture/overview.md), [`docs/town-generation/water-and-bridges.md`](docs/town-generation/water-and-bridges.md).

## Placement essentials

- Plot **always tried first**; gap-fill core-only for `fill_in` types.
- Hop bands: core ⊂ suburban; rural outside suburban; `any` = all roads.
- Alleys are **roads** — same `tryPlaceOnTownRoad`.
- Frontiers on `Town.frontierManager`; `notifyPlacementFrontier` only entry.
- Terrain: border → scan → anchor → loose fallback. See [`docs/placement/terrain-buildings.md`](docs/placement/terrain-buildings.md).

## Config pointers

- `diagram.*`, `world.pixels_per_unit`, `plots.*`, `voronoi.*`, `terrain.*`, `growth.*` — [`docs/config/reference.md`](docs/config/reference.md)
- Building types — [`app/config/buildings.yml`](app/config/buildings.yml) via [`DefCache`](app/core/DefCache.h)

## HUD keys

**T** terrain · **Z** zones · **B** bridges · **P** biome plots · **G** auto-grow · slider = target count

Detail: [`docs/app/rendering-and-debug.md`](docs/app/rendering-and-debug.md).

## Key source files

```
app/core/
  Town.h / Town.cpp       — data, frontage, bridge buckets
  TownBuilder.cpp         — Voronoi (library confined here)
  RoadNetwork.cpp         — sanitize, bridges, corridors
  BuildingPlacer.cpp      — growth sync
  GrowthRings.cpp         — hop bands, road sweeps
  FrontierManager.cpp     — frontier notify fan-out
  BorderPlacement.cpp     — terrain border buildings
```
