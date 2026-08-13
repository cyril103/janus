import io
import hashlib
import json
import os
import stat
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import downstream_canary  # noqa: E402


SHA = "0123456789abcdef0123456789abcdef01234567"


def archive(path: Path, *, binary=True, stdlib=True, revision=SHA):
    with tarfile.open(path, "w:gz") as output:
        entries = {
            "janus/bin/janus": "#!/bin/sh\nexit 0\n",
            "janus/share/janus/stdlib/std/core.janus": "module std.core\n",
            "janus/share/janus/build-identity.json": json.dumps({
                "schema_version": 1, "version": "0.11.0", "revision": revision,
                "dirty": False, "channel": "package", "identity": f"0.11.0+g{revision}"}),
        }
        if not binary:
            entries.pop("janus/bin/janus")
        if not stdlib:
            entries.pop("janus/share/janus/stdlib/std/core.janus")
        for name, contents in entries.items():
            data = contents.encode()
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mode = 0o755 if name.endswith("/janus") else 0o644
            output.addfile(info, io.BytesIO(data))
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    path.with_name(path.name + ".sha256").write_text(f"{digest}  {path.name}\n")


class CandidateValidationTests(unittest.TestCase):
    def test_rejects_missing_binary_stdlib_and_wrong_identity(self):
        for options, message in [
            ({"binary": False}, "binary"), ({"stdlib": False}, "stdlib"),
            ({"revision": "f" * 40}, "revision")]:
            with self.subTest(options=options), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "candidate.tar.gz"
                archive(path, **options)
                with self.assertRaisesRegex(ValueError, message):
                    downstream_canary.validate_archive(path, SHA)

    def test_rejects_path_traversal_before_extraction(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "candidate.tar.gz"
            with tarfile.open(path, "w:gz") as output:
                info = tarfile.TarInfo("../escape")
                info.size = 1
                output.addfile(info, io.BytesIO(b"x"))
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            path.with_name(path.name + ".sha256").write_text(
                f"{digest}  {path.name}\n")
            with self.assertRaisesRegex(ValueError, "unsafe"):
                downstream_canary.validate_archive(path, SHA)

    def test_rejects_backslashes_special_files_and_case_collisions(self):
        for names in [
            ["..\\escape"],
            ["janus/Foo", "janus/foo"],
            ["janus/fifo"],
        ]:
            with self.subTest(names=names), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "candidate.tar.gz"
                with tarfile.open(path, "w:gz") as output:
                    for name in names:
                        info = tarfile.TarInfo(name)
                        if name.endswith("fifo"):
                            info.type = tarfile.FIFOTYPE
                            output.addfile(info)
                        else:
                            info.size = 1
                            output.addfile(info, io.BytesIO(b"x"))
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                path.with_name(path.name + ".sha256").write_text(
                    f"{digest}  {path.name}\n")
                with self.assertRaisesRegex(ValueError, "unsafe|ambiguous"):
                    downstream_canary.validate_archive(path, SHA)

    def test_rejects_missing_or_mismatched_checksum(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "candidate.tar.gz"
            archive(path)
            checksum = path.with_name(path.name + ".sha256")
            checksum.unlink()
            with self.assertRaisesRegex(ValueError, "checksum"):
                downstream_canary.validate_archive(path, SHA)
            checksum.write_text(f"{'0' * 64}  {path.name}\n")
            with self.assertRaisesRegex(ValueError, "does not match"):
                downstream_canary.validate_archive(path, SHA)

    def test_contract_runs_every_required_janus8_gate(self):
        self.assertEqual(downstream_canary.JANUS8_COMMANDS, [
            ["fmt", "--check"], ["check", "--all", "--deny-warnings"],
            ["test", "--fail-if-empty"], ["build"],
        ])
        source = (ROOT / "scripts/downstream_canary.py").read_text()
        self.assertIn('tests/native_syntax.sh', source)
        self.assertIn('tests/native_syntax_mutation.sh', source)
        self.assertIn('tests/smoke.sh', source)

    def test_release_and_nightly_share_the_canary_before_publication(self):
        nightly = (ROOT / ".github/workflows/nightly.yml").read_text()
        release = (ROOT / ".github/workflows/ci.yml").read_text()
        invocation = "scripts/downstream_canary.py"
        self.assertIn(invocation, nightly)
        self.assertIn(invocation, release)
        self.assertLess(nightly.index(invocation), nightly.index("Upload immutable candidate"))
        self.assertLess(release.index(invocation), release.index("Publish GitHub release"))
        self.assertIn(downstream_canary.JANUS8_REVISION, nightly)
        self.assertIn(downstream_canary.JANUS8_REVISION, release)


if __name__ == "__main__":
    unittest.main()
