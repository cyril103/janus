#!/usr/bin/env python3
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def run(command, env):
    return subprocess.run(command, env=env, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--janusup", required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="janus-archive-test-") as temporary:
        fixtures = Path(temporary) / "fixtures"
        subprocess.run([sys.executable, str(args.source / "tests/fixtures/toolchain-archives/generate.py"),
                        str(fixtures)], check=True)
        manifest = json.loads((fixtures / "manifest.json").read_text())
        powershell = shutil.which("pwsh") or shutil.which("powershell")
        validators = {
            "janusup": lambda archive, root: [str(args.janusup), "validate-archive", str(archive), root],
            "install.sh": lambda archive, root: ["sh", str(args.source / "scripts/install.sh"),
                                                   "--validate-archive", str(archive), root],
        }
        if powershell:
            validators["install.ps1"] = lambda archive, root: [powershell, "-NoProfile", "-File",
                str(args.source / "scripts/install.ps1"), "-ValidateArchivePath", str(archive),
                "-ExpectedRoot", root]
        env = os.environ.copy()
        env.update(JANUS_ARCHIVE_TEST_MAX_ENTRIES="8",
                   JANUS_ARCHIVE_TEST_MAX_FILE_SIZE="64",
                   JANUS_ARCHIVE_TEST_MAX_TOTAL_SIZE="72")
        failures = []
        for case in manifest:
            formats = case.get("formats", ["tar.gz", "zip"])
            for extension in formats:
                archive = fixtures / f"{case['name']}.{extension}"
                expected_root = "janus-test-Linux-x86_64"
                for validator, command in validators.items():
                    if validator == "janusup" and extension != ("zip" if os.name == "nt" else "tar.gz"):
                        continue
                    if validator == "install.sh" and extension != "tar.gz":
                        continue
                    if validator == "install.ps1" and extension != "zip":
                        continue
                    result = run(command(archive, expected_root), env)
                    actual = result.returncode == 0
                    if actual != case["accepted"]:
                        failures.append(f"{validator}: {archive.name}: expected "
                                        f"{'accept' if case['accepted'] else 'reject'}, exit={result.returncode}\n"
                                        f"stdout={result.stdout}\nstderr={result.stderr}")
        if failures:
            print("\n".join(failures), file=sys.stderr)
            return 1
        if not powershell:
            print("archive validation: PowerShell unavailable; C++ and POSIX paths tested")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
