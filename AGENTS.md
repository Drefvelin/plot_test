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
- Helpers: [`app/core/common/Units.h`](app/core/common/Units.h).

## Voronoi library boundary (mandatory)

**[jc_voronoi](app/core/generation/third_party/jc_voronoi.h)** only in [`TownBuilder.cpp`](app/core/generation/TownBuilder.cpp) at initial build.

After build: no `jcv_*` anywhere else; all geometry from stored `Town` data and our math.

## Core data model

[`app/core/town/Town.h`](app/core/town/Town.h):

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
- Building types — [`app/config/buildings.yml`](app/config/buildings.yml) via [`DefCache`](app/core/config/DefCache.h)

## HUD keys

**T** terrain · **Z** zones · **B** bridges · **P** biome plots · **G** auto-grow · slider = target count

Detail: [`docs/app/rendering-and-debug.md`](docs/app/rendering-and-debug.md).

## Key source files

Flat `app/core/` is now organized into subsystem folders (include root stays `app/core`, so includes are path-qualified, e.g. `#include "town/Town.h"`).

```
app/core/
  common/      Vec2.h, Units.h, Geometry.{h,cpp}, RenderPrimitives.{h,cpp}
  config/      Config, TownConfig, DefCache, BuildingTypes.h, YamlUtil
  util/        Logger, Profile, MemoryReport
  terrain/     Terrain.h, TerrainCatalog, TerrainColors, TerrainAtlas.h, TerrainBake
  town/        Town.h (data hub) + split impls:
                 Town.cpp (shared helpers, namespace townint) + TownInternal.h
                 TownJunctions, TownFrontage, TownCarving, TownSecondary,
                 TownBridgeBuckets, TownMeshes
  generation/  TownBuilder (Voronoi confined here), third_party/jc_voronoi.h
  roads/       RoadNetwork.h + split impls:
                 RoadNetwork.cpp (shared helpers, namespace roadnet) + RoadNetworkInternal.h
                 CorridorCull, RoadIntersectionSplit, WaterSanitize, RoadDedup,
                 BridgeResolve, SecondaryRoadPlacement, RoadExhaustion
  placement/
    orchestration/  BuildingPlacer (growth sync), BuildingGrowthQueue, GrowthRings
                    (hop bands, road sweeps), PlacementPrep, PlacementFloors,
                    MovableRelocation
    zones/          FrontageZones
    frontage/       FrontagePlacement, FrontageGapFill
    frontier/       FrontierManager (notify fan-out), PlacementFrontier,
                    TerrainScanFrontier, BorderFrontier, FrontierSlotUtils
    terrain/        TerrainPlacement, BorderPlacement (terrain border buildings)
    geometry/       PlotGeometry, PlotDimensions, BuildingLayout
    logging/        PlacementLogging, TerrainPlacementLogging
  render/      App, Hud, Camera
  main.cpp     (stays at root)
```
