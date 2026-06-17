# Plot Test — Agent Notes

## Design plans

Architecture and roadmap docs: [`plans/README.md`](plans/README.md). **Building placement rules:** [`plans/placement-model.md`](plans/placement-model.md) (authoritative). Update docs when changing terrain, placement, town build, or related config (see `.cursor/rules/plans-documentation.mdc`).

## Coordinate scale

**10 pixels = 1 world unit** (`world.pixels_per_unit: 10` in `app/config/config.yml`).

- All simulation geometry (Voronoi sites, roads, plots, frontage, terrain) is stored in **world units**.
- Rendering multiplies by `pixels_per_unit` to get screen/diagram pixels.
- Example: a 1024×1024 unit diagram renders at 10240×10240 pixels.

Helpers live in [`app/core/Units.h`](app/core/Units.h).

## Voronoi library boundary (mandatory)

We use **[jc_voronoi](app/core/third_party/jc_voronoi.h)** (`jcv_*` API) in **one place only**: [`TownBuilder.cpp`](app/core/TownBuilder.cpp) during initial town build.

**Allowed:** call library functions strictly to **populate our stored structs** from the generated diagram — principally `Road` (`a`, `b`) and the minimum reads needed to create them (diagram generate, iterate edges/sites, copy coordinates, free diagram). Copy values into `Vec2` / `Town` and discard the library objects.

**Forbidden after that point:**

- No `#include` of jc_voronoi outside `TownBuilder.cpp`.
- No `jcv_*` types, pointers, or API calls in junctions, frontage segments, plot placement, carving, mesh rebuilds, or any other gameplay/geometry logic.
- Do **not** use library helpers for edge direction, perpendiculars, inside/outside tests, distances, or any other math — implement that in our code (`Vec2`, `Town.cpp`, `BuildingPlacer.cpp`, etc.) using **only data already on `Town`** (`roads`, `junctions`, `buildingInstances`, terrain, …).

Infrastructure libraries (SFML rendering, yaml-cpp config, etc.) are fine. The rule is about **Voronoi/diagram geometry**: once `Town` is built, all further geometry is **our code + our stored data**, not the Voronoi library.

## Core data model

[`app/core/Town.h`](app/core/Town.h):

| Type | Role |
|------|------|
| `Town` | Root object; owns `roads`, `junctions`, building instances, render meshes |
| `Road` | Segment from `a` to `b`; may be primary, secondary/alley, terrain corridor, or bridge; frontage on `sideA` / `sideB` |
| `RoadSideFrontage` | One bank of a road. Bank 0 is left of `a→b`; bank 1 is right. Holds `segments` (plot frontage) and `wallSegments` (free wall gaps for alleys/gap-fill). `exhausted` holds per-bank skip flags (`PlotDone`, `AlleyDone`, `GapDone`). |
| `Junction` | Road endpoint where roads meet; `pos` + `roadIds[]` |
| `Plot` | Road-facing lot created only on placement: `corners[0]`–`[1]` along frontage, `[2]`–`[3]` inward |
| `BuildingInstance` | Spawned building: `buildingType` + assigned `Plot`; lives in `Town.buildingInstances` |

Build pipeline:

1. `TownBuilder::build(config)` — jc_voronoi → roads; corridors/bridges; `assignRoadSideInwards(terrain)`; `ensureTownFrontageInitialized` (segments + frontier once)
2. `BuildingPlacer::sync` — place buildings using road banks, frontage segments, ray-depth checks, junctions, roads, and **`TerrainAtlas` buildability** (plot/footprint corners + edge samples; water-facing banks skipped)
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
    fill_in: true        # permission for core gap-fill only; all buildings use plots — see plans/placement-model.md
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

[`app/config/town.yml`](app/config/town.yml) sets target building counts and `initial_suburban_max_hops` (starting suburban ring). [`config.yml`](app/config/config.yml) `town.seed` shuffles a build queue.

