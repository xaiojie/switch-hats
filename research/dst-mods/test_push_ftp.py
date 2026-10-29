import ftplib
import tempfile
import unittest
from pathlib import Path

from push_ftp import remove_tree, upload_tree


class DiskFTP:
    """Keep the remote filesystem real; replace only FTP transport."""

    def __init__(self, root, corrupt=False):
        self.root = root
        self.corrupt = corrupt

    def path(self, remote):
        return self.root / remote.lstrip("/")

    def mkd(self, remote):
        try:
            self.path(remote).mkdir()
        except FileExistsError:
            raise ftplib.error_perm("550 Already exists")

    def storbinary(self, command, stream):
        assert command.startswith("STOR ")
        data = stream.read()
        self.path(command[5:]).write_bytes(data + (b"corrupt" if self.corrupt else b""))

    def retrbinary(self, command, callback):
        assert command.startswith("RETR ")
        callback(self.path(command[5:]).read_bytes())

    def mlsd(self, remote):
        if not self.path(remote).is_dir():
            raise ftplib.error_perm("550 No such file or directory")
        for p in self.path(remote).iterdir():
            yield p.name, {"type": "dir" if p.is_dir() else "file"}

    def nlst(self, remote):
        return [name for name, facts in self.mlsd(remote)]

    def delete(self, remote):
        try:
            self.path(remote).unlink()
        except (FileNotFoundError, IsADirectoryError):
            raise ftplib.error_perm("550 No such file or directory")

    def rmd(self, remote):
        try:
            self.path(remote).rmdir()
        except OSError:
            raise ftplib.error_perm("550 Cannot remove directory")


class DeploymentVerificationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        base = Path(self.temp.name)
        self.local = base / "local"
        (self.local / "scripts").mkdir(parents=True)
        (self.local / "scripts/globalvariableoverrides.lua").write_bytes(b"MODS_ENABLED = true\n")
        self.remote = base / "remote"
        self.remote.mkdir()

    def test_stale_override_prevents_success_and_is_preserved_for_backup(self):
        stale = self.remote / "romfs/scripts/modindex.lua"
        stale.parent.mkdir(parents=True)
        stale.write_bytes(b"old game script")
        with self.assertRaisesRegex(RuntimeError, "modindex.lua"):
            upload_tree(DiskFTP(self.remote), self.local, "/romfs")
        self.assertEqual(stale.read_bytes(), b"old game script")

    def test_corrupt_upload_prevents_success(self):
        with self.assertRaisesRegex(RuntimeError, "globalvariableoverrides.lua"):
            upload_tree(DiskFTP(self.remote, corrupt=True), self.local, "/romfs")

    def test_matching_upload_succeeds_without_touching_other_titles(self):
        unrelated = self.remote / "other-title.bin"
        unrelated.write_bytes(b"keep")
        upload_tree(DiskFTP(self.remote), self.local, "/romfs")
        self.assertEqual((self.remote / "romfs/scripts/globalvariableoverrides.lua").read_bytes(), b"MODS_ENABLED = true\n")
        self.assertEqual(unrelated.read_bytes(), b"keep")

    def test_remove_handles_dbi_bare_names_and_only_removes_target(self):
        target = self.remote / "contents/game/romfs/scripts"
        target.mkdir(parents=True)
        (target / "override.lua").write_bytes(b"remove")
        other = self.remote / "contents/other-game.bin"
        other.write_bytes(b"keep")
        remove_tree(DiskFTP(self.remote), "/contents/game")
        self.assertFalse((self.remote / "contents/game").exists())
        self.assertEqual(other.read_bytes(), b"keep")


if __name__ == "__main__":
    unittest.main()
