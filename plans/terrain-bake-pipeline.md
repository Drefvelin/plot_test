# Terrain Bake Pipeline

One-time conversion from terrain image to `TerrainAtlas`. Runs at startup or when loading a precomputed cache.

## Inputs

- Terrain PNG (same extent as diagram: width × height in world units, or matching pixel size with known scale)
- `app/config/terrain.txt` — RGB → terrain kind mapping
- Diagram bounds from config (`diagram.width`, `diagram.height`, `world.pixels_per_unit`)

## Step 1 — Label image

For each pixel, map RGB to `TerrainKind` using **exact match first**, then **nearest** colour from `terrain.txt` (max per-channel delta 32). Tie-break: **river over sea** when distances are equal (narrow blue strokes).

Output: labelled raster (same size as source image).

## Step 2 — Forbidden zones (hard constraints)

1. Merge `sea` and `river` labels into a **water mask**
2. Optionally dilate river mask by N pixels (buffer) so buildings stay back from banks
3. Find connected components on the forbidden mask
4. For each component, trace outer contour (marching squares)
5. Walk boundary; emit contour graph nodes every `simplify_tolerance` world units; subdivide edges that cross raw water
6. Apply `water_inset` (dilate forbidden mask) for buildable queries and forbidden outline trace; **`shore_inset`** dilates sea mask before tracing **`seaOutlines`** (land-side shore); **`river_inset`** dilates river mask before tracing **`riverOutlines`** (land-side bank — same role as shore for border frontier)
7. Store as `forbiddenPolygons` polylines (node chains) + separate shore/river graphs

Bake logs: `river=`, `river_nodes=`, `river_inset=`; warning if `river_components > 0` but `river_nodes == 0`. After town build: `border_frontier: sea_slots=… river_slots=…`.

**Use:** point-in-polygon rejection during footprint validation.

## Step 3 — River centerlines

Do **not** use river outline for placement logic. Extract a **centerline polyline**:

- **Skeleton / thinning** on the river pixel blob, or
- Trace both banks and average, or
- Longest-path through the river pixel graph

Then simplify the polyline. Store per connected river:

- `centerline`
- `avgWidth` (median width sampled along skeleton)

**Use:** watermill anchors, bridge detection, road parallel bias, `distToRiver` for buildable margin.

## Step 4 — Shoreline

Trace the boundary between `sea` and non-sea pixels:

- Open or closed polyline(s) depending on map topology
- Simplify aggressively

**Use:** fisherman / dock anchors, coastal road bias, `distToShore`.

## Step 5 — Land biome outlines (forest, hills, plains)

After labelling the raster:

1. Count non-water pixels per land kind (`Plains`, `Forest`, `Hills`, `Mountain`); pick **majority** → `majorityLandKind` (tie-break: Plains > Forest > Hills > Mountain). This is the implicit background biome for the map (often plains; can be forest on a 75%-forest map).
2. For each land kind **other than** `majorityLandKind`, trace its boundary using the **same pipeline as rivers/shore**:
   - `extractInterfacePolylines` on the kind mask
   - `buildContourGraphs(..., closed=false, waterSafe=false)` resampled by `simplify_tolerance`
3. Store open polylines in `forestOutlines`, `hillsOutlines` (hills + mountain mask), `plainsOutlines`. Majority kind gets an **empty** outline vector (no outer boundary around the whole habitable area).
4. Debug draw: forest green, hills gray-brown; open polylines like river/shore.

**Use:** `distToRegionEdge(p, kind)` for placement scoring; `sample(p)` for exact in-kind tests. `plainsOutlines` stored when plains is minority (not drawn in debug v1).

**Not used:** closed `traceExactContours` loops (removed).

## Step 6 — Coarse grid

Build a low-res grid over diagram extent (recommended **128×128** for a 1024 diagram → 8 units per cell):

- For each grid cell, sample centre (or majority vote over subsamples) → dominant `TerrainKind`
- Mark grid cell `Forbidden` if majority is sea/river

**Use:** O(1) `sample(p)` for placement validation, segment scoring, and fast rejection before expensive polygon tests.

## Step 7 — Optional distance fields

If profiling shows polyline distance queries are hot, precompute at bake time:

- Distance to nearest forbidden boundary
- Distance to forest / hills outline edges (capped at max “near” distance)

Runtime **`distToRegionEdge`** walks smoothed outline polylines (segment distance). Returns a large sentinel when the kind is majority (no outline). Not required for phase 1 beyond the polyline walk.

## Validation (debug)

After bake, log counts and draw debug overlays:

- Forbidden polygon count and vertex counts
- `majority_land=` kind name
- River / shore / forest / hills outline node counts (0 for majority land kind)
- Grid histogram per terrain kind (when coarse grid exists)

Optional: assert no `plains` grid cells inside forbidden polygons at cell centres.

## What to avoid

- Keeping full-resolution label image for runtime placement queries
- One global polygon per terrain type (use connected components — map has multiple forests/hill masses)
- Over-tight contours — simplified blobs slightly smaller than art is acceptable
