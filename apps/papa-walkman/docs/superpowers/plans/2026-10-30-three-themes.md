# Three Themes Implementation Plan

> Execute with superpowers:executing-plans; delegate the independent native UI module and perform a fresh final review.

Goal: Three complete native themes, immediate persistent switching and a compiled 0.5.0 NRO.
Architecture: Settings owns stable Theme enum, View carries it, renderer resolves palette/player geometry. Maintenance persists settings asynchronously. Existing native renderer remains the app runtime.
Spec: ../specs/2026-10-30-three-themes-design.md

## Tasks

- [x] Settings/root: core.hpp/.cpp Theme {AlbumLiner=0,NightRecord=1,LyricTheatre=2}, NormalizeTheme(int), NextTheme(Theme), ThemeName(Theme), Settings.theme. Test legacy/missing/invalid values, round trip and backup in core_tests; maintenance retains latest theme in runtime_tests.
- [x] Native UI worker: ui.hpp/.cpp/ui_views.inc (+ optional new include), tests/ui_tests.cpp, tests/interaction_tests.cpp, tools/preview.cpp. Add View.theme default AlbumLiner, Action::CycleTheme, per-theme shared PlayerLayout rectangles. Distinct player layouts plus full app palettes, settings button at old Tape rect (488,440,212,64). Tests cover hit/seek geometry, all-theme rendering and edge states; previews all themes/pages and compare to references. Build in a separate host output folder to avoid build races.
- [x] Root integration: main.cpp CycleTheme updates settings and queues save(false), right-stick on Settings triggers it; compose supplies theme. interaction.cpp excludes CycleTheme from library rebuild and cancels held touch on theme change. Add theme state to TouchInput in interaction.hpp. UI worker owns corresponding interaction tests.
- [x] Asset: generated transparent vinyl PNG embedded as source/ui_vinyl.inc (owned by root), decoded once in Renderer; dynamic song cover uses existing CoverImage data.
- [x] Verification/root: ASan/UBSan full suite, original build.sh, NRO/NACP checks, fresh read-only review and fixes, native PNG review, docs/validation-0.5.0.md and matching NRO/ELF archive.

## Review focus

Theme switch during pressed/dragged pointer must cancel before applying new coordinates. Unknown config values must select safe palette. Audio/queue must remain untouched on theme action. Dark child pages need contrast and legible status errors. Different layouts must use matching seek rectangles for drawing, taps and drags. Long strings/missing metadata may not overlap transport.

User explicitly authorized all designs and implementation; no additional approval gate. Existing dirty checkout retained because a fresh checkout would drop prior repairs; 0.4.0 is separately archived. No automatic commits or deployment.

Completed 2026-10-30: native 0.5.0 archived with matching ELF/NACP; full 13/13 host checks, final UI/interaction 2/2 after review fixes, 54 native previews. No deployment or hardware claims. See ../../validation-0.5.0.md.
