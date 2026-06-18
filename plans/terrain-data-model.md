# Terrain Data Model

Runtime structure after baking. See [`TerrainAtlas.h`](../app/core/TerrainAtlas.h) and [`TerrainBake.cpp`](../app/core/TerrainBake.cpp).

## TerrainAtlas

Top-level object loaded once and passed into town build and placement.

```cpp
struct TerrainAtlas {
    TerrainKind majorityLandKind;  // dominant non-water kind for this map (often plains)

    std::vector<std::vector<Vec2>> forbiddenPolygons;  // dilated sea+river
    std::vector<std::vector<Vec2>> riverOutlines;
    std::vector<std::vector<Vec2>> seaOutlines;
    std::vector<std::vector<Vec2>> forestOutlines;   // empty when forest is majority
    std::vector<std::vector<Vec2>> hillsOutlines;    // hills+mountain mask; empty when majority
    std::vector<std::vector<Vec2>> plainsOutlines;   // empty when plains is majority

    std::vector<TerrainKind> raster;       // full-res labelled image
    std::vector<uint8_t>     forbiddenDilated;
    // ... corridor road graphs, overlay texture, debug meshes
};
```

Land biome outlines use the same **interface polyline + simplify** pipeline as rivers/shore. Only **minority** land kinds (≠ `majorityLandKind`) are traced; the majority kind is the implicit background and has no outline.

## Query API

| Function | Status | Purpose |
|----------|--------|---------|
| `TerrainKind sample(Vec2 p)` | **Done** | Full-res raster lookup |
| `bool isBuildable(Vec2 p)` | **Done** | Not in dilated forbidden mask |
| `bool hasRegionOutline(TerrainKind kind)` | **Done** | False when kind is majority (empty outlines) |
| `float distToRegionEdge(Vec2 p, TerrainKind kind)` | **Done** | Min distance to smoothed outline segments; large sentinel if no outline |
| `float distToRiver(Vec2 p)` | Planned | Min distance to river centerline (not outline) |
| `float distToShore(Vec2 p)` | Planned | Min distance to `seaOutlines` |
| `float terrainAffinity(buildingType, Vec2 p)` | Planned | Lower = better match for building def |

Inside a biome: use `sample(p) == kind`. Outlines are for **edge distance**, not polygon inside/outside tests.

Distance queries walk simplified polylines via `distancePointToSegment`. Cap search radius at placement time when scoring (e.g. 20 units near forest).

## Extensions on existing types

### Road / frontage segment

Terrain preferences should be computed from frontage segment points or sampled areas around the candidate plot. The road-only model does not store cells.

```cpp
TerrainKind dominantTerrain = TerrainKind::Plains;
float plainsCoverage = 1.f;   // 0..1
```

Compute by sampling the terrain grid near the frontage segment midpoint or over the candidate plot footprint.

### BuildingDef (`DefCache` / `buildings.yml`)

Authoritative placement semantics: [`placement-model.md`](placement-model.md).

Per-type fields include `type` (`urban` | `residential` | `rural` | `any`) and optional `fill_in`:

| `type` | Hop band | Plots | `fill_in: true` |
|--------|----------|-------|-----------------|
| `residential` / `urban` | Town ring (`hop <= suburbanMaxHop`) | **Always** | Extra **core-only** gap-fill when densifying |
| `rural` | Outside town ring (`hop > suburbanMaxHop`) | **Always** | Ignored |
| `any` | All roads (no hop gate) | **Always** | Ignored |

Example:

```yaml
house:
  type: residential
  fill_in: true   # core gap-fill allowed; plots always

church:
  type: urban
  fill_in: false  # plots only
```

Extend definitions with terrain preferences:

```yaml
farm:
  type: rural
  terrain:
    prefer: [plains]
    min_coverage: 0.6        # nearby/footprint plains coverage threshold

lumber_camp:
  type: rural
  terrain:
    prefer: [forest]
    near_edge_dist: 20       # score bonus within N units of forest polygon

mine:
  type: rural
  terrain:
    prefer: [hills, mountain]
    near_edge_dist: 8

watermill:
  terrain:
    anchor: river            # Feature-anchor placement mode
    max_dist_from_road: 25

fisherman:
  terrain:
    anchor: shore
    max_dist_inland: 15
```

Split the generic `resource` building into **mine** and **lumber_camp** (or sub-types) when implementing.

### Bridge / road metadata (future)

```cpp
struct Bridge {
    Vec2 pos;
    int  roadId;
    float t;  // param along road if needed
};
```

Mark Voronoi or secondary roads that cross a river polygon at bake or town-build time.

## Caching

Optional serialized cache (e.g. `app/cache/terrain_atlas.bin`) keyed by terrain image hash + `terrain.txt` hash + diagram dimensions. Rebuild when any input changes.
