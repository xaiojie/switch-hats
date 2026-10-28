> 历史设计/计划：当前0.2.1以[Switch Link 原生 HTTP 安装方案](../specs/2026-10-28-native-http-install.md)为准。旧 FTP 安装及由 DBI 执行安装的流程不再用于新版。

# HTTP installation and FTP file management

The user explicitly chose the proven DBI SwitchLibrary HTTP installation path after ~4 MB/s versus ~178 KB/s FTP. Retain FTP for SD management because the running PC HTTP library cannot read the console SD. Do not interrupt the current HTTP installation, restart its 8080 server, modify its active package or switch the console mode.

Approaches: reuse the compatible existing 8080 library and its directory (chosen to preserve the live download); replace it with a new sharing server immediately (rejected because it interrupts the installation); integrate console HTTP file management (deferred because it requires switching the running console and is not part of the verified SwitchLibrary protocol).

Keep the polished loopback 8088 UI. Its installation destination becomes an HTTP package library independent of FTP connectivity: atomic package upload to the computer, listing, copyable source URL/configuration, clear DBI menu instructions. DBI selects and initiates installation; publishing or serving bytes never means installed. Retain FTP upload/download/browse/mkdir/rename/delete on the SD tab, clearly labelled FTP. Disable the old web FTP installation endpoint to prevent accidental use.

Provide a standalone read-only LAN HTTP share on 8080 with Apache-style directory links, HEAD/Range streaming and genuine per-request byte progress. If an existing compatible server owns 8080 and the same directory, attach read-only and never restart/stop it. Its older status only exposes access history, so explicitly label live progress unavailable rather than fabricate it. No deletion or replacement of library packages in this change. Same-name publication is rejected. Preserve all old files and native client binaries.

Local upload APIs remain loopback and Origin checked. Public share serves only root-level package files, no traversal, links outside the root, credentials or SD contents. Chunk streaming must not load large packages into memory. Avoid committing data, installers and private material.
