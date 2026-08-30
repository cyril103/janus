#!/usr/bin/env python3
"""Keep J0000 out of production and reduce subsystem-generic diagnostics."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path


def source_texts(root: Path) -> list[str]:
    return [
        path.read_text(encoding="utf-8")
        for path in sorted((root / "src").rglob("*.cpp"))
    ]


def production_texts(root: Path) -> list[str]:
    paths = []
    for directory in ("include", "src", "tools"):
        paths.extend((root / directory).rglob("*.cpp"))
        paths.extend((root / directory).rglob("*.hpp"))
    return [path.read_text(encoding="utf-8") for path in sorted(paths)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root.resolve()
    baseline = json.loads(
        (root / "docs" / "diagnostic-migration-baseline.json").read_text(
            encoding="utf-8"
        )
    )
    texts = source_texts(root)
    all_calls = sum(len(re.findall(r"CompileError\s*\{", text)) for text in texts)
    classified_calls = sum(
        len(re.findall(r"CompileError\s*\{\s*DiagnosticCode::", text, re.DOTALL))
        for text in texts
    )
    current = {
        "legacy_compile_error_calls": all_calls - classified_calls,
        "j0000_production_references": sum(
            text.count("J0000") + text.count("DiagnosticCode::Unclassified")
            for text in production_texts(root)
        ),
    }
    errors = []
    for metric, value in current.items():
        limit = baseline[metric]
        if value > limit:
            errors.append(f"{metric}: {value} dépasse la baseline {limit}")
    for error in errors:
        print(f"ERROR {error}", file=sys.stderr)
    print(
        "Migration diagnostics: "
        f"{current['legacy_compile_error_calls']} appels à préciser, "
        f"{current['j0000_production_references']} référence J0000 en production"
    )
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
