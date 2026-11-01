# Unified native pages and lyric motion

User feedback is concrete: inspect latest screenshots, make Library and Settings match the approved Player layouts, reduce visual clutter, replace abrupt lyric text swaps with scrolling. Existing three-theme approval stands; this is a correction within that design, implemented directly.

Evidence: four actual Switch screenshots in build/diagnostics/device-0.5.0/202610302012*.jpg and 2026103020130200*.jpg. Library/Settings have a separate two-tier old header, many filled controls and a loud persistent transport. Player has a compact centered segmented header and editorial whitespace. Timed lyrics directly replace before/current/after at Current() boundaries, and main redraws statically every 100ms. Device log confirms 0.5.0 runs and loads artwork/73 lyric rows, but cannot prove next revision's visual or performance acceptance.

Use the selected landing-page-design skill's visual principles adapted to native Switch: one dominant task, flat theme colors, consistent spacing, readable system fonts, quiet secondary actions, large touch targets. Preserve native audio/network behavior and existing installation.

- One shared compact header on every page, matching Player's 64px high centered navigation. A quiet mini transport for child/library/settings pages.
- Library: narrow source rail, restrained Songs/Albums/Artists selector and search; spacious rows or album tiles. Avoid a wall of equally prominent action buttons.
- Settings: four sections Playback/Appearance, Music Sources, Storage/Maintenance, Playlists. Show only the selected section's controls. Preserve every existing action, real source management, volume/mode/cache/import/export, three-theme choice, touch and controller access.
- Lyrics: monotonic-clock 500ms eased vertical movement with opacity/scale focus on current line; clipped within its region, smooth in all three Player layouts and the expanded lyric page. Plain/missing lyrics stay honest. Track/theme/page/document changes and seeks reset obsolete transitions. Pause must not advance lyrics; no animation work forever when idle.
- Main loop requests additional paints only while a lyric transition is active. No file I/O, network calls, sleeping or image decoding in drawing. Existing caching and background persistence remain.
- Version 0.5.1 in CMake twice and log minor. Build, validate, and original-path FTP deployment if reachable; full readback hash and log clearing. Current user has already authorized this ongoing on-device correction workflow.

Acceptance: all pages share header/font/palette/transport language; settings is meaningfully quieter; deterministic animation tests show intermediate positions and settle; real native PNGs and frame sequence verify geometry/clipping; full host tests and Switch build pass; deployment evidence separate from subsequent device acceptance.
