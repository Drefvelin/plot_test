# Terrain Step 1 — Implementation Plan

Concrete plan for the first shippable slice: **load terrain, bake geometry, render overlay + debug outlines, visually clip roads at water**. No building placement rules, no real road graph splits, no bridges yet.

## Goal

After Step 1 you can:

- Point config at `assets/terrain.png` (swap files to try maps)
- Toggle terrain overlay (3 modes: terrain+debug, debug only, off)
- Toggle debug outlines (forbidden water, river outline, shoreline, forest, hills)
- See primary/secondary roads **stop at water** in the mesh (visual clip only)
- Validate the bake pipeline before touching placement or road topology

## Non-goals (defer to Step 1.5 / Phase 1b)

| Deferred | Why |
|----------|-----|
| Split `Road` records at water | Changes junctions and bridge legality; do after bake looks correct |
| `isBuildable()` in footprint validation | Placement phase |
| Segment terrain scoring | Needs grid + frontage sampling |
| Bridges | Needs real road splits first |
| Voronoi site bias away from water | Step 2 |
| Forest/hills region polygons | Phase 2 biome scoring |
| Serialized atlas cache | Phase 5 polish |

## Scope summary

```
config.yml ──► Config.terrain
terrain.txt ──► colour → TerrainKind map
terrain.png ──► TerrainAtlas (bake once at startup)
                    ├── forbidden polygons (sea + river)
                    ├── shoreline polyline
                    ├── river centerlines (skeleton, debug)
                    ├── buildable grid 128×128
                    └── sf::Texture for overlay
App + Hud ──► single Terrain toggle (terrain+debug / debug / off), draw order
rebuildRoadMesh ──► clip segments to land (mesh only, Road struct unchanged)
```

---

## Task breakdown

### Task 1 — Config and assets layout

**Files:** `app/config/config.yml`, `app/core/Config.h`, `app/core/Config.cpp`, `CMakeLists.txt`, `assets/` (folder)

**Work:**

1. Add `assets/` at project root; document that terrain PNGs live here.
2. Add config block:

   ```yaml
   terrain:
     image: assets/terrain.png
     colors: app/config/terrain.txt
     grid_size: 128              # coarse buildable/terrain grid
     simplify_tolerance: 2.0     # world units, max edge length along contour
     water_inset: 2.0            # world units, buildable buffer from water (sea + river)
     shore_inset: 2.0            # world units, shoreline graph inset from raw coast
     contour_width: 1.0          # world units, debug contour line width (like roads)
     clip_roads_at_water: true   # visual mesh clip in step 1
   ```

3. Add `TerrainConfig` struct to `Config.h` with defaults and path resolution relative to project root (same pattern as `resolveProjectRoot` in `Config.cpp`).
4. Extend `CMakeLists.txt` `POST_BUILD` to copy `assets/` beside the exe (mirror `app/config` copy).

**Acceptance:** App loads config; missing image logs a warning and runs without terrain (overlay toggles disabled or no-op).

---

### Task 2 — Terrain types and colour map loader

**Files:** `app/core/Terrain.h`, `app/core/TerrainColors.cpp` (or inline in bake)

**Work:**

1. Define `enum class TerrainKind { Unknown, Sea, River, Plains, Forest, Hills, Mountain, Forbidden }`.
2. Parse `terrain.txt` (`key = R, G, B` per line) into `std::vector<std::pair<std::array<uint8_t,3>, TerrainKind>>`.
3. `TerrainKind classifyPixel(sf::Color)` — exact RGB match; unknown → `Unknown`.

**Acceptance:** Unit-style log at bake: counts per kind from a test load.

---

### Task 3 — Terrain bake module

**Files:** `app/core/TerrainAtlas.h`, `app/core/TerrainBake.cpp`, add to `CMakeLists.txt`

**Work:**

Implement `TerrainAtlas bakeTerrain(const Config&, const std::filesystem::path& projectRoot)` returning:

