#!/usr/bin/env python3
"""Compile les doctests Janus du site avec le moteur officiel de ``janus test``."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--compiler",
        type=Path,
        default=REPOSITORY / "build" / "janus",
        help="binaire janus à utiliser (par défaut build/janus)",
    )
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    if not compiler.is_file():
        raise SystemExit(
            f"Compilateur absent : {compiler} "
            "(configurez et compilez Janus, ou utilisez --compiler)"
        )

    result = subprocess.run(
        [
            str(compiler),
            "test",
            "--doc",
            "--doc-path",
            "docs",
            "--locked",
        ],
        cwd=REPOSITORY / "website",
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        timeout=120,
        check=False,
    )
    print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=__import__("sys").stderr)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
