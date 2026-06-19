# Growth Queue and Controls

## Purpose

Seeded building mix, interactive slider, CLI auto-grow, and failure reporting.

## What it does

- **Queue** — `BuildingGrowthQueue` shuffles all types from [`town.yml`](../../app/config/town.yml) counts × [`buildings.yml`](../../app/config/buildings.yml) defs; seeded by `town.seed`.
- **Slider** — HUD bar sets target 0…max; shows `{placed} placed / {target} target` and **Failures: N**.
- **One per sync** — Each `BuildingPlacer::sync` handles at most one queue index; slider jumps catch up one building per frame.
- **Auto-grow** — CLI `--auto-grow [N]` raises target by one building per `--auto-grow-ms` interval (default 50 ms). **G** toggles in-app auto-grow to max.
- **Auto-exit** — `--auto-exit` alone quits after town load (logs + memory report). With `--auto-grow`, waits until cursor catches target; exit code = failure count.

Bare `--auto-grow` defaults to **200** (or `town.yml` total if higher). Config mirrors: `growth.auto_grow`, `growth.auto_grow_ms`, `growth.auto_exit`, `growth.profile`.

## How it works

| Control | Source |
|---------|--------|
| Target count | HUD drag, `--auto-grow`, config |
| Queue order | `town.seed` + type counts |
| Starting suburban ring | `initial_suburban_max_hops` in town.yml |
| Verbose segment logs | `growth.verbose_placement_logs` |
| Profiling | `--profile` or `growth.profile: true` → summary on exit |

**Failures** — Failed indices in `placementFailedIndices`; reasons in `layout.log` (`placement_skipped`: `rural_out_of_range`, `ring_no_slot`, etc.). Healthy runs: Failures = 0.

**Memory report** — On exit, `logs/memory.md` (simulation data breakdown; excludes terrain rasters and debug probe history).

Example unattended run:

```cmd
PlotApp.exe --auto-grow 50 --auto-exit --profile
```

## Interactions

- Sync pipeline: [../placement/sync-pipeline.md](../placement/sync-pipeline.md)
- Hop rings: [../placement/zones-and-rings.md](../placement/zones-and-rings.md)
- Debug HUD: [../app/rendering-and-debug.md](../app/rendering-and-debug.md)
- Config: [../config/reference.md](../config/reference.md)
