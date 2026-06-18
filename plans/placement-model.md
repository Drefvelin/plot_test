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
| **Rural** | Farms and rural terrain buildings (`farm`, `lumber_camp`, `mine`). **Outside** suburban — not in the town ring. |
| **Any** | Border terrain buildings (`fisher_hut`, `watermill`). **Any** road frontage that fits — core, suburban, or rural. Still terrain-first (sea/river preference). |

Rural is strictly **beyond** the suburban edge. Urban/residential never use the rural band. **`any`** ignores hop band limits but keeps terrain rules.

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

**Farms / rural specials** — rural band, plot only. Types: `farm`, `lumber_camp`, `mine`.

**Border terrain (`type: any`)** — `fisher_hut`, `watermill`. Plot on **any** eligible frontage (town ring or fringe); terrain-first border/scan still applies.

### Terrain placement

Rural (and any building with a `terrain:` block in [`buildings.yml`](../app/config/buildings.yml)) can require a **preferred biome** and a **placement mode**:

| Mode | Meaning | Hard minimum | Scoring |
|------|---------|--------------|---------|
| **inside** | Plot should sit on the preferred kind | ≥1 plot corner on `prefer` and buildable | Higher score when more corners match |
| **proximity** | Near the biome outline edge | Buildable only; segment beyond `proximity_max_dist` skipped in terrain scan | Closer to outline = better |
| **border** | Plot on road frontage facing preferred outline; main on plot back (band) or hugging outline | Plot buildability + border band/hug rules; building: no road/building overlap only; water spill OK | Closest border bucket segment by `centerDist` |

**Requirement** (`terrain.requirement`):

| Value | On terrain-first failure |
|-------|--------------------------|
| **loose** (default) | Fall back to vanilla frontier road plot on the same band scope (no terrain *preference* rules; standard buildability still applies) |
| **strict** | Fail placement (`watermill` only today) |

**Majority rule:** if `terrain.prefer` equals the map's `majorityLandKind`, **proximity is treated as inside** (no outline to measure against).

**Terrain-first order** ([`GrowthRings.cpp`](../app/core/GrowthRings.cpp) `tryPlaceRuralOnRoads` / `tryPlaceAnyOnRoads`):

