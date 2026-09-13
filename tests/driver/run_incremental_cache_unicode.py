#!/usr/bin/env python3
"""Unicode module imports must behave identically in every CLI cache mode."""

import argparse
import os
import pathlib
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--janus", required=True, type=pathlib.Path)
    parser.add_argument("--work-dir", required=True, type=pathlib.Path)
    args = parser.parse_args()
    janus = str(args.janus.resolve())
    root = args.work_dir.resolve()
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)

    def run(project, *command, valid=True):
        result = subprocess.run(
            [janus, *command], cwd=project, capture_output=True,
            text=True, encoding="utf-8", errors="replace",
            env={**os.environ, "LC_ALL": "C"},
        )
        assert (result.returncode == 0) == valid, (
            f"{project.name}: {command}: {result.returncode}\n"
            f"{result.stdout}\n{result.stderr}"
        )

    cases = [
        ("latin", "café", "café", ["café"]),
        ("non_latin", "数学", "数学", ["数学"]),
        ("qualified", "données.数学.Δ2", "données.数学.Δ2", ["données.数学.Δ2"]),
        ("nfc", "café", "cafe\u0301", ["cafe\u0301", "café"]),
        ("qualified_nfc", "données.école", "donne\u0301es.e\u0301cole",
         ["données.e\u0301cole", "donne\u0301es.école"]),
        ("ascii", "_lib.answer2", "_lib.answer2", ["_lib.answer2"]),
    ]
    for name, module_path, declaration, imports in cases:
        project = root / name
        project.mkdir()
        dependency = project.joinpath(*module_path.split(".")).with_suffix(".janus")
        dependency.parent.mkdir(parents=True, exist_ok=True)
        dependency.write_text(
            f"module {declaration}\ndef answer() : int {{ return 42 }}\n",
            encoding="utf-8",
        )
        (project / "main.janus").write_text(
            "".join(f"import {module}\n" for module in imports)
            + "def main() : int { return answer() }\n", encoding="utf-8",
        )
        run(project, "check", "main.janus")
        run(project, "build", "main.janus", "--emit", "llvm-ir",
            "--no-cache", "-o", "uncached.ll")
        cache = project / ".janus-cache" / "v1"
        assert not cache.exists(), "--no-cache created a cache"
        run(project, "build", "main.janus", "--emit", "llvm-ir", "-o", "cold.ll")
        entries = {p: (p.read_bytes(), p.stat().st_mtime_ns)
                   for p in cache.glob("entries/*.entry")}
        assert len(entries) == 1, f"{name}: cold build did not populate the cache"
        run(project, "build", "main.janus", "--emit", "llvm-ir", "-o", "warm.ll")
        assert entries == {p: (p.read_bytes(), p.stat().st_mtime_ns)
                           for p in cache.glob("entries/*.entry")}, (
            f"{name}: warm build rewrote the cache instead of reusing it"
        )
        clean = (project / "uncached.ll").read_bytes()
        assert clean == (project / "cold.ll").read_bytes(), name
        assert clean == (project / "warm.ll").read_bytes(), name

    # The CLI must reject malformed segments before resolving or snapshotting paths.
    invalid = [".café", "café.", "café..数学", "../café", "café/数学",
               "café\\数学", "2café", "\u0301café", "café\u200d", "café😀", "café\udcff"]
    for index, module in enumerate(invalid):
        project = root / f"invalid_{index}"
        project.mkdir()
        (project / "main.janus").write_bytes(
            (f"import {module}\ndef main() : int {{ return 0 }}\n")
            .encode("utf-8", errors="surrogateescape")
        )
        run(project, "check", "main.janus", valid=False)
        for flags in [("--no-cache",), ()]:
            run(project, "build", "main.janus", "--emit", "llvm-ir",
                "-o", "invalid.ll", *flags, valid=False)
            assert not (project / "invalid.ll").exists()


if __name__ == "__main__":
    main()