| Field | Step 1 bake |
|-------|-------------|
| `forbiddenPolygons` | Merge sea + river labels; connected components; contour trace; simplify |
| `shoreline` | Boundary between sea and non-sea; simplified polyline |
| `riverCenterlines` | Skeleton / medial-axis on river blobs only; simplified |
| `grid` | `grid_size × grid_size` dominant kind; `Forbidden` where sea/river |
| `gridOrigin`, `gridCellSize` | Cover `diagram.width/height` in world units |
| `overlayTexture` | Loaded PNG as `sf::Texture` (same image or resampled to diagram size) |
| `valid` | false if image missing or load failed |

**Algorithms (step 1 — keep simple):**

1. **Load image** with SFML `sf::Image` / `loadFromFile`.
2. **Coordinate mapping:** image pixel `(px, py)` → world `(x, y)`:
   - Convention: image width/height maps to `diagram.width` × `diagram.height` world units.
   - `worldX = (px + 0.5) / imageW * diagram.width`
   - `worldY = (py + 0.5) / imageH * diagram.height`
3. **Forbidden mask:** raster pass labelling sea + river; connected components (4- or 8-neighbour); marching squares or border following for outer contour; Ramer–Douglas–Peucker with `simplify_tolerance`.
4. **Shoreline:** pixels where sea meets non-sea; chain into polyline(s); simplify.
5. **River skeleton (debug):** on river-only mask, use a simple thinning / distance-transform ridge or “trace centre column per scanline” fallback for v1; simplify. Perfect skeleton not required — must be visibly centred in the river for tuning.
6. **Grid:** for each raster grid-cell centre, sample labelled pixel → store kind.

**Query helpers on `TerrainAtlas`:**

```cpp
bool isForbidden(Vec2 worldPos) const;   // grid lookup or point-in-polygon
bool isBuildable(Vec2 worldPos) const;  // !isForbidden (no margin yet)
TerrainKind sample(Vec2 worldPos) const;
```

Use grid first; polygon fallback optional for step 1.

**Logging:** new channel `terrain` or use `render` — polygon count, vertex counts, grid histogram, bake time ms.

**Acceptance:** Bake completes on test map; log shows non-zero forbidden area; outlines enclose visible water in the art.

---

### Task 4 — Debug line meshes

**Files:** `app/core/TerrainAtlas.h`, `app/core/TerrainBake.cpp`

**Work:**

Build `sf::VertexArray` **triangle meshes** (1 world unit wide, like roads) from **contour graphs**:

| Layer | Source | Colour |
|-------|--------|--------|
| Forbidden | `forbiddenPolygons` (water inset) | Magenta |
| River banks | `riverOutlines` | Cyan |
| Shore | `seaOutlines` (shore inset) | Yellow |
| Forest / hills | region graphs | Green / orange |

**Contour graph bake:** extract land-side interface polylines; place nodes every `simplify_tolerance` world units; connect with straight edges; subdivide any edge that crosses raw water until clean (forest/hills skip subdivide). Config: `water_inset`, `shore_inset`, `contour_width`.

**Acceptance:** Outlines read as smooth edge graph, not pixel staircase; bake log reports node/edge counts.

---

### Task 5 — App rendering and overlay toggle

**Files:** `app/core/App.h`, `app/core/App.cpp`, `app/core/main.cpp`

**Work:**

1. Load `TerrainAtlas` after `Config::load` in `main` or `App` ctor; pass into `App`.
2. `TerrainOverlayMode` enum:
   - `TerrainAndDebug` — PNG + outline lines (default when atlas valid)
   - `DebugOnly` — white disc + outline lines
   - `Off` — white disc only
3. **Draw order** (world view):

   ```
   clear(window background grey)
   if (showTerrainOverlay_ && atlas.valid)
       draw terrainSprite   // full diagram extent, replaces white disc
   else
       draw diagramSprite   // existing white circle
   town.roadMesh            // clipped when terrain active — Task 6
   junctions, frontage, buildings, …
   if (showTerrainDebug_)
       draw debug line meshes (on top of roads)
   HUD (screen space)
   ```

