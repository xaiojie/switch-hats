# Walkman Experience Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Three simple navigation entries, responsive input, and actual cloud artwork/lyrics metadata.
**Architecture:** Keep native renderer and libnx thread ownership. Separate UI, media resolution, and background maintenance; root integrates event loop and model updates.
**Tech Stack:** C++17, libnx, curl, stb, yyjson, CMake, ASan/UBSan host suite.
**Spec:** docs/superpowers/specs/2026-10-30-walkman-experience-design.md

## Global Constraints

Preserve 0.3.12 startup fix, no detach, same forwarder/title ID/NRO path. Version 0.4.0 in three locations. Maintain cancellation and bounded metadata/cache/network operations. Never call host rendering or an upload hardware acceptance. Existing uncommitted edits belong to the current work and must remain.

## Tasks

- [x] UI: update ui.hpp/ui.cpp/ui_views.inc, UI tests, preview fixtures; larger touch targets, three main entries and child pages; cached scaled artwork; verify real renders.
- [x] Media: bounded HTTP prefix fetch; reusable memory ID3 parser; current cloud art/lyrics resolution and cached enrichment; robust controlled fixtures, cancellation, legacy encoding and missing data.
- [x] Maintenance: runtime background save/cache service; batched nonblocking logging; final save/flush and errors; deterministic host tests.
- [x] Integration: main.cpp and app_model metadata propagation; coherent queue, transient notices, page/input map, live drag, 60 Hz polling, measured render cadence and frame telemetry; add tests for extracted portable interaction policy.
- [x] Verification: full host/HTTP/sanitizer suite 13/13; native build; native screenshot review; fresh code review plus fixes; HTTP regression suite repeats 3/3. Evidence: docs/validation-0.4.0.md and build/diagnostics/.
- [ ] Deployment: original path FTP overwrite, readback SHA256, clear and verify logs; retrieve real device result and record remaining limits.

## Review Focus

Cancellation during network metadata lookup must not publish the prior song's assets. Huge or malformed ID3 and wrong lyric matches must remain bounded and honest. Periodic writes must not block input. Page-specific shortcuts must agree with visible hints. Shutdown must persist the latest state before services close.

## Execution rulings

Work in the existing source checkout because this task continues its uncommitted 0.3.12 repair; do not create a clean checkout that drops it. Root alone builds/deploys and changes CMake/version. Workers own disjoint modules and report test evidence; no automatic commits of inherited changes.

Latest user steering: Switch unavailable; complete code and compilation first. Deployment and device acceptance explicitly deferred. Release 0.4.0 is built and archived, not deployed. Known Kiro M3U URL requests JSON first while preserving configured source identity/cache key, falls back on fetch/schema failure; native live check confirms all 90 supported songs retain IDs. Final review and decisions: build/diagnostics/review-0.4.0.md.
