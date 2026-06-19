# Architecture Overview

## Purpose

Describe how a town is built once at load, then grows building-by-building along a shared road graph with terrain constraints.

## What it does

1. **Load** — Generate Voronoi roads inside a circular diagram, merge terrain corridor roads, sanitize at water, create bridges, assign road banks, bake terrain, initialize frontage.
2. **Grow** — Seeded queue of building types; each sync places at most one building using hop bands, frontiers, and terrain rules.
3. **Render** — SFML meshes for roads, junctions, buildings; HUD slider and debug overlays.

There is no stored Voronoi cell mesh. After initial build, all geometry is roads, junctions, frontage segments, and placement-time plots.

## How it works

```text
Terrain PNG + terrain.txt
        │
        ▼  bake once
   TerrainAtlas
        │
TownBuilder::build
  jc_voronoi → roads
  → appendCorridorRoads
  → splitRoadsAtIntersections
  → sanitizeRoadGraphAtWater
  → mergeWatersideJunctions
  → resolveBridges
  → buildBridgeBuckets
  → cullVoronoiRoadsParallelToCorridors
  → indexJunctions
  → assignRoadSideInwards
  → ensureTownFrontageInitialized + frontier FullRebuild
        │
BuildingPlacer::sync (each frame / auto-grow step)
  → one queue index: rural | any | urban/residential path
  → carve frontage on success
  → notifyPlacementFrontier
  → rebuild meshes
        │
App render loop
```

| Stage | Primary files |
|-------|---------------|
| Config load | `Config.cpp`, `app/config/*.yml` |
| Town build | `TownBuilder.cpp`, `RoadNetwork.cpp`, `TerrainBake.cpp` |
| Placement | `BuildingPlacer.cpp`, `GrowthRings.cpp`, `FrontagePlacement.cpp` |
| Frontiers | `FrontierManager.cpp`, `PlacementFrontier.cpp` |
| Render | `App.cpp`, `Hud.cpp` |

## Interactions

- Data structures: [data-model.md](data-model.md)
- Scale and Voronoi rule: [constraints.md](constraints.md)
- Road graph details: [../town-generation/road-graph.md](../town-generation/road-graph.md)
- Water and bridges: [../town-generation/water-and-bridges.md](../town-generation/water-and-bridges.md)
- Placement rules: [../placement/rules.md](../placement/rules.md)
- Growth controls: [../growth/queue-and-controls.md](../growth/queue-and-controls.md)
