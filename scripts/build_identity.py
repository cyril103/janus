#!/usr/bin/env python3
"""Resolve one reproducible identity for source and packaged Janus tools."""
from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import re
import subprocess
from pathlib import Path


FULL_SHA = re.compile(r"[0-9a-f]{40}")


@dataclasses.dataclass(frozen=True)
class BuildIdentity:
    version: str
    revision: str
    dirty: bool
    channel: str
    target: str = "unknown"
    llvm: str = "unknown"
    source_digest: str = ""

    @property
    def display_version(self) -> str:
        if self.channel == "stable" and not self.dirty:
            return self.version
        suffix = f"+g{self.revision[:12]}"
        if self.dirty:
            suffix += f".dirty.{self.source_digest[:12]}"
        return self.version + suffix

    @property
    def identity(self) -> str:
        return f"{self.display_version}.{self.channel}"

    def value(self) -> dict[str, object]:
        return {"schema_version": 1, "version": self.version,
                "display_version": self.display_version,
                "revision": self.revision, "dirty": self.dirty,
                "channel": self.channel, "identity": self.identity,
                "target": self.target, "llvm": self.llvm,
                "source_digest": self.source_digest or None}

    def json(self) -> str:
        return json.dumps(self.value(), sort_keys=True, separators=(",", ":"))


def resolve(version: str, revision: str, dirty: bool, exact_tag: str | None,
            packaged: bool, *, target: str = "unknown",
            llvm: str = "unknown", source_digest: str = "") -> BuildIdentity:
    if not FULL_SHA.fullmatch(revision):
        raise ValueError("source revision must be a full lowercase 40-character SHA")
    stable = not packaged and not dirty and exact_tag == f"v{version}"
    channel = "package" if packaged else ("stable" if stable else "source")
    if dirty and not re.fullmatch(r"[0-9a-f]{64}", source_digest):
        source_digest = hashlib.sha256(b"dirty").hexdigest()
    return BuildIdentity(version, revision, dirty, channel, target, llvm,
                         source_digest)


def _git(repo: Path, *args: str, check: bool = True) -> str:
    result = subprocess.run(["git", "-C", str(repo), *args], text=True,
                            capture_output=True)
    if check and result.returncode:
        raise ValueError(result.stderr.strip() or "git identity probe failed")
    return result.stdout.strip() if result.returncode == 0 else ""


def from_git(version: str, repo: Path, *, injected_sha: str = "",
             target: str = "unknown", llvm: str = "unknown") -> BuildIdentity:
    if injected_sha:
        head = _git(repo, "rev-parse", "HEAD", check=False)
        exact_tag = (_git(repo, "describe", "--tags", "--exact-match", check=False)
                     or None)
        dirty = bool(_git(repo, "status", "--porcelain", "--untracked-files=normal",
                          check=False))
        if head == injected_sha and not dirty and exact_tag == f"v{version}":
            return resolve(version, injected_sha, False, exact_tag, False,
                           target=target, llvm=llvm)
        return resolve(version, injected_sha, False, None, True,
                       target=target, llvm=llvm)
    revision = _git(repo, "rev-parse", "HEAD")
    status = _git(repo, "status", "--porcelain", "--untracked-files=normal")
    dirty = bool(status)
    source_digest = ""
    if dirty:
        digest = hashlib.sha256()
        digest.update(status.encode())
        digest.update(_git(repo, "diff", "--binary", "HEAD").encode())
        for line in status.splitlines():
            if line.startswith("?? "):
                path = repo / line[3:]
                if path.is_file():
                    digest.update(line[3:].encode())
                    digest.update(path.read_bytes())
        source_digest = digest.hexdigest()
    exact_tag = _git(repo, "describe", "--tags", "--exact-match", check=False) or None
    return resolve(version, revision, dirty, exact_tag, False,
                   target=target, llvm=llvm, source_digest=source_digest)


def _cxx(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def header(identity: BuildIdentity) -> str:
    values = identity.value()
    return "#pragma once\n" + "".join(
        f'#define JANUS_BUILD_{name.upper()} "{_cxx("" if value is None else str(value).lower() if isinstance(value, bool) else str(value))}"\n'
        for name, value in values.items() if name != "schema_version")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--source-sha", default="")
    parser.add_argument("--target", default="unknown")
    parser.add_argument("--llvm", default="unknown")
    parser.add_argument("--json-output", type=Path, required=True)
    parser.add_argument("--header-output", type=Path, required=True)
    args = parser.parse_args()
    identity = from_git(args.version, args.source, injected_sha=args.source_sha,
                        target=args.target, llvm=args.llvm)
    def write_if_changed(path: Path, contents: str) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists() or path.read_text() != contents:
            path.write_text(contents)

    write_if_changed(args.json_output, identity.json() + "\n")
    write_if_changed(args.header_output, header(identity))


if __name__ == "__main__":
    main()
