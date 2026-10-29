# Software validation — 0.3.0 test build

Date: 2026-10-29 (Asia/Hong_Kong). This records software checks before pushing the development branch, not a completed hardware release.

## Fresh checks

- `tools/test-host.ps1`: all 11 CTest groups passed with assertions enabled: core, UTF-8 path, decoder, engine, media, app model, runtime, pause gate, UI, assets and audio.
- Its controlled HTTP suite passed startup, unknown duration, redirects, Range, truncated/late responses, cancellation, bounded seeking and cached resume checks using the synthesized MP3 fixture.
- `switch-link`: Python 3.11 `unittest discover -s tests` passed all 89 tests. The anonymous-write warning comes from the local FTP test fixture.
- `switch-link/static`: all 31 browser tests passed in headless Edge, including mobile layout, plain HTTP LAN source editing and copy fallback.
- `build.ps1` built the ARM64 NRO with devkitA64 GCC 16.1.0, libnx, CMake and Ninja. `inspect_nro.py` verified version 0.3.0, UTF-8 titles in all 16 language slots and a 256×256 JPEG icon.
- NRO size: 1,918,245 bytes. SHA256: `105c74cc3a364d592b164f9770437b8fa44eae6c4ef82a272acc0f056ebbd7cd`. This matches the earlier uploaded test build.
- Actual UI code produced nine demonstration previews covering SD/cloud libraries, albums, cover/tape, lyrics, queue, settings and buffering. Cover, albums and lyrics previews were visually inspected; these are desktop renders, not Switch screenshots.

## Remaining validation and known limits

- The 0.3.0 NRO was uploaded and read back with matching SHA256. Playback, headphones, HOME/sleep, real network interruption and long sessions on the Switch still require user acceptance.
- Chinese filenames work in the Windows scan/settings tests. Native Switch Link FTP Chinese create/mkdir/rename operations still fail in the diagnostic build; the underlying fix is pending. Do not treat the Windows tests as proof of a device fix.
- Album groups use metadata tags. Folder-name fallback and automatic repair of incorrectly encoded source tags have not been implemented. Audio decoding currently supports MP3 only.
- Cloud seeking/resume depends on server behavior, duration information and cached range. A software fixture does not prove behavior of every external HTTPS server.
- LAN access is covered by HTTP and browser tests. The production service was left stopped at the user's request; another physical device has not been used for a LAN acceptance check.
- `releases/0.2.0` remains the earlier archive. The existing release packaging script targets 0.2.0; no formal 0.3.0 release package was generated in this push.
