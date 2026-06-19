# Water and Bridges

## Purpose

Keep the road graph on buildable land, detect shore junctions from terrain raster, pair crossings, and reveal bridges as the town grows.

## What it does

```
Voronoi + corridor roads
  → splitRoadsAtIntersections
  → sanitizeRoadGraphAtWater (all non-bridge primaries)
  → mergeWatersideJunctions
  → resolveBridges
  → cullVoronoiRoadsParallelToCorridors
  → buildBridgeBuckets
  → assignRoadSideInwards (bridges skip frontage)
```

- **Sanitize** — Split at land/water boundary, clip/cull submerged spans, snap junctions landward.
- **Waterside set** — Raster probe: junction or disc within `bridge_waterside_max_dist` hits forbidden terrain (`sea`, `river`).
- **Merge** — Cluster nearby waterside junctions within `shore_junction_merge_dist` (default 2 u).
- **Pair** — Same water body, span limits, same-bank hop check (≤8 non-bridge hops), deferral for closer partners.
- **Snap** — Slide chord along inset tangent; require mostly-water interior on snapped segment.
- **Buckets** — BFS `bridge_bucket_hops` from each bridge end; hidden until a building lands on a bucket road; seed bridge nearest `Town.center` always visible.

Bridges render brown; skip water clip and frontage assignment.

## How it works

Implementation: [`RoadNetwork.cpp`](../../app/core/roads/RoadNetwork.cpp), [`TownBuilder.cpp`](../../app/core/generation/TownBuilder.cpp), bucket reveal in [`Town.cpp`](../../app/core/town/Town.cpp).

**Waterside detection** — `collectWatersideJunctionIds` samples `TerrainAtlas::sample` at junction and on a disc (step 0.25 u). No outline geometry for eligibility.

**Pairing rules**

| Rule | Detail |
|------|--------|
| Same water body | Same forbidden kind, or opposite-bank exception at mouths |
| Span | 0.5 u ≤ length ≤ `bridge_max_span` |
| Same bank | Reject if ≤8 road hops between junctions on land |
| Deferral | Closer valid partner or same-bank fork nearer to opposite shore |

**Growth reveal** — `buildBridgeBuckets` after resolve; `rebuildRoadMesh` omits unrevealed bridges in normal view. Debug **B** shows all.

Config (`config.yml` → `terrain`):

```yaml
bridges_enabled: true
bridge_max_span: 80.0
bridge_waterside_max_dist: 4.0
shore_junction_merge_dist: 2.0
bridge_snap_enabled: true
bridge_snap_search_radius: 8.0
bridge_bucket_hops: 2
```

Logs: `bridge.log` (`waterside`, `pair_ok`, `placed`, `bridge_buckets`, `bridge_revealed`); `voronoi.log` for sanitize summary.

## Interactions

- Terrain bake (forbidden raster): [terrain/bake-and-atlas.md](terrain/bake-and-atlas.md)
- Road graph: [road-graph.md](road-graph.md)
- Debug view: [../app/rendering-and-debug.md](../app/rendering-and-debug.md)
- Bridges block plot depth rays: [../placement/frontage-and-plots.md](../placement/frontage-and-plots.md)
