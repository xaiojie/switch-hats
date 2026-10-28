> 历史设计/计划：当前0.2.1以[Switch Link 原生 HTTP 安装方案](../specs/2026-10-28-native-http-install.md)为准。旧 FTP 安装及由 DBI 执行安装的流程不再用于新版。

# HTTP installation Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans inline. Steps use checkbox tracking.

**Goal:** HTTP game installation and FTP SD management in one honest, differentiated UI.
**Architecture:** Reuse the live compatible HTTP service without interruption; otherwise start a standalone read-only HTTP share. Local UI adds atomic publication and independent HTTP status while retaining FTP management.
**Tech Stack:** Python 3.11 stdlib, existing HTML/CSS/JS, unittest and Playwright/Edge.
**Spec:** ../specs/2026-10-28-http-install-design.md

## Global Constraints
- Do not interrupt or replace the running 8080 HTTP installation.
- No automatic game installation, SD mode switches, overwrite or deletion.
- Byte delivery/publication must never be labelled installed.
- Keep 8088 local; HTTP share 8080 LAN and file APIs FTP.

## Review Focus
- Current download survives UI deployment and bridge shutdown.
- FTP unavailable does not disable HTTP publication.
- Unsafe paths/symlinks and incomplete/colliding uploads never become public packages.
- Range/HEAD observations and other browsers do not invent DBI download or installation success.
- A file chooser captures its library identity independently of a changing FTP selection.

### Task 1: HTTP library and share
- [x] Write failing tests for atomic local publication, collisions/partial uploads, isolation, directory links/Range/HEAD and live compatible-service reuse.
- [x] Run red tests, implement library.py, run green tests.

### Task 2: Local API and protocol split
- [x] Write failing tests: publish with no FTP device, independent HTTP status, stale library identity rejection and retired FTP install endpoint.
- [x] Implement http.py/server.py integration, preserve existing file operations; run backend suite.

### Task 3: UI and delivery
- [x] Write failing real-browser tests for protocol labels, HTTP ready while FTP disconnected, publication/DBI instructions and no false installed status.
- [x] Update installation page and dedicated library.js; retain FTP UI, adapt superseded tests and run full browser suite.
- [x] Fresh review, backup changed app files, deploy/restart only idle 8088. Verify real HTTP server PID/connection unaffected and capture read-only UI proof.

## Execution ledger
- Initial: live 8080 PID 25408 and active Switch HTTP connection confirmed. Work remains in the existing isolated feat/switch-link checkout. User already specified this approach; no additional design approval needed.

- Task 1/2: red/green verified. Full backend47 passed. Full browser21 passed. Protocol-specific fixtures updated because FTP installation endpoint is intentionally retired.
- Real read-only preview8089: legacy8080 reused without restart, real library2 files, FTP absent but HTTP publication enabled, no browser errors or mobile overflow.
- Ruling: retain the running legacy server rather than migrate its active stream; its missing live byte counters are clearly disclosed in UI. Managed future HTTP share has truthful request-level counters.

- Final review: fixed both reported P2s (legacy waiting sentinel and incompatible JSON shape), each with regression checks. Also fixed reproducible Windows unread-body rejection resets with bounded draining.
- Final validation: backend50/50, browser21/21 pass. Deployed17 changed source/UI/test files to E repo with SHA verification and backups under work/http-upgrade-backup-20261028-152828.
- Runtime note: before controlled deployment, both old local services had already exited during a tool reconnection; no stop command was issued. Restarted the new0.2.0 companion; its managed HTTP share serves the same existing library and source URL.
- Real deployed read-only QA: HTTP ready,2 packages, FTP independent, no browser errors or mutations,390px no overflow. HEAD length3005823376 and Range0-15 returned206/16 bytes. No game installation initiated.
