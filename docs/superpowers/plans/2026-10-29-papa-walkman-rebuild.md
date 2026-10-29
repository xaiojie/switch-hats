# Papa Walkman Rebuild Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task. User selected direct execution with implementation details delegated.

**Goal:** Deliver a beautiful local MP3 player NRO with Chinese UI and original icon.

**Architecture:** Portable queue/library/resampling/decoder core; a single-owner audio worker with libnx output; software UI shared by Switch and desktop render tests.

**Tech Stack:** C++17, devkitA64 16.1.0, libnx, minimp3, stb_truetype, CMake/Ninja, native Windows build script.

**Spec:** `docs/superpowers/specs/2026-10-29-papa-walkman-rebuild-design.md`

## Global Constraints

- Only replace `papa-walkman/`; preserve complete old project in local backup.
- MP3 only; music in `sdmc:/music`; no network or game installation changes.
- 1280×720 UI; shared system Chinese font; embedded original 256×256 icon.
- Fixed 48000Hz stereo output; streaming decode; audio worker owns decoder and output buffers.
- No automatic device deployment, service restart, or GitHub push.

## Review Focus

- Empty/all-invalid libraries must remain responsive and never loop indefinitely.
- Pause/seek/switch must preserve audio-buffer ownership and end-of-track draining.
- Mono and 22050/44100/48000/96000Hz input must preserve samples across output blocks.
- Scanning hidden files, symlinks, nested directories and uppercase suffixes must be bounded and deterministic.
- Long UTF-8 titles and touch gestures must stay within bounds and not trigger unintended playback.

### Task 1: Portable playback foundation

**Files:** `papa-walkman/source/core.{hpp,cpp}`, `decoder.{hpp,cpp}`, `tests/core_tests.cpp`, `tests/decoder_tests.cpp`.

**Interfaces:** `Queue` selects indices and next/previous/end behavior; `ScanLibrary(root, limit, depth)` returns tracks; `Resampler::Push/Pull` yields stereo frames; `Decoder::Open/Read/Seek` owns FILE callbacks.

- [x] Write failing tests for queue boundaries/shuffle, scan exclusions, config clamps and resampling counts/continuity.
- [x] Implement core and run native host tests.
- [x] Generate a short local synthetic MP3 fixture and test streaming decode/seek and invalid file handling.

### Task 2: Interface and application integration

**Files:** `source/ui.{hpp,cpp}`, `source/audio.{hpp,cpp}`, `source/main.cpp`, `tools/preview.cpp`.

**Interfaces:** `Render(Canvas&, View)` uses core track/snapshot data; `HitTest` returns UI action and optional song index; `AudioPlayer` exposes commands and mutex-protected `Snapshot`.

- [x] Write failing UI bounds/touch-hit tests.
- [x] Implement reusable renderer with cached text and animated reels; render playing/paused/empty screenshots using Windows fonts.
- [x] Implement Switch output with three aligned buffers, stop/flush on replacement, drain on EOF, pause/resume without recycling queued buffers.
- [x] Connect async scanning, touch, controller input, config persistence and error states.

### Task 3: Build, package and review

**Files:** `CMakeLists.txt`, `build.ps1`, `build.sh`, `tools/test.ps1`, `README.md`, `assets/`, `releases/0.2.0/`.

- [x] Create portable CMake/Ninja build and embed icon/NACP.
- [x] Run all player tests, compile actual ARM64 NRO, inspect metadata and hashes.
- [x] Review source and fix important findings; use fresh reviewer only for whole-project review as required by execution skill.
- [x] Package NRO, source/build instructions, checksums and tested UI previews; report hardware verification status explicitly.

## Execution ledger

- Ruling: work directly on new branch `feat/papa-walkman-rebuild` in the user-named repository; user delegated details and requested replacement there. No push.
- Ruling: retain public-domain minimp3 dependency from archive, replace all old application code and design.
- Baseline: old player had no automated tests; repository was clean at `ffe67f4`.
- Task 1: complete — queue/resampler/config tests first failed at initial next-track behavior; decoder first failed on missing error behavior, then passed actual sine-tone decode/seek/cancellation.
- Task 2: complete — touch test first failed on main playback target, then passed. Real Engine/output-queue tests first failed opening a track, then passed pause preservation, consumed-frame progress, seek reset and EOF drain.
- Final: fixed Switch config replacement semantics with backup recovery — recovery test RED→GREEN. Same-volume and overwritten config behavior covered.
- Final: fixed main touch pause changing selected track — current-track target test RED→GREEN.
- Final review: independent read-only review by walkman_review found one remaining P2 EOF deduplication issue; no critical memory/threading issue.
- Final: fixed EOF event reuse after seek by monotonic completion event — regression first failed at completion event missing; final suite recorded below.
- Final: Ruling: no explicit PCM cache flush added — official audout playtone example uses the same write/submit path, so review found no verified defect. Native audio output still requires hardware testing.
- Final: Ruling: large-directory scans remain bounded by depth/track count but can take longer for many unrelated files — this is acceptable for the basic version; scan runs away from the UI thread. Shutdown may wait for a scan to finish.
- Final: Ruling: desktop rendering does not establish native sound quality, power/focus behavior or performance — report those as unverified until hardware testing.
- Final verification: `tools/test.ps1` passed 66 core checks, 16 decoder checks, engine integration tests and UI tests. Final ARM64 build passed without application warnings. NRO title/version/all 16 language entries and 256×256 embedded JPEG validated. All four same-code UI previews visually inspected.
- Delivery: local branch `feat/papa-walkman-rebuild` retained for user hardware testing; no integration or publication requested in this task.
