# Terrain Bake Pipeline

One-time conversion from terrain image to `TerrainAtlas`. Runs at startup or when loading a precomputed cache.

## Inputs

- Terrain PNG (same extent as diagram: width × height in world units, or matching pixel size with known scale)
- `app/config/terrain.txt` — RGB → terrain kind mapping
- Diagram bounds from config (`diagram.width`, `diagram.height`, `world.pixels_per_unit`)

## Step 1 — Label image

For each pixel, map RGB to `TerrainKind` using exact or nearest match from `terrain.txt`.

Output: labelled raster (same size as source image).

## Step 2 — Forbidden zones (hard constraints)

1. Merge `sea` and `river` labels into a **water mask**
2. Optionally dilate river mask by N pixels (buffer) so buildings stay back from banks
3. Find connected components on the forbidden mask
4. For each component, trace outer contour (marching squares)
5. Walk boundary; emit contour graph nodes every `simplify_tolerance` world units; subdivide edges that cross raw water
6. Apply `water_inset` (dilate forbidden mask) for buildable queries and forbidden outline trace; `shore_inset` for shore graph
7. Store as `forbiddenPolygons` polylines (node chains) + separate shore/river graphs

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

## Step 5 — Region polygons (forest, hills, mountain)

For each terrain kind separately:

1. Connected components on labelled raster
2. Trace outer contour per blob
3. Simplify (fewer vertices than raw pixels — ~20–80 per blob is fine)
4. Store in `forests`, `hills`, `mountains`

Optional: compute an **outward buffer** polygon for “near X” zones (e.g. +10 units) at bake time instead of distance queries every placement.

**Use:** mine / logging camp scoring; not required for exact in-forest tests.

## Step 6 — Coarse grid

Build a low-res grid over diagram extent (recommended **128×128** for a 1024 diagram → 8 units per cell):

- For each grid cell, sample centre (or majority vote over subsamples) → dominant `TerrainKind`
- Mark grid cell `Forbidden` if majority is sea/river

**Use:** O(1) `sample(p)`; cell dominant terrain at town build; fast rejection before expensive polygon tests.

## Step 7 — Optional distance fields

If profiling shows polygon distance queries are hot, precompute at bake time:

- Distance to nearest forbidden boundary
- Distance to forest / hills polygon edges ( capped at max “near” distance)

Store as float grids aligned with the coarse terrain grid. Not required for phase 1.

## Validation (debug)

After bake, log counts and draw debug overlays:

- Forbidden polygon count and vertex counts
- River centerline length(s)
- Shoreline point count
- Grid histogram per terrain kind

Optional: assert no `plains` grid cells inside forbidden polygons at cell centres.

## What to avoid

- Keeping full-resolution label image for runtime placement queries
- One global polygon per terrain type (use connected components — map has multiple forests/hill masses)
- Over-tight contours — simplified blobs slightly smaller than art is acceptable
