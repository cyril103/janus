#!/usr/bin/env python3
"""Deterministic nightly channel metadata and publication gates."""
from __future__ import annotations
import argparse
import dataclasses
import datetime as dt
import functools
import hashlib
import re
from pathlib import Path

FULL_SHA = re.compile(r"[0-9a-f]{40}")
TOKEN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+-]*")
SEMVER = re.compile(
    r"v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
    r"(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?")


@functools.total_ordering
@dataclasses.dataclass(frozen=True)
class ReleaseVersion:
    major: int
    minor: int
    patch: int
    prerelease: tuple[str, ...] = ()

    @classmethod
    def parse(cls, value: str) -> "ReleaseVersion":
        match = SEMVER.fullmatch(value)
        if match is None:
            raise ValueError(f"invalid release version: {value}")
        prerelease = tuple((match.group(4) or "").split(".")) if match.group(4) else ()
        for identifier in prerelease:
            if identifier.isdigit() and len(identifier) > 1 and identifier.startswith("0"):
                raise ValueError("numeric prerelease identifiers must not contain leading zeroes")
        return cls(
            int(match.group(1)), int(match.group(2)), int(match.group(3)),
            prerelease)

    def __lt__(self, other: object) -> bool:
        if not isinstance(other, ReleaseVersion):
            return NotImplemented
        core, other_core = self.core, other.core
        if core != other_core:
            return core < other_core
        if not self.prerelease:
            return bool(other.prerelease)
        if not other.prerelease:
            return False
        for left, right in zip(self.prerelease, other.prerelease):
            if left == right:
                continue
            left_numeric, right_numeric = left.isdigit(), right.isdigit()
            if left_numeric and right_numeric:
                return int(left) < int(right)
            if left_numeric != right_numeric:
                return left_numeric
            return left < right
        return len(self.prerelease) < len(other.prerelease)

    @property
    def core(self) -> tuple[int, int, int]:
        return self.major, self.minor, self.patch

def project_version(source: Path) -> str:
    match = re.search(
        r"project\s*\(\s*janus\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
        source.read_text())
    if match is None:
        raise ValueError("could not read Janus project version")
    return match.group(1)

def _time(value: str) -> dt.datetime:
    if not value.endswith("Z"):
        raise ValueError("timestamp must be UTC (Z)")
    return dt.datetime.fromisoformat(value[:-1] + "+00:00")

@dataclasses.dataclass(frozen=True)
class ChannelManifest:
    version: str
    release: str
    source_sha: str | None = None
    published_at: str | None = None
    def validate(self) -> None:
        if not TOKEN.fullmatch(self.version) or not TOKEN.fullmatch(self.release):
            raise ValueError("invalid manifest token")
        if (self.source_sha is None) != (self.published_at is None):
            raise ValueError("source SHA and publication time must appear together")
        if self.source_sha is not None:
            if not FULL_SHA.fullmatch(self.source_sha):
                raise ValueError("source SHA must be 40 lowercase hexadecimal characters")
            _time(self.published_at or "")
    def render(self) -> str:
        self.validate()
        fields = [self.version, self.release]
        if self.source_sha is not None:
            fields += [self.source_sha, self.published_at or ""]
        return " ".join(fields) + "\n"

def parse_manifest(text: str) -> ChannelManifest:
    fields = text.split()
    if len(fields) not in (2, 4):
        raise ValueError("channel manifest must contain exactly 2 or 4 fields")
    result = ChannelManifest(*fields)
    result.validate()
    return result


def check_release_promotion(candidate: str, channel: str,
                            current: ChannelManifest | None) -> None:
    version = ReleaseVersion.parse(candidate)
    if channel == "stable" and version.prerelease:
        raise ValueError("stable channel requires a stable release tag")
    if channel == "beta" and not version.prerelease:
        raise ValueError("beta channel requires a prerelease tag")
    if channel not in ("stable", "beta"):
        raise ValueError(f"unsupported release channel: {channel}")
    if current is not None:
        current_version = ReleaseVersion.parse(current.version)
        if version <= current_version:
            raise ValueError(
                f"candidate {candidate} must be newer than current {current.version}")

