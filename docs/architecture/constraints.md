# Architecture Constraints

## Purpose

Fixed rules that every subsystem must respect: coordinate scale and Voronoi library isolation.

## What it does

**World units** — All simulation geometry uses `Vec2` / `float` in world units. Rendering multiplies by `pixels_per_unit` (default **10** → 10 screen pixels = 1 unit). A 1024×1024 unit diagram renders at 10240×10240 pixels.

Helpers: [`app/core/Units.h`](../../app/core/Units.h). Config: `world.pixels_per_unit` in [`config.yml`](../../app/config/config.yml).

**Voronoi library boundary** — [`jc_voronoi`](../../app/core/third_party/jc_voronoi.h) (`jcv_*`) is used in **one file only**: [`TownBuilder.cpp`](../../app/core/TownBuilder.cpp) during initial town build.

Allowed: generate diagram, iterate edges/sites, copy coordinates into `Road` (`a`, `b`), free diagram.

Forbidden after build:

- No `#include` of jc_voronoi outside `TownBuilder.cpp`
- No `jcv_*` in junctions, frontage, placement, carving, or mesh code
- No library math helpers — use `Vec2`, `Town.cpp`, `PlotGeometry.cpp`, etc. on stored `Town` data only

Infrastructure libraries (SFML, yaml-cpp) are unrestricted.

**Terrain query constraint** — Placement must not scan the full PNG per candidate. Use baked `TerrainAtlas` (`isBuildable`, raster `sample`, outline distance). See [terrain overview](../town-generation/terrain/overview.md).

## How it works

Town build copies Voronoi edges into `std::vector<Road>` and never retains the library diagram. All later road splits, alleys, and bridge creation mutate `Town.roads` directly.

Terrain image coordinates align with diagram bounds (`diagram.width/height`).

## Interactions

- Scale in agent cheat sheet: [`AGENTS.md`](../../AGENTS.md)
- Road graph generation: [../town-generation/road-graph.md](../town-generation/road-graph.md)
- Terrain bake: [../town-generation/terrain/bake-and-atlas.md](../town-generation/terrain/bake-and-atlas.md)
