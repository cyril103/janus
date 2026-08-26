import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import nightly_release  # noqa: E402


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
        promote = release.index('gh release upload "channel-$JANUS_RELEASE_CHANNEL"')
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

    def test_beta_channel_manifest_preserves_the_prerelease_version(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "version"
            subprocess.run(
                ["cmake", f"-DSOURCE_DIR={ROOT}", "-DRELEASE=v0.21.0-rc.10",
                 f"-DOUTPUT={output}", "-P",
                 str(ROOT / "cmake" / "write_channel_manifest.cmake")],
                check=True)
            self.assertEqual("0.21.0-rc.10 v0.21.0-rc.10\n", output.read_text())


if __name__ == "__main__":
    unittest.main()
