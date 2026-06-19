# Rendering and Debug

## Purpose

Visualize town state, terrain, placement bands, and bridge diagnostics from the SFML app.

## What it does

- White disc background; `roadMesh`, `junctionMesh`, building meshes, frontage debug overlays.
- Screen-fixed HUD: growth slider, failure count, hop band legend, toggle buttons.

## How it works

### HUD controls

| Key / button | Effect |
|--------------|--------|
| Drag slider | Set target building count |
| **T** / Terrain | Cycle terrain overlay (terrain+debug / debug / off) |
| **Z** / Zones | Hop-band road stripe tint (core red, suburban light blue, rural brown) |
| **B** / Bridges | Bridge debug: terrain image, white probe circles at `bridge_waterside_max_dist`, red X at water hits, purple=waterside junctions, brown bridges `B{id}` |
| **P** / Biome plots | Terrain-tagged building plot outlines + labels (`type`, placement/prefer) |
| **G** | Toggle auto-grow to max |
| Right-drag | Pan |
| Scroll | Zoom toward cursor |

**Zone tint caveat** — When **Z** is on, unrevealed bridges may be hidden from the hop debug mesh (normal road mesh respects bridge reveal separately).

### Frontage debug overlay

Rebuilt each sync:

| Colour | Meaning |
|--------|---------|
| Green | Live plot frontage segments |
| Yellow | Wall gaps between main fronts |
| Orange | Wall gaps in alley frontier |
| Orange probes | Failed alley **creation** attempts for current queue index |

Terrain **T** draws forest (green) and hills (gray-brown) outlines when non-majority.

Files: [`App.cpp`](../../app/core/render/App.cpp), [`Hud.cpp`](../../app/core/render/Hud.cpp).

Logs written to `logs/` — see [config reference](../config/reference.md) logging section.

## Interactions

- Bridges debug: [../town-generation/water-and-bridges.md](../town-generation/water-and-bridges.md)
- Zone bands: [../placement/zones-and-rings.md](../placement/zones-and-rings.md)
- Growth HUD: [../growth/queue-and-controls.md](../growth/queue-and-controls.md)
