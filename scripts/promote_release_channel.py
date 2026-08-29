#!/usr/bin/env python3
"""Atomically promote a release-channel manifest through a Git ref."""

from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path


ABSENT = "absent"


def run_git(repository: Path, *arguments: str, input_text: str | None = None) -> str:
    result = subprocess.run(
        ["git", "-C", str(repository), *arguments],
        input=input_text.encode("utf-8") if input_text is not None else None,
        capture_output=True,
    )
    if result.returncode != 0:
        message = (result.stderr.strip() or result.stdout.strip()).decode(
            "utf-8", errors="replace")
        raise RuntimeError(message or f"git {' '.join(arguments)} failed")
    return result.stdout.decode("utf-8").strip()


def promote(repository: Path, channel: str, manifest: Path, expected: str) -> str:
    if channel not in ("stable", "beta"):
        raise ValueError(f"unsupported release channel: {channel}")
    if expected != ABSENT and (
        len(expected) != 40 or any(character not in "0123456789abcdef" for character in expected)
    ):
        raise ValueError("expected channel revision must be a lowercase Git SHA or 'absent'")

    ref = f"refs/heads/channel-{channel}"
    blob = run_git(repository, "hash-object", "-w", str(manifest))
    tree = run_git(repository, "mktree", input_text=f"100644 blob {blob}\tversion\n")
    commit_arguments = ["commit-tree", tree]
    if expected != ABSENT:
        commit_arguments.extend(("-p", expected))
    environment = os.environ.copy()
    environment.setdefault("GIT_AUTHOR_NAME", "Janus release automation")
    environment.setdefault("GIT_AUTHOR_EMAIL", "release@janus-lang.invalid")
    environment.setdefault("GIT_COMMITTER_NAME", environment["GIT_AUTHOR_NAME"])
    environment.setdefault("GIT_COMMITTER_EMAIL", environment["GIT_AUTHOR_EMAIL"])
    commit = subprocess.run(
        ["git", "-C", str(repository), *commit_arguments],
        input=f"Promote Janus {channel} channel\n".encode("utf-8"),
        capture_output=True,
        env=environment,
    )
    if commit.returncode != 0:
        message = commit.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(message or "git commit-tree failed")
    revision = commit.stdout.decode("utf-8").strip()
    lease = "" if expected == ABSENT else expected
    run_git(
        repository,
        "push",
        "origin",
        f"{revision}:{ref}",
        f"--force-with-lease={ref}:{lease}",
    )
    return revision


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--channel", choices=("stable", "beta"), required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--expected", required=True)
    args = parser.parse_args()
    revision = promote(args.repository, args.channel, args.manifest, args.expected)
    print(revision)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
