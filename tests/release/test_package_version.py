import io
import json
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import validate_package_identity


class PackageVersionTests(unittest.TestCase):
    def test_reconfigure_same_build_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source"
            build = Path(directory) / "build"
            source.mkdir()

            def configure(version, *options):
                (source / "CMakeLists.txt").write_text(
                    'cmake_minimum_required(VERSION 3.21)\n'
                    f'project(test VERSION {version} LANGUAGES NONE)\n'
                    f'include("{ROOT.as_posix()}/cmake/package_version.cmake")\n'
                    'file(WRITE "${CMAKE_BINARY_DIR}/version" "${JANUS_PACKAGE_VERSION}")\n')
                return subprocess.run(
                    ["cmake", "-S", str(source), "-B", str(build), *options],
                    capture_output=True, text=True, check=True).stdout

            configure("0.23.0", "-DJANUS_PACKAGE_VERSION:STRING=0.12.0")
            self.assertEqual((build / "version").read_text(), "0.23.0")
            self.assertNotIn("JANUS_PACKAGE_VERSION:STRING", (build / "CMakeCache.txt").read_text())
            configure("0.24.0")
            self.assertEqual((build / "version").read_text(), "0.24.0")
            output = configure("0.24.0", "-DJANUS_PACKAGE_VERSION_OVERRIDE=0.24.0-nightly.example")
            self.assertIn("override active: 0.24.0-nightly.example", output)
            configure("0.25.0")
            self.assertEqual((build / "version").read_text(), "0.24.0-nightly.example")
            configure("0.25.0", "-DJANUS_PACKAGE_VERSION_OVERRIDE=")
            self.assertEqual((build / "version").read_text(), "0.25.0")

    def test_archive_manifest_and_each_binary_must_agree(self):
        for platform, extension in (("Linux-x86_64", ".tar.gz"), ("Windows-AMD64", ".zip")):
            for version in ("0.24.0", "0.24.0-nightly.example"):
                with self.subTest(platform=platform, version=version), tempfile.TemporaryDirectory() as directory:
                    basename = f"janus-{version}-{platform}"
                    archive = Path(directory) / (basename + extension)
                    manifest = {"schema_version": 1, "version": version, "revision": "a" * 40}

                    def write_archive(value):
                        name = basename + "/share/janus/build-identity.json"
                        content = json.dumps(value).encode()
                        if extension == ".zip":
                            with zipfile.ZipFile(archive, "w") as output:
                                output.writestr(name, content)
                        else:
                            with tarfile.open(archive, "w:gz") as output:
                                entry = tarfile.TarInfo(name)
                                entry.size = len(content)
                                output.addfile(entry, io.BytesIO(content))

                    def result(value):
                        return subprocess.CompletedProcess([], 0, json.dumps(value))

                    write_archive(manifest)
                    with patch.object(validate_package_identity.subprocess, "run", return_value=result(manifest)) as run:
                        validate_package_identity.validate(archive, version, platform)
                        self.assertEqual(run.call_count, 3)
                    with self.assertRaisesRegex(ValueError, "archive name"):
                        validate_package_identity.validate(archive, "0.23.0", platform)
                    write_archive(dict(manifest, version="0.23.0"))
                    with self.assertRaisesRegex(ValueError, "manifest version"):
                        validate_package_identity.validate(archive, version, platform)
                    write_archive(manifest)
                    for index, tool in enumerate(("janus", "janus-lsp", "janusup")):
                        results = [result(manifest)] * 3
                        results[index] = result(dict(manifest, revision="b" * 40))
                        with patch.object(validate_package_identity.subprocess, "run", side_effect=results):
                            with self.assertRaisesRegex(ValueError, tool + " identity"):
                                validate_package_identity.validate(archive, version, platform)


if __name__ == "__main__":
    unittest.main()
