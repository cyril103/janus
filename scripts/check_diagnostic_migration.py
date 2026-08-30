#!/usr/bin/env python3
"""Prevent growth of legacy J0000-producing diagnostic paths."""

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
        "explicit_unclassified_diagnostics": sum(
            1
            for text in texts
            for _ in re.findall(r"DiagnosticCode::Unclassified", text)
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
        f"{current['legacy_compile_error_calls']} appels historiques, "
        f"{current['explicit_unclassified_diagnostics']} J0000 explicites"
    )
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
