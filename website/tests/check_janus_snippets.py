#!/usr/bin/env python3
"""Compile les blocs Janus autonomes publiés dans le Book et les tutoriels."""

from __future__ import annotations

import argparse
import re
import subprocess
import tempfile
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
DOCS = REPOSITORY / "website" / "docs"
FENCE = re.compile(r"```janus(?:[^\n]*)\n(.*?)```", re.DOTALL)


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

    version = subprocess.run(
        [str(compiler), "--version"],
        cwd=REPOSITORY,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
        timeout=30,
        check=True,
    ).stdout.strip()

    failures: list[str] = []
    checked = 0
    pages = [DOCS / "index.md"]
    pages.extend(sorted((DOCS / "book").glob("*.md")))
    pages.extend(sorted((DOCS / "tutorials").glob("*.md")))

    with tempfile.TemporaryDirectory(prefix="janus-site-snippets-") as temporary:
        temporary_path = Path(temporary)
        for page in pages:
            text = page.read_text(encoding="utf-8")
            for index, code in enumerate(FENCE.findall(text), start=1):
                if "def main()" not in code:
                    continue
                checked += 1
                source = temporary_path / f"{page.stem}-{index}.janus"
                source.write_text(code.strip() + "\n", encoding="utf-8")
                result = subprocess.run(
                    [str(compiler), "check", str(source)],
                    cwd=REPOSITORY,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                    capture_output=True,
                    timeout=30,
                    check=False,
                )
                if result.returncode != 0:
                    failures.append(
                        f"{page.relative_to(REPOSITORY)} bloc {index}:\n"
                        f"{result.stdout}{result.stderr}"
                    )

    if failures:
        print("\n".join(failures))
        return 1
    print(f"{checked} blocs Janus autonomes compilés avec {version}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
