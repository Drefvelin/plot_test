# Terrain & Placement Integration

How baked terrain connects to the existing town growth and placement pipeline.

## Current placement flow (no terrain)

1. `TownBuilder::build` — Voronoi → `Town` (cells, roads, junctions)
2. `BuildingPlacer::sync` — growth queue drives building count
3. Slot collection — `collectFrontageSlots` / `collectAllGapFillSlots` over all roads
4. Scoring — `scoreSegmentForZone` in `FrontageZones.cpp` (urban / residential / rural)
5. Hard rejects — zone score `1e9f`; geometry checks in `PlotGeometry.cpp`
6. Modes — `PlotLot`, `SegmentGapFill`; alley logic in `SecondaryRoadPlacement.cpp`

Terrain adds **hard rejects**, **score terms**, **cell tags**, and eventually **new placement modes**.

## Hard constraints (must not place)

Apply when validating plot corners and building footprints (same layer as overlap / protected plot checks in `PlotGeometry.cpp`):

```cpp
bool isBuildable(const TerrainAtlas& terrain, Vec2 p, float riverMargin) {
    if (pointInAnyPolygon(p, terrain.forbidden)) return false;
    if (distToRiver(p, terrain.rivers) < riverMargin) return false;
    return true;
}
```

Reject entire candidate if **any** footprint corner fails.

Also at town build: mark cells with high water coverage as `cell.buildable = false` so slot collection can skip them early.

## Soft constraints (prefer / score)

Extend or complement `scoreSegmentForZone` in `FrontageZones.cpp`:

```cpp
float terrainScore = terrainAffinity(buildingType, midpoint, cell, terrain);
if (terrainScore >= 1e8f) return 1e9f;  // hard reject via affinity
return existingZoneScore + terrainScore * weight;
```

Examples:

| Building | Rule |
|----------|------|
| `farm` | Require cell `plainsCoverage >= threshold`; prefer higher coverage |
| `mine` | Minimize `distToRegionEdge(p, Hills)` under cap |
| `lumber_camp` | Minimize distance to forest edge; allow up to ~20 units |
| `house` / `workshop` | Prefer buildable plains; no special anchor |

Keep hard water/river rejection separate from soft biome preference.

## Feature-anchor placement (new mode)

Some buildings should not use road frontage as primary logic:

| Building | Anchor | Selection |
|----------|--------|-----------|
| Watermill | River centerline point | On river; near road/junction within max distance; segment not already used |
| Fisherman / dock | Shoreline point | On shore; road/frontage within inland distance |
| Mine | Hills polygon edge | Near edge; road access |

Proposed enum extension on `BuildingInstance`:

```cpp
enum class BuildingPlacementMode {
    PlotLot,
    SegmentGapFill,
    FeatureAnchor,  // river | shore | hills edge
};
```

`BuildingPlacer::sync` routes queue entries with `terrain.anchor` set to a dedicated placer before or instead of generic frontage search.

## Roads, bridges, and following terrain

### Bridges

When a `Road` segment intersects a forbidden river polygon:

- Record a `Bridge` at intersection
- Allow road to cross (render bridge; no building on crossing)
- Detect at town build by segment–polygon intersection test

### Roads following rivers / shore

Do not replace Voronoi graph initially. **Bias** candidate roads:

- In `SecondaryRoadPlacement` / alley probing: score candidates parallel to nearby river centerline or shoreline within a corridor (~5–10 units)
- Optional later: snap one coastal primary edge to simplified shoreline polyline

### Voronoi site bias (optional)

When placing Voronoi sites in `TownBuilder`, bias generators toward plains grid cells and away from forbidden cells so fewer cells are mostly water.

## Alley / gap-fill interaction

General center-out gap fill (`tryPlaceSegmentMain(..., roadFilter=-1, useCellCentroid=true)`) already walks **all** roads including alleys and does not check alley cell `Finished` state.

Terrain hard rejects apply equally to alley segment gap fill. Soft terrain scoring can apply to rural/farm/resource types in the gap-fill band.

See implemented alley behaviour in `BuildingPlacer.cpp` and `SecondaryRoadPlacement.cpp`.

## Config surface

| File | Keys |
|------|------|
| `app/config/terrain.txt` | Colour → kind (exists) |
| `app/config/config.yml` | `terrain.image_path`, bake grid size (proposed) |
| `app/config/buildings.yml` | Per-type `terrain.prefer`, `anchor`, thresholds (proposed) |
| `app/config/town.yml` | River margin, near-forest distance defaults (proposed) |

## Logging

Add a `terrain` log channel (or use `layout`) for:

- Bake summary (counts, timing)
- Rejected placements (`terrain_forbidden`, `terrain_biome`)
- Anchor placements (`watermill`, `dock`, `mine`)

Match existing patterns in `Logger` / `PlacementLogging.cpp`.