4. `terrainSprite_` positioned/scaled to match `diagramTexture_` (0,0)–(`renderWidth`, `renderHeight`).

5. When overlay on, optionally **skip** white circle fill in `buildDiagram()` or always skip white and let terrain/white be chosen at draw time (prefer draw-time branch — simpler).

**Acceptance:** Toggle switches between white disc and terrain; roads remain visible on top.

---

### Task 6 — Hud toggles

**Files:** `app/core/Hud.h`, `app/core/Hud.cpp`, `app/core/App.cpp`

**Work:**

1. Add two small toggle buttons in the top bar (right side or second row):
   - **Terrain** — overlay on/off
   - **Debug** — outline overlays on/off
2. Keyboard shortcuts (optional): `T` terrain, `D` debug — only when HUD/window focused and not conflicting with camera.
3. `Hud` callbacks or bool refs passed from `App`:

   ```cpp
   hud_.setTerrainToggles(&showTerrainOverlay_, &showTerrainDebug_, atlasValid);
   ```

4. Extend `handleEvent` hit-test for toggle rects; return true when consumed.

**Acceptance:** Click toggles work; label shows current state (`Terrain: on`).

---

### Task 7 — Visual road clip at water

**Files:** `app/core/Town.cpp` (`rebuildRoadMesh`), `app/core/TerrainClip.h` (optional)

**Work:**

1. Add overload or optional parameter:

   ```cpp
   void rebuildRoadMesh(Town& town, ..., const TerrainAtlas* terrain = nullptr);
   ```

2. When `terrain != nullptr && config.terrain.clipRoadsAtWater`:

   For each road `A → B` in world units:

   - Walk segment in steps (grid cell size or fixed ~1 unit steps)
   - Collect maximal **land** intervals where `!terrain->isForbidden(midpoint)`
   - For each land interval `[t0, t1]`, append one thick mesh segment

   Pseudocode:

   ```cpp
   std::vector<std::pair<float,float>> landIntervals = clipSegmentToLand(a, b, terrain);
   for (auto [t0, t1] : landIntervals)
       appendThickSegment(lerp(a,b,t0), lerp(a,b,t1), ...);
   ```

3. **Do not** modify `Road.a`, `Road.b`, junctions, or frontage.

4. Call sites: `TownBuilder::build` and `BuildingPlacer` mesh rebuild — pass atlas when available.

**Acceptance:** Roads visibly stop at river/sea edges; junction dots may still sit in wrong places (expected until Step 1.5).

**Known visual glitch (OK for step 1):** Junctions at original Voronoi endpoints may float in water while road mesh stops short.

---

### Task 8 — Wire startup and failure modes

**Files:** `app/core/main.cpp`, `app/core/App.cpp`

**Work:**

1. Resolve paths: `projectRoot / config.terrain.image`.
2. If bake fails: log, set `atlas.valid = false`, app runs as today.
3. Log bake summary to `logs/render.log` or new `logs/terrain.log` (add channel in `config.yml` logging section).

**Acceptance:** Missing PNG does not crash; toggles greyed out or hidden.

---

### Task 9 — Documentation sync

**Files:** `plans/terrain-roadmap.md`, `plans/README.md`, `AGENTS.md` (brief pointer)

**Work:**

- Mark Step 1 tasks done as implemented
- Note coordinate convention (image → world mapping)
- Document toggles and config keys

---

## Implementation order

Recommended PR sequence (can be one branch if preferred):

| Order | Task | Depends on |
|-------|------|------------|
| 1 | Task 1 — Config + assets copy | — |
| 2 | Task 2 — Colour map loader | Task 1 |
| 3 | Task 3 — Bake core (forbidden + grid minimum) | Task 2 |
| 4 | Task 4 — Debug meshes (forbidden + shoreline) | Task 3 |
| 5 | Task 5 — App overlay rendering | Task 3 |
| 6 | Task 6 — Hud toggles | Task 5 |
| 7 | Task 3b — River skeleton in bake | Task 3 |
| 8 | Task 4b — River debug mesh | Task 7 |
| 9 | Task 7 — Visual road clip | Task 3 |
| 10 | Task 8–9 — Polish + docs | all |

