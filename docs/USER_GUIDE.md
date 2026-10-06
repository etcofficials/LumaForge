# LumaForge 0.4 — User Guide

LumaForge is a video editor for Windows built to stay smooth on modest PCs
(it is developed on a quad-core i5 with a GeForce GT 730). Everything below
describes features that exist in this version.

## Getting started

1. Double-click **LumaForge Launcher.exe** (or the LumaForge shortcut).
2. On the Home screen choose **New Project**, **Open Project** or a recent
   project. If LumaForge did not close properly last time, a banner offers to
   **recover** your unsaved work.
3. Import media: drag files from Explorer into the Media panel, or
   **File > Import Media… (Ctrl+I)** / **Import Folder… (Ctrl+Shift+I)**.
4. Drag clips to the timeline, edit, then **Export (Ctrl+M)**.

Your data lives in a `LumaForgeData` folder on the drive LumaForge runs from
(for example `G:\LumaForgeData`): Projects, Exports, Proxies, caches,
Autosave, Backups, Recordings and Logs. Nothing is written to your user
profile. **File > Settings… (Ctrl+,)** can move these folders.

## Workspaces

The tabs under the toolbar arrange the panels for a task:
**EDIT**, **COLOR** (scopes + colour tools), **AUDIO** (mixer), **TEXT**
(titles), **EFFECTS** and **EXPORT** (export settings + render queue).
**Workspace > Reset** restores a workspace's layout. Any panel can be shown
from **View > Panels** (for example **Performance**).

## Media panel

Grid or list view, search, type filter (video / audio / images / unused /
missing files) and **bins** (folders: right-click > Move to Bin, or the folder
button). The small blue tab on a thumbnail means the clip is used in the
timeline; **P** means a proxy exists. Right-click for Rename, Relink, Reveal in
Explorer, Properties, Create/Delete Proxy and Remove. Double-click opens the
clip in the source preview.

## Timeline editing

Tools: **V** select, **B** blade, **R** trim (ripple/roll), **H** hand,
**Z** zoom, **T** text. Common keys:

| Action | Key |
|---|---|
| Play / pause, shuttle | Space, J / K / L |
| Frame step, 10 frames | ← →, Shift+← → |
| Previous / next edit | ↑ ↓ |
| Mark In / Out, clear | I / O, Ctrl+Shift+X |
| Split at playhead | S |
| Delete, ripple delete | Delete, Shift+Delete |
| Copy / cut / paste (insert) / paste over | Ctrl+C / X / V, Ctrl+Shift+V |
| Duplicate | Ctrl+D |
| Undo / redo | Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z |
| Lift / extract In–Out | ; / ' |
| Nudge | Alt+← →  (Alt+Shift for 10 frames) |
| Enable / disable clip | Shift+E |
| Add marker, next / previous marker | M, Shift+M, Ctrl+Shift+M |
| Add transition at nearest edge | Ctrl+Shift+D |
| Frame hold (2 s) | Shift+H |
| Add text / subtitle | Ctrl+T, Shift+T |
| Command palette | Ctrl+K |
| All shortcuts (editable) | F1 |

Tracks: add, rename, reorder, lock, mute/hide, solo and resize them from the
track header (right-click for more). **N** toggles snapping.

## Inspector

Shows the properties of the selected clips, in tabs: **Video** (crop, mask,
solid/adjustment settings), **Transform** (position, scale, rotation,
opacity, flips, Fit/Fill/1:1, picture-in-picture corners), **Color**,
**Effects**, **Audio**, **Speed** (presets, custom, reverse), **Text** and
**Transition**. With several clips selected only the shared properties are
shown and changes apply to all of them.

**Keyframes**: the diamond beside a slider adds a keyframe at the playhead.
Once a property has keyframes, changing it at another time adds another key.
Right-click the diamond for interpolation (linear, smooth, hold), previous /
next keyframe and Clear Animation. Key marks appear on the clip.

