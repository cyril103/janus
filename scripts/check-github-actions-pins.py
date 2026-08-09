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
BLOCK_SCALAR = re.compile(
    r"^(\s*)(?:-\s*)?[A-Za-z0-9_.-]+\s*:\s*[|>][+-]?\d?(?:\s+#.*)?$"
)
REMOTE_ACTION = re.compile(
    r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*@([0-9a-fA-F]{40})$"
)
DOCKER_ACTION = re.compile(r"^docker://[^@\s]+@sha256:[0-9a-fA-F]{64}$")
VERSION_COMMENT = re.compile(r"^v?\d+(?:\.\d+){0,2}(?:\b|$)")


def without_block_scalar_bodies(text: str) -> str:
    masked: list[str] = []
    block_indent: int | None = None
    for line in text.splitlines(keepends=True):
        content = line.rstrip("\r\n")
        indentation = len(content) - len(content.lstrip(" "))
        if block_indent is not None:
            if not content.strip() or indentation > block_indent:
                masked.append(" " * len(content) + line[len(content) :])
                continue
            block_indent = None
        match = BLOCK_SCALAR.fullmatch(content)
        if match is not None:
            block_indent = len(match.group(1))
        masked.append(line)
    return "".join(masked)


def escaped_mapping_key_lines(text: str) -> set[int]:
    text = without_block_scalar_bodies(text)
    lines: set[int] = set()
    index = 0
    line_number = 1
    while index < len(text):
        character = text[index]
        if character == "\n":
            line_number += 1
            index += 1
            continue
        if character == "#" and (index == 0 or text[index - 1].isspace()):
            newline = text.find("\n", index)
            if newline < 0:
                break
            index = newline
            continue
        if character == "'":
            index += 1
            while index < len(text):
                if text[index] == "\n":
                    line_number += 1
                if text[index] == "'":
                    if index + 1 < len(text) and text[index + 1] == "'":
                        index += 2
                        continue
                    index += 1
                    break
                index += 1
            continue
        if character != '"':
            index += 1
            continue

        start_line = line_number
        escaped = False
        index += 1
        while index < len(text):
            if text[index] == "\\":
                escaped = True
                if index + 1 < len(text) and text[index + 1] == "\n":
                    line_number += 1
                index += 2
                continue
            if text[index] == "\n":
                line_number += 1
            if text[index] == '"':
                index += 1
                break
            index += 1
        following = index
        while following < len(text) and text[following].isspace():
            following += 1
        if escaped and following < len(text) and text[following] == ":":
            lines.add(start_line)
    return lines


def workflow_files(root: Path) -> list[Path]:
    workflows = root / ".github" / "workflows"
    return sorted((*workflows.glob("*.yml"), *workflows.glob("*.yaml")))


def violations(root: Path) -> list[str]:
    failures: list[str] = []
    for path in workflow_files(root):
        text = path.read_text(encoding="utf-8")
        escaped_key_lines = escaped_mapping_key_lines(text)
        for line_number, line in enumerate(
            text.splitlines(), start=1
        ):
            location = f"{path.relative_to(root)}:{line_number}"
            if line_number in escaped_key_lines:
                failures.append(
                    f"{location}: escaped double-quoted mapping keys are forbidden"
                )
                continue
            match = USES.match(line)
            if match is None:
                if not line.lstrip().startswith("#") and USES_KEY.search(line):
                    failures.append(
                        f"{location}: uses must be a single-line scalar"
                    )
                continue
            payload = match.group(1)
            reference, separator, comment = payload.partition(" #")
            reference = reference.strip()
            comment = comment.strip() if separator else None
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
        inline.unlink()
        escaped_key = workflows / "escaped-key.yml"
        escaped_key.write_text(
            "name: escaped-key\njobs:\n  test:\n    steps:\n"
            '      - "\\u0075ses": actions/checkout@v7\n',
            encoding="utf-8",
        )
        if not violations(root):
            print("self-test escaped key failed: expected rejection", file=sys.stderr)
            return 1
        escaped_key.write_text(
            "name: escaped-key\njobs:\n  test:\n    steps:\n"
            '      - "u\\\n        ses": actions/checkout@v7\n',
            encoding="utf-8",
        )
        if not violations(root):
            print("self-test multiline escaped key failed: expected rejection", file=sys.stderr)
            return 1
        benign_escaped_text = {
            "comment": '      # "\\u0075ses": actions/checkout@v7\n      - run: echo ok\n',
            "block-scalar": '      - run: |\n          printf \'"\\u0075ses": value\'\n',
            "single-quoted-value": '      - run: \'"\\u0075ses": value\'\n',
            "double-quoted-value": '      - run: "\\"\\\\u0075ses\\": value"\n',
        }
        for name, steps in benign_escaped_text.items():
            escaped_key.write_text(
                f"name: {name}\njobs:\n  test:\n    steps:\n{steps}",
                encoding="utf-8",
            )
            unexpected = violations(root)
            if unexpected:
                print(
                    f"self-test {name} failed: unexpected rejection: {unexpected}",
                    file=sys.stderr,
                )
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