Minimum viable demo after **Tasks 1–6 + forbidden bake**: overlay toggle works. Add **7–9** for roads stopping at water and river skeleton visible.

---

## File checklist (new / modified)

| Path | Action |
|------|--------|
| `assets/terrain.png` | Add (user art; copy from workspace or placeholder) |
| `app/config/config.yml` | Add `terrain` section + optional `terrain` log channel |
| `app/config/terrain.txt` | Existing — no change |
| `app/core/Config.h` / `.cpp` | `TerrainConfig` |
| `app/core/Terrain.h` | Kinds + colour map |
| `app/core/TerrainAtlas.h` | Atlas struct + queries |
| `app/core/TerrainBake.cpp` | Bake implementation |
| `app/core/TerrainDebugMesh.cpp` | Optional — or part of bake |
| `app/core/Town.cpp` | `rebuildRoadMesh` clip |
| `app/core/Town.h` | Optional atlas param on rebuild |
| `app/core/TownBuilder.cpp` | Pass atlas to mesh build |
| `app/core/BuildingPlacer.cpp` | Pass atlas to mesh rebuild |
| `app/core/App.h` / `.cpp` | Atlas, sprites, draw order |
| `app/core/Hud.h` / `.cpp` | Toggles |
| `app/core/main.cpp` | Load bake, construct App |
| `CMakeLists.txt` | Sources + assets copy |

---

## Acceptance criteria (Step 1 complete)

- [x] `terrain.image` in config loads PNG from `assets/`
- [x] Terrain toggle cycles terrain+debug → debug only → off
- [x] Debug outlines (magenta/cyan/yellow/green/orange) from simplified polygons
- [x] Roads do not draw across sea/river (mesh clip); logical roads unchanged
- [x] Missing terrain file → graceful fallback to current behaviour
- [x] `plans/` updated; bake convention documented

**Coordinate mapping:** image pixel `(px, py)` → world `( (px+0.5)/imageW * diagram.width, (py+0.5)/imageH * diagram.height )`.

**UI:** Hud **Terrain** button (top-right); keyboard `T` cycles modes when atlas valid.

**Queries:** `isForbidden()` uses raster as authority for water; definite land kinds short-circuit before polygon test. Bake log reports vertex counts and effective water simplify tolerance.

---

## Step 1.5 preview (next plan)

Corridor roads, intersection split, bridges, and road-only placement are implemented — see [terrain-corridor-roads.md](terrain-corridor-roads.md) and [road-only-model.md](road-only-model.md).

Remaining Step 1.5 / Phase 1b items:

1. Biome-specific scoring from terrain coverage
2. Feature-anchor placement for river/shore/hills buildings
3. Tune `corridor_edge_spacing` / insets per map in `config.yml`

See [terrain-roadmap.md](terrain-roadmap.md) Phase 1b and Phase 4.

---

## Risks and mitigations

| Risk | Mitigation |
|------|------------|
| Image size ≠ diagram size | Explicit world mapping in bake; log image vs diagram dimensions |
| Skeleton noisy on wide rivers | Simplify aggressively; replace with better algorithm later |
| Slow bake on large PNG | Bake once at startup; cache in Phase 5 |
| Road clip stair-steps on diagonal | Finer step size (0.5 units) or analytic segment–polygon clip in Step 1.5 |
| Cells span rivers | Expected; document; fix with site bias in Step 2 |

---

## Test map notes

Use the project terrain art (sea bottom, river centre, forests, hills). Verify:

- Roads toward coast stop at shoreline
- River crossing Voronoi edges show a gap in the mesh (two stubs)
- Forbidden outline includes both sea and river colours from `terrain.txt`
