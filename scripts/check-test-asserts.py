#!/usr/bin/env python3

import argparse
import contextlib
import io
import re
import sys
import tempfile
from pathlib import Path


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
STANDARD_ASSERT = re.compile(r"(?<![\w.])assert\s*\(")


def find_standard_asserts(root: Path):
    for source in sorted(root.rglob("*")):
        if not source.is_file() or source.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        for line_number, line in enumerate(
            source.read_text(encoding="utf-8").splitlines(), start=1
        ):
            if STANDARD_ASSERT.search(line):
                yield source, line_number, line.strip()


def check(root: Path) -> int:
    if not root.is_dir():
        print(f"test source directory does not exist: {root}", file=sys.stderr)
        return 2
    violations = list(find_standard_asserts(root))
    for source, line_number, line in violations:
        print(
            f"{source}:{line_number}: standard assert() is forbidden; "
            f"use JANUS_REQUIRE(): {line}",
            file=sys.stderr,
        )
    return 1 if violations else 0


def self_test() -> int:
    with tempfile.TemporaryDirectory() as temporary_directory:
        fixture = Path(temporary_directory) / "guard_fixture.cpp"
        fixture.write_text(
            "static_assert(true);\nint main() { assert(false); }\n",
            encoding="utf-8",
        )
        with contextlib.redirect_stderr(io.StringIO()):
            result = check(Path(temporary_directory))
    if result != 1:
        print("assert guard self-test failed", file=sys.stderr)
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Reject standard assert() calls from C and C++ tests."
    )
    parser.add_argument("root", nargs="?", type=Path, default=Path("tests"))
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    return self_test() if arguments.self_test else check(arguments.root)


if __name__ == "__main__":
    raise SystemExit(main())
