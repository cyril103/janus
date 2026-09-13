#!/usr/bin/env python3
"""Exercise real downloads and extraction with an instrumented POSIX tar."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--janusup", type=Path, required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--arch", required=True)
    args = parser.parse_args()
    janusup = args.janusup.resolve()
    real_tar = shutil.which("tar")
    assert real_tar
    with tempfile.TemporaryDirectory(prefix="janusup-scratch-test-") as temporary:
        root = Path(temporary)
        scratch = root / "temporary spaces é漢"
        scratch.mkdir()
        sentinel = scratch / "unrelated"
        sentinel.mkdir()
        (sentinel / "keep").write_text("untouched")
        basename = f"janus-1.2.3-{args.platform}-{args.arch}"
        package = root / basename
        (package / "bin").mkdir(parents=True)
        for program in ("janus", "janusc", "janusup", "janus-lsp"):
            (package / "bin" / program).write_text("#!/bin/sh\nexit 0\n")
            (package / "bin" / program).chmod(0o755)
        mirror = root / "dist"
        release = mirror / "v1.2.3"
        release.mkdir(parents=True)
        archive = release / f"{basename}.tar.gz"
        with tarfile.open(archive, "w:gz") as output:
            output.add(package, arcname=basename)
        checksum = Path(str(archive) + ".sha256")
        checksum.write_text(hashlib.sha256(archive.read_bytes()).hexdigest())
        wrappers = root / "wrappers"
        wrappers.mkdir()
        wrapper = wrappers / "tar"
        wrapper.write_text('''#!/usr/bin/env python3
import json, os, pathlib, subprocess, sys, time
if "-xf" in sys.argv:
    archive = pathlib.Path(sys.argv[sys.argv.index("-xf") + 1])
    destination = pathlib.Path(sys.argv[sys.argv.index("-C") + 1])
    record = pathlib.Path(os.environ["TAR_RECORDS"]) / str(os.getpid())
    pending = record.parent.parent / (record.name + ".pending")
    pending.write_text(json.dumps({"path": str(archive.parent),
                                  "mode": archive.parent.stat().st_mode & 0o777}))
    pending.rename(record)
    deadline = time.monotonic() + 15
    while not pathlib.Path(os.environ["TAR_RELEASE"]).exists():
        if time.monotonic() > deadline:
            sys.exit(90)
        time.sleep(0.01)
    if os.environ.get("TAR_FAIL") == "1":
        (destination / "partial").write_text("partially extracted")
        sys.exit(42)
sys.exit(subprocess.call([os.environ["REAL_TAR"], *sys.argv[1:]]))
''')
        wrapper.chmod(0o755)
        env = os.environ.copy()
        env.update(TMPDIR=str(scratch), TMP=str(scratch), TEMP=str(scratch),
                   JANUS_DIST_SERVER=str(mirror),
                   JANUS_ALLOW_UNVERIFIED_PRIVATE_MIRROR="1",
                   PATH=str(wrappers) + os.pathsep + env["PATH"], REAL_TAR=real_tar)

        def assert_clean():
            assert set(scratch.iterdir()) == {sentinel}, list(scratch.iterdir())
            assert (sentinel / "keep").read_text() == "untouched"

        for fail in (False, True):
            records = root / f"records-{fail}"
            records.mkdir()
            release_marker = root / f"release-{fail}"
            processes = []
            try:
                for index in range(2):
                    child_env = dict(env, JANUSUP_HOME=str(root / f"home-{fail}-{index}"),
                                     TAR_RECORDS=str(records), TAR_RELEASE=str(release_marker),
                                     TAR_FAIL="1" if fail else "0")
                    processes.append(subprocess.Popen(
                        [str(janusup), "install", "1.2.3"], env=child_env,
                        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE))
                deadline = time.monotonic() + 15
                while len(list(records.iterdir())) < 2:
                    assert time.monotonic() < deadline, "extractions did not start"
                    assert all(p.poll() is None for p in processes), "installer exited early"
                    time.sleep(0.01)
                observed = [json.loads(path.read_text()) for path in records.iterdir()]
                assert len({item["path"] for item in observed}) == 2, observed
                for item in observed:
                    assert Path(item["path"]).parent == scratch, observed
                    assert item["mode"] == 0o700, observed
                release_marker.touch()
                for process in processes:
                    stdout, stderr = process.communicate(timeout=30)
                    assert (process.returncode != 0) == fail, stdout + stderr
                    if fail:
                        assert "could not extract" in stderr, stderr
                assert_clean()
            finally:
                release_marker.touch()
                for process in processes:
                    if process.poll() is None:
                        process.kill()
                    process.communicate()

        # Errors before extraction must also release the download directory.
        channel = mirror / "channel-stable"
        channel.mkdir()
        (channel / "version").write_text("invalid manifest with trailing garbage")
        checksum.write_text("0" * 64)
        for name in ("stable", "1.2.3"):
            result = subprocess.run([str(janusup), "install", name],
                                    env=dict(env, JANUSUP_HOME=str(root / "error-home")),
                                    text=True, capture_output=True, timeout=30)
            assert result.returncode != 0, result.stdout + result.stderr
            expected = "channel manifest" if name == "stable" else "SHA-256 verification failed"
            assert expected in result.stderr, result.stderr
            assert_clean()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
