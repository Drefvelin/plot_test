# Building placement model

**Authoritative rules** for how buildings are placed. Hop-ring growth, terrain, and alleys must follow this document. If code or other plans disagree, **this file wins** until deliberately changed.

---

## Plain English summary

Read this section first. It states the rules in normal language — no implementation jargon required.

### What we are building

A town grows building by building along **roads**. Every building sits on a **plot**: a road-facing lot (frontage along the road, depth inward). There is no such thing as a “normal building without a plot.” Farms, houses, churches, workshops — all use plots.

### Three distance bands from the town centre

Think of concentric rings measured in **hops** (how many road junctions away from the centre):

| Band | Who builds here |
|------|-----------------|
| **Core** | Urban and residential buildings (houses, workshops, churches). This is the dense heart. |
| **Suburban** | Same urban/residential buildings. Core is *inside* suburban — suburban is the larger town area that includes the core. |
| **Rural** | Farms and resource buildings only. **Outside** suburban — not in the town ring. |

Rural is strictly **beyond** the suburban edge. Urban/residential never use the rural band.

### Every building type uses plots

- A **plot** is a rectangle (or quadrilateral) with two corners on the road and two corners set back into the block.
- Placement always aims to create a real plot on a road frontage, then lay out building footprints **on** that plot.
- Normal growth should produce a real plot for every building. The only intentional exception is **core gap-fill** (see below): footprint-only, no plot.

### Gap-fill is extra density in the core only

Some building types have `fill_in: true` in config (e.g. houses, workshops). That means:

- They still get **plots** like everyone else in suburban and core.
- **Additionally**, in the **core only**, the placer may try to **gap-fill**: squeeze a building into a narrow gap between existing frontages on the same road bank, when a full plot does not fit.

Gap-fill is **not** a substitute for plots. It is **not** used in suburban-only areas. It is **not** tried before a normal plot on the first buildings at the start of growth.

**Churches** have `fill_in: false` — plot only, never gap-fill.

**Farms / resources** — rural band, plot only.

### Roads are roads

The Voronoi roads at town creation, terrain corridors, bridges, and **alleys** are all entries in the same `town.roads` list.

- **Alleys** are roads that are added later (not at initial load), carved from gaps between buildings on primary roads.
- When placing a building, the game does **not** use a separate “alley plot method” vs “main road plot method.” It picks frontage on **whichever road** fits the zone band and scoring — alley or primary, same rules.
- The only special things about alleys are: **how they are created** (ray from a wall gap, can pass through an existing alley), and the `isSecondary` flag for rendering and alley-creation logic. After an alley exists, frontage segments, plots, gap-fill, and carving work like any other road.

### How growth roughly works

1. Shuffle a queue of building types (houses, workshops, etc.).
2. For each queue entry, try to place one building.
3. **Urban/residential:** pull closest eligible plot frontage from distance buckets (core / suburban), then gap-fill from wall-gap frontier when densifying core.
4. **Rural:** pull closest eligible plot from rural bucket only.
5. If the town ring is full for this building, **expand** the ring (bump) and try again.
6. In **core densify** phase (after a bump), types with `fill_in` may also create **new alleys** from wall gaps and use **gap-fill** on core roads — still on real road frontage, still band-aware.

### Common misconceptions (wrong vs right)

| Wrong | Right |
|-------|--------|
| Houses are suburban plot-only; workshops own the core | Houses, workshops, churches all use the **town ring** (suburban + core) |
| `fill_in` means “place without a plot” | **Plot is always tried first**; `fill_in` allows an **extra** core-only gap-fill fallback with **no plot** |
| Try gap-fill first everywhere for houses | **Plot first** everywhere; gap-fill only in core when densifying |
| Alleys need their own placement API | Alleys are **roads**; one plot placement path, band filter decides eligible roads |
| Rural can sit inside suburban | Rural is **only** where hop > suburban max |

### Code alignment

