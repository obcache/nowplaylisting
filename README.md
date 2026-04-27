# NowPlaylisting

NowPlaylisting is an OBS source plugin for Windows (OBS 30+) that plays music and video playlists with built-in metadata text overlays, album art rendering, and performance-friendly motion/visual effects.

## New in This Update

This release focuses on workflow speed, playlist management, and live visual control:

- Saved playlist manager in source properties:
  - `New`, `Save`, `Rename`, and `Export` JSON playlists.
- Selected-track metadata editor with explicit save:
  - Artist, Title, and per-track `Show Tags`.
- Playlist handling improvements:
  - Move up/down controls.
  - Expand/Collapse extra preview rows for faster inspection in properties.
- Optional text-file outputs for current Artist/Title:
  - Useful for advanced scene compositions and custom text stacks.
- Audio-reactive media motion:
  - Bounce Intensity and Camera Shake Intensity.
- Media-only FX pipeline (does not affect text):
  - Brightness, Contrast, Saturation, Glow, and Vignette controls.

## Full Feature Overview

- Playlist-driven media source with standard OBS media controls:
  - Play/Pause, Stop, Next, Previous, Seek, Time/Duration.
- Supports both audio and video files in one playlist.
- Audio playback with:
  - Album art extraction (when available).
  - Artist/Title text overlays.
- Video playback with:
  - Clean media render (text can be hidden per track).
- 1:1 output canvas (`480x480`) designed for music widgets and social overlays.
- Live property preview for most controls (no reopen required for visual tuning).

## Supported Formats

- Audio: `.mp3`, `.wav`, `.aiff`, `.aif`
- Video: `.mp4`, `.mpg`, `.mpeg`, `.mkv`, `.avi`

## Playlist Workflow

- Add files or folders directly from source properties.
- Folder entries are expanded into individual playable files.
- Optional recursive folder scan.
- Shuffle + loop playback modes.
- Move selected tracks up/down in the playlist.
- Selected-track editor with explicit save:
  - Artist
  - Title
  - Show Tags (per-track text visibility)
- Expand/Collapse extra playlist preview rows in properties for easier review.

## Metadata + Tag Behavior

- On import, metadata is read from Windows shell properties (artist/title).
- Metadata is stored with playlist entries and used during playback.
- Per-track tag visibility (`Show Tags`) lets you hide text for specific tracks.
- Optional export of current Artist/Title to text files for external GDI+/scene workflows.

## Saved Playlists

Built-in saved playlist management in source properties:

- Select a saved playlist from dropdown.
- `New`: create and save current playlist under a name.
- `Save`: overwrite currently selected saved playlist.
- `Rename`: rename the selected saved playlist.
- `Export`: write current playlist to a portable JSON file.

Saved playlists are persisted to a shared JSON store in the plugin config directory.

## Visual + Motion Controls

### Text styling

- Font family/style/size
- Text color
- Outline color/size
- Shadow color/size
- X/Y text offset

### Audio-reactive motion

- Bounce Intensity
- Camera Shake Intensity

### Media-only effects (album art/video block, not text)

- Brightness
- Contrast
- Saturation
- Glow color/size/intensity
- Vignette strength/roundness

## Installation

Install to standard OBS plugin paths:

- Binary: `obs-plugins/64bit`
- Data: `data/obs-plugins/nowplaylisting`

Then restart OBS and add **NowPlaylisting** as a source.

## Build (Developers)

### Requirements

- Windows
- OBS 30.x development environment (`libobs` available to CMake)
- CMake 3.28+
- MSVC (Visual Studio 2022 toolchain)

### Build commands

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config RelWithDebInfo
```
