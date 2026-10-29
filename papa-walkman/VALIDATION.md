# Software validation — 0.2.0

Date: 2026-10-29 (Asia/Hong_Kong).

## Confirmed

- Built ARM64 NRO with devkitA64 GCC 16.1.0, libnx, CMake and Ninja using `build.ps1`.
- Native package title is 爬爬的随身听, version 0.2.0; all 16 title slots use UTF-8. Menu icon is a 256×256 JPEG.
- `tools/test.ps1`: 66 core checks, 16 real MP3 decoder checks, playback engine integration suite, and UI suite passed.
- Resampling covers 22050/44100/48000/96000Hz mono and stereo, split input blocks, output block continuity and EOF sample accounting.
- Engine tests decode actual synthesized MP3 through the real resampler, controlled output queue and player state machine. Covered pause buffer ownership, consumed-frame progress, seek clearing, EOF drain, repeated completion events after seek and output failures.
- Library/config tests cover limits, nesting, hidden/unsupported files, uppercase suffix, settings clamps, replacement and backup recovery.
- UI tests cover song selection, offset rows, seeking bounds, current-track touch pause, empty library and render memory guards.
- Playing, paused, empty and error screens rendered with the actual UI implementation and visually inspected. The preview uses sample names and Windows fonts, not a Switch screenshot.
- Independent read-only review found one P2 completion-deduplication issue; a regression failed first, then passed after correction. No other critical or important finding remained.
- Source and install ZIP integrity and SHA256 manifests checked. Old app remains recoverable from the local pre-rebuild backup.

## Hardware validation still required

This build has not been opened on the user's Switch. Actual sound quality, device routing/headphones, underruns, focus/HOME/sleep behavior, touch feel, sustained performance and long sessions remain unverified. A software test pass is not a hardware acceptance result.

Large scans run on a separate thread; quitting waits for a scan to finish. Scans are limited to eight directory levels and 10000 MP3 tracks but directories containing many unrelated files may take longer.