1. **Border types** — [`tryPlaceBorderPlot`](../app/core/BorderPlacement.cpp): peek [`BorderFrontier`](../app/core/BorderFrontier.cpp) buckets (plot segments whose inward ray reaches the building's `prefer` outline before crossing another road); **plot required** on frontage (`buildBorderHugPlot` / band plot), then main building (band = back of plot; hug = extend beyond plot to outline). Up to `border_max_attempts` (default 32) segment retries; failed attempts log `border_attempt_fail`. Loose hug retries **band** style after exhaustion.
2. **All terrain types** — `tryPlaceRoadPlot` with **terrain scan** (`collectFrontageSlots` sorted by zone + terrain score; not the centerDist frontier).
3. **Loose only** — after terrain scan, **`terrain_anchor`** (BFS from last real terrain placement on that `prefer` kind; cap = `plots.terrain_anchor_max_roads` in [`config.yml`](../app/config/config.yml), default **4**) then vanilla frontier on the type's band scope (`frontier_loose_fallback` in layout log). Rural = rural band only; `any` = all bands. Anchor roads are updated only by successful terrain scan or border placement — not by anchor/loose fallback.
4. **Strict** — stop after step 2 fails (`terrain_strict_fail` in layout log).

Urban/residential buildings without `terrain:` still use the plot frontier only.

`farm` is not special — `terrain: { prefer: plains, placement: inside, requirement: loose }` like any other inside type.

Config keys: `terrain.prefer` (scalar **or YAML list** — OR semantics at peek; alias `water` → sea+river), `terrain.placement`, `terrain.requirement`, `border_style` (`band`|`hug`), `border_overhang_dist`, `proximity_max_dist`, `border_min_dist`, `border_max_dist`. Town [`town.yml`](../app/config/town.yml): `border_max_attempts`. Border buckets rebuilt at `FullRebuild` and refreshed per bank on carve. Logic in [`BorderFrontier.cpp`](../app/core/BorderFrontier.cpp), [`BorderPlacement.cpp`](../app/core/BorderPlacement.cpp), [`TerrainPlacement.cpp`](../app/core/TerrainPlacement.cpp). Terrain scan / anchor still use `preferKinds.front()` when a list is given.

### Roads are roads

The Voronoi roads at town creation, terrain corridors, bridges, and **alleys** are all entries in the same `town.roads` list.

- **Alleys** are roads that are added later (not at initial load), carved from gaps between buildings on primary roads.
- When placing a building, the game does **not** use a separate “alley plot method” vs “main road plot method.” It picks frontage on **whichever road** fits the zone band and scoring — alley or primary, same rules.
- The only special things about alleys are: **how they are created** (ray from a wall gap, can pass through an existing alley), and the `isSecondary` flag for rendering and alley-creation logic. After an alley exists, frontage segments, plots, gap-fill, and carving work like any other road.

### How growth roughly works

1. Shuffle a queue of building types (houses, workshops, etc.).
2. For each queue entry, try to place one building.
3. **Urban/residential:** pull closest eligible plot frontage from distance buckets (core / suburban), then gap-fill from wall-gap frontier when densifying core.
4. **Rural:** terrain-tagged types use terrain-first scan on the rural band; others pull closest eligible plot from rural frontier bucket.
5. **Any:** `fisher_hut` / `watermill` — terrain-first on **all** roads (no hop band filter); no ring bump loop.
6. If the town ring is full for this building, **expand** the ring (bump) and try again.
7. In **core densify** phase (after a bump), types with `fill_in` may also create **new alleys** from wall gaps and use **gap-fill** on core roads — still on real road frontage, still band-aware.

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
- [`Plot`](../app/core/Town.h): `corners[4]`, `roadId`, `roadBank`, `area`, optional `outlineTangent` / `outlineInward` (hug plots); created at placement time on the instance.
- [`BuildingInstance`](../app/core/Town.h): `id` (uint32 queue index), `typeId` (uint16 DefCache building type id), `plot`, `footprints`, `placementMode`.
- **Footprint orthogonality:** every `BuildingFootprint` must be an orthogonal rectangle (four 90° corners; axis-aligned or rotated, never a parallelogram warped to plot sides). Building orientation is **not** required to match plot front/back or outline tangent. Enforced via `footprintHasRightAngles` in [`PlotGeometry.cpp`](../app/core/PlotGeometry.cpp) (`footprintPlacementValid`, gap-fill, border placement).
- **Border building layout:** border types peek per-terrain **border frontier** buckets (road plot segments classified by inward ray → nearest outline, rejected if another road is hit closer than the outline). **Plot required** on frontage; then main at plot back (band, shrink steps) or hugging outline (may extend beyond plot). Validation: **no road overlap** and **no building overlap** only — water spill OK. Failed attempts log `border_attempt_fail` with `main=` / `slot=` reject counts. Loose hug retries band style.

### Zone types (`buildings.yml` → `BuildingDef.type`)

| `type` | Hop gate | Plot | Gap-fill |
|--------|----------|------|----------|
| `residential` | Town ring: `roadHop <= suburbanMaxHop` | **Required** | Only if `fill_in: true` **and** road in **core** (`roadHop <= urbanCoreMaxHop`) **and** core densify active |
| `urban` | Town ring | **Required** | Same as residential |
| `rural` | `roadHop > suburbanMaxHop` | **Required** | Never |
| `any` | No hop gate (all roads) | **Required** | Never |

`fill_in` in YAML maps to `BuildingDef.fillIn`. It is a **permission** for core gap-fill, not a replacement for plots.

`fisher_hut` and `watermill` use `type: any` — terrain-first border placement along preferred outline kinds.

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
| `PlacementFrontier` | `FrontierManager` + `PlacementFrontier.cpp` | Unified storage on `Town.frontierManager` (`plot[3]`, `wall[3]`, `alley`, terrain-scan, `border[]`); peek/consume APIs for plot/wall/alley/scan/border |
| `peekClosestPlotSlot` / `peekClosestWallGapSlot` | `PlacementFrontier.cpp` | Pull closest-first segment in selected bands (merge ≤3 bucket heads) |
| `collectFrontageSlots` | `FrontagePlacement.cpp` | Full scan; terrain-first rural path + per-road `roadFilter >= 0` fallback |
| `tryPlaceRoadPlot` | `FrontagePlacement.cpp` | `RoadPlotSearchMode`: Frontier (urban/rural default), TerrainScan (terrain-first rural), FrontierLooseFallback (loose rural retry) |
| `tryPlaceSegmentMain` | `FrontageGapFill.cpp` | **Core densify only** for `fill_in` types: wall-gap frontier pull when `roadFilter < 0` |

**Intended order per road (urban/residential):**

1. `tryPlaceRoadPlot` (always).
2. If `fill_in` && core band && `DensifyCore`: optional `tryPlaceSegmentMain` on same road (gap-fill). *Open design note:* gap-fill may remain footprint-first for tight gaps; houses should still prefer full plots — see product rule below.*

**Rural:** `tryPlaceRuralOnRoads` — terrain-tagged types use terrain-first on the rural band (border placer → terrain scan → **terrain anchor** → loose frontier fallback or strict fail). Non-terrain rural uses rural frontier band only (closest-first by **road midpoint** distance). `Town.lastTerrainAnchorRoadId[prefer]` stores the road id of the last successful terrain scan/border placement per `TerrainKind`.

**Any:** `tryPlaceAnyOnRoads` — same terrain-first pipeline as rural but `BandFilter::none()` (all roads). Used by `fisher_hut` and `watermill`. No ring bump loop.

### Placement frontiers (`FrontierManager` + `PlacementFrontier.cpp`)

All frontier buckets live on **`Town.frontierManager`**. Hot-path slot collection uses maintained frontiers instead of scanning all roads each attempt:

| Bucket | Source segments | Band (road midpoint) |
|--------|-----------------|----------------------|
| `plot[0..2]` | `side.segments` with `width >= syncMinPlotFrontage` | Core / suburban / rural via `roadFrontierBand(town, roadId)` |
| `wall[0..2]` | `side.wallSegments` with `width >= syncMinGapWidth` | Same bands |
| `alley` | Developed-bank wall gaps in core, not in `checkedAlleyGaps` | Core distance only (road midpoint) |
| `border[]` | Plot frontage segments whose inward ray hits a terrain outline (`BorderSlotRef`) | `peekNextBorderSlot` by road midpoint; `consumeBorderSlot` on place |
| `scanPlains` / `scanForest` / `scanHills` | Road plot segments at bootstrap | Terrain proximity/inside |

- **Sync:** `notifyPlacementFrontier` only — no direct `frontierRefresh*` from carve/placement code. Events: `FullRebuild`, `PlotCarved`, `RingExtended`, `TopologyChanged`, `InstanceRemoved`.
- **Rebuild:** `FullRebuild` rebuilds plot/wall/alley + terrain-scan + border (town bootstrap after `ensureTownFrontageInitialized`). `TopologyChanged` is **bulk secondary replay only** (`rebuildSecondaryRoadsFromRecords`) — same rebuilds, not per-alley.
- **Incremental alley:** `applySecondaryRoadRecord` calls `notifyRoadFrontierRefresh` on host, new alley, and roads sharing endpoints at `a`/`b` — not `TopologyChanged`.
- **Border placement:** [`BorderPlacement.cpp`](../app/core/BorderPlacement.cpp) + [`BorderFrontier.cpp`](../app/core/BorderFrontier.cpp). Plot-first on peeked segment; band/hug building layout; retry loop (`border_max_attempts`). **`placement: border`** types never use `frontier_loose_fallback`; loose retries **band** after hug fails.
- **Incremental:** `PlotCarved` refreshes plot/wall/alley bank + terrain-scan + border bank. `InstanceRemoved` refreshes bank when border building removed. Restore replay suppresses notify.
- **Ring bump:** `RingExtended` runs `frontierExtendBands` (wall resync on band change, re-bucket all three road frontier types per affected bank).
- **Pull:** `peekClosestPlotSlot` / `peekClosestWallGapSlot` / `peekNextTerrainScanSlot` — unchanged behaviour.

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
   - **Rural** → `tryPlaceRuralOnRoads` (plot only, rural band).
   - **Any** → `tryPlaceAnyOnRoads` (plot only, all bands; terrain-first).
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
| Depth-cache key quantization | `kDepthCacheTStep` / `kDepthCacheDimStep` in [`PlotGeometry.cpp`](../app/core/PlotGeometry.cpp) — fewer distinct per-bank depth memo entries | Done |
| Building type IDs | [`BuildingTypes.h`](../app/core/BuildingTypes.h), `DefCache::typeIdFor`, `BuildingInstance.typeId` | Done |
| Sim memory report | [`MemoryReport.cpp`](../app/core/MemoryReport.cpp) — markdown breakdown on exit; alley probe debug excluded from sim totals | Done |
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
| [`MemoryReport.cpp`](../app/core/MemoryReport.cpp) | Sim memory breakdown on exit |
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
