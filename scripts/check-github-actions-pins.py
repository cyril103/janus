#!/usr/bin/env python3
"""Reject mutable remote references in GitHub Actions workflows."""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path

USES = re.compile(r"^\s*(?:-\s*)?(?:uses|['\"]uses['\"])\s*:\s*(.*?)\s*$")
USES_KEY = re.compile(r"(?:\buses|['\"]uses['\"])\s*:")
REMOTE_ACTION = re.compile(
    r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*@([0-9a-fA-F]{40})$"
)
DOCKER_ACTION = re.compile(r"^docker://[^@\s]+@sha256:[0-9a-fA-F]{64}$")
VERSION_COMMENT = re.compile(r"^v?\d+(?:\.\d+){0,2}(?:\b|$)")


def workflow_files(root: Path) -> list[Path]:
    workflows = root / ".github" / "workflows"
    return sorted((*workflows.glob("*.yml"), *workflows.glob("*.yaml")))


def violations(root: Path) -> list[str]:
    failures: list[str] = []
    for path in workflow_files(root):
        for line_number, line in enumerate(
            path.read_text(encoding="utf-8").splitlines(), start=1
        ):
            match = USES.match(line)
            if match is None:
                if not line.lstrip().startswith("#") and USES_KEY.search(line):
                    failures.append(
                        f"{path.relative_to(root)}:{line_number}: uses must be a single-line scalar"
                    )
                continue
            payload = match.group(1)
            reference, separator, comment = payload.partition(" #")
            reference = reference.strip()
            comment = comment.strip() if separator else None
            location = f"{path.relative_to(root)}:{line_number}"
            if reference.startswith("./"):
                if ".." in Path(reference).parts:
                    failures.append(
                        f"{location}: local actions must remain inside the repository"
                    )
                continue
            if reference.startswith("docker://"):
                if DOCKER_ACTION.fullmatch(reference) is None:
                    failures.append(
                        f"{location}: Docker actions must use an immutable sha256 digest"
                    )
                continue
            if REMOTE_ACTION.fullmatch(reference) is None:
                failures.append(
                    f"{location}: remote action must be pinned to a full 40-character commit SHA"
                )
                continue
            if comment is None or VERSION_COMMENT.match(comment) is None:
                failures.append(
                    f"{location}: pinned remote action must retain a version comment such as '# v7'"
                )
    return failures


def run_self_test() -> int:
    cases = {
        "pinned": ("actions/checkout@" + "a" * 40 + " # v7", True),
        "mutable-tag": ("actions/checkout@v7", False),
        "short-sha": ("actions/checkout@deadbeef # v7", False),
        "missing-comment": ("actions/checkout@" + "a" * 40, False),
        "invalid-comment": ("actions/checkout@" + "a" * 40 + " # pinned", False),
        "local": ("./.github/actions/build", True),
        "local-traversal": ("./../outside", False),
        "docker-digest": ("docker://alpine@sha256:" + "b" * 64, True),
        "docker-tag": ("docker://alpine:3.22", False),
        "dynamic": ("${{ matrix.action }}", False),
    }
    with tempfile.TemporaryDirectory(prefix="janus-action-pin-test-") as temporary:
        root = Path(temporary)
        workflows = root / ".github" / "workflows"
        workflows.mkdir(parents=True)
        for name, (reference, expected) in cases.items():
            path = workflows / f"{name}.yml"
            path.write_text(
                f"name: {name}\njobs:\n  test:\n    steps:\n      - uses: {reference}\n",
                encoding="utf-8",
            )
            actual = not violations(root)
            path.unlink()
            if actual != expected:
                print(
                    f"self-test {name} failed: expected accepted={expected}, got {actual}",
                    file=sys.stderr,
                )
                return 1
        inline = workflows / "inline.yml"
        inline.write_text(
            "name: inline\njobs:\n  test:\n    steps:\n"
            "      - { uses: actions/checkout@v7 }\n",
            encoding="utf-8",
        )
        if not violations(root):
            print("self-test inline mapping failed: expected rejection", file=sys.stderr)
            return 1
    print("GitHub Actions pin policy self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", nargs="?", type=Path, default=Path.cwd())
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    if arguments.self_test:
        return run_self_test()

    root = arguments.root.resolve()
    failures = violations(root)
    if failures:
        print("GitHub Actions pin policy violations:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    print("GitHub Actions pin policy passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
