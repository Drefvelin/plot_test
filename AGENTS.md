# Plot Test — Agent Notes

## Design plans

Architecture and roadmap docs: [`plans/README.md`](plans/README.md). Update them when changing terrain, placement, town build, or related config (see `.cursor/rules/plans-documentation.mdc`).

## Coordinate scale

**10 pixels = 1 world unit** (`world.pixels_per_unit: 10` in `app/config/config.yml`).

- All simulation geometry (Voronoi sites, roads, plots, cell boundaries) is stored in **world units**.
- Rendering multiplies by `pixels_per_unit` to get screen/diagram pixels.
- Example: a 1024×1024 unit diagram renders at 10240×10240 pixels.

Helpers live in [`app/core/Units.h`](app/core/Units.h).

## Voronoi library boundary (mandatory)

We use **[jc_voronoi](app/core/third_party/jc_voronoi.h)** (`jcv_*` API) in **one place only**: [`TownBuilder.cpp`](app/core/TownBuilder.cpp) during initial town build.

**Allowed:** call library functions strictly to **populate our stored structs** from the generated diagram — principally `Road` (`a`, `b`, `cellA`, `cellB`) and the minimum reads needed to create them (diagram generate, iterate edges/sites, copy coordinates, free diagram). Copy values into `Vec2` / `Town` and discard the library objects.

**Forbidden after that point:**

- No `#include` of jc_voronoi outside `TownBuilder.cpp`.
- No `jcv_*` types, pointers, or API calls in junctions, frontage segments, plot placement, carving, mesh rebuilds, or any other gameplay/geometry logic.
- Do **not** use library helpers for edge direction, perpendiculars, inside/outside tests, distances, or any other math — implement that in our code (`Vec2`, `Town.cpp`, `BuildingPlacer.cpp`, etc.) using **only data already on `Town`** (`roads`, `junctions`, `cells`, `plots`, …).

Infrastructure libraries (SFML rendering, yaml-cpp config, etc.) are fine. The rule is about **Voronoi/diagram geometry**: once `Town` is built, all further geometry is **our code + our stored data**, not the Voronoi library.

## Core data model

[`app/core/Town.h`](app/core/Town.h):

| Type | Role |
|------|------|
| `Town` | Root object; owns `cells`, `roads`, `junctions`, render meshes |
| `Cell` | One Voronoi cell: seed `site`, `boundary` polygon, `roadIds`, `plots` |
| `Road` | Shared edge between two cells (`a`, `b`, `cellA`, `cellB`); frontage on `sideA` / `sideB` |
| `Junction` | Road endpoint where roads meet; `pos` + `roadIds[]` |
| `Plot` | Road-facing lot: `corners[0]`–`[1]` on road (frontage), `[2]`–`[3]` depth into cell |
| `BuildingInstance` | Spawned building: `buildingType` + assigned `Plot`; lives in `Town.buildingInstances` |

Build pipeline:

1. `TownBuilder::build(config)` — jc_voronoi → copy into `Town` (`cells`, `roads`); then `indexJunctions`, `buildJunctionMesh`, `resetRoadFrontageSegments` (all our code, no jc_voronoi)
2. `BuildingPlacer::sync` — place buildings using segments/junctions/roads from `Town` only
3. `App` — white disc background texture; draws `roadMesh`, `junctionMesh`, frontage overlays, buildings

## Config keys

- `diagram.width/height/radius` — world units
- `world.pixels_per_unit` — px per unit (default 10)
- `plots.min_area` / `plots.max_area` — plot area bounds in square units
- `voronoi.scale` / `voronoi.seed` — site density and RNG seed

## Definition cache

[`app/config/buildings.yml`](app/config/buildings.yml) maps building types to size categories and defines area bands:

```yaml
buildings:
  house:
    size: small
    type: residential   # urban | residential | rural
    rgb: [220, 180, 140]
  church:
    size: huge
    type: urban
    rgb: [160, 160, 210]
  # farm, resource, workshop ...

sizes:
  small:
    min_area: 25
    max_area: 75
  # medium, large, huge ...
```

Loaded at startup into [`DefCache`](app/core/DefCache.h):

- `building("house")` → `BuildingDef` with `sizeCategory`, `type`, `rgb`
- `sizeBand("medium")` → min/max area for a category
- `sizeBandForBuilding("church")` → huge band
- `plotFitsBuilding(plotArea, "farm")` → whether a plot can hold that building

## Town growth (slider)

[`app/config/town.yml`](app/config/town.yml) sets target building counts. [`config.yml`](app/config/config.yml) `town.seed` shuffles a build queue (79 entries for default counts).

- `BuildingGrowthQueue` — seeded shuffle of all building types; slider selects first N
- `BuildingInstance` — one spawned building with its `Plot`; stored on `Town.buildingInstances`
- `BuildingPlacer::sync` — slider up adds road-facing instances, slider down removes them
- Plots align to roads: frontage parallel to road edge, depth into the cell (see diagram in spec)
- `Hud` — screen-fixed bar at top; drag to set 0…max buildings

Plot assignment not implemented yet.

```
app/core/
  Town.h / Town.cpp       — data structures; junctions, frontage segments (our geometry only)
  TownBuilder.*           — jc_voronoi → Town (library confined here)
  BuildingPlacer.*        — plot/building placement from Town data
  App.*                   — window + render loop
  Config.*                — YAML loading
  Units.h                 — unit ↔ pixel conversion
```
