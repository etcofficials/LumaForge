# LumaForge — notes for Claude Code sessions

## Hard rules
- **Never install, download, extract or cache anything on C:.** Everything lives on G: (see TOOLS.md).
  Set `TEMP`/`TMP` to `G:\Caches\tmp` for long-running processes. No system PATH / registry changes.
- The UI thread never decodes, renders video, probes files or blocks on I/O.
- Hardware capabilities are measured (`hardware/HardwareDetector`), never inferred from GPU names.
- Timeline time is `lf::TimePoint` in flicks; frame rates are `lf::Rational`. No doubles in edit math.
- Every document edit is a `lf::cmd::Command` pushed to the `UndoStack`.
- Engine modules (`core`, `hardware`, `rendering`, `commands`, `diagnostics`, future `media`,
  `timeline`, `playback`, `project`, `export`, `cache`) are Qt-free. Only `ui/` and `app/` link Qt.
- Shortcuts and action labels live only in `src/ui/ActionRegistry.cpp`.
- Do not construct Qt objects that need the application (e.g. `QKeySequence(StandardKey)`) at
  static-initialisation time.

## Build / run
```powershell
. G:\LumaForge\tools\setup-environment.ps1   # required in every new shell (PowerShell 5.1)
cmake --preset debug ; cmake --build --preset debug
.\build\debug\bin\lf_tests.exe
.\build\debug\bin\LumaForge.exe --diagnostics=graphics
```
- PowerShell 5.1: save `.ps1` files as UTF-8 **with BOM** and ASCII punctuation only; `$Args` is a
  reserved automatic variable; native tools that log to stderr trip `$ErrorActionPreference='Stop'`.
- Toolchain check: `tools/diagnose-toolchain.ps1` (all checks must pass before blaming the code).
- Logs: `build/<preset>/bin/logs/lumaforge.log` (share-readable). Settings: `build/<preset>/bin/LumaForge/LumaForge.ini`.
- `build/<preset>/bin` is self-contained (Qt + plugins + CRT + FFmpeg deployed by `cmake/LumaForgeDeploy.cmake`);
  the exe must launch from a shell WITHOUT the env script. Exit 0xC0000135 = a DLL is missing from bin.
  `--software-preview` forces the QPainter preview path (GPU stays the default).

## Target machine facts (verified)
GT 730 = Fermi GF108 (`10DE:0F02`), driver 391.35 (final). D3D11 FL 11_0 works; D3D11VA H.264/
MPEG-2/VC-1 decode yes; HEVC/VP9/AV1 no; no NVENC; `h264_nvenc` fails to load; export uses libx264.
CPU i5-2400S: 4C/4T, AVX but no AVX2.

## Where things are
- Architecture: `docs/ARCHITECTURE.md`; decisions: `docs/DECISIONS.md`; toolchain: `TOOLS.md`.
- Milestone order: media engine -> playback -> timeline model -> project -> export -> cache/proxies.
