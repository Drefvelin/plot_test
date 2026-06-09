# Voronoi Plot

Interactive C++ viewer that generates a circular Voronoi diagram and lets you pan with right-click drag.

## Prerequisites

- Windows 10 or later
- [CMake](https://cmake.org/download/) 3.20+ on your `PATH`
- A C++17 compiler — Visual Studio 2022 (Desktop development with C++) or Build Tools with MSVC

## Quick start

1. Open **cmd**
2. Change to the project root:

   ```cmd
   cd d:\Dokumenter\GameDev\plot_test
   ```

3. Run:

   ```cmd
   run.bat
   ```

`run.bat` configures CMake, builds Release, and launches `PlotApp.exe`.

## Controls

- **Right-click drag** — pan the view
- **Scroll wheel** — zoom in/out (toward cursor)

## Coordinate scale

**10 pixels = 1 world unit** (`world.pixels_per_unit: 10`). Diagram sizes in config are in world units; the renderer multiplies by 10 for pixel output. See [`AGENTS.md`](AGENTS.md) for the data model.

## Configuration

Edit [`app/config/config.yml`](app/config/config.yml):

- `diagram.width` / `diagram.height` / `diagram.radius` — town size in **world units** (default 1024×1024, radius 512)
- `world.pixels_per_unit` — pixels per unit (`10` → 10px = 1 unit, 10240px diagram)
- `plots.min_area` / `plots.max_area` — plot size bounds in **square units**
- `buildings.yml` — building type → size category, and category min/max area bands (see [`DefCache`](app/core/DefCache.h))
- `voronoi.scale` — cell density multiplier (`1.0` ≈ 200 sites)
- `voronoi.seed` — RNG seed (same seed gives the same diagram)
- `colors` — inside (white), outside (grey), edges/roads (black)

After changing config, run `run.bat` again to rebuild if needed and restart the app.

## Project layout

| Path | Purpose |
|------|---------|
| `app/core/` | C++ source (`Town`, `Cell`, `Road`, `Plot`) |
| `app/config/config.yml` | Settings |
| `app/config/buildings.yml` | Building types and plot size bands |
| `AGENTS.md` | Scale, data model, and agent notes |
| `logs/` | Runtime log files (`app.log`, `voronoi.log`, `render.log`) |
| `build/` | CMake build output (generated) |

## Troubleshooting

- **`cmake` is not recognized** — install CMake and add it to your `PATH`, or use the Developer Command Prompt for VS.
- **Build fails** — ensure MSVC is installed; try running `run.bat` from a Developer Command Prompt.
- **Window does not appear** — check `logs/app.log` for startup errors.
