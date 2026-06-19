# Terrain Queries

## Purpose

Answer “can we build here?” and “how close to biome/water edge?” from baked data without rescanning art.

## What it does

| API | Status | Purpose |
|-----|--------|---------|
| `sample(Vec2 p)` | Done | Full-res raster kind at point |
| `isBuildable(Vec2 p)` | Done | Not in dilated forbidden mask |
| `hasRegionOutline(kind)` | Done | False when kind is map majority |
| `distToRegionEdge(p, kind)` | Done | Min distance to smoothed outline; large sentinel if no outline |
| `distToRiver` / `distToShore` | Planned | Dedicated centerline/shore distance |
| `terrainAffinity(type, p)` | Planned | Soft scoring helper |

Inside a biome: `sample(p) == kind`. Outlines measure **edge distance**, not polygon membership.

Placement validation uses `polygonBuildable()` — all plot/footprint corners and edge samples must be buildable.

## How it works

Files: [`TerrainAtlas.h`](../../../app/core/terrain/TerrainAtlas.h), [`TerrainPlacement.cpp`](../../../app/core/placement/terrain/TerrainPlacement.cpp), [`PlotGeometry.cpp`](../../../app/core/placement/geometry/PlotGeometry.cpp).

**Building terrain fields** (`buildings.yml` → `DefCache`):

```yaml
terrain:
  prefer: plains          # or [sea, river] / alias water
  placement: inside       # inside | proximity | border
  requirement: loose      # loose | strict
  border_style: hug       # band | hug (border only)
  proximity_max_dist: 20
  border_min_dist: 2
  border_max_dist: 12
  border_overhang_dist: 2
```

**Majority rule:** if `prefer == majorityLandKind`, proximity behaves as inside.

**Type bands** (with terrain):

| `type` | Hop gate |
|--------|----------|
| `rural` | `hop > suburbanMaxHop` |
| `any` | All roads (`fisher_hut`, `watermill`) |
| `urban` / `residential` | Town ring; usually no `terrain:` block |

Forbidden kinds configured in `app/config/terrains.yml`.

Logs: `terrain_forbidden` (`probe.log`), `terrain_reject` (`layout.log`), `terrain_place.log` for terrain-first trace.

## Interactions

- Authoritative placement semantics: [../../placement/rules.md](../../placement/rules.md)
- Border/scan routing: [../../placement/terrain-buildings.md](../../placement/terrain-buildings.md)
- Bake source: [bake-and-atlas.md](bake-and-atlas.md)
- YAML reference: [../../config/reference.md](../../config/reference.md)
