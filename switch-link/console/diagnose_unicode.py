"""Build-time diagnostic instrumentation for the pinned FTP filesystem.

Only operates on the supplied local build tree. No live device or network access.
The original source and 0.2.1 release are retained for rollback.
"""
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("--backup", type=Path, required=True)
args = parser.parse_args()
source = args.source.resolve()
backup = args.backup.resolve()
target = source / "build/SwitchLink/_deps/ftpsrv-src/src/platform/nx/vfs/vfs_nx_fs.c"
assert (source/"sphaira/CMakeLists.txt").is_file() and target.is_file()
backup.mkdir(parents=True, exist_ok=True)
text = target.read_text(encoding="utf-8")
if "SL_UTF8_DIAGNOSTICS" not in text:
    (backup/"vfs_nx_fs.c").write_text(text, encoding="utf-8", newline="\n")
    helper = r'''
#define SL_UTF8_DIAGNOSTICS 1
#include <stdio.h>
#include <sys/stat.h>
static void sl_utf8_path_result(const char* op, const char* path, Result rc) {
    const char* logfile = "sdmc:/config/switch-link/utf8-path.log";
    mkdir("sdmc:/config/switch-link", 0777);
    FILE* out = fopen(logfile, "ab");
    if (!out) return;
    if (fseek(out, 0, SEEK_END) == 0 && ftell(out) > 128*1024) {
        fclose(out);
        out = fopen(logfile, "wb");
        if (!out) return;
    }
    fprintf(out, "%s rc=%08lx path_hex=", op, (unsigned long)rc);
    for (size_t i=0; path && path[i] && i<FS_MAX_PATH; ++i)
        fprintf(out, "%02x", (unsigned char)path[i]);
    fputc('\n', out);
    fclose(out);
}
'''
    text = text.replace('#include <string.h>', '#include <string.h>\n'+helper, 1)
    old = "        fsFsCreateFile(fs, nxpath, 0, 0);"
    assert old in text
    text = text.replace(old, '''        Result created = fsFsCreateFile(fs, nxpath, 0, 0);
        sl_utf8_path_result("create", nxpath, created);
        if (R_FAILED(created) && created != FsError_PathAlreadyExists)
            return vfs_fs_set_errno(created);''', 1)
    old = "    if (R_FAILED(rc = fsFsOpenFile(fs, nxpath, open_mode, &f->fd))) {\n        return vfs_fs_set_errno(rc);"
    assert old in text
    text = text.replace(old, '    if (R_FAILED(rc = fsFsOpenFile(fs, nxpath, open_mode, &f->fd))) {\n        sl_utf8_path_result("open", nxpath, rc);\n        return vfs_fs_set_errno(rc);', 1)
    old = "    if (R_FAILED(rc = fsFsCreateDirectory(fs, nxpath))) {\n        return vfs_fs_set_errno(rc);"
    assert old in text
    text = text.replace(old, '    if (R_FAILED(rc = fsFsCreateDirectory(fs, nxpath))) {\n        sl_utf8_path_result("mkdir", nxpath, rc);\n        return vfs_fs_set_errno(rc);', 1)
    old = "    if (R_FAILED(rc = fsFsGetEntryType(fs, nxpath_src, &type))) {\n        return vfs_fs_set_errno(rc);"
    assert old in text
    text = text.replace(old, '    if (R_FAILED(rc = fsFsGetEntryType(fs, nxpath_src, &type))) {\n        sl_utf8_path_result("rename-source", nxpath_src, rc);\n        return vfs_fs_set_errno(rc);', 1)
    old = "        if (R_FAILED(rc = fsFsRenameFile(fs, nxpath_src, nxpath_dst))) {\n            return vfs_fs_set_errno(rc);"
    assert old in text
    text = text.replace(old, '        if (R_FAILED(rc = fsFsRenameFile(fs, nxpath_src, nxpath_dst))) {\n            sl_utf8_path_result("rename-from", nxpath_src, rc);\n            sl_utf8_path_result("rename-to", nxpath_dst, rc);\n            return vfs_fs_set_errno(rc);', 1)
    target.write_text(text, encoding="utf-8", newline="\n")
for relative in ["sphaira/CMakeLists.txt", "sphaira/source/switch_link.cpp", "sphaira/source/ui/menus/ftp_menu.cpp"]:
    path = source/relative
    data = path.read_text(encoding="utf-8")
    original = backup / path.name
    if not original.exists():
        original.write_text(data, encoding="utf-8", newline="\n")
    data = data.replace("Switch Link 0.2.1", "Switch Link 0.2.2 UTF8").replace("0.2.1", "0.2.2")
    path.write_text(data, encoding="utf-8", newline="\n")
print(json.dumps({"instrumented":str(target),"sha256":hashlib.sha256(target.read_bytes()).hexdigest(),
                  "log":"sdmc:/config/switch-link/utf8-path.log"}, indent=2))
