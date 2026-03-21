# nowplaylisting

`nowplaylisting` is an OBS source plugin (Windows, OBS 30+) that plays a playlist of media files/folders.

## Implemented v1 behavior

- Source type with OBS media transport controls (play/pause/stop/next/previous/seek/time).
- Playlist property with add/remove/reorder (editable list).
- Playlist entries accept both file and folder paths.
- Supported media extensions:
  - Audio: `.mp3`, `.wav`, `.aiff`, `.aif`
  - Video: `.mp4`, `.mpg`, `.mpeg`, `.mkv`, `.avi`
- Recursive folder scan toggle.
- Shuffle + loop support, with reshuffle on each completed loop pass.
- Metadata extraction on Windows (`artist`, `title`) from shell property store.
  - If metadata is missing, text is left empty.
- Audio files:
  - Attempts album-art extraction and renders it as background.
  - Renders artist/title as two centered lines with style controls.
- Video files:
  - Plays video only (no metadata text overlay).
- Source output canvas defaults to `480x480` (`1:1`), with media fit-to-edge scaling that preserves media aspect ratio and leaves unused area transparent.
- Text style controls:
  - Font (includes size), color, outline color/size, shadow color/size, X/Y offsets.
- Text font size values are internally scaled by `3x` for this source so practical on-screen sizing maps to more typical UI font numbers.
- Source properties update live while editing (no `OK` required to preview most changes).

## Build

Prerequisites:

- Windows
- OBS 30.x development environment (`libobs` CMake package available)
- CMake 3.28+
- MSVC toolchain

Configure and build:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config RelWithDebInfo
```

Install layout is configured for OBS plugin paths:

- Binary: `obs-plugins/64bit`
- Data: `data/obs-plugins/nowplaylisting`

## Post-Objective Backlog

1. Remove the standalone `Recursive` option from source properties.
2. Add `Add Recursive` to the playlist `Add` context menu (alongside `Add Files` and `Add Folder`).
3. Make both `Add Folder` and `Add Recursive` enumerate matching media files immediately and add file entries individually to the playlist (no persistent folder entry rows).
4. Add `Artist` and `Title` columns to the playlist UI.
5. Persist extracted metadata in playlist storage when new items are added, and make playlist `Artist`/`Title` cells editable with changes saved.
6. During playback, use `Artist`/`Title` from the saved playlist metadata first (not direct shell extraction each time), with shell extraction only as import/default behavior.
7. In the listview rework, add a per-track `Show Tags` checkbox that controls whether artist/title text is rendered for that specific track during playback.