- `BuildingGrowthQueue` — seeded shuffle of all building types; slider selects first N
- `BuildingInstance` — one spawned building with its `Plot`; stored on `Town.buildingInstances`
- **Placement model** — [`plans/placement-model.md`](plans/placement-model.md): plot is **always tried first** on every road; rural = outside suburban; urban/residential = town ring (core ⊂ suburban); `fill_in` = optional **core-only** gap-fill after plot fails (footprint-only, no plot); alleys are **roads** (same `tryPlaceOnTownRoad` path).
- **Hop rings** — `GrowthRings.cpp` implements band expansion (`suburbanMaxHop`, `urbanCoreMaxHop`, `ringPhase`). Operational detail in [`plans/terrain-placement.md`](plans/terrain-placement.md).
- `BuildingPlacer::sync` — **one queue index per call**; no per-sync full carve replay, exhaustion scan, frontier rebuild, or alley-record town scan; carve + frontier refresh on success only
- **Gap-fill / alley records** — `removeSecondaryRecordsBlockedByMainFootprint` on successful gap-fill place only (`SecondaryRoadPlacement.cpp`); not every sync
- **Auto-grow** — CLI `--auto-grow [N]` raises target by **one building per step** (`--auto-grow-ms` interval from config, default 50 ms). Optional `--auto-exit` quits after target is reached and placed (exit code = failure count). Config `growth:` keys; bare `--auto-grow` defaults to **200** (or `town.yml` total if higher). **G** toggles in-app auto-grow to max.
- **Growth sync** — each `BuildingPlacer::sync` handles **at most one** queue index (one building request). Slider jumps catch up one building per frame, not in a batch drain.
- **Placement frontiers** — bootstrap once at town build; incremental bank refresh on carve; `frontierExtendBands` on ring bump with `restoreRoadWallFromInstances` when a road's zone band changes; full `restoreRoadFrontageFromInstances` on demolish; `removeBuildingInstance` for demolish (one-bank restore)
- **Profiling** — `--profile` or `growth.profile: true` enables scoped timing (`Profile.h`); summary on `--auto-exit`. Child scopes under `PlacerSync` / `PlaceGapFill` / `MeshRebuild` break down the hot path (nested scopes overlap in totals).
- **Junction hop cache** — `Town` caches junction BFS hops and per-road hop values (`getJunctionHops`, `getRoadHop` in `FrontageZones.cpp`); invalidated on `indexJunctions` / road topology changes. Collectors take cached hops instead of recomputing per placement attempt.
- **Per-bank road exhaustion** — updated on carve (`refreshBankExhaustionAfterCarve`); bootstrap once. Not rescanned every sync.
- **Placement perf caches** — [`PlacementPrep`](app/core/PlacementPrep.h) hoists per-index setup. [`RoadAttemptMemo`](app/core/PlacementPrep.h) skips roads that already failed plot+gap for the current queue index. Per-bank lazy depth memo on `RoadSideFrontage` (`maxPlotDepthToRoadHit`). Per-bank `mainOccupancyT` merged t-spans for gap-fill clip (rebuilt on carve/bootstrap). `Town.secondaryRoadIds` for fast alley overlap. `Town::roadTopologyGeneration` + `invalidateRoadTopologyCaches()` clears depth + occupancy caches on topology change.
- `Hud` — screen-fixed bar at top; drag to set 0…max buildings; shows `{placed} placed / {target} target` and **Failures: N** under the slider (red when N > 0, green when 0). **T** / terrain toggle cycles overlay modes including **hop**. **G** toggles auto-grow.

Alleys are secondary roads. They are created from primary road wall gaps on **developed banks only** (at least one building on that road+bank, plot or gap-fill). Probes run in nearest-gap order with **seeded shuffle** of gap positions and angle fan (`townSeed`); the probe ray starts inset from the wall gap for clearance, but the **placed alley** starts at the gap point on the host road centerline. Straight probes stop at the first non-source road (pass through existing alleys); on any straight failure the same `(gapT, angle)` tries a **depth stub** to `maxPlotDepthToRoadHit`, then scans a **180° turn fan** (≥19 directions, seeded shuffle). All ray hits are scored; a fully validated road-to-road turn wins, otherwise the best partial hit is used (so turns can be 30°, 45°, etc., not only 90°). Dead-end stub if no turn hit. Rejects include main-building corridor hits and alleys too parallel to another on the same bank (`min_alley_bank_angle_sep_deg`). Junctions on crossed roads via `splitRoadsAtAlleyEndpoints` (host at `alley.a`, dest at `alley.b`).

## Terrain placement

- Baked [`TerrainAtlas`](app/core/TerrainAtlas.h): `isBuildable()` uses dilated forbidden raster (sea, river, etc.).
- Plot and footprint candidates must pass `polygonBuildable()` (all corners + edge samples buildable).
- `assignRoadSideInwards(town, terrain)` sets bank `inward` only on the buildable side of each road; zero inward skips frontage on water-facing banks.
- Rejection logs: `terrain_forbidden` (`probe.log`), `terrain_reject` (`layout.log`).

```
app/core/
  Town.h / Town.cpp       — data structures; junctions, frontage segments (our geometry only)
  TownBuilder.*           — jc_voronoi → Town (library confined here)
  PlacementFrontier.cpp   — distance-bucketed frontiers; incremental refresh
  Town.cpp                — ensureTownFrontageInitialized, applySecondaryRoadRecord, removeBuildingInstance
  BuildingPlacer.*        — plot/building placement from Town data
  App.*                   — window + render loop
  Config.*                — YAML loading
  Units.h                 — unit ↔ pixel conversion
```
