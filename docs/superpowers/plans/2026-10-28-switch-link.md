> 历史设计/计划：当前0.2.1以[Switch Link 原生 HTTP 安装方案](../specs/2026-10-28-native-http-install.md)为准。旧 FTP 安装及由 DBI 执行安装的流程不再用于新版。

# Switch Link Implementation Plan

**Goal:** A usable, honest two-function Switch/PC tool with automatic connection.
**Spec:** ../specs/2026-10-28-switch-link-design.md
**Architecture:** Python standard-library service and real FTP transport, static browser UI, independently built Sphaira-derived console client.
**Execution:** User said directly start; work proceeds without another design approval round. Parallel agents own independent console and web files; primary owns backend, tests and integration.

## Constraints and review focus
- Preserve existing website, downloads, uncommitted work and live SD/emuMMC.
- File transfer success cannot be equated with title installation success.
- A non-Switch FTP host must not be selected automatically.
- Failed discovery, IP changes and disconnected consoles must not show a connected badge.
- Traversal, overwrite collisions and changing target during active jobs need behavioral tests.
- Read-only browsing and intentional file operations are separate; no bulk destructive tests on live SD.

## Tasks
- [x] Console agent: inspected pinned upstream, established portable official toolchain, compiled and checked actual AArch64 .nro; GPL license/corresponding-source ZIP included.
- [x] UI agent: real responsive Chinese SD browser and installation queue; no fake device state; 12 UI checks pass.
- [x] Primary: failing tests then implementation/fixes; 24 backend integration tests pass, including 13s slow native receipt and cross-device confirmation regression.
- [x] Integration: port8088 running from delivered E: repository folder. User launched native client on OLED. Actual UDP beacon, root listing, 68-byte upload/download byte-match and cleanup verified through the new API.
- [x] Independent headless Edge visual QA verified real connected SD browser and 390px mobile layout, no page errors/overflow.
- [x] Fresh review found and fixed spool leak, device-binding race, receipt timeout and malformed beacon exceptions. Output packaging and launch URL prepared.
- [ ] Full actual game installation: user explicitly chose not to perform this hardware test in this session. Preserve this limitation in delivery; do not fabricate success or start a package.

## Execution rulings
- Native worktree tool could not accept the E: repository because it is outside this chat's registered workspace. A local independent clone on feat/switch-link preserves the original checkout and running service.
- Service startup was rejected by automatic approval without a detailed reason. Safer implementation binds the new web service to loopback and discovers only consoles advertising Switch Link or previously authorized/selected peers; broad subnet probing has been removed. This retains normal zero-IP use with the new console client and avoids touching unrelated LAN devices.
