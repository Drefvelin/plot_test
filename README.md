# Voronoi Plot

Interactive C++ town generator: Voronoi roads, terrain-aware growth, building placement along road frontage.

## Prerequisites

- Windows 10 or later
- [CMake](https://cmake.org/download/) 3.20+ on your `PATH`
- C++17 compiler — Visual Studio 2022 or Build Tools with MSVC

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

- **Right-click drag** — pan
- **Scroll wheel** — zoom toward cursor
- **HUD slider** — target building count
- **T / Z / B / P / G** — debug overlays (see [docs/app/rendering-and-debug.md](docs/app/rendering-and-debug.md))

## Documentation

Architecture, placement rules, terrain, and config: **[docs/README.md](docs/README.md)**

Agent cheat sheet: [`AGENTS.md`](AGENTS.md)

## Coordinate scale

**10 pixels = 1 world unit** (`world.pixels_per_unit: 10`). Diagram sizes in config are world units.

## Configuration

Edit [`app/config/config.yml`](app/config/config.yml), [`app/config/town.yml`](app/config/town.yml), [`app/config/buildings.yml`](app/config/buildings.yml). Full key list: [docs/config/reference.md](docs/config/reference.md).

After changing config, run `run.bat` again to rebuild if needed and restart.

## Project layout

| Path | Purpose |
|------|---------|
| `app/core/` | C++ source (`Town`, `Road`, placement, terrain) |
| `app/config/` | YAML + terrain colour map |
| `docs/` | System documentation |
| `AGENTS.md` | Agent cheat sheet |
| `logs/` | Runtime logs |
| `build/` | CMake output (generated) |

## Troubleshooting

- **`cmake` is not recognized** — install CMake and add to `PATH`, or use Developer Command Prompt for VS.
- **Build fails** — ensure MSVC installed; try Developer Command Prompt.
- **Window does not appear** — check `logs/app.log`.
