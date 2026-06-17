# Road-Only Town Model

The town model no longer stores Voronoi cells. Voronoi remains only an initial road generator in `TownBuilder.cpp`; after generation, the system works with roads, junctions, road-bank frontage segments, plots created at placement time, buildings, and terrain.

**Placement rules (plots, zones, alleys):** [`placement-model.md`](placement-model.md).

## Model

- `Road` is the canonical geometry primitive. Roads may be primary Voronoi roads, terrain corridor roads, bridges, or secondary alley roads.
- `RoadSideFrontage` lives on each road. Bank `0` is left of `a→b`; bank `1` is right of `a→b`.
- `RoadFrontageSegment` is keyed by `(roadId, bankIndex, segmentId)`.
- `Plot` exists only on a `BuildingInstance` after placement. It stores `roadId` and `roadBank`.
- `SecondaryRoadRecord` stores `hostRoadId` and `hostBankIndex`, not a containing area.

## Build Pipeline

```text
Voronoi roads
  -> append terrain corridor roads
  -> split roads at intersections
  -> split/remove illegal water crossings and create bridges
  -> cull Voronoi roads parallel to corridor chains
  -> index junctions
  -> assign bank inwards
  -> create frontage segments
```

There is no face extraction, cell boundary rebuild, or pre-generated plot pass.

## Placement

Placement iterates road-bank frontage segments. For a candidate plot, the inward depth cap is:

```text
maxDepth = nearest inward ray hit to another road / 2
```

The host road is ignored by the ray. All other roads are blockers, including bridges, corridors, and alleys. Placement hard-rejects candidates outside the town disc, on forbidden terrain, overlapping existing buildings/plots, crossing roads, or overlapping alleys.

Zone scoring still uses urban/residential/rural rules, but segment distance is now measured from the segment midpoint to `town.center`.

## Alleys

Alleys are created road-by-road from primary road wall gaps, closest to town center first.

- The spawn ray starts inset from the wall gap for clearance checks; the **placed alley** starts at the gap point on the host road centerline (`gapPoint`), with the host road split there at rebuild.
- **Probe order per gap position + angle:** (1) straight ray to another road; (2) on any straight failure, depth stub to `maxPlotDepthToRoadHit` inward from `gapPoint`; (3) if stub is valid, scan **180° turn fan** (≥19 directions −90°…+90° relative to stub, seeded shuffle), score all ray hits, prefer fully validated road-to-road turn else **best partial** (`alley_turn_partial` in log); (4) dead-end stub if no turn hit. Crossing-angle gate applies only when validating a perfect match.
- **Host bank gate:** the gap's bank must already have at least one `BuildingInstance` (plot or gap-fill); undeveloped banks are skipped without marking the gap checked (`undeveloped_bank_skipped` in `alley_diag`).
- Probe positions walk along the gap (`alleys_per_unit_length`) and angle fan (`alley_angle_count`); both orders are **seeded-shuffled** per gap so the first successful probe is not always perpendicular.
- The ray succeeds when it hits another road with a different `roadId`.
- If it hits a primary/corridor/bridge road, the alley stops there.
- If it hits an existing alley, that road is split at the snapped hit point and the probe continues on the far side. Junction positions use road-centerline snaps; `splitRoadsAtAlleyEndpoints` in `Town.cpp` re-applies crossing splits whenever secondary roads are rebuilt from records.
- **Main buildings block** the corridor (main footprints and non-`allow_plot_fill` plots). **Auxiliary footprints** (`mainBuilding == false`) in the path are **demolished** on apply (footprint removed, main kept).
- Gap-fill (`SegmentGapFill`) main footprints still block.

**Quality gates** (road/junction geometry only — no cells; `SecondaryRoadPlacement.cpp` + `PlotGeometry.cpp`; config in `town.yml`):

| Key | Rejects |
|-----|---------|
| `min_alley_side_road_dist` | Spawn inset from wall gap; perpendicular clearance on each side of corridor (post-validate) |
| `min_alley_crossing_angle_deg` | Terminal junction where alley meets dest road at an acute / X-like angle (spear corners) |
| `min_alley_bank_angle_sep_deg` | New alley too parallel or anti-parallel to an existing alley on the same host bank |
| `min_alley_endpoint_spacing` | New alley start/end too close to existing junctions or secondary road endpoints |

Probing is simulate → validate → apply (failed probes do not mutate the town). Set any key to `0` to disable that check. Layout log: `blocked_by_main`, `thin_side`, `bad_angle`, `endpoint_spacing`, `bank_parallel`, `depth_stub_fail`, `turn_fail`, `dead_end`, `turn`, `alley_demolish`.

After creation, alleys behave like other roads for frontage, plots, carving, and road hit tests. The `isSecondary` flag is retained so future alley creation can pass through alleys. **There is no separate alley plot API** — see [`placement-model.md`](placement-model.md).
