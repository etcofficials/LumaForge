# LumaForge

A lightweight, native Windows video editor built around one principle: **a professional editing
experience without demanding modern high-end hardware.**

C++20 · Qt 6 Widgets · FFmpeg · Direct3D 11. No Electron, no web view, no Python in the engine.

## Status — V0.1 foundation shell

The application launches, shows the editor layout (media panel, D3D11 preview surface with
transport, inspector, custom timeline with ruler/playhead/zoom), detects the machine's real
hardware capabilities on a background thread, and reports them honestly in the status bar and the
Diagnostics window. Media import, playback, timeline editing, project files and export follow in
the next milestones (see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)).

## Build

Everything lives on `G:` with a portable toolchain — see [TOOLS.md](TOOLS.md) for the layout,
setup scripts and caveats.

```powershell
. G:\LumaForge\tools\setup-environment.ps1     # every new shell
cmake --preset debug                          # or release / relwithdebinfo
cmake --build --preset debug
.\build\debug\bin\lf_tests.exe                # unit tests
.\build\debug\bin\LumaForge.exe               # add --diagnostics[=graphics|log] to open Diagnostics on start
```

The environment script is needed to *build*. Running needs nothing: every build deploys the Qt runtime,
plugins, the matching MSVC runtime and the FFmpeg DLLs into `build/<preset>/bin` (see `cmake/LumaForgeDeploy.cmake`),
so `LumaForge.exe` starts from Explorer or any shell. `--software-preview` forces the software preview path.

## Repository layout

| Path | Contents |
|---|---|
| `src/core` | time (flicks), rationals, results, logging |
| `src/hardware` | D3D11 device, capability detection, encoder probes |
| `src/rendering` | D3D11 preview renderer, frame layout |
| `src/commands` | command + undo stack |
| `src/diagnostics` | system information |
| `src/ui` | theme, icons, actions, panels, timeline, preview, dialogs, main window |
| `src/app` | entry point, manifest, version resource |
| `tests` | engine unit tests |
| `resources` | stylesheet and SVG icons (compiled in) |
| `tools` | toolchain fetch / setup / diagnose scripts |
| `docs` | architecture and decision records |

## Target machine

Intel Core i5‑2400S · 12 GB RAM · NVIDIA GeForce GT 730 (Fermi GF108, 4 GB, driver 391.35) ·
Windows 10 22H2. Hardware acceleration is detected at runtime; nothing is assumed from the GPU name.