def check_freshness(nightly: ChannelManifest, stable: ChannelManifest,
                    now: dt.datetime, maximum_age_hours: int) -> None:
    if nightly.published_at is None or stable.published_at is None:
        raise ValueError("freshness requires timestamped manifests")
    nightly_time, stable_time = _time(nightly.published_at), _time(stable.published_at)
    if nightly_time > now.astimezone(dt.timezone.utc):
        raise ValueError("nightly publication time is in the future")
    if nightly_time < stable_time:
        raise ValueError("nightly is older than stable")
    if now.astimezone(dt.timezone.utc) - nightly_time > dt.timedelta(hours=maximum_age_hours):
        raise ValueError(f"nightly age exceeds {maximum_age_hours} hours")

def publish(publisher, payload: bytes, manifest: str) -> None:
    candidate = publisher.upload_candidate(payload)
    publisher.verify_checksum(candidate, hashlib.sha256(payload).hexdigest())
    publisher.verify_attestation(candidate)
    publisher.smoke(candidate, manifest)
    publisher.promote_manifest(manifest)

class LocalPublisher:
    def __init__(self, root: Path, fail_at: str | None = None):
        self.root, self.fail_at, self.events = root, fail_at, []
    def _gate(self, name: str) -> None:
        self.events.append(name)
        if self.fail_at == name:
            raise RuntimeError(f"injected {name} failure")
    def upload_candidate(self, payload: bytes) -> Path:
        self._gate("upload")
        path = self.root / "candidate"
        path.write_bytes(payload)
        return path
    def verify_checksum(self, candidate: Path, expected: str) -> None:
        self._gate("checksum")
        if hashlib.sha256(candidate.read_bytes()).hexdigest() != expected:
            raise RuntimeError("checksum mismatch")
    def verify_attestation(self, candidate: Path) -> None:
        self._gate("attestation")
    def smoke(self, candidate: Path, manifest: str) -> None:
        self._gate("smoke")
    def promote_manifest(self, manifest: str) -> None:
        self._gate("promote")
        temporary = self.root / "version.new"
        temporary.write_text(manifest)
        temporary.replace(self.root / "version")

def main() -> None:
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    write = commands.add_parser("write-manifest")
    for name in ("version", "release", "source_sha", "published_at", "output"):
        write.add_argument("--" + name.replace("_", "-"), required=True)
    fresh = commands.add_parser("check-freshness")
    fresh.add_argument("--nightly", type=Path, required=True)
    fresh.add_argument("--stable", type=Path, required=True)
    fresh.add_argument("--now", required=True)
    fresh.add_argument("--maximum-age-hours", type=int, default=168)
    times = commands.add_parser("check-times")
    times.add_argument("--nightly-published", required=True); times.add_argument("--stable-published", required=True)
    times.add_argument("--now", required=True); times.add_argument("--maximum-age-hours", type=int, default=168)
    version = commands.add_parser("project-version")
    version.add_argument("--source", type=Path, required=True)
    promotion = commands.add_parser("check-promotion")
    promotion.add_argument("--candidate", required=True)
    promotion.add_argument("--channel", choices=("stable", "beta"), required=True)
    promotion.add_argument("--current", type=Path)
    args = parser.parse_args()
    if args.command == "write-manifest":
        Path(args.output).write_text(ChannelManifest(args.version, args.release, args.source_sha, args.published_at).render())
    elif args.command == "check-freshness":
        check_freshness(parse_manifest(args.nightly.read_text()), parse_manifest(args.stable.read_text()), _time(args.now), args.maximum_age_hours)
    elif args.command == "check-times":
        placeholder = "0" * 40
        check_freshness(ChannelManifest("nightly", "nightly", placeholder, args.nightly_published),
                        ChannelManifest("stable", "stable", placeholder, args.stable_published),
                        _time(args.now), args.maximum_age_hours)
    elif args.command == "check-promotion":
        current = parse_manifest(args.current.read_text()) if args.current else None
        check_release_promotion(args.candidate, args.channel, current)
    else:
        print(project_version(args.source))
if __name__ == "__main__":
    main()
