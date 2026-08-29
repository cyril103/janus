import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import nightly_release  # noqa: E402
import promote_release_channel  # noqa: E402


class ReleasePromotionTests(unittest.TestCase):
    def manifest(self, version: str, release: str | None = None):
        return nightly_release.ChannelManifest(version, release or f"v{version}")

    def test_stable_promotion_requires_a_strictly_newer_version(self):
        current = self.manifest("0.20.0")
        nightly_release.check_release_promotion("v0.20.1", "stable", current)
        with self.assertRaisesRegex(ValueError, "newer"):
            nightly_release.check_release_promotion("v0.20.0", "stable", current)
        with self.assertRaisesRegex(ValueError, "newer"):
            nightly_release.check_release_promotion("v0.19.9", "stable", current)

    def test_beta_promotion_uses_semver_prerelease_ordering(self):
        current = self.manifest("0.20.0-rc.2", "v0.20.0-rc.2")
        nightly_release.check_release_promotion("v0.20.0-rc.10", "beta", current)
        with self.assertRaisesRegex(ValueError, "newer"):
            nightly_release.check_release_promotion("v0.20.0-rc.1", "beta", current)

    def test_stable_version_has_higher_precedence_than_its_prereleases(self):
        stable = nightly_release.ReleaseVersion.parse("0.20.0")
        candidate = nightly_release.ReleaseVersion.parse("0.20.0-rc.10")
        self.assertGreater(stable, candidate)

    def test_channel_rejects_the_wrong_tag_kind(self):
        with self.assertRaisesRegex(ValueError, "stable"):
            nightly_release.check_release_promotion("v0.20.0-rc.1", "stable", None)
        with self.assertRaisesRegex(ValueError, "prerelease"):
            nightly_release.check_release_promotion("v0.20.0", "beta", None)

    def test_cli_checks_a_downloaded_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            current = Path(directory) / "version"
            current.write_text("0.20.0 v0.20.0\n")
            result = subprocess.run(
                [sys.executable, str(ROOT / "scripts" / "nightly_release.py"),
                 "check-promotion", "--candidate", "v0.20.0",
                 "--channel", "stable", "--current", str(current)],
                text=True, capture_output=True)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("newer", result.stderr)

    def test_documentation_guard_rejects_a_missing_release_link(self):
        required = (
            "CMakeLists.txt",
            "CHANGELOG.md",
            "README.md",
            "editors/vscode/package.json",
            "editors/vscode/package-lock.json",
            "docs/graphics.md",
            "docs/stability-contract.md",
            "docs/development.md",
            "stdlib/std/graphics/resources.janus",
            "stdlib/std/graphics/input.janus",
        )
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            for relative in required:
                destination = source / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / relative, destination)
            command = [
                "cmake", f"-DSOURCE_DIR={source}", "-P",
                str(ROOT / "cmake" / "verify_documentation.cmake"),
            ]
            subprocess.run(command, check=True, capture_output=True, text=True)
            changelog = source / "CHANGELOG.md"
            changelog.write_text(changelog.read_text().replace(
                "[0.22.0]: https://github.com/cyril103/janus/releases/tag/v0.22.0",
                "[0.22.0]: https://github.com/cyril103/janus/releases/tag/v0.22.0-wrong"))
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(0, result.returncode)
            self.assertIn("exact release link", result.stderr)

    def test_release_workflow_serializes_and_guards_before_publication(self):
        workflow = (ROOT / ".github" / "workflows" / "ci.yml").read_text()
        release = workflow[workflow.index("  release:"):workflow.index("  diagnostic-fuzz:")]
        self.assertIn("concurrency:", release)
        self.assertIn("cancel-in-progress: false", release)
        guard = release.index("check-promotion")
        checksums = release.index("sha256sum --check *.sha256")
        attest = release.index("actions/attest@")
        publish = release.index("gh release create")
        verify_attestations = release.index("gh attestation verify")
        promote_latest = release.index('gh release edit "$GITHUB_REF_NAME" --latest')
        promote = release.index("python3 scripts/promote_release_channel.py")
        self.assertIn("JANUS_PACKAGE_VERSION", release)
        self.assertIn(
            'project-version --source "$GITHUB_WORKSPACE/CMakeLists.txt"', release)
        self.assertNotIn('version="${GITHUB_REF_NAME#v}"', release)
        for expected in (
            'janus-${version}-Linux-x86_64.tar.gz',
            'janus-${version}-Darwin-arm64.tar.gz',
            'janus-${version}-Windows-AMD64.zip',
            'janus-language.vsix',
        ):
            self.assertIn(expected, release)
        subject_paths = release[attest:publish]
        self.assertIn("dist/release/*.vsix", subject_paths)
        verification = release[verify_attestations:promote]
        self.assertIn('--source-digest "$GITHUB_SHA"', verification)
        self.assertIn('--source-ref "$GITHUB_REF"', verification)
        self.assertIn(
            'github.com/$GITHUB_REPOSITORY/.github/workflows/ci.yml', verification)
        self.assertNotIn(
            '$GITHUB_SERVER_URL/$GITHUB_REPOSITORY/.github/workflows/ci.yml',
            verification)
        self.assertLess(guard, checksums)
        self.assertLess(checksums, attest)
        self.assertLess(attest, publish)
        self.assertLess(publish, verify_attestations)
        self.assertLess(verify_attestations, promote_latest)
        self.assertLess(promote_latest, promote)
        self.assertLess(verify_attestations, promote)
        self.assertIn("--force-with-lease", (
            ROOT / "scripts" / "promote_release_channel.py").read_text())
        self.assertNotIn("gh release upload", release)

    def test_channel_manifest_promotion_is_atomic_and_compare_and_exchange(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            remote = root / "remote.git"
            checkout = root / "checkout"
            subprocess.run(["git", "init", "--bare", str(remote)], check=True,
                           capture_output=True)
            subprocess.run(["git", "init", str(checkout)], check=True,
                           capture_output=True)
            subprocess.run(["git", "-C", str(checkout), "remote", "add", "origin",
                            str(remote)], check=True)
            manifest = root / "version"
            manifest.write_text("0.21.0 v0.21.0\n")
            first = promote_release_channel.promote(
                checkout, "stable", manifest, promote_release_channel.ABSENT)
            manifest.write_text("0.22.0 v0.22.0\n")
            second = promote_release_channel.promote(checkout, "stable", manifest, first)
            version_entry = subprocess.run(
                ["git", "-C", str(checkout), "ls-tree", second],
                check=True, text=True, capture_output=True).stdout.split()
            self.assertEqual(["100644", "blob", version_entry[2], "version"], version_entry)
            version_blob = version_entry[2]
            self.assertEqual(
                "0.22.0 v0.22.0\n",
                subprocess.run(
                    ["git", "-C", str(checkout), "cat-file", "blob", version_blob],
                    check=True, text=True, capture_output=True).stdout)
            self.assertEqual(
                first,
                subprocess.run(
                    ["git", "-C", str(checkout), "rev-parse", f"{second}^"],
                    check=True, text=True, capture_output=True).stdout.strip())

            manifest.write_text("0.23.0 v0.23.0\n")
            with self.assertRaises(RuntimeError):
                promote_release_channel.promote(checkout, "stable", manifest, first)
            self.assertEqual(
                second,
                subprocess.run(
                    ["git", "-C", str(checkout), "ls-remote", "--exit-code", "origin",
                     "refs/heads/channel-stable"],
                    check=True, text=True, capture_output=True).stdout.split()[0])

    def test_default_channel_manifests_use_atomic_git_refs(self):
        source = (ROOT / "tools" / "janusup" / "main.cpp").read_text()
        self.assertIn('name == "nightly" ? "nightly-channel" : "channel-" + name', source)
        self.assertIn("raw.githubusercontent.com/cyril103/janus/", source)
        nightly = (ROOT / ".github" / "workflows" / "nightly.yml").read_text()
        self.assertIn("cyril103/janus/channel-stable/version", nightly)
        self.assertNotIn("gh release download channel-stable", nightly)

    def test_beta_channel_manifest_preserves_the_prerelease_version(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "version"
            subprocess.run(
                ["cmake", f"-DSOURCE_DIR={ROOT}", "-DRELEASE=v0.22.0-rc.10",
                 f"-DOUTPUT={output}", "-P",
                 str(ROOT / "cmake" / "write_channel_manifest.cmake")],
                check=True)
            self.assertEqual("0.22.0-rc.10 v0.22.0-rc.10\n", output.read_text())


if __name__ == "__main__":
    unittest.main()
