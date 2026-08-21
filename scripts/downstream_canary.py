#!/usr/bin/env python3
"""Run pinned downstream canaries using only a candidate toolchain archive."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import tarfile
import tempfile
import re
from pathlib import Path, PurePosixPath


JANUS8_REVISION = "b962b69ea14a47016556f9e2a111c7dab1e2e02d"
JANUS8_COMMANDS = [["fmt", "--check"], ["check", "--all", "--deny-warnings"],
                   ["test", "--fail-if-empty"], ["build"]]
JANUS_STUDIO_REVISION = "d0e88543427d462df9c34d74c0800a83f55a21ec"
JANUS_STUDIO_COMMANDS = [["fmt", "--check"], ["check", "--all"],
                         ["test", "--fail-if-empty", "--release"],
                         ["build", "--release"]]


def _members(archive: Path) -> list[tarfile.TarInfo]:
    with tarfile.open(archive, "r:gz") as source:
        members = source.getmembers()
    if len(members) > 100_000:
        raise ValueError("candidate archive has too many entries")
    seen: dict[str, bool] = {}
    roots: set[str] = set()
    total_size = 0
    for member in members:
        parts = PurePosixPath(member.name).parts
        if (member.name.startswith(("/", "\\")) or "\\" in member.name or
                re.match(r"^[A-Za-z]:", member.name) or
                any(part in ("", ".", "..") for part in parts) or
                member.issym() or member.islnk() or
                not (member.isfile() or member.isdir())):
            raise ValueError(f"unsafe archive entry: {member.name}")
        roots.add(parts[0].casefold())
        if len(roots) != 1:
            raise ValueError("candidate archive has multiple roots")
        normalized = "/".join(parts).casefold()
        if normalized in seen:
            raise ValueError(f"ambiguous archive entry: {member.name}")
        for parent_index in range(1, len(parts)):
            parent = "/".join(parts[:parent_index]).casefold()
            if parent in seen and not seen[parent]:
                raise ValueError(f"ambiguous archive entry: {member.name}")
        if not member.isdir() and any(
                existing.startswith(normalized + "/") for existing in seen):
            raise ValueError(f"ambiguous archive entry: {member.name}")
        seen[normalized] = member.isdir()
        total_size += member.size
        if member.size > 512 * 1024 * 1024 or total_size > 2 * 1024 * 1024 * 1024:
            raise ValueError("candidate archive exceeds extraction limits")
    return members


def validate_archive(archive: Path, expected_revision: str) -> dict[str, object]:
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
    match = re.fullmatch(r"janus-(\d+\.\d+\.\d+)-Linux-x86_64\.tar\.gz",
                         archive.name)
    expected_version = match.group(1) if match else identity.get("version")
    required = {"schema_version", "version", "display_version", "revision",
                "dirty", "channel", "identity", "target", "llvm",
                "source_digest"}
    channel = identity.get("channel")
    display = (expected_version if channel == "stable" else
               f"{expected_version}+g{expected_revision[:12]}")
    expected_identity = f"{display}.{channel}"
    if identity.get("revision") != expected_revision:
        raise ValueError("candidate revision does not match expected revision")
    if (set(identity) != required or identity.get("schema_version") != 1 or
            identity.get("version") != expected_version or
            channel not in ("package", "stable") or
            identity.get("dirty") is not False or
            identity.get("display_version") != display or
            identity.get("identity") != expected_identity or
            not identity.get("target") or not identity.get("llvm") or
            identity.get("source_digest") is not None):
        raise ValueError("candidate build identity is invalid or inconsistent")
    return identity


def run(archive: Path, expected_revision: str, janus8_checkout: Path,
        studio_checkout: Path) -> None:
    packaged_identity = validate_archive(archive, expected_revision)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        with tarfile.open(archive, "r:gz") as source:
            source.extractall(root, filter="data")
        janus = next(root.glob("*/bin/janus"))
        identity = json.loads(subprocess.run(
            [str(janus), "--version", "--json"], check=True, text=True,
            capture_output=True).stdout)
        if identity != packaged_identity:
            raise RuntimeError("extracted janus identity differs from packaged identity")
        home = root / "home"
        cache = root / "cache"
        registry = root / "registry"
        temp = root / "tmp"
        home.mkdir()
        cache.mkdir()
        registry.mkdir()
        temp.mkdir()
        env = {"PATH": str(janus.parent) + os.pathsep + "/usr/bin:/bin",
               "HOME": str(home), "XDG_CACHE_HOME": str(cache),
               "XDG_CONFIG_HOME": str(root / "config"),
               "XDG_DATA_HOME": str(root / "data"),
               "JANUS_CACHE": str(cache / "janus"),
               "JANUS_REGISTRY": str(registry),
               "JANUSUP_HOME": str(root / "janusup"), "TMPDIR": str(temp)}
        for command in JANUS8_COMMANDS:
            subprocess.run([str(janus), *command], cwd=janus8_checkout,
                           env=env, check=True)
        subprocess.run(["bash", "tests/native_syntax.sh"], cwd=janus8_checkout,
                       env=env, check=True)
        subprocess.run(["bash", "tests/native_syntax_mutation.sh"], cwd=janus8_checkout,
                       env=env, check=True)
        subprocess.run(["bash", "tests/smoke.sh"], cwd=janus8_checkout,
                       env=env, check=True)
        for command in JANUS_STUDIO_COMMANDS:
            subprocess.run([str(janus), *command], cwd=studio_checkout,
                           env=env, check=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--expected-revision", required=True)
    parser.add_argument("--janus8-checkout", type=Path, required=True)
    parser.add_argument("--studio-checkout", type=Path, required=True)
    args = parser.parse_args()
    run(args.archive, args.expected_revision, args.janus8_checkout,
        args.studio_checkout)


if __name__ == "__main__":
    main()
