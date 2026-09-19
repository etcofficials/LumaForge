# LumaForge — Development Toolchain

Everything used to build LumaForge lives on **G:**. Nothing is installed on C:, nothing is
registered with Windows, and no system-wide PATH or registry changes are made. The setup is
reproducible from the scripts in `tools/`.

## Layout

| Component | Version | Location | Source | Fetched by |
|---|---|---|---|---|
| MSVC toolset (x64 host → x64 target) | 14.44 (VS 17.14) | `G:\DevTools\MSVC\<toolset>\` | Official Microsoft packages from the VS release manifest | `tools/fetch-portable-msvc.py` |
| MSVC runtime redistributables (release + debug CRT) | 14.44 | `G:\DevTools\MSVC\Redist\` | same | same |
| Windows SDK (desktop x64 subset) | 10.0.26100 | `G:\DevTools\WindowsSDK\` | same | same |
| CMake | 4.4.x | `G:\DevTools\CMake\` | Kitware portable zip (GitHub releases) | `tools/fetch-portable-tools.ps1` |
| Ninja | 1.13.x | `G:\DevTools\Ninja\` | ninja-build portable zip (GitHub releases) | same |
| FFmpeg dev libraries (shared) | n8.1 | `G:\LumaForge\third_party\ffmpeg\` | BtbN FFmpeg-Builds (`win64-gpl-shared`) | same |
| Qt 6 (qtbase, qtsvg, qttools) | 6.8.3 LTS, MSVC 2022 x64 | `G:\Qt\6.8.3\msvc2022_64\` | Official Qt archives from download.qt.io via `aqtinstall` | `tools/fetch-qt.ps1` |
| Python venv (hosts `aqtinstall` only) | — | `G:\DevTools\pyvenv\` | created from the existing C: Python 3.12 | `tools/fetch-qt.ps1` |
| Git | 2.55 | `G:\Git\` | pre-existing | — |

Caches (safe to delete at any time; scripts re-download what is missing):

| Cache | Location |
|---|---|
| VS manifest + MSVC/SDK packages | `G:\Caches\msvc\` |
| CMake / Ninja / FFmpeg archives | `G:\Caches\downloads\` |
| Qt archives (`aqt --keep`) | `G:\Caches\qt\` |
| pip | `G:\Caches\pip\` |
| TEMP/TMP during setup and dev sessions | `G:\Caches\tmp\` |

Build output: `G:\LumaForge\build\<preset>\` (git-ignored).

## Daily use

Every new PowerShell session (and after a reboot):

```powershell
. G:\LumaForge\tools\setup-environment.ps1
```

This dot-sources `tools/setup-msvc.ps1` (the portable equivalent of `vcvars64.bat`),
then adds CMake, Ninja, Qt and FFmpeg to the session `PATH`, and exports the `LUMAFORGE_*`
variables that `CMakePresets.json` reads. It is session-only and idempotent.

Then:

```powershell
cmake --preset debug
cmake --build --preset debug
```

## Verifying the toolchain

```powershell
G:\LumaForge\tools\diagnose-toolchain.ps1
```

Compiles and runs a Win32 executable (cl/link/rc/mt/SDK), then a CMake+Ninja project that
links Qt 6 Widgets and FFmpeg and runs both. Prints a summary of versions and the exact paths
in use. Exit code 0 means all eleven checks passed.

## Re-creating the toolchain from scratch

```powershell
python G:\LumaForge\tools\fetch-portable-msvc.py --msvc-dest G:\DevTools\MSVC --sdk-dest G:\DevTools\WindowsSDK --cache G:\Caches\msvc
G:\LumaForge\tools\fetch-portable-tools.ps1
G:\LumaForge\tools\fetch-qt.ps1
G:\LumaForge\tools\diagnose-toolchain.ps1
```

The canonical `setup-msvc.ps1` is `tools/setup-msvc.ps1` (used by `setup-environment.ps1`).
A copy is kept at `G:\DevTools\setup-msvc.ps1` for use without the repository.

## Application runtime files (development builds)

- Log: `build/<preset>/bin/logs/lumaforge.log` (opened share-readable; tail it while the app runs).
- Settings: `build/<preset>/bin/LumaForge/LumaForge.ini` (portable; nothing in the user profile).
- `LumaForge.exe --diagnostics[=graphics|log]` opens the Diagnostics window at start.

## Notes and caveats

### Portable MSVC is unsupported by Microsoft
The MSVC and Windows SDK bits are the *identical* packages the Visual Studio installer would
download, extracted without the installer. The Build Tools license terms apply. What you do
not get: `vswhere`, "Developer Command Prompt" shortcuts, Visual Studio IDE integration, or
automatic updates. None of that is required for the CMake + Ninja workflow. If a full Visual
Studio is ever installed, the project builds unchanged — only the environment script differs.

Why: the official installer unconditionally puts the VS Installer (~300 MB) and the Windows
SDK (~1.5–2 GB) on C:, with no supported relocation.

### Windows SDK subset
Only the desktop x64 components are extracted: headers (um/shared/winrt/cppwinrt), x64 libs,
x64 tools (`rc.exe`, `mt.exe`, `fxc.exe`, `signtool.exe`), UCRT headers/libs. No ARM, x86,
App Certification Kit, IoT/Mobile/Team extension SDKs. The list is `SDK_MSIS` in
`tools/fetch-portable-msvc.py`.

### FFmpeg licensing
The BtbN **gpl** build is used because it includes `libx264` (the CPU H.264 encoder fallback)
and D3D11VA/DXVA2 hardware decoding. libx264 is GPL; if LumaForge is distributed under a
non-GPL license, switch to `-FFmpegLicense lgpl` in `fetch-portable-tools.ps1` and use a
different software H.264 encoder (e.g. OpenH264 or Media Foundation `h264_mf`). Only the
export encoder backend is affected; the rest of the code does not care.

### Qt modules
`qtbase` (Core, Gui, Widgets, Network, ...), `qtsvg` (SVG icon plugin — needed for our
vector icons), `qttools` (`windeployqt` for packaging). No Qt Quick / Declarative, no
Multimedia (media handling is FFmpeg), no translations. Qt 6.8.3 is the last open-source
patch release of the 6.8 LTS series.

### Debug builds
Debug executables need `msvcp140d.dll`, `vcruntime140d.dll` and `ucrtbased.dll`.
`setup-msvc.ps1` puts `G:\DevTools\MSVC\Redist\...\debug_nonredist\x64\Microsoft.VC144.DebugCRT`
and `G:\DevTools\MSVC\Redist\debug_ucrt_x64` on the session PATH for this reason.

### Release runtime
The machine already has the VC++ 2015–2022 x64 runtime (14.51) in `System32`. For
distribution, ship the DLLs from `G:\DevTools\MSVC\Redist\<ver>\x64\Microsoft.VC144.CRT\`
next to `LumaForge.exe` (app-local deployment); no installer-side redist needed.

## Target machine (for reference)

- Intel Core i5-2400S (4C/4T, Sandy Bridge — AVX, no AVX2), 12 GB RAM
- NVIDIA GeForce GT 730 **GF108 / Fermi** variant (PCI `10DE:0F02`), 4 GB, driver 391.35 (final Fermi driver)
  - no NVENC (Kepler+ only), no usable NVDEC via modern FFmpeg (driver too old)
  - D3D11VA / DXVA2 H.264 decode expected to work; D3D11 feature level 11_0
- No Intel iGPU exposed → no Quick Sync
- Windows 10 Pro 22H2 (19045)
