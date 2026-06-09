# Plans

Design and implementation notes for plot_test. These documents describe **intended architecture** and **current direction**. They are not a substitute for reading the code, but they should stay aligned with it.

## Documents

| Document | Contents |
|----------|----------|
| [terrain-system.md](terrain-system.md) | Goals, constraints, and high-level architecture |
| [terrain-data-model.md](terrain-data-model.md) | Baked geometry layers, queries, and config |
| [terrain-bake-pipeline.md](terrain-bake-pipeline.md) | One-time image → geometry conversion steps |
| [terrain-placement.md](terrain-placement.md) | How terrain hooks into building and road placement |
| [terrain-roadmap.md](terrain-roadmap.md) | Phased implementation order and status |
| **[terrain-step1-implementation.md](terrain-step1-implementation.md)** | **Step 1 task list: config, bake, overlay, debug, visual road clip** |

## Related project docs

- [AGENTS.md](../AGENTS.md) — coordinate scale, Voronoi boundary rule, core data model
- [app/config/terrain.txt](../app/config/terrain.txt) — terrain colour definitions (not yet loaded by code)

## Status

| Area | Status |
|------|--------|
| Terrain Step 1 (overlay, bake, debug, visual road clip) | **Done** — see `TerrainAtlas`, `TerrainBake.cpp`, `App.cpp`, `Hud.cpp` |
| Terrain Step 1.5 (road split, placement validation) | Not started |
| Terrain-aware placement (biomes) | Not started |
| Alley gap-fill (center-out) | **Implemented** — see `BuildingPlacer.cpp`, `SecondaryRoadPlacement.cpp` |

When implementation begins, update the status tables in this file and in [terrain-roadmap.md](terrain-roadmap.md).
