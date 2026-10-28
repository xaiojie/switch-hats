> 历史设计/计划：当前0.2.1以[Switch Link 原生 HTTP 安装方案](../specs/2026-10-28-native-http-install.md)为准。旧 FTP 安装及由 DBI 执行安装的流程不再用于新版。

# Switch Link

User authorized implementation after approving a two-function Switch tool: SD file management (browse, upload, download, mkdir, rename, delete) and game installation, with automatic LAN discovery and an attractive Chinese interface. Use an independent development checkout; preserve the original DBI, SD/emummc and website on port 8080. The new companion uses port 8088.

Preferred approach is a slim independent Sphaira-derived console client, reusing its proven file/FTP-install code and preserving upstream licensing, plus a Python local companion with a polished responsive web UI. A DBI-compatible file-management mode is allowed, but must never pretend ordinary FTP file upload installs a title. Original DBI and a fully from-scratch installer were considered; the former cannot expose the requested unified workflow and the latter adds unnecessary format/installation risks.

Discovery uses the console UDP beacon and previously authorized/selected peers only; it never sweeps unrelated LAN addresses. No IP entry is required for the normal path. Only identify a Switch after FTP login and SD-marker verification. First selection is explicit; remember the selected random device ID. IP changes trigger rediscovery. Do not read or publish prod.keys, console identifiers or credentials in logs. LAN isolation can prevent discovery and must produce a useful error. The companion binds only to loopback by default.

Jobs distinguish local ingestion, transfer, installation confirmation and failure. FTP transfer completion without a reliable console installation receipt is shown as sent/awaiting confirmation, never installed. Default target is SD. File writes use temporary files and verified sizes; overwrite backups are kept before replacement. Path traversal, rename collisions and changing devices during active jobs are rejected. Delete is a visible deliberate action.

UI direction: Chinese, slate/ivory surfaces, restrained cobalt/teal accents, strong spacing and typography, two navigation destinations (SD card / Installation), device card with honest state, breadcrumbs and file list, clear drag target, real task progress, useful empty/error states, responsive layout. No fake connected device or simulated success.

Console target: OLED, HOS 22.5.0 emuMMC / Atmosphere 1.12.1. Produce an actual .nro with the intended two-function UX if the portable build toolchain succeeds. A source-only build or unverified hardware state is disclosed honestly. Do not overwrite or launch new console software on the live console until the build has been checked and the existing operation state is known.

## Companion API contract

All JSON except file bodies. Errors: {ok:false,error:string}. GET /api/status returns {app,version,lan_url,device:null|{id,name,ip,port,connected,mode,can_install,install_reason,free_bytes,total_bytes},discovery:{running,error},jobs:[...]}. GET /api/devices returns {devices:[same device objects],scanning,error}. POST /api/discover starts discovery. POST /api/connect {id} selects a discovered device. GET /api/files?path=/ returns {path,entries:[{name,path,type:'directory'|'file',size,modified}]}. PUT /api/files/upload?path=/&name=encoded&overwrite=false ingests raw file and queues upload. GET /api/files/download?path=encoded streams a file. POST /api/files/mkdir {path,name}; POST /api/files/rename {path,name}; DELETE /api/files?path=encoded. PUT /api/install?name=encoded ingests a package and queues installation only if the device advertises verified install support. GET /api/jobs returns {jobs:[]}. POST /api/jobs/cancel {id} cancels a waiting job or requests safe cancellation.

Job: {id,name,kind:'upload'|'install'|'download',state:'receiving'|'queued'|'transferring'|'awaiting_confirmation'|'completed'|'failed'|'cancelled',bytes_done,bytes_total,progress,speed,error,created_at}. UI should work with missing optional fields, poll every 2s, and update without losing selection or scroll position. It must show awaiting_confirmation distinctly from completed.
