#!/usr/bin/env python3
"""Run the pinned Janus8 canary using only a candidate toolchain archive."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import tarfile
import tempfile
from pathlib import Path, PurePosixPath


JANUS8_REVISION = "b962b69ea14a47016556f9e2a111c7dab1e2e02d"
JANUS8_COMMANDS = [["fmt", "--check"], ["check", "--all", "--deny-warnings"],
                   ["test", "--fail-if-empty"], ["build"]]


def _members(archive: Path) -> list[tarfile.TarInfo]:
    with tarfile.open(archive, "r:gz") as source:
        members = source.getmembers()
    seen: set[str] = set()
    total_size = 0
    for member in members:
        parts = PurePosixPath(member.name).parts
        if (member.name.startswith(("/", "\\")) or "\\" in member.name or
                ".." in parts or member.issym() or member.islnk() or
                not (member.isfile() or member.isdir())):
            raise ValueError(f"unsafe archive entry: {member.name}")
        normalized = "/".join(parts).casefold()
        if normalized in seen:
            raise ValueError(f"ambiguous archive entry: {member.name}")
        seen.add(normalized)
        total_size += member.size
        if member.size > 512 * 1024 * 1024 or total_size > 2 * 1024 * 1024 * 1024:
            raise ValueError("candidate archive exceeds extraction limits")
    return members


def validate_archive(archive: Path, expected_revision: str) -> None:
    checksum = archive.with_name(archive.name + ".sha256")
    if not checksum.is_file():
        raise ValueError("candidate archive has no SHA-256 checksum")
    fields = checksum.read_text().split()
    if len(fields) != 2 or fields[1].lstrip("*") != archive.name:
        raise ValueError("candidate checksum manifest is invalid")
    actual = hashlib.sha256(archive.read_bytes()).hexdigest()
    if fields[0].lower() != actual:
        raise ValueError("candidate archive checksum does not match")
    members = _members(archive)
    names = [member.name for member in members]
    if not any(name.endswith("/bin/janus") for name in names):
        raise ValueError("candidate archive has no janus binary")
    if not any("/share/janus/stdlib/" in name and not name.endswith("/") for name in names):
        raise ValueError("candidate archive has no packaged stdlib")
    identity_name = next((name for name in names
                          if name.endswith("/share/janus/build-identity.json")), None)
    if identity_name is None:
        raise ValueError("candidate archive has no build identity")
    with tarfile.open(archive, "r:gz") as source:
        identity = json.load(source.extractfile(identity_name))
    if identity.get("revision") != expected_revision:
        raise ValueError("candidate revision does not match expected revision")
    if identity.get("channel") not in ("package", "stable") or identity.get("dirty") is not False:
        raise ValueError("candidate identity is not a clean package build")


def run(archive: Path, expected_revision: str, checkout: Path) -> None:
    validate_archive(archive, expected_revision)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        with tarfile.open(archive, "r:gz") as source:
            source.extractall(root, filter="data")
        janus = next(root.glob("*/bin/janus"))
        identity = json.loads(subprocess.run(
            [str(janus), "--version", "--json"], check=True, text=True,
            capture_output=True).stdout)
        if identity["revision"] != expected_revision:
            raise RuntimeError("extracted janus reports the wrong revision")
        home = root / "home"
        cache = root / "cache"
        registry = root / "registry"
        home.mkdir()
        cache.mkdir()
        registry.mkdir()
        env = {"PATH": str(janus.parent) + os.pathsep + "/usr/bin:/bin",
               "HOME": str(home), "XDG_CACHE_HOME": str(cache),
               "XDG_CONFIG_HOME": str(root / "config"),
               "XDG_DATA_HOME": str(root / "data"),
               "JANUS_CACHE": str(cache / "janus"),
               "JANUS_REGISTRY": str(registry),
               "JANUSUP_HOME": str(root / "janusup")}
        for command in JANUS8_COMMANDS:
            subprocess.run([str(janus), *command], cwd=checkout, env=env, check=True)
        subprocess.run(["bash", "tests/native_syntax.sh"], cwd=checkout, env=env, check=True)
        subprocess.run(["bash", "tests/native_syntax_mutation.sh"], cwd=checkout,
                       env=env, check=True)
        subprocess.run(["bash", "tests/smoke.sh"], cwd=checkout, env=env, check=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--expected-revision", required=True)
    parser.add_argument("--checkout", type=Path, required=True)
    args = parser.parse_args()
    run(args.archive, args.expected_revision, args.checkout)


if __name__ == "__main__":
    main()
