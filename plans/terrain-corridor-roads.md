# Terrain Corridor Roads

Shore and river **corridor roads** are baked from terrain masks (separate inset/spacing from debug overlays), merged into the town road graph, split at interior intersections, and used to cull parallel Voronoi roads. They are ordinary roads in the road-only model.

## Pipeline

```
Voronoi roads + disc border
  → appendCorridorRoads (shore + river graphs)
  → splitRoadsAtIntersections
  → sanitizeRoadGraphAtWater (corridors + Voronoi; once at load)
  → resolveBridges (optional)
  → cullVoronoiRoadsParallelToCorridors
  → assignRoadSideInwards + meshes + placement
```

Implementation: `TownBuilder.cpp`, `RoadNetwork.cpp`, `TerrainBake.cpp`.

## Config (`config.yml` — no code defaults)

```yaml
terrain:
  corridor_roads_enabled: true
  shore_road_inset: 50.0      # world units from raw sea mask
  river_road_inset: 50.0      # world units from raw river mask
  corridor_edge_spacing: 40.0 # max straight edge along contour walk
  corridor_parallel_probe_offset: 4.0  # perpendicular probe distance each side
  corridor_parallel_cos: 0.98          # min |dot| for "parallel"
```

Debug keys (`shore_inset`, `water_inset`, `simplify_tolerance`, `contour_width`) remain overlay-only.

## Parallel Voronoi cull

After corridor roads are split and junctions indexed, `cullVoronoiRoadsParallelToCorridors` removes interior Voronoi roads that run parallel to corridor chains without crossing at chain endpoints.

1. **Build corridor chains** — Walk corridor-only edges junction-to-junction between degree-≠2 corridor nodes; deduplicate undirected chains.
2. **Probe along chain** — Sample every ~5 u (or `corridor_parallel_probe_offset`, whichever is smaller). At each sample, cast short segments perpendicular at **±offset** from the corridor centerline.
3. **Mark for deletion** — Any non-corridor, non-secondary road hit by a probe that is parallel to the local corridor tangent (`|dot| ≥ corridor_parallel_cos`) and **not incident** to either chain endpoint junction is marked.
4. **Delete whole roads** — Remove all marked roads, re-index `road.id`, call `indexJunctions`.

Radial Voronoi roads that cross corridors at chain endpoint junctions are preserved (incident to endpoint junction).

## Atlas bake

On `bakeTerrain`:

- `shoreRoadGraph` — `buildWaterContourGraph(sea, shore_road_inset, corridor_edge_spacing)`
- `riverRoadGraph` — `buildWaterContourGraph(river, river_road_inset, corridor_edge_spacing)`

Same contour algorithm as debug (interface polylines → resample → water-safe subdivide). Graphs are simplified to drop edges shorter than 0.5u before use.

Logged on channel `terrain`: `shore_road_nodes`, `river_road_nodes`, insets, spacing.

## Road network

| API | Role |
|-----|------|
| `appendCorridorRoads` | Emits all corridor polyline segments with `isTerrainCorridor = true` |
| `splitRoadsAtIntersections` | Interior crosses via `segmentCrossingParams` (PlotGeometry); min segment 0.5u; re-index `road.id`; frontage reset on splits |
| `sanitizeRoadGraphAtWater` | Load-time boundary split + clip trim for all non-bridge primaries (Voronoi + corridor) |
| `cullVoronoiRoadsParallelToCorridors` | Removes parallel non-incident Voronoi roads along corridor chains |

Corridor roads count as **primary** (`primaryRoadCount`).

## Logging

Channel `voronoi`:

- `corridor_roads emitted=…`
- `roads_before_corridors`, `roads_after_corridors`, `roads_after_split`
- `culled_parallel_voronoi=…`

## Acceptance

- Corridor and Voronoi roads trimmed to buildable land at load; no graph roads or junctions in water (bridges excepted)
- Full corridor roads visible on map (sea + river); junctions (red) at corridor × Voronoi crosses after split
- No thin sliver road strips between corridor and interior Voronoi road on river/coast test areas
- Radial Voronoi roads still cross corridors at junctions; parallel "back roads" removed
- Buildings place on road-bank frontage without water overlap (placement validation uses terrain and road-hit checks)

## Out of scope

- Partial clip of parallel Voronoi span (whole-road delete only)
- Snapping corridor endpoints to existing junctions before split
- Serialized atlas cache
- Runtime water sanitize during placement or alley rebuild

See also: [terrain-roadmap.md](terrain-roadmap.md) Phase 4.
