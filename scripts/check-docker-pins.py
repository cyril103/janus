#!/usr/bin/env python3
"""Require immutable external bases in the publication Dockerfiles."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys
import unittest


PUBLICATION_DOCKERFILES = ("website/Dockerfile", "registry/Dockerfile")
DIGEST = re.compile(r"[^\s@$]+@sha256:[0-9a-f]{64}")
FROM = re.compile(
    r"FROM\s+(?:--platform=\S+\s+)?(\S+)(?:\s+AS\s+([\w-]+))?\s*",
    re.IGNORECASE,
)


def violations(source: str) -> list[str]:
    failures = []
    stages: set[str] = set()
    count = 0
    for number, line in enumerate(source.splitlines(), 1):
        line = line.strip()
        if not re.match(r"FROM(?:\s|$)", line, re.IGNORECASE):
            continue
        count += 1
        match = FROM.fullmatch(line)
        if match is None:
            failures.append(f"{number}: FROM must be a single-line image reference")
            continue
        reference, alias = match.groups()
        if reference != "scratch" and reference.lower() not in stages:
            if DIGEST.fullmatch(reference) is None:
                failures.append(f"{number}: external base must use a full sha256 digest: {reference}")
        if alias:
            stages.add(alias.lower())
    if not count:
        failures.append("no FROM instruction found")
    return failures


class PinPolicyTests(unittest.TestCase):
    def test_references(self):
        pinned = "python:3.13-alpine@sha256:" + "a" * 64
        cases = [
            (f"FROM {pinned} AS builder", True),
            (f"from --platform=linux/amd64 {pinned} as builder", True),
            (f"FROM {pinned} AS builder\nFROM builder AS final", True),
            ("FROM scratch", True),
            ("FROM python:3.13-alpine", False),
            ("FROM python", False),
            ("FROM ${BASE}", False),
            ("FROM ${BASE}@sha256:" + "a" * 64, False),
            ("FROM python@sha256:abcdef", False),
            ("FROM builder\nFROM " + pinned + " AS builder", False),
            ("FROM \\\n python:3.13-alpine", False),
            ("# FROM python\n", False),
            (f"FROM {pinned}\nFROM nginx:alpine", False),
        ]
        for source, accepted in cases:
            with self.subTest(source=source):
                self.assertEqual(not violations(source), accepted)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", nargs="?", type=Path, default=Path.cwd())
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        result = unittest.TextTestRunner().run(
            unittest.defaultTestLoader.loadTestsFromTestCase(PinPolicyTests)
        )
        return 0 if result.wasSuccessful() else 1
    failures = []
    for name in PUBLICATION_DOCKERFILES:
        try:
            source = (args.root / name).read_text(encoding="utf-8")
        except OSError as error:
            failures.append(f"{name}: {error}")
            continue
        failures.extend(f"{name}:{failure}" for failure in violations(source))
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print("Publication Docker image pin policy passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
