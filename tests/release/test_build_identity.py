import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import build_identity  # noqa: E402


SHA = "0123456789abcdef0123456789abcdef01234567"


class BuildIdentityTests(unittest.TestCase):
    def test_exact_clean_tag_is_canonical_stable(self):
        identity = build_identity.resolve("0.11.0", SHA, False, "v0.11.0", False)
        self.assertEqual(identity.display_version, "0.11.0")
        self.assertEqual(identity.channel, "stable")

    def test_post_tag_dirty_and_package_builds_never_collide(self):
        source = build_identity.resolve("0.11.0", SHA, False, None, False)
        dirty = build_identity.resolve("0.11.0", SHA, True, None, False)
        package = build_identity.resolve("0.11.0", SHA, False, None, True)
        self.assertEqual(source.channel, "source")
        self.assertEqual(package.channel, "package")
        self.assertNotEqual(source.identity, dirty.identity)
        self.assertNotEqual(source.identity, package.identity)
        self.assertTrue(dirty.dirty)

    def test_archive_without_git_requires_injected_full_sha(self):
        with self.assertRaisesRegex(ValueError, "source revision"):
            build_identity.resolve("0.11.0", "", False, None, True)
        with self.assertRaises(ValueError):
            build_identity.resolve("0.11.0", "deadbeef", False, None, True)

    def test_json_is_machine_readable_and_complete(self):
        value = json.loads(build_identity.resolve(
            "0.11.0", SHA, False, None, True, target="x86_64-linux",
            llvm="18.1.8").json())
        self.assertEqual(value["schema_version"], 1)
        self.assertEqual(value["revision"], SHA)
        self.assertEqual(value["channel"], "package")
        self.assertIs(value["dirty"], False)
        self.assertIn("identity", value)
        self.assertEqual(value["target"], "x86_64-linux")
        self.assertEqual(value["llvm"], "18.1.8")

    def test_git_probe_distinguishes_post_tag_and_dirty(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            subprocess.run(["git", "init", "-q", repo], check=True)
            subprocess.run(["git", "-C", repo, "config", "user.email", "test@example.com"], check=True)
            subprocess.run(["git", "-C", repo, "config", "user.name", "Test"], check=True)
            (repo / "tracked").write_text("one\n")
            subprocess.run(["git", "-C", repo, "add", "tracked"], check=True)
            subprocess.run(["git", "-C", repo, "commit", "-qm", "one"], check=True)
            subprocess.run(["git", "-C", repo, "tag", "v0.11.0"], check=True)
            tagged = build_identity.from_git("0.11.0", repo)
            self.assertEqual(tagged.channel, "stable")
            injected = build_identity.from_git(
                "0.11.0", repo, injected_sha=tagged.revision)
            self.assertEqual(injected.channel, "stable")
            self.assertEqual(injected.display_version, "0.11.0")
            (repo / "tracked").write_text("dirty\n")
            first_dirty = build_identity.from_git("0.11.0", repo)
            self.assertTrue(first_dirty.dirty)
            (repo / "tracked").write_text("different dirty contents\n")
            second_dirty = build_identity.from_git("0.11.0", repo)
            self.assertNotEqual(first_dirty.identity, second_dirty.identity)

    def test_untracked_unicode_and_quoted_paths_participate_in_dirty_digest(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            subprocess.run(["git", "init", "-q", repo], check=True)
            subprocess.run(["git", "-C", repo, "config", "user.email", "test@example.com"], check=True)
            subprocess.run(["git", "-C", repo, "config", "user.name", "Test"], check=True)
            (repo / "tracked").write_text("tracked\n")
            subprocess.run(["git", "-C", repo, "add", "tracked"], check=True)
            subprocess.run(["git", "-C", repo, "commit", "-qm", "base"], check=True)
            unusual = repo / 'é space "quote".janus'
            unusual.write_text("one\n")
            first = build_identity.from_git("0.11.1", repo)
            unusual.write_text("two\n")
            second = build_identity.from_git("0.11.1", repo)
            self.assertTrue(first.dirty)
            self.assertNotEqual(first.source_digest, second.source_digest)
            self.assertNotEqual(first.identity, second.identity)


if __name__ == "__main__":
    unittest.main()
