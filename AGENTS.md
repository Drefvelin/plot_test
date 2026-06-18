# Plot Test — Agent Notes

## Design plans

Architecture and roadmap docs: [`plans/README.md`](plans/README.md). **Building placement rules:** [`plans/placement-model.md`](plans/placement-model.md) (authoritative). Update docs when changing terrain, placement, town build, or related config (see `.cursor/rules/plans-documentation.mdc`).

## Coordinate scale

**10 pixels = 1 world unit** (`world.pixels_per_unit: 10` in `app/config/config.yml`).

- All simulation geometry (Voronoi sites, roads, plots, frontage, terrain) is stored in **world units** as `Vec2` / `float`.
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
| `BuildingInstance` | Spawned building: `typeId` (DefCache id), `id` (uint32 queue index), assigned `Plot`; lives in `Town.buildingInstances` |

Build pipeline:

1. `TownBuilder::build(config)` — jc_voronoi → roads; corridors/bridges; `assignRoadSideInwards(terrain)`; `ensureTownFrontageInitialized` (segments + frontier once)
2. `BuildingPlacer::sync` — place buildings using road banks, frontage segments, ray-depth checks, junctions, roads, and **`TerrainAtlas` buildability** (plot/footprint corners + edge samples; water-facing banks skipped)
3. `App` — white disc background texture; draws `roadMesh`, `junctionMesh`, frontage overlays, buildings

## Config keys

- `diagram.width/height/radius` — world units
- `world.pixels_per_unit` — px per unit (default 10)
- `plots.min_area` / `plots.max_area` — plot area bounds in square units
- `plots.terrain_anchor_max_roads` — BFS road cap for rural `terrain_anchor` fallback per `TerrainKind` (default 4)
- `voronoi.scale` / `voronoi.seed` — site density and RNG seed

## Definition cache

[`app/config/buildings.yml`](app/config/buildings.yml) maps building types to size categories and defines area bands:

```yaml
buildings:
  house:
    size: small
    type: residential   # urban | residential | rural | any
    fill_in: true        # permission for core gap-fill only; all buildings use plots — see plans/placement-model.md
    rgb: [220, 180, 140]
  church:
    size: huge
    type: urban
    rgb: [160, 160, 210]
  # farm, lumber_camp, mine, fisher_hut, watermill, workshop ...

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
- **Movable buildings** — `movable: true` in `buildings.yml` (farm, lumber_camp, mine, fisher_hut, watermill). On ring bump, zone-incompatible movables try a new rural/suburban slot first; old instance removed only on success. Failures increment `moveFailureCount` (HUD: **Move failures**). Find-first flow in `MovableRelocation.cpp`.
- **Placement frontiers** — all frontier state on `Town.frontierManager`: hop-band plot/wall/alley (Core/Suburban/Rural), terrain-scan (plains/forest/hills), **border** frontage buckets per `TerrainId` (plot segments whose inward ray reaches a terrain outline **before** crossing another road — source road excluded; sorted by **road midpoint** distance). Zone/band is **not** stored on segments — O(1) `roadCenterDist(town, roadId)` / `roadFrontierBand(town, roadId)` from `town.roads[roadId]`. **`notifyPlacementFrontier`** is the single sync entry point — fan-out on carve (`PlotCarved`), topology rebuild, demolish (`InstanceRemoved`), ring bump. Bootstrap via `notify(FullRebuild)` at town build. Border placement peeks border buckets (`peekNextBorderSlot`); **plot required** on frontage then main building (band/hug); up to `border_max_attempts` retries. Success logs `border_attempt_ok mode=border_plot`. No road/building overlap; water spill OK.
- **Profiling** — `--profile` or `growth.profile: true` enables scoped timing (`Profile.h`); summary on `--auto-exit`. Child scopes under `PlacerSync` / `PlaceGapFill` / `MeshRebuild` break down the hot path (nested scopes overlap in totals). Terrain/border child scopes: `TerrainBorderPlace`, `TerrainScanPeek`, `TerrainScanTrySlot`.
- **Memory report** — on exit (before teardown), writes simulation-data breakdown to `logs/memory.md` (capacity-based; excludes terrain image rasters, render caches, and debug-only alley probe history). See `MemoryReport.cpp`.
- **Junction hop cache** — `Town` caches junction BFS hops and per-road hop values (`getJunctionHops`, `getRoadHop` in `FrontageZones.cpp`); invalidated on `indexJunctions` / road topology changes. Collectors take cached hops instead of recomputing per placement attempt.
- **Per-bank road exhaustion** — updated on carve (`refreshBankExhaustionAfterCarve`); bootstrap once. Not rescanned every sync.
- **Placement perf caches** — [`PlacementPrep`](app/core/PlacementPrep.h) hoists per-index setup. [`RoadAttemptMemo`](app/core/PlacementPrep.h) skips roads that already failed plot+gap for the current queue index. Per-bank lazy depth memo on `RoadSideFrontage` (`maxPlotDepthToRoadHit`). Per-bank `mainOccupancyT` merged t-spans for gap-fill clip (rebuilt on carve/bootstrap). `Town.secondaryRoadIds` for fast alley overlap. `Town::roadTopologyGeneration` + `invalidateRoadTopologyCaches()` clears depth + occupancy caches on topology change.
- `Hud` — screen-fixed bar at top; drag to set 0…max buildings; shows `{placed} placed / {target} target` and **Failures: N** under the slider (red when N > 0, green when 0). **T** cycles terrain overlay modes; **Z** zone hop tint; **P** / **Biome plots** shows terrain-tagged building plot outlines plus rotated labels (`type`, `placement/prefer` e.g. `farm` / `prox/plains`); **G** toggles auto-grow.

Alleys are secondary roads. They are created from primary road wall gaps on **developed banks only** (at least one building on that road+bank, plot or gap-fill). Probes run in nearest-gap order with **seeded shuffle** of gap positions and angle fan (`townSeed`); the probe ray starts inset from the wall gap for clearance, but the **placed alley** starts at the gap point on the host road centerline. Straight probes stop at the first non-source road (pass through existing alleys); on any straight failure the same `(gapT, angle)` tries a **depth stub** to `maxPlotDepthToRoadHit`, then scans a **180° turn fan** (≥19 directions, seeded shuffle). All ray hits are scored; a fully validated road-to-road turn wins, otherwise the best partial hit is used (so turns can be 30°, 45°, etc., not only 90°). Dead-end stub if no turn hit. Rejects include main-building corridor hits and alleys too parallel to another on the same bank (`min_alley_bank_angle_sep_deg`). Junctions on crossed roads via `splitRoadsAtAlleyEndpoints` (host at `alley.a`, dest at `alley.b`).

## Terrain placement

- Baked [`TerrainAtlas`](app/core/TerrainAtlas.h): `isBuildable()` uses dilated forbidden raster (sea, river, etc.). **`majorityLandKind`** is the dominant non-water biome per map; minority kinds get smoothed **outline polylines** (same trace path as river/shore). `distToRegionEdge(p, kind)` / `hasRegionOutline(kind)` for edge distance; debug **T** draws forest (green) and hills (gray-brown) outlines when non-majority.
- **Terrain modes** on `BuildingDef.terrain` (`inside` / `proximity` / `border`; `requirement: loose | strict`; `border_style: band | hug`; **`prefer` scalar or list** e.g. `prefer: [sea, river]` or `water`): terrain-first uses `TerrainScanFrontier` peek ([`FrontagePlacement.cpp`](app/core/FrontagePlacement.cpp) `RoadPlotSearchMode::TerrainScan`); loose types fall back to vanilla frontier on their band scope; `watermill` is strict. Border placer [`BorderPlacement.cpp`](app/core/BorderPlacement.cpp) peeks [`BorderFrontier`](app/core/BorderFrontier.cpp) buckets — **plot required** on frontage, then main (band/hug); road/building overlap rejected, water spill OK. Loose types retry band style after hug fails. Rural band: `farm`, `lumber_camp`, `mine`. **`type: any`**: `fisher_hut`, `watermill`.
- **Terrain bake:** nearest RGB classify in [`TerrainColors.cpp`](app/core/TerrainColors.cpp); `terrain.river_inset` (default = `shore_inset`) for land-side `riverOutlines`; bake log `border_frontier: sea_slots=… river_slots=…`.
- All building footprints must have four 90° corners (`footprintHasRightAngles`); building orientation is independent of skewed plot or outline edges — layouts use orthogonal rectangles (road-perpendicular or rotated), never parallelograms warped to plot sides.
- Plot and footprint candidates must pass `polygonBuildable()` (all corners + edge samples buildable).
- `assignRoadSideInwards(town, terrain)` sets bank `inward` only on the buildable side of each road; zero inward skips frontage on water-facing banks.
- Rejection logs: `terrain_forbidden` (`probe.log`), `terrain_reject` (`layout.log`). **Terrain-first trace:** `logs/terrain_place.log` — grep `queueIndex=N` for phase, slot tries, and final `event=placed` (path, corners, terrain scores).

```
app/core/
  Town.h / Town.cpp       — data structures; junctions, frontage segments (our geometry only)
  TownBuilder.*           — jc_voronoi → Town (library confined here)
  FrontierManager.cpp     — unified frontier storage + notify fan-out (plot/wall/alley/terrain-scan/border)
  BorderFrontier.cpp      — border bucket rebuild + peek (frontage segment → outline ray; reject if another road blocks)
  PlacementFrontier.cpp   — peek/consume APIs; internal rebuild/refresh (called only from notify)
  Town.cpp                — ensureTownFrontageInitialized, applySecondaryRoadRecord, removeBuildingInstance
  BuildingPlacer.*        — plot/building placement from Town data
  App.*                   — window + render loop
  Config.*                — YAML loading
  Units.h                 — unit ↔ pixel conversion
```
