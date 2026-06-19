# Configuration Reference

## Purpose

Single index of YAML keys loaded into [`Config.h`](../../app/core/config/Config.h) and related definition files.

## What it does

Three files at runtime (resolved from exe dir / project root):

- [`app/config/config.yml`](../../app/config/config.yml) — window, diagram, terrain, growth, logging
- [`app/config/town.yml`](../../app/config/town.yml) — building counts, hop start, alley tuning
- [`app/config/buildings.yml`](../../app/config/buildings.yml) — building types, sizes, terrain prefs

Colour map: [`app/config/terrain.txt`](../../app/config/terrain.txt) (not YAML).

## How it works

### `config.yml`

| Section | Key | Default (struct) | Purpose |
|---------|-----|------------------|---------|
| `window` | `width`, `height`, `title` | 1280, 720, "Voronoi Plot" | SFML window |
| `diagram` | `width`, `height`, `radius` | 1024, 1024, 512 | Town extent (world units) |
| `world` | `pixels_per_unit` | 10 | Render scale |
| `plots` | `min_area`, `max_area` | 50, 200 | Plot area bounds (sq units) |
| `plots` | `max_depth_to_front_ratio` | 1.5 | Depth symmetry cap |
| `plots` | `frontage_setback` | 2.0 | Front row inset from road |
| `plots` | `terrain_anchor_max_roads` | 4 | BFS cap for terrain anchor fallback |
| `voronoi` | `scale`, `seed` | 1.0, 42 | Site density and RNG |
| `town` | `seed` | 12345 | Growth queue shuffle |
| `terrain` | `image` | assets/terrain.png | Terrain PNG path |
| `terrain` | `colors` | app/config/terrain.txt | RGB → kind map |
| `terrain` | `grid_size` | 128 | Coarse grid resolution |
| `terrain` | `simplify_tolerance` | 2.0 | Outline simplify (world u) |
| `terrain` | `water_inset`, `shore_inset`, `river_inset` | 2.0 each | Dilate/trace insets |
| `terrain` | `contour_width` | 1.0 | Debug overlay line width |
| `terrain` | `clip_roads_at_water` | true | Enable water sanitize |
| `terrain` | `corridor_roads_enabled` | false | Shore/river corridor roads |
| `terrain` | `shore_road_inset`, `river_road_inset` | 0 | Corridor offset from mask |
| `terrain` | `corridor_edge_spacing` | 0 | Max straight edge along contour |
| `terrain` | `corridor_parallel_probe_offset` | 0 | Parallel cull probe distance |
| `terrain` | `corridor_parallel_cos` | 0 | Min \|dot\| for parallel cull |
| `terrain` | `bridges_enabled` | false | Bridge pairing |
| `terrain` | `bridge_max_span` | 80.0 | Max bridge chord length |
| `terrain` | `bridge_waterside_max_dist` | 4.0 | Raster waterside probe radius |
| `terrain` | `shore_junction_merge_dist` | 2.0 | Cluster waterside junctions (0=off) |
| `terrain` | `bridge_snap_enabled` | false | Chord snap along shore |
| `terrain` | `bridge_snap_search_radius` | 0 | Snap search distance |
| `terrain` | `bridge_bucket_hops` | 2 | Reveal bucket BFS depth |
| `colors` | `inside`, `outside`, `edges`, `secondary_edges`, `bridge` | see YAML | Render colours |
| `debug` | `highlight_color` | 128,128,128 | Debug highlight |
| `logging` | `directory`, `flush_interval_ms`, `files[]` | logs, 2000 | Log channels → files |
| `growth` | `auto_grow`, `auto_grow_ms` | 0, 50 | CLI auto-grow defaults |
| `growth` | `auto_exit`, `profile` | false | Exit and profiling |
| `growth` | `verbose_placement_logs` | false | Per-sync segment inventory |

### `town.yml`

| Key | Purpose |
|-----|---------|
| `initial_suburban_max_hops` | Starting suburban ring (rural = hop > this) |
| `min_wall_gap_for_alley` | Min wall gap width for alley probes |
| `min_alley_length` | Min alley segment length |
| `max_alley_angle_deg` | Alley angle fan half-width |
| `alleys_per_unit_length` | Probe positions per gap width |
| `alley_angle_count` | Angles per gap position |
| `min_alley_crossing_angle_deg` | Reject acute terminal crossings |
| `min_alley_bank_angle_sep_deg` | Min angle vs existing bank alleys |
| `min_alley_endpoint_spacing` | Min junction/endpoint separation |
| `min_alley_side_road_dist` | Spawn inset + side clearance |
| `alley_side_road_sample_count` | Interior clearance samples |
| `alley_fill_fail_limit` | Abandon alley after N fill failures |
| `border_max_attempts` | Border placement retries per queue index |
| `border_outline_probe_max_dist` | Deprecated (compat only) |
| `border_outline_sample_step` | Debug outline draw step |
| `buildings.<type>` | Target count per type in full town |

### `buildings.yml`

Per building type:

| Field | Purpose |
|-------|---------|
| `type` | `urban` \| `residential` \| `rural` \| `any` |
| `fill_in` | Allow core gap-fill (default true for many) |
| `allow_plot_fill` | Church: block plot-fill alleys |
| `movable` | Relocate on ring bump when zone incompatible |
| `rgb` | Display colour |
| `terrain.*` | See [terrain queries](../town-generation/terrain/queries.md) |
| `buildings` | Size mix (small/medium/… counts) |

Sections: `building_templates`, `plot_sizes` — area bands for layouts and plots.

Loaded via [`DefCache`](../../app/core/config/DefCache.h).

## Interactions

- Architecture overview: [../architecture/overview.md](../architecture/overview.md)
- Agent cheat sheet: [`AGENTS.md`](../../AGENTS.md)
- Terrain bake keys: [../town-generation/terrain/bake-and-atlas.md](../town-generation/terrain/bake-and-atlas.md)
- Placement rules: [../placement/rules.md](../placement/rules.md)