## Effects, transitions and titles

* **Effects** browser: colour (exposure, temperature, tint, hue shift,
  vibrance, brightness, contrast, saturation), blur & sharpen, and stylize
  (grayscale, sepia, vignette, invert, posterize, pixelate). Drag onto a clip
  or select clips and press Apply.
* **Transitions** browser: dissolves, dips to black/white, wipes, slides,
  pushes and zoom. Drag onto a clip edge or use Ctrl+Shift+D. A transition
  between two touching clips is centred on the cut.
* **Titles** browser: title, subtitle, lower third, caption, chapter,
  callout and quote presets.
* **Masks** (Video tab): rectangle or ellipse, feather, invert.
* **Adjustment clips** (Timeline > Add Adjustment Clip): their colour,
  effects, crop and mask apply to everything below — for example a masked
  blur over part of the picture. **Solid colour** clips are also available.

## Subtitles

**Text > Import Subtitles…** reads `.srt` or `.vtt` onto a subtitle track
(marked CC); **Text > Add Subtitle (Shift+T)** adds one; **Export
Subtitles…** writes `.srt` or `.vtt`. At export you can burn them into the
picture and/or save an `.srt` next to the video.

## Audio

Per clip: volume (keyframeable), mute, fades, pan, 3-band EQ, noise gate,
compressor and **Normalize** (-1 dBFS). The **Mixer** (AUDIO workspace) has a
fader, pan, mute and solo per audio track, live meters, and the master volume
with a limiter. **Audio > Record Voice-over… (Ctrl+Shift+R)** records from the
microphone at the playhead (optionally while the timeline plays — use
headphones) and places it on a free audio track.

## Colour

The **COLOR** workspace shows scopes for the current frame: luma waveform,
RGB parade, vectorscope (with 75 % targets and a skin-tone line) and
histogram. Colour correction is in the Inspector's Color tab
(Copy/Paste between clips).

## Smooth playback on slower PCs

* Preview quality (toolbar or Playback menu): Auto, Full, ½, ¼, ⅛.
* **Proxies**: right-click media > Create Proxy (a small 540p copy), or use
  the Performance panel's "Create Proxies for These" for files your graphics
  card cannot decode (for example HEVC/H.265 or 4K on a GT 730). Turn them on
  with **Playback > Use Proxies for Preview (Ctrl+Alt+P)**. Exports always use
  the original files.
* The **Performance** panel (click the fps/hardware chips on the toolbar)
  shows what is limiting playback and how big each cache is.

## Exporting

**Export (Ctrl+M)** opens the EXPORT workspace. Pick a preset (YouTube
1080p/720p, high quality, small web, fast draft, HEVC, MKV archive) or set
format (MP4/MOV/MKV), codec (H.264, HEVC), encoder, resolution, quality or
bitrate, speed, audio and range (whole timeline or In–Out). The summary shows
the size, length, estimated file size and free disk space; problems such as
missing media block the export with an explanation. Press **Export**: jobs run
one after another in the **Render Queue** with progress, speed and time left;
when finished use **Show in Folder** or **Play**. **File > Export Frame…
(Ctrl+Shift+E)** saves the current frame as PNG.

## Safety nets

* Every edit can be undone (Edit menu shows what will be undone/redone).
* **Autosave** every 2 minutes while there are unsaved changes (Settings).
* **Recovery**: after a crash the Home screen offers your last autosave.
* **Backups**: saving keeps the previous version of the project in
  `LumaForgeData\Backups` (newest 10).
* Crash reports (`crash-*.dmp`) and the log are in `LumaForgeData\Logs`.

## Troubleshooting

* *Playback stutters*: lower the preview quality or create proxies.
* *"Media offline"*: right-click the item > Relink.
* *Black preview / graphics problems*: Settings > Preview > turn off hardware
  decoding (restart), or start with `--software-preview`.
* *Help > System Information* shows what LumaForge detected about your PC.
