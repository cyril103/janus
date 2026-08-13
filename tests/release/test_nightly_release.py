import datetime as dt
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import nightly_release  # noqa: E402


SHA = "0123456789abcdef0123456789abcdef01234567"


class ManifestTests(unittest.TestCase):
    def test_reads_project_version_for_nightly_package_identity(self):
        self.assertEqual(nightly_release.project_version(ROOT / "CMakeLists.txt"),
                         "0.10.0")

    def test_round_trip_snapshot_manifest(self):
        manifest = nightly_release.ChannelManifest(
            "0.10.0-nightly.20260813.01234567", f"nightly-{SHA}", SHA,
            "2026-08-13T03:17:00Z")
        self.assertEqual(manifest, nightly_release.parse_manifest(manifest.render()))

    def test_legacy_two_field_manifest_remains_supported(self):
        parsed = nightly_release.parse_manifest("0.10.0 v0.10.0\n")
        self.assertEqual((parsed.version, parsed.release), ("0.10.0", "v0.10.0"))
        self.assertIsNone(parsed.source_sha)

    def test_rejects_trailing_garbage_and_non_full_sha(self):
        with self.assertRaises(ValueError):
            nightly_release.parse_manifest(
                f"0.10.0-nightly nightly-{SHA} {SHA} 2026-08-13T03:17:00Z garbage\n")
        with self.assertRaises(ValueError):
            nightly_release.ChannelManifest("0.10.0-nightly", "nightly-deadbeef", "deadbeef",
                                             "2026-08-13T03:17:00Z").validate()


class FreshnessTests(unittest.TestCase):
    def test_rejects_nightly_older_than_stable_or_threshold(self):
        now = dt.datetime(2026, 8, 13, 12, tzinfo=dt.timezone.utc)
        nightly = nightly_release.ChannelManifest("0.10.1-nightly", f"nightly-{SHA}", SHA,
                                                  "2026-08-10T00:00:00Z")
        stable = nightly_release.ChannelManifest("0.10.0", "v0.10.0", SHA,
                                                 "2026-08-11T00:00:00Z")
        with self.assertRaisesRegex(ValueError, "older than stable"):
            nightly_release.check_freshness(nightly, stable, now, 168)
        stable = nightly_release.ChannelManifest("0.10.0", "v0.10.0", SHA,
                                                 "2026-08-01T00:00:00Z")
        with self.assertRaisesRegex(ValueError, "exceeds"):
            nightly_release.check_freshness(nightly, stable, now, 48)

    def test_rejects_a_future_nightly_timestamp(self):
        now = dt.datetime(2026, 8, 13, 12, tzinfo=dt.timezone.utc)
        nightly = nightly_release.ChannelManifest(
            "0.10.1-nightly", f"nightly-{SHA}", SHA,
            "2026-08-13T12:00:01Z")
        stable = nightly_release.ChannelManifest(
            "0.10.0", "v0.10.0", SHA, "2026-08-11T00:00:00Z")
        with self.assertRaisesRegex(ValueError, "future"):
            nightly_release.check_freshness(nightly, stable, now, 168)


class AtomicPublishTests(unittest.TestCase):
    def test_failure_at_every_gate_preserves_channel_manifest(self):
        for failure in ("upload", "checksum", "attestation", "smoke"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                channel = root / "version"
                channel.write_text("0.10.0 nightly-old\n")
                publisher = nightly_release.LocalPublisher(root, fail_at=failure)
                with self.assertRaises(RuntimeError):
                    nightly_release.publish(publisher, b"candidate", "new manifest\n")
                self.assertEqual(channel.read_text(), "0.10.0 nightly-old\n")

    def test_manifest_is_last_operation(self):
        with tempfile.TemporaryDirectory() as directory:
            publisher = nightly_release.LocalPublisher(Path(directory))
            nightly_release.publish(publisher, b"candidate", "new manifest\n")
            self.assertEqual(publisher.events,
                             ["upload", "checksum", "attestation", "smoke", "promote"])


class WorkflowPolicyTests(unittest.TestCase):
    def test_nightly_workflow_is_manual_without_long_fuzz_and_all_uses_are_pinned(self):
        workflow = (ROOT / ".github/workflows/nightly.yml").read_text()
        self.assertIn("workflow_dispatch:", workflow)
        self.assertNotIn("duration 3600", workflow)
        self.assertIn("cyril103/janus8", workflow)
        self.assertIn(
            "nightly-$GITHUB_SHA-$GITHUB_RUN_ID-$GITHUB_RUN_ATTEMPT", workflow)
        self.assertIn("$GITHUB_SHA.$GITHUB_RUN_ID.$GITHUB_RUN_ATTEMPT", workflow)
        self.assertIn("needs: identity", workflow)
        self.assertIn("JANUS_PACKAGE_VERSION='${{ needs.identity.outputs.version }}'", workflow)
        self.assertIn("EXPECTED_VERSION: ${{ needs.identity.outputs.version }}", workflow)
        self.assertIn("lld: /usr/lib/llvm-18/bin/ld.lld", workflow)
        self.assertIn("cc: /usr/bin/clang", workflow)
        self.assertIn("llvm: /opt/homebrew/opt/llvm@19/lib/cmake/llvm", workflow)
        self.assertIn("lld: /opt/homebrew/opt/lld@19/bin/ld64.lld", workflow)
        self.assertIn("brew install llvm@19 lld@19 ninja", workflow)
        self.assertIn('echo "/usr/lib/llvm-18/bin" >> "$GITHUB_PATH"', workflow)
        self.assertIn("install: git mingw-w64-clang-x86_64-clang", workflow)
        self.assertIn("shell: msys2 {0}", workflow)
        self.assertIn(
            "actions/download-artifact@3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c",
            workflow)
        self.assertIn(
            'test -f "dist/janus-$EXPECTED_VERSION-Windows-AMD64.zip"',
            workflow)
        self.assertNotIn("Windows-x86_64.zip", workflow)
        self.assertIn("re.fullmatch(r\"[0-9a-f]{40}\", old)", workflow)
        self.assertIn('payload.update({"parents":[old]} if old else {})', workflow)
        self.assertNotIn('--jq .object.sha 2>/dev/null || true', workflow)
        self.assertIn('else\n            old=""', workflow)
        self.assertNotIn("gh release upload nightly", workflow)
        self.assertNotIn("gh release upload channel-nightly", workflow)
        self.assertIn("git/refs/heads/nightly-channel", workflow)
        self.assertLess(
            workflow.index("E2E janusup and Janus8 smoke against candidate"),
            workflow.index("Atomically advance the channel branch"))
        checker = subprocess.run(
            [sys.executable, str(ROOT / "scripts/check-github-actions-pins.py"), str(ROOT)],
            text=True, capture_output=True)
        self.assertEqual(checker.returncode, 0, checker.stdout + checker.stderr)

    def test_apple_disables_only_leak_detection_not_address_sanitizer(self):
        scripts = [
            ROOT / "tests/runtime/run_janus_example.cmake",
            ROOT / "tests/runtime/run_janus_trap.cmake",
            ROOT / "tests/interop/run_c_interop.cmake",
        ]
        for script in scripts:
            text = script.read_text()
            self.assertIn("APPLE", text, script)
            self.assertIn("detect_leaks=0:halt_on_error=1", text, script)
            self.assertIn("-fsanitize=", text, script)


if __name__ == "__main__":
    unittest.main()
