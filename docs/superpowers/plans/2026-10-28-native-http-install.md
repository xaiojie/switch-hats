# Native HTTP installation implementation plan

Spec: ../specs/2026-10-28-native-http-install.md. User explicitly corrects the prior implementation and authorizes own-client HTTP installation. Work in existing isolated feat/switch-link checkout.

- [x] Native owner: bounded UDP command validation/idempotence, HTTP engine integration, result POST, version/build/source packaging and parser tests. Own console/ and incremental upstream overlay/build files only.
- [x] Backend owner: capability-bound HTTP jobs and public source/result handlers, native beacon capabilities, local API, tests. Own switch_link/, server.py and Python tests only.
- [x] Web owner: own-client install controls, captured-device binding, native HTTP progress, remove DBI instructions, browser tests. Own static/ only.
- [x] Integrate, full tests/build, independent final review, deploy idle PC service and copy independent NRO folder if FTP available. Ask user to open new client only when artifact ready. No actual game install initiated in this task without new instruction.

Ledger: prior0.2.0 mistakenly used DBI; this plan replaces that choice. FTP file operations and existing HTTP game files remain. Native NSZ is disabled in current toolchain; do not falsely advertise support.

- 完成记录：主机NRO0.2.1与电脑端已部署。用户已在网页发起并实测原生HTTP安装；两次安装均获得Result0、registered_titles=1。后台61项、网页30项及两套原生解析器检查通过。未验证XCI完整安装、断网续装和掉电恢复。
