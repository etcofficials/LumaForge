# LumaForge Architecture

Status: V0.1 foundation shell. This document describes the structure that exists today and the
seams already reserved for the next milestones (media engine, playback, timeline model, project,
export). Decisions with rationale live in [DECISIONS.md](DECISIONS.md).

## Principles

1. **The UI thread never touches pixels.** Decoding, probing, thumbnails, waveforms, export and
   hardware detection run on worker threads. The UI observes results.
2. **Capabilities are measured, never assumed.** Hardware acceleration is decided per machine by
   creating devices, checking decoder profiles and opening encoders (`hardware/`).
3. **Exact time.** Timeline positions are 64-bit *flicks* (1/705,600,000 s). Frame boundaries of
   every common rate and audio sample boundaries are integers; no floating point in edit math.
4. **Engine modules are Qt-free.** `core`, `hardware`, `rendering`, `commands`, `diagnostics` (and
   the coming `media`, `timeline`, `playback`, `project`, `export`, `cache`) are plain C++20 static
   libraries, unit-testable without a GUI. Only `ui/` and `app/` link Qt.
5. **Every edit is a Command.** `commands/` provides `Command` + `UndoStack` (linear history,
   merging, macros, clean-state tracking). Model mutation only happens through commands.
6. **Fail gracefully, explain in Diagnostics.** `lf::Error` separates a user message from technical
   detail; the detail goes to the log and the Diagnostics window, not to dialogs.

## Module map

```
src/
  core/         Time (flicks), Rational, Result/Error, Log (sinks: file, debugger, ring buffer), StringUtil, Version
  hardware/     GpuDevice (the one D3D11 device), HardwareDetector (DXGI adapters, decoder profiles,
                encoder probes), HwCapabilities (report types)
  rendering/    PreviewRenderer (D3D11 flip-model swap chain on a native HWND), FrameLayout (letterbox math)
  commands/     Command, UndoStack
  diagnostics/  SystemInfo (CPU/RAM/OS/build facts)
  ui/           Theme (tokens -> QPalette + QSS), Icons (recoloured SVG), ActionRegistry (all actions
                and shortcuts), panels (Media, Inspector), preview (PreviewWidget, PreviewPanel),
                timeline (TimelineView, TimelinePanel), dialogs (Diagnostics), MainWindow
  app/          main.cpp: logging, GPU device, window, async hardware detection
tests/          engine unit tests (zero-dependency harness)
resources/      theme/lumaforge.qss, icons/*.svg
tools/          toolchain fetch/setup/diagnose scripts (see ../TOOLS.md)
```

Dependency direction: `app -> ui -> {rendering, hardware, commands, diagnostics} -> core`.
`rendering -> hardware` (shares `GpuDevice`). FFmpeg is linked by `hardware` (encoder probes,
hwdevice enumeration) and `diagnostics` (versions); the media engine will link it too.

## Threading model (target)

| Thread | Owns | Notes |
|---|---|---|
| UI (Qt main) | widgets, document model, `UndoStack`, `ActionRegistry` | never blocks on I/O or decoding |
| Render | D3D11 immediate context use for the preview | today: on-demand from the widget; becomes a dedicated thread with the playback engine |
| Playback engine | master clock, frame scheduling, drop policy | next milestone |
| Decoder (per active source) | FFmpeg demux/decode -> bounded `FrameQueue` | D3D11VA or software |
| Worker pool | probing, thumbnails, waveforms, proxies | `N-1` threads |
| Export | separate decode+encode pipeline | never shares the preview decoder |
| hw-detect | `HardwareDetector::detect()` at startup | result posted to `MainWindow` |

`GpuDevice` is shared by the decoder, renderer and future compositor. `ID3D11Device` is
free-threaded; immediate-context use is serialised with `GpuDevice::contextLock()`, and the same
lock will be handed to FFmpeg's `d3d11va` hwcontext through its lock/unlock callbacks. The device
has `ID3D10Multithread` protection enabled for this reason.

## Preview path

```
PreviewWidget (QWidget, WA_PaintOnScreen + WA_NativeWindow, no Qt backing store)
   -> PreviewRenderer (IDXGISwapChain1, FLIP_DISCARD, B8G8R8A8)
        -> clear surround, ClearView(frame rect)          [today]
        -> NV12/P010 texture -> YUV->RGB pixel shader     [media milestone]
```

If `GpuDevice::create` fails or the swap chain is lost irrecoverably, `PreviewWidget` switches to a
`QPainter` fallback that draws the identical layout via `render::fitFrame`. The status bar shows
which backend is active; Diagnostics explains why.

## Time

`lf::TimePoint` / `lf::Duration` / `lf::TimeRange` in `core/Time.h`, all in flicks.
Helpers: `frameDuration(fps)`, `frameIndexAt`, `timeOfFrame`, `snapToFrame`,
`ticksFromTimeBase` / `timeBaseFromTicks` for FFmpeg `AVRational` time bases. Frame rates are
`lf::Rational` (`fps::f29_97 == 30000/1001`).

## Actions and shortcuts

`ui/ActionRegistry.cpp` holds the single table of `ActionId -> {text, icon, shortcut, tip}`.
Menus, the toolbar, panel headers and transport buttons all display the same `QAction` objects, so
enabled state, labels and shortcuts are consistent by construction. Shortcut data is stored as
plain values and converted to `QKeySequence` at runtime (standard-key sequences need the platform
theme, which does not exist during static initialisation).

Known follow-up: single-key shortcuts (`S`, `N`, `K`, arrows, Space) are window-wide; a focus-aware
filter must suspend them while a text field is being edited (media rename, project name).

## Timeline view

`TimelineView` is a hand-painted widget: track headers, ruler, lanes, playhead, hover guide.
Zoom keeps the time under the cursor fixed; the wheel scrolls horizontally, Ctrl+wheel zooms.
The playhead and hover guide repaint only their own columns (`update(rect)`), so scrubbing does
not repaint the tracks. Ruler tick spacing is chosen from frame-based and second-based candidates
to keep labels >= 90 px apart at any zoom. Clips plug into `paintTracks()` once the timeline model
exists; the coordinate system (`xFromTicks`/`ticksFromX`) is final.

## Logging and diagnostics

`lf::log::Logger` fans out to a file (`<exe>/logs/lumaforge.log`, share-readable), the debugger and
a ring buffer. The Diagnostics window (View -> Diagnostics, or `LumaForge.exe --diagnostics[=graphics|log]`)
shows System, Graphics & Acceleration (with a plain-language verdict) and the live log, and can copy
the whole report to the clipboard. Settings are stored in `<exe>/LumaForge/LumaForge.ini`
(portable; keeps the user profile clean during development).

## Planned next (in order)

1. `media/`: `MediaProbe` (metadata), `MediaSource`, `Decoder` interface with D3D11VA + software
   implementations, `VideoFrame` (GPU texture or CPU planes), `AudioBuffer`.
2. `playback/`: `MasterClock`, `FrameQueue`, `FrameScheduler`, seek coalescing, drop accounting;
   the render thread; NV12 shader in `rendering/`.
3. `timeline/`: `Sequence`/`Track`/`Clip` model + edit commands (add, move, trim, split, delete,
   snapping), media bin model, drag & drop.
4. `project/`: versioned JSON, relinking, migrations.
5. `export/`: `ExportJob` with encoder selection from `HwReport`, progress/cancel.
6. `cache/`: thumbnails, waveforms, frame cache budgets; proxy hooks.
