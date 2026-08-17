#!/usr/bin/env python3
"""Synchronise les guides canoniques du dépôt dans le site MkDocs."""

from __future__ import annotations

import argparse
import re
import shutil
from pathlib import Path, PurePosixPath
from urllib.parse import unquote

VERSION = "v0.13.0"
REPOSITORY_URL = "https://github.com/cyril103/janus"
DOCUMENTS = (
    "getting-started.md",
    "language-guide.md",
    "numeric-conversions.md",
    "text.md",
    "tooling.md",
    "registry-protocol-v1.md",
    "compiler-performance.md",
    "api-documentation.md",
    "doctests.md",
    "testing.md",
    "diagnostics.md",
    "stdlib-reference.md",
    "graphics.md",
    "stability-contract.md",
    "stability-inventory-0.8.md",
    "known-limitations-0.8.md",
    "readiness-1.0.md",
    "release-severity-policy-0.8.md",
    "development.md",
    "migration-0.5-to-0.6.md",
    "migration-0.5-to-0.8.md",
)
DOCUMENT_VERSIONS = {}
ASSETS = ("public-surface-0.5.json",)
LINK_RE = re.compile(r"(?P<image>!)?\[(?P<label>[^]]*)\]\((?P<target>[^)]+)\)")
NOTICE = """> **Documentation canonique** — Cette page est générée depuis
> [`docs/{name}`]({url}/blob/{version}/docs/{name}).
> Ne modifiez pas cette copie : corrigez le document source dans le dépôt.

"""


def _rewrite_target(
    target: str, source: Path, repository: Path, image: bool, version: str = VERSION
) -> str:
    if target.startswith(("https://", "http://", "mailto:", "#")):
        return target

    path_part, marker, fragment = target.partition("#")
    decoded = unquote(path_part)
    candidate = (source.parent / decoded).resolve()
    docs_root = (repository / "docs").resolve()

    if candidate.parent == docs_root and candidate.name in (*DOCUMENTS, *ASSETS):
        rewritten = candidate.name
    else:
        try:
            relative = candidate.relative_to(repository.resolve())
        except ValueError:
            return target
        relative_url = PurePosixPath(relative).as_posix()
        if image:
            rewritten = f"https://raw.githubusercontent.com/cyril103/janus/{version}/{relative_url}"
        else:
            kind = "tree" if candidate.is_dir() or not candidate.suffix else "blob"
            rewritten = f"{REPOSITORY_URL}/{kind}/{version}/{relative_url}"

    return f"{rewritten}{marker}{fragment}" if marker else rewritten


def rewrite_links(
    text: str, source: Path, repository: Path, version: str = VERSION
) -> str:
    def replace(match: re.Match[str]) -> str:
        image = bool(match.group("image"))
        target = _rewrite_target(
            match.group("target"), source, repository, image, version
        )
        prefix = "!" if image else ""
        return f"{prefix}[{match.group('label')}]({target})"

    return LINK_RE.sub(replace, text)


def sync(repository: Path, destination: Path) -> None:
    repository = repository.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    expected = set(DOCUMENTS)
    for stale in destination.glob("*.md"):
        if stale.name not in expected:
            stale.unlink()

    for name in DOCUMENTS:
        source = repository / "docs" / name
        if not source.is_file():
            raise FileNotFoundError(f"Document canonique introuvable : {source}")
        version = DOCUMENT_VERSIONS.get(name, VERSION)
        content = rewrite_links(
            source.read_text(encoding="utf-8"), source, repository, version
        )
        notice = NOTICE.format(name=name, url=REPOSITORY_URL, version=version)
        (destination / name).write_text(notice + content, encoding="utf-8")

    for name in ASSETS:
        source = repository / "docs" / name
        if not source.is_file():
            raise FileNotFoundError(f"Ressource canonique introuvable : {source}")
        shutil.copyfile(source, destination / name)


def main() -> None:
    default_repository = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, default=default_repository)
    parser.add_argument(
        "--destination",
        type=Path,
        default=default_repository / "website" / "docs" / "reference" / "generated",
    )
    args = parser.parse_args()
    sync(args.repository, args.destination)
    print(f"{len(DOCUMENTS)} documents synchronisés dans {args.destination}")


if __name__ == "__main__":
    main()
