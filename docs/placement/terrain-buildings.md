# Terrain Buildings

## Purpose

Route rural and `type: any` buildings through terrain-first placement: border buckets, terrain scan, anchor fallback, strict/loose requirement.

## What it does

Buildings with `terrain:` in [`buildings.yml`](../../app/config/buildings.yml) bypass vanilla plot frontier for the first attempt(s):

| Mode | Behaviour |
|------|-----------|
| **inside** | ≥1 plot corner on `prefer`; score by corner match |
| **proximity** | Prefer segments near outline edge; skip beyond `proximity_max_dist` in scan |
| **border** | Separate placer: frontage segment whose inward ray hits outline before another road |

**Terrain-first order** (`tryPlaceRuralOnRoads` / `tryPlaceAnyOnRoads`):

1. Border types → `tryPlaceBorderPlot` (peek border buckets; plot required; band/hug main).
2. All terrain types → `tryPlaceRoadPlot` with `TerrainScan` mode.
3. Loose only → `terrain_anchor` BFS then vanilla frontier on band scope.
4. Strict → fail after step 2 (`watermill`).

`placement: border` never uses `frontier_loose_fallback`; loose hug retries **band** after exhaustion.

## How it works

| Building | `type` | `prefer` | `placement` | `requirement` |
|----------|--------|----------|-------------|---------------|
| `farm` | rural | plains | inside | loose |
| `lumber_camp` | rural | forest | proximity | loose |
| `mine` | rural | hills | proximity | loose |
| `fisher_hut` | any | `[sea, river]` | border hug | loose |
| `watermill` | any | river | border hug | **strict** |

**Border layout** — [`BorderFrontier.cpp`](../../app/core/placement/frontier/BorderFrontier.cpp) classifies plot segments at init; [`BorderPlacement.cpp`](../../app/core/placement/terrain/BorderPlacement.cpp) peeks closest, builds plot (`buildBorderHugPlot` or band), places main. Validation: no road/building overlap; water spill OK. Up to `border_max_attempts` retries.

**Terrain scan** — [`TerrainScanFrontier.cpp`](../../app/core/placement/frontier/TerrainScanFrontier.cpp) pre-sorted buckets (Plains/Forest/Hills).

**Anchor** — `Town.lastTerrainAnchorRoadId[prefer]` updated on successful scan/border only; BFS cap `plots.terrain_anchor_max_roads`.

Logs: `terrain_place.log` (grep `queueIndex=N`), `layout.log` (`border_*`, `frontier_loose_fallback`, `terrain_strict_fail`).

Hard rejects: `polygonBuildable` on all candidates — same as [frontage-and-plots.md](frontage-and-plots.md).

## Interactions

- Authoritative rules: [rules.md](rules.md)
- Terrain queries: [../town-generation/terrain/queries.md](../town-generation/terrain/queries.md)
- Frontiers: [frontiers.md](frontiers.md)
- Sync pipeline: [sync-pipeline.md](sync-pipeline.md)
