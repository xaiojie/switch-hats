# Task 3 fresh-review fixes

Reviewed baseline: `44761c0`. Scope: stream/decoder implementation and controlled tests, the permitted minimal minimp3 override, and this report. No service restarts, deployments, source-song writes or device actions.

## Findings and fixes

1. **False EOF from short decoder callbacks.** The callback now accumulates the complete requested byte count across any number of source fragments. It returns short only on terminal EOF/error/cancellation. `MINIMP3_IO_SIZE` is 32 KiB, strictly greater than the library's 16 KiB minimum frame window (compile-time assertion). The vendor header has only an `#ifndef MINIMP3_IO_SIZE` guard and a local-change comment; the original default and notices remain intact. Decoder requests retain at least 128 KiB of disk cache so the opening window survives the first-frame rewind. A generated 32-copy, approximately 65-second no-Xing fixture is decoded completely: PCM frame count equals exactly 32 times the original two-second synthetic tone. HTTP delivery fragments the same long bytes into 137-byte writes and must decode exactly the same PCM frame count as the local fixture, including PCM before transfer EOF.

2. **Tagged MP3 ending before transport verification.** When remote minimp3 returns zero samples, Decoder calls the new `ByteSource::Finish()`, which drains trailing transport data and waits for verified transport EOF or an error/cancellation. Only then can Engine observe a successful decoder EOF. The tagged fixture advertises four MP3 frames, delivers substantially more body data, then closes short of the advertised HTTP length. Engine must report an error, remain uncompleted and leave its completion counter at zero. A separate five-second stalled trailing-body case verifies cancellation during Finish in less than 500 ms. Initial playback still does not wait for full transport completion.

3. **Unvalidated Content-Range body extent.** Every 206 now independently checks `body == rangeEnd - rangeStart + 1`, even without Content-Length. Tests cover a connection-delimited short 206 and a chunked short 206 whose HTTP chunk framing successfully terminates with a zero chunk. Both are transport errors, not EOF.

4. **Detached cancellation-token lifetime.** All worker reads of the external atomic pointer are protected by a separate cancellation mutex, including progress callbacks and pointer copying during request replacement. Close acquires the request mutex, then retires the pointer under the cancellation mutex: it sets stop and nulls the pointer before returning or releasing its request reference. If a callback already holds that mutex, Close waits until its token read has finished while the caller still owns the token. If Close acquired it first, every subsequent callback sees stop/null while holding the same mutex. Lock order is request mutex -> cancellation mutex; progress takes only cancellation mutex, never the request mutex, so there is no inverse order. Initialization occurs before worker publication. The single-consumer contract requires callers to retain the token until Close returns and not concurrently call consumer operations. The lifetime probe creates an atomic in a VirtualAlloc page, closes the source, destroys the atomic, protects the page as PAGE_NOACCESS and releases it; repeated 40 times while HTTP requests can still be detached. This is stress coverage plus the lock-order/lifetime argument, not a claim that a timed test can deterministically prove absence of a race. No pre-fix failing lifetime-stress result is claimed.

5. **Past-end seek and unsigned subtraction.** Seek rejects offsets beyond a known total before creating a replacement HTTP request. Exact EOF is accepted by moving to that offset on the existing request and waiting for its terminal state, avoiding an invalid Range request at the length itself. Read checks `pos >= end` before any subtraction once terminal. Tests include a server that initially honors Range and subsequently returns 200, past-end rejection, and exact-EOF zero reads; ordinary ignored-Range and cached seeks remain covered.

## Failed-first evidence

The new regression modes were executed against the old production implementation before the corresponding fixes:

```text
./papa-walkman/tools/test-stream.ps1 -Review long
FAIL: d.Duration()>60
./papa-walkman/tools/test-stream.ps1 -SkipBuild -Review fragment
FAIL: d.Open(base+"/fragment",nullptr,0,status)
./papa-walkman/tools/test-stream.ps1 -Review late
FAIL: !e.State().error.empty()
./papa-walkman/tools/test-stream.ps1 -Review range-short
FAIL: !s.Status().error.empty()
./papa-walkman/tools/test-stream.ps1 -Review seek
FAIL: !s.Seek(s.Status().totalBytes+1)
```

An initial fixture-server indentation mistake was repaired before these results; it is not counted as a production regression. The runner now detects a fixture server exiting during startup. `-SkipBuild` only reuses an already built test executable; normal invocation always compiles.

## Final verification

Command from repository root:

```powershell
./papa-walkman/tools/test-stream.ps1
```

The runner uses VS2022 x64 C++17, the same libcurl implementation as production through Git's host libcurl DLL, a private dynamic-port Python HTTP server, generated synthetic fixtures under ignored build-host, and automatic server cleanup. It now runs the baseline plus every review mode by default.

```text
PASS controlled HTTP startup, unknown duration, bounds, redirects, Range, truncation, 404, cancellation
PASS review long
PASS review fragment
PASS review late
PASS review late-cancel
PASS review range-short
PASS review seek
PASS review lifetime
```

Final targeted baseline regression compilation used `vcvars64.bat`, `cl /std:c++17 /EHsc /utf-8 /O2 /Isource /Ithird_party/minimp3 /I<portlibs-switch-include>`, with `source/stream.cpp source/decoder.cpp` and `build-host/stream/curl.lib`; the engine executable additionally compiled `source/engine.cpp source/core.cpp`.

```text
build-host/stream-decoder-tests.exe tests/tone.mp3
PASS 16 checks: real MP3 stream, duration, seek, cancellation, invalid files
build-host/stream-engine-tests.exe tests/tone.mp3
PASS engine: real decode/output queue, pause ownership, progress, seek reset, EOF drain, output errors
```

## Public contract and remaining gates

Only public API addition: `bool ByteSource::Finish()`. Existing Open/Read/Seek signatures and Decoder/Engine interfaces remain compatible. `stream.hpp` now explicitly documents cancellation-token lifetime and the single-consumer contract. No Engine header changes in this fix package.

Whole retained-prefix time seeking remains conservative and unchanged. TLS verification remains enabled with no forced CA location. Native build, native trust-store acceptance, device playback, global cache quota and crash-orphan cleanup remain integration/hardware gates; this report claims host controlled-transport behavior only.
