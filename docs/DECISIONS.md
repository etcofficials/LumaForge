# Architecture Decision Records

Short entries: context, decision, consequences. Newest last.

## ADR-001 — Portable MSVC + Windows SDK on G: instead of the Visual Studio installer
**Context.** C: has very little free space. The official VS Build Tools installer pins ~2–2.5 GB to
C: (the Installer itself and the Windows SDK) with no supported relocation.
**Decision.** Download the official MSVC/SDK packages from the VS release manifest and extract them
to `G:\DevTools` (`tools/fetch-portable-msvc.py`). Session-only environment scripts; no registry or
system PATH changes.
**Consequences.** Zero C: footprint; unsupported layout (no vswhere/IDE integration); fully
reproducible from scripts. Documented in `TOOLS.md`.

## ADR-002 — Qt 6 Widgets, not Qt Quick
**Context.** Pro-tool layout (panels, splitters, custom-painted timeline), low GPU cost for UI on a
weak GPU, and the video surface must be a native D3D11 window independent of Qt's rendering.
**Decision.** Qt Widgets with Fusion style + token-driven stylesheet; custom `QWidget` painting for
the timeline; native child window for video.
**Consequences.** Predictable performance, no QML runtime, no Qt RHI/scenegraph contention with our
D3D11 device. Animations are minimal by design.

## ADR-003 — Time in flicks (1/705,600,000 s), frame rates as exact rationals
**Context.** NTSC rates (30000/1001) and mixed audio sample rates make floating-point or
millisecond timelines drift and mis-snap.
**Decision.** `int64` flicks for every timeline value; `Rational` for rates; conversion helpers for
FFmpeg time bases.
**Consequences.** Exact frame math for all common rates; trivially correct snapping; large but safe
range (~414 years).

## ADR-004 — One shared D3D11 device; preview via flip-model swap chain on a native HWND
**Context.** Frames decoded by D3D11VA live in VRAM; copying them back for Qt to paint would cost
the CPU budget we do not have on the target machine.
**Decision.** `GpuDevice` is created once and shared by decoder, renderer and compositor.
`PreviewWidget` opts out of Qt's backing store (`WA_PaintOnScreen`) and `PreviewRenderer` presents
through `IDXGISwapChain1` (FLIP_DISCARD). Software `QPainter` fallback keeps the app usable without
a GPU.
**Consequences.** Zero-copy path from decoder to screen; immediate-context access must be
serialised (`contextLock`); device-loss handling is explicit.

## ADR-005 — Capability detection by attempt, not by name
**Context.** The development machine's "GT 730" is the Fermi variant with a final driver from 2018:
no NVENC, no usable NVDEC via modern FFmpeg, but D3D11VA H.264 works. Name-based heuristics would be
wrong on the very first target.
**Decision.** `HardwareDetector` enumerates DXGI adapters, creates a D3D11 device, checks decoder
profiles with `CheckVideoDecoderFormat`, and probes each H.264 encoder by opening it with FFmpeg.
Results are logged, shown in Diagnostics, and drive the status bar wording ("CPU encode").
**Consequences.** Honest UI; ~60 ms startup work on a background thread; encoder probe reasons are
captured from FFmpeg's log for the report.

## ADR-006 — FFmpeg GPL shared build for development
**Context.** The BtbN `win64-gpl-shared` build includes libx264 (the CPU H.264 encoder fallback) and
D3D11VA/DXVA2 hwaccels; an LGPL build lacks x264.
**Decision.** Use the GPL build now; keep the encoder choice behind the export backend so an LGPL
build (with `h264_mf` or OpenH264) is a fetch-script switch, not a code change.
**Consequences.** Distribution under a non-GPL licence requires the swap before release.

## ADR-007 — Command-based editing with a lazy macro insertion
**Context.** Undo/redo must be architectural. Interactive gestures (drags) often begin a macro
that ends up empty.
**Decision.** `UndoStack` inserts a macro into history only on its first real push, so an empty
macro never truncates the redo branch. Consecutive commands can coalesce via `mergeId`.
**Consequences.** Safer redo history; simple rule for future edit operations.

## ADR-008 — Static libraries per module, Qt-free engine
**Context.** Testability and keeping engine code independent of the UI toolkit.
**Decision.** Each module is a static lib with `include/lumaforge/<module>/` public headers; only
`ui/` and `app/` link Qt. Tests link engine libs directly.
**Consequences.** Slightly more CMake; clean dependency direction; the media engine can be
benchmarked headless.

## ADR-009 — One generic timeline command over a computed diff
**Context.** Move, trim, split, delete, ripple, paste, duplicate, speed and every Inspector slider
all need undo; per-operation command classes would multiply state-capture bugs.
**Decision.** `ops::*` are pure functions returning a `SequenceEdit`; `SequenceEditCommand` applies
it, records the before-state on first execution and merges consecutive edits with the same
`mergeId`. A merge is refused across the saved (clean) state.
**Consequences.** Undo correctness is checked in one place; new operations are functions with unit
tests of exact numbers; the UI never touches the project directly.

## ADR-010 — Effects as a flattened stack, rendered in the existing layer shader
**Context.** The GT 730 cannot afford a multi-pass filter chain per layer at 30 fps.
**Decision.** A clip carries an ordered list of `Effect{type, enabled, amount}`. At program-build
time `applyEffects` folds it into the `ColorAdjust` the shader already applies plus a small
`EffectParams` (grayscale/sepia mixes, single-pass 12-tap blur, unsharp-mask sharpen, vignette).
**Consequences.** New effect types are data + a few shader lines; blur quality is bounded by the
single pass (acceptable at preview sizes; measured 30 fps with blur + sepia + colour).

## ADR-011 — Text rasterised by DirectWrite/Direct2D into a WIC bitmap, cached by content hash
**Context.** Titles need real typography (kerning, fallback fonts, outlines) without touching the
GPU state of the compositor or re-rendering every frame.
**Decision.** `TextRasterizer` lays out with DirectWrite, converts glyph runs to Direct2D geometry
(fill / outer stroke / jittered shadow / background box) into a premultiplied BGRA WIC bitmap sized
to the frame; the engine uploads it once and keeps an LRU of six textures keyed by hash.
**Consequences.** Device independent, works on WARP; text costs nothing per frame until it changes;
shadow blur is an approximation (no D2D effects on a WIC target).
