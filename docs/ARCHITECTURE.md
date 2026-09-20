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
  core/         Time (flicks), Rational, Result/Error, Log, StringUtil, Version,
                VisualParams (Transform2D, Crop, ColorAdjust, Effect stack + applyEffects, TextParams)
  hardware/     GpuDevice (the one D3D11 device, WARP fallback), HardwareDetector, HwCapabilities
  media/        MediaProbe, Thumbnailer, VideoDecoder (D3D11VA / software), AudioDecoder (swresample),
                VideoFrame, Waveform (peak envelopes)
  rendering/    FrameTexture pool (NV12/I420/BGRA), Compositor (layers -> render target, HLSL built by fxc),
                PreviewRenderer (flip-model swap chain), TextRasterizer (DirectWrite + Direct2D -> BGRA)
  audio/        AudioOutput (WASAPI shared, event driven)
  playback/     Program (SourceProgram / SequenceProgram), VideoReader (per clip decode thread),
                AudioMixer (gains, fades, speed resampling), PlaybackEngine (render thread, text cache)
  project/      Ids, MediaItem, Sequence (Track/Clip), Project, Document (undo + listeners),
                MediaCommands, TimelineCommands (SequenceEdit diff command), TimelineOps (pure edit
                logic: overwrite/move/trim/split/ripple/paste/duplicate/speed/snap), ProjectSerializer (.lfproj)
  commands/     Command, UndoStack
  diagnostics/  SystemInfo, CrashHandler
  ui/           Theme, Icons, ActionRegistry (all actions, shortcuts, tools), MainWindow,
                panels (Media, Inspector), inspector/ (sections: clip, text, typography, transform,
                crop, color, effects, audio, speed), preview/, timeline/ (view, panel), media/
                (model, thumbnails, waveforms, import), project/ (ProjectController), dialogs/, widgets/
  app/          main.cpp: logging, GPU device, window, CLI (--timeline, --source, .lfproj)
tests/          engine unit + integration tests (real media in tests/media)
resources/      theme/lumaforge.qss, icons/*.svg, shaders/*.hlsl
tools/          toolchain fetch/setup/diagnose scripts (see ../TOOLS.md)
```

Dependency direction: `app -> ui -> {playback, project, rendering, media, hardware, commands,
diagnostics} -> core`. `playback -> project` (SequenceProgram snapshots the project). Only `ui/`
and `app/` link Qt.

## Editing model

All timeline state is plain data (`project::Sequence`). Every edit is computed by a pure function in
`project::ops` as a `SequenceEdit` (clips added / removed / replaced, tracks replaced) and executed
by one command type, `SequenceEditCommand`, which captures the before-state on first run and can
coalesce consecutive edits of the same control (`mergeId` = property x gesture). The Inspector's
sections only ever call `InspectorContext::editSelection` / `pushEdit`; the timeline view only ever
calls `ops::*` + `commit`. Nothing in the UI mutates the project directly.

Playback consumes an immutable `SequenceProgram` built from the project after each change (one
rebuild per event-loop turn). Effects are flattened into the shader's colour parameters + looks at
that point; text layers are rasterised lazily on the render thread and cached by content hash.

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

`TimelineView` is a hand-painted widget: track headers (visibility/mute/solo/lock toggles), ruler,
lanes, clips (kind colour, name, duration, poster frame, real audio peaks shaped by gain), playhead,
snap guide, marquee. Zoom keeps the time under the cursor fixed; the wheel scrolls horizontally,
Ctrl+wheel zooms. Repaints are regional: playhead columns, changed clip rects on selection, lane
rows on edits, the ruler only when the sequence end moves. Tools (Select / Blade) are a property of
the view set through the ActionRegistry's exclusive tool actions; gestures (move, trim, marquee,
scrub) preview through `ops::applied()` and commit one command on release.

## Logging and diagnostics

`lf::log::Logger` fans out to a file (`<exe>/logs/lumaforge.log`, share-readable), the debugger and
a ring buffer. The Diagnostics window (View -> Diagnostics, or `LumaForge.exe --diagnostics[=graphics|log]`)
shows System, Graphics & Acceleration (with a plain-language verdict) and the live log, and can copy
the whole report to the clipboard. Settings are stored in `<exe>/LumaForge/LumaForge.ini`
(portable; keeps the user profile clean during development).

## Planned next (in order)

1. `export/`: compositor readback -> libx264/AAC -> MP4 with progress/cancel; encoder choice from `HwReport`.
2. Transitions (cross dissolve) and a performance monitor.
3. Frame cache / proxies for heavier sources; time-stretch (pitch-preserving) for speed changes.
