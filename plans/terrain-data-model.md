# Terrain Data Model

Proposed runtime structure after baking. Names are suggestions; adjust to match code when implemented.

## TerrainAtlas

Top-level object loaded once and passed into town build and placement.

```cpp
enum class TerrainKind {
    Unknown,
    Sea,
    River,
    Plains,
    Forest,
    Hills,
    Mountain,
    Forbidden,  // sea | river (with optional buffer)
};

struct TerrainRegion {
    TerrainKind kind;
    std::vector<Vec2> polygon;  // simplified, closed
};

struct RiverFeature {
    std::vector<Vec2> centerline;  // open polyline, simplified
    float avgWidth = 0.f;          // estimated at bake time (world units)
};

struct TerrainAtlas {
    std::vector<TerrainRegion> forbidden;   // water: sea + river (+ buffer)
    std::vector<TerrainRegion> forests;
    std::vector<TerrainRegion> hills;
    std::vector<TerrainRegion> mountains;
    std::vector<RiverFeature>  rivers;
    std::vector<Vec2>          shoreline;   // polyline(s): sea ↔ land boundary

    // Optional fast sampling (e.g. 128×128 over diagram extent)
    int   gridW = 0;
    int   gridH = 0;
    Vec2  gridOrigin{};
    float gridCellSize = 0.f;
    std::vector<TerrainKind> grid;  // row-major, dominant kind per cell
};
```

## Query API (proposed)

These should be cheap at placement time:

| Function | Purpose |
|----------|---------|
| `TerrainKind sample(Vec2 p)` | Coarse grid lookup or cascade point-in-polygon |
| `bool isBuildable(Vec2 p, float margin)` | Not inside forbidden; optional margin from river centerline |
| `float distToRiver(Vec2 p)` | Min distance to any river centerline segment |
| `float distToShore(Vec2 p)` | Min distance to shoreline polyline |
| `float distToRegionEdge(Vec2 p, TerrainKind kind)` | Distance to nearest edge of matching region polygons |
| `float terrainAffinity(buildingType, Vec2 p)` | Lower = better match for building def |

Distance queries can use segment distance against simplified polylines/polygons. For “near forest”, cap search radius (e.g. 20 units) so work stays bounded.

## Extensions on existing types

### Cell (`Town.h`)

Tag each Voronoi cell once when the town is built:

```cpp
TerrainKind dominantTerrain = TerrainKind::Plains;
float plainsCoverage = 1.f;   // 0..1
bool buildable = true;        // false if mostly water / forbidden
```

Compute by sampling the terrain grid over the cell polygon (or integrating over cell area at bake/town-build time). **Not** per placement attempt.

### BuildingDef (`DefCache` / `buildings.yml`)

Extend definitions with terrain preferences:

```yaml
farm:
  type: rural
  terrain:
    prefer: [plains]
    min_coverage: 0.6        # cell plains coverage threshold

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
