# Task 2 review fixes

Scope: media.cpp, media.hpp, media_tests.cpp, and this report only. No UI/core/build changes, device actions, or repeat user-library scan.

## Failed-first reproduction

Added independent regression checks before changing production code, then ran from papa-walkman:

```powershell
& ./build-host/media-tests.exe
```

Observed exit 1 against the previous implementation:

```text
REVIEW FAIL: aggregate expanded LRC text <=1MiB
REVIEW FAIL: mismatching URI consumes marker
REVIEW FAIL: invalid replacement clears marker
REVIEW FAIL: invalid EXTINF duration becomes unknown
REVIEW FAIL: invalid EXTINF duration becomes unknown
REVIEW FAIL: aggregate 10001 tracks rejected without changing caller
REVIEW FAIL: malformed track rejected before materialization
REVIEW FAIL: save aggregate 10001 tracks rejected
REVIEW FAIL: valid resume included in aggregate track quota
REVIEW FAIL: overquota primary recovers valid backup
review regressions: 10 failures
```

The LRC fixture uses twenty timestamps and a 64 KiB suffix (about 1.25 MiB expanded), avoiding a dangerous reproduction allocation. The state fixture contains two valid arrays of 5001 and 5000 minimal tracks.

## Fixes

- LRC checks a shared 1 MiB text budget before copying each timed suffix or appending plain lyrics. Overflow or excess lines returns empty Lyrics.
- State loading validates entry shapes and the aggregate playlist plus valid-resume count before constructing Tracks. All playlists share a 10000-track quota; invalid resume tracks are not materialized. Semantic failures trigger backup recovery, preserving the caller if both files are invalid.
- Saving applies the same quota and rejects malformed tracks and nonfinite durations/positions/offsets before writing. A failed save preserves the primary file.
- Each URI consumes its pending PAPA-TRACK identity, even on mismatch. Replacement markers clear prior identity before parsing.
- EXTINF accepts only complete-string positive finite numbers. Overflow, NaN, Infinity, trailing garbage, negative and absent values become unknown (zero).
- Header documents both aggregate limits.

## Verification

Ran from repository root:

```powershell
& papa-walkman/tools/test-media.ps1 2>&1 | Select-Object -Last 10
```

Observed exit 0:

```text
review regressions: 0 failures
media tests passed
```

A subsequent direct binary run also produced the same two success lines with exit 0.

Additional coverage: exactly 10000 tracks load/save successfully; adding a valid resume exceeds the quota; malformed empty-object tracks are rejected; overquota primary recovers backup; failed save preserves primary favorites; later matching URI and invalid replacement cannot inherit stale SD identity. EXTINF cases include 1e999, NaN, Infinity, 7junk, garbage. Existing metadata, encoding, lyrics, catalogs, search, mixed playlist round-trip, and recovery tests remain in the successful focused run.

Compiler: GCC 6 gnu++17 via the existing script. Integrated MSVC/native checks remain root integration verification. The earlier accepted read-only 101-file smoke was intentionally not repeated.
