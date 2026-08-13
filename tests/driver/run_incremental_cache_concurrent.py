#!/usr/bin/env python3
import argparse
import hashlib
import pathlib
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--janus", required=True, type=pathlib.Path)
    parser.add_argument("--work-dir", required=True, type=pathlib.Path)
    args = parser.parse_args()

    root = args.work_dir.resolve()
    project = root / "project"
    if root.exists():
        import shutil
        shutil.rmtree(root)
    (project / "src" / "lib").mkdir(parents=True)
    (project / "janus.toml").write_text(
        '[package]\nname = "concurrent-cache"\nversion = "0.1.0"\n'
        'entry = "src/main.janus"\n',
        encoding="utf-8",
    )
    (project / "src" / "main.janus").write_text(
        "import lib.answer\ndef main() : int { return answer() }\n",
        encoding="utf-8",
    )
    (project / "src" / "lib" / "answer.janus").write_text(
        "module lib.answer\ndef answer() : int { return 9 }\n",
        encoding="utf-8",
    )

    command = [str(args.janus.resolve()), "build"]
    suffix = ".exe" if sys.platform == "win32" else ""
    executable = project / "target" / "debug" / f"concurrent-cache{suffix}"
    cache = project / "target" / ".janus-cache" / "v1"

    for iteration in range(20):
        processes = [
            subprocess.Popen(command, cwd=project, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True)
            for _ in range(8)
        ]
        failures = []
        for index, process in enumerate(processes):
            stdout, stderr = process.communicate(timeout=60)
            if process.returncode != 0:
                failures.append(
                    f"iteration {iteration}, build {index}: {process.returncode}"
                    f"\n{stdout}\n{stderr}"
                )
        if failures:
            raise AssertionError("concurrent builds failed:\n" + "\n".join(failures))

        if not executable.is_file():
            raise AssertionError("concurrent build did not produce the executable")
        run = subprocess.run([str(executable)], cwd=project, check=False)
        if run.returncode != 9:
            raise AssertionError(f"concurrent output is invalid: exit={run.returncode}")

        entries = list((cache / "entries").glob("*.entry"))
        artifacts = list((cache / "artifacts").glob("*.bin"))
        temporaries = list(cache.rglob("*.tmp-*"))
        if len(entries) != 1 or len(artifacts) != 1 or temporaries:
            raise AssertionError(
                f"cache publication is not clean: entries={len(entries)}, "
                f"artifacts={len(artifacts)}, temporaries={temporaries}"
            )

        cached_digest = hashlib.sha256(artifacts[0].read_bytes()).digest()
        output_digest = hashlib.sha256(executable.read_bytes()).digest()
        if cached_digest != output_digest:
            raise AssertionError("published cache artifact differs from final output")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
