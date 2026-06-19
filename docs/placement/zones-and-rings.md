# Zones and Hop Rings

## Purpose

Concentric growth bands from town centre control which building types may use which roads.

## What it does

| Band | Hop range | Building types |
|------|-----------|----------------|
| **Core** | 0 … `urbanCoreMaxHop` | Urban/residential (densify target) |
| **Suburban (town ring)** | 0 … `suburbanMaxHop` | Urban/residential |
| **Rural** | `hop > suburbanMaxHop` | `farm`, `lumber_camp`, `mine` |
| **Any** | All roads | `fisher_hut`, `watermill` |

Core ⊂ suburban. Rural is strictly outside suburban. `any` ignores hop gates but keeps terrain rules.

**Ring bump** — When town-ring sweep fails for current index: `suburbanMaxHop += 1`, `urbanCoreMaxHop = suburbanMaxHop - 1`, `ringPhase = DensifyCore`. Roads changing band get wall resync (`restoreRoadWallFromInstances`).

**Movable buildings** — On bump, zone-incompatible movables try relocation first (`MovableRelocation.cpp`); old instance removed only on success. HUD: **Move failures**.

## How it works

Fields on `Town`: `suburbanMaxHop`, `urbanCoreMaxHop`, `ringPhase` (`Normal` | `DensifyCore`).

Helpers in [`GrowthRings.cpp`](../../app/core/placement/orchestration/GrowthRings.cpp), [`FrontageZones.cpp`](../../app/core/placement/zones/FrontageZones.cpp):

- `roadHop`, `getRoadHop`, `getJunctionHops` (cached BFS)
- `collectCoreRoadIds`
- `bumpGrowthRings`, `roadFrontierBand`

Starting suburban ring from [`town.yml`](../../app/config/town.yml): `initial_suburban_max_hops`.

```text
  rural          hop > suburbanMaxHop
  ┌─────────────────────────────────────┐
  │  town ring     hop 0 .. suburbanMax │
  │  ┌───────────────────────────────┐  │
  │  │  core   hop 0 .. urbanCoreMax │  │
  │  └───────────────────────────────┘  │
  └─────────────────────────────────────┘
```

Layout logs: `ring_attempt`, `ring_bump`, `ring_band_exhausted`, `ring_summary`.

## Interactions

- Rules: [rules.md](rules.md)
- Sync orchestration: [sync-pipeline.md](sync-pipeline.md)
- Frontier band buckets: [frontiers.md](frontiers.md)
- Debug zone tint: [../app/rendering-and-debug.md](../app/rendering-and-debug.md)