Per-road placement is centralized in `tryPlaceOnTownRoad` (`GrowthRings.cpp`): **plot first**, then gap-fill only when `mayGapFillOnRoad` (core hop, `DensifyCore`, `fill_in` type). Gap-fill success uses `SegmentGapFill` with an empty `Plot` by design. See [Implementation map](#implementation-map).

---

## Technical reference

### Coordinate and road primitives

- World units; see [`AGENTS.md`](../AGENTS.md).
- [`Road`](../app/core/Town.h): segment `a`–`b`; flags `isSecondary`, `isBridge`, `isTerrainCorridor`.
- [`RoadSideFrontage`](../app/core/Town.h): per-bank `segments` (plot frontage) and `wallSegments` (free wall gaps) with `startT`/`endT` along the road edge.
- **Wall segments:** one span per bank at init `[setback, len−setback]`; carved only by `carveRoadWallForFootprint` on **main** building footprints (plot lots and gap-fill). Plot width does not carve the wall line — gaps between main fronts remain in `wallSegments`. Alley/gap-fill collectors read stored `wallSegments`.
- [`Plot`](../app/core/Town.h): `corners[4]`, `roadId`, `roadBank`, `area`; created at placement time on the instance.
- [`BuildingInstance`](../app/core/Town.h): `buildingType`, `plot`, `footprints`, `placementMode`.

### Zone types (`buildings.yml` → `BuildingDef.type`)

| `type` | Hop gate | Plot | Gap-fill |
|--------|----------|------|----------|
| `residential` | Town ring: `roadHop <= suburbanMaxHop` | **Required** | Only if `fill_in: true` **and** road in **core** (`roadHop <= urbanCoreMaxHop`) **and** core densify active |
| `urban` | Town ring | **Required** | Same as residential |
| `rural` | `roadHop > suburbanMaxHop` | **Required** | Never |

`fill_in` in YAML maps to `BuildingDef.fillIn`. It is a **permission** for core gap-fill, not a replacement for plots.

### Hop rings (`Town` + `GrowthRings.cpp`)

| Field | Meaning |
|-------|---------|
| `suburbanMaxHop` | Town ring edge. Roads with hop ∈ [0, suburbanMaxHop] are eligible for urban/residential. |
| `urbanCoreMaxHop` | Core edge (−1 until first bump). Subset [0, urbanCoreMaxHop] ⊂ town ring. |
| `ringPhase` | `Normal` or `DensifyCore` (after bump: prioritise core densify for `fill_in` types before suburban remainder). |

**Bump:** when a full town-ring road sweep fails for the current queue index, `suburbanMaxHop += 1`, `urbanCoreMaxHop = suburbanMaxHop - 1`, `ringPhase = DensifyCore`. Checked alley gap state is preserved across bumps (only new hop band exposes fresh gaps).

```text
  rural          hop > suburbanMaxHop
  ┌─────────────────────────────────────┐
  │  town ring     hop 0 .. suburbanMax │
  │  ┌───────────────────────────────┐  │
  │  │  core   hop 0 .. urbanCoreMax │  │
  │  └───────────────────────────────┘  │
  └─────────────────────────────────────┘
```

Helpers: `roadHop`, `collectSuburbanRoadIds`, `collectCoreRoadIds`, `collectRuralRoadIds`, `bumpGrowthRings`, `maxRoadHopInTown`.

### Placement entrypoints (intended)

**Single plot path** — all building types, all road kinds (primary, corridor, bridge bank, secondary/alley):

| Function | File | Role |
|----------|------|------|
| `PlacementFrontier` | `PlacementFrontier.cpp` | Distance-bucketed frontiers (`plot[3]`, `wall[3]`, `alley`); rebuilt on sync init / topology change; incremental refresh on carve |
| `peekClosestPlotSlot` / `peekClosestWallGapSlot` | `PlacementFrontier.cpp` | Pull closest-first segment in selected bands (merge ≤3 bucket heads) |
| `collectFrontageSlots` | `FrontagePlacement.cpp` | Legacy full scan; retained for per-road `roadFilter >= 0` fallback and debug |
| `tryPlaceRoadPlot` | `FrontagePlacement.cpp` | Frontier pull when `roadFilter < 0`; pick slot, depth cap, validate plot, `layoutBuildingsOnPlot` → `PlotLot` |
| `tryPlaceSegmentMain` | `FrontageGapFill.cpp` | **Core densify only** for `fill_in` types: wall-gap frontier pull when `roadFilter < 0` |

**Intended order per road (urban/residential):**

1. `tryPlaceRoadPlot` (always).
2. If `fill_in` && core band && `DensifyCore`: optional `tryPlaceSegmentMain` on same road (gap-fill). *Open design note:* gap-fill may remain footprint-first for tight gaps; houses should still prefer full plots — see product rule below.*

**Rural:** `tryPlaceRoadPlot` only with rural frontier band. Ordering is **closest-first by `centerDist`** (not rural target-distance zone score on the frontier path).

### Placement frontiers (`PlacementFrontier.cpp`)

Hot-path slot collection uses maintained frontiers instead of scanning all roads each attempt:

| Bucket | Source segments | Band (`centerDist`) |
|--------|-----------------|---------------------|
| `plot[0..2]` | `side.segments` with `width >= syncMinPlotFrontage` | Core / suburban / rural |
| `wall[0..2]` | `side.wallSegments` with `width >= syncMinGapWidth` | Same bands |
| `alley` | Developed-bank wall gaps in core, not in `checkedAlleyGaps` | Core distance only |

- **Rebuild:** `rebuildPlacementFrontier` once at town bootstrap and after full `rebuildSecondaryRoadsFromRecords` (debug/trim); `frontierExtendBands` on ring bump re-syncs **wall** geometry for roads whose distance band changed and **re-buckets plot frontier refs** (`frontierRefreshPlotBank` per bank — plot segment geometry unchanged, but refs must move e.g. Rural→Suburban when the ring expands).
- **Incremental:** `frontierRefreshPlotBank` / `WallBank` / `AlleyBank` from carve hooks in `Town.cpp`; `frontierRefreshRoad` after incremental alley apply.
- **Pull:** `peekClosestPlotSlot` / `peekClosestWallGapSlot` merge selected bucket heads by `centerDist`; failed attempts skip segment IDs for the rest of that placement try; successful carve refreshes the bank.
- **Rural behavior change:** frontier path orders rural plots by distance from town centre, not `scoreSegmentForZone` rural target key.
- **Ring bumps** remain until a follow-up replaces them with slice extension; empty frontier → existing bump loop in `BuildingPlacer.cpp`.

### Alleys

**Creation only** — [`SecondaryRoadPlacement.cpp`](../app/core/SecondaryRoadPlacement.cpp):

- Wall gap on host road → inward ray → hit road → `SecondaryRoadRecord` + `Road` with `isSecondary = true`.
- **Host bank must already have a building** (PlotLot or SegmentGapFill on that `roadId`+`bankIndex`); gaps on undeveloped banks are skipped (not marked checked).
- Ray from **wall gap** on the host road (`gapPoint`); spawn inset `max(frontage_setback, min_alley_side_road_dist)` is used only for the probe ray origin and clearance checks — the placed alley starts at `gapPoint` on the host road with a T-junction split.
- **Straight first:** inward-angled ray to dest road (may chain through existing alleys). On **any** straight failure for a `(gapT, angle)` pair, **fallback:** depth stub to `maxPlotDepthToRoadHit` along bank inward from `gapPoint` (frontage = gap width); if stub validates, scan a **180° turn fan** (≥19 directions from −90° to +90° relative to stub, seeded shuffle). Cache all ray hits; prefer a **fully validated** road-to-road turn, otherwise the **best partial** hit (crossing angle, primary dest, length, non-right-angle bonus). Dead-end stub if no turn hit (`dead_end=1` / `turn=1` / `alley_turn_partial` in layout log).
- Main building / blocking plot → reject; auxiliary footprint in corridor → demolished on apply.
- Ray may chain through existing alleys (split crossed road at hit, continue on far side). Crossing junctions snap to dest road centerline; splits restored on every `rebuildSecondaryRoadsFromRecords` via `splitRoadsAtAlleyEndpoints`.
- **Angle variation:** probe angles and gap positions are shuffled per gap (seeded by `townSeed`); a new alley is rejected if its direction is within `min_alley_bank_angle_sep_deg` of parallel or anti-parallel to any existing alley on the same host bank (`bank_parallel` in `alley_diag`).
- Reject if corridor hits building.
- `enqueuePendingAlleyFill` tracks alleys that need a building on them; **intended:** next `fill_in` placement uses the **same** `tryPlaceRoadPlot` on that alley `roadId`, not a parallel placer.

**After creation:** `buildSecondaryRoadFrontageSegments`, same carving/overlap/terrain rules. `collectFrontageSlots` does not exclude secondaries.

**Differences from primary roads (only these):**

| Aspect | Primary | Alley (`isSecondary`) |
|--------|---------|------------------------|
| Created at town load | Yes | No — added during growth |
| Pass-through when spawning new alley | N/A | Yes |
| Plot placement API | `tryPlaceRoadPlot` | **Same** |
| Visual / debug colour | Primary | Secondary |

**Frontage debug overlay** (dashed setback lines + inward arrows, rebuilt each `BuildingPlacer::sync`):

| Source | Colour | Meaning |
|--------|--------|---------|
| Live `side->segments` | Green | Plot frontage gaps (between plots) |
| Live `side->wallSegments` | Yellow | Wall gaps between main building fronts |
| `side->wallSegments` also in `frontiers.alley` | Orange | Wall gaps eligible for alley probes |

Segment id labels at line midpoints. Orange highlights alley frontier membership on top of live wall geometry.

Orchestration in `GrowthRings.cpp` (`tryPlaceInUrbanCore`, pending fills) should shrink over time toward “sweep roads in band, plot + optional core gap-fill” without alley-specific plot APIs.

### Growth orchestration (`BuildingPlacer::sync`)

1. `ensureTownFrontageInitialized` once at town build (segments + frontier bootstrap).
2. For each queue index until cursor reaches target:
   - **Rural** → `tryPlaceRuralOnRoads` (plot only).
   - **Urban/residential** → pending alley plot attempts (same plot API) → optional `tryPlaceInUrbanCore` when `DensifyCore` → town-ring road sweep (`tryPlaceSuburbanOnRoads`: **plot per road**, then core gap-fill only when gated) → bump loop on exhaustion.
3. Success → `buildingInstances.push_back`; failure → `placementFailedIndices`.

Interactive UI: max 1 queue index per frame; bump budget spread (`placementBumpCount`, max 3 bumps/sync).

### Config (`buildings.yml`)

```yaml
house:
  type: residential
  fill_in: true    # may gap-fill in core when densifying; always uses plots in normal placement

church:
  type: urban
  fill_in: false   # plots only, never gap-fill

workshop:
  type: urban
  # fill_in defaults true in DefCache — core gap-fill allowed; plots always

farm:
  type: rural
  # rural band only; plots only
```

### Product rule (plots vs gap-fill footprints)

- **All types:** default success is `BuildingPlacementMode::PlotLot` with a populated `Plot`.
- **Gap-fill** (`SegmentGapFill`): core-only densification when a full plot does not fit; footprint on road frontage, **no plot** (`plot = Plot{}`). Only attempted after plot fails on that road, only during `DensifyCore`, only for `fill_in` types on roads with hop ≤ `urbanCoreMaxHop`.

### Implementation map

| Rule | Location | Status |
|------|----------|--------|
| Plot before gap-fill | `tryPlaceOnTownRoad` | Done |
| Frontier plot / wall / alley pull | `PlacementFrontier.cpp`, `FrontagePlacement.cpp`, `FrontageGapFill.cpp`, `SecondaryRoadPlacement.cpp` | Done |
| Gap-fill only in core | `mayGapFillOnRoad` | Done |
| Gap-fill has no plot | `FrontageGapFill.cpp` → `SegmentGapFill` | By design |
| Alleys use same per-road path | `tryFillBlockingPendingAlleys`, `tryPlaceInUrbanCore` → `tryPlaceOnTownRoad` | Done |
| Densify core before suburban sweep | `BuildingPlacer::sync` bump loop | Done |
| Wall resync on zone change | `frontierExtendBands` → `restoreRoadWallFromInstances` when midpoint band changes | Done |

### Related files

| File | Role |
|------|------|
| [`PlacementFrontier.cpp`](../app/core/PlacementFrontier.cpp) | Distance-bucketed plot/wall/alley frontiers |
| [`BuildingPlacer.cpp`](../app/core/BuildingPlacer.cpp) | Growth sync, queue cursor |
| [`GrowthRings.cpp`](../app/core/GrowthRings.cpp) | Hop bands, road sweeps, bump |
| [`FrontagePlacement.cpp`](../app/core/FrontagePlacement.cpp) | Plot placement |
| [`FrontageGapFill.cpp`](../app/core/FrontageGapFill.cpp) | Gap-fill (core densify) |
| [`FrontageZones.cpp`](../app/core/FrontageZones.cpp) | Zone scoring, `roadHop` |
| [`SecondaryRoadPlacement.cpp`](../app/core/SecondaryRoadPlacement.cpp) | Alley creation |
| [`terrain-placement.md`](terrain-placement.md) | Terrain hooks + hop-ring **operational** notes (subordinate to this doc) |

### Debug

- Layout log: `ring_*`, `placement_skipped`, `gap_fill`, `gap_fill_diag`
- **Zone tint** — separate HUD **Zones** toggle (or **Z**): same-width roads with a **stripe** pattern by placement band — core red, suburban light blue, rural brown; primary = black+band stripes, alleys = green+band stripes; junction discs match band. Independent of terrain toggle (**T**).
- Hop band hop ranges shown below failure count in HUD (`core 0–N suburban … rural …`).
- Orange segments: failed alley **creation** probes for current queue index (not plot failures)

---

## Changelog

| Date | Change |
|------|--------|
| 2026-06 | Initial authoritative doc: plots for all types, core-only gap-fill, alleys as roads, misconception table |
| 2026-06 | Frontier bucket placement: closest-first plot/wall/alley pull; rural closest-first on frontier path |
