#!/usr/bin/env python3
"""Validate the current pre-1.0 stability inventory and all of its links."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from urllib.parse import unquote


ROW_RE = re.compile(
    r"^\|\s*`([^`]+)`\s*\|\s*`(stable-candidate|experimental|internal-detail)`\s*\|",
    re.MULTILINE,
)
LINK_RE = re.compile(r"(?<!!)\[[^\]]+\]\(([^)]+)\)")
REQUIRED_GROUPS = {
    "syntax.declarations",
    "syntax.control-flow",
    "syntax.generics-traits",
    "syntax.modules-visibility",
    "syntax.ownership",
    "semantics.numeric",
    "semantics.ownership-cleanup",
    "semantics.errors-panics",
    "semantics.constant-evaluation",
    "manifest.package",
    "manifest.dependencies.path",
    "manifest.dependencies.git",
    "manifest.dependencies.registry",
    "lockfile.format-v1",
    "lockfile.locked",
    "lockfile.offline",
    "resolver.path",
    "resolver.git",
    "resolver.registry",
    "c-abi.extern-def",
    "c-abi.scalars",
    "c-abi.pointers",
    "c-abi.variadics",
    "package.archives",
    "package.janusup",
    "protocol.registry-v1",
    "tooling.janus-lsp",
    "tooling.vscode",
}


def anchors(markdown: str) -> set[str]:
    result: set[str] = set()
    for explicit in re.findall(r'<a\s+id="([^"]+)"', markdown):
        result.add(explicit)
    for heading in re.findall(r"^#{1,6}\s+(.+?)\s*$", markdown, re.MULTILINE):
        anchor = heading.strip().lower()
        anchor = re.sub(r"[^\w\s-]", "", anchor, flags=re.UNICODE)
        anchor = re.sub(r"[\s_]+", "-", anchor).strip("-")
        result.add(anchor)
    return result


def check_links(root: Path, inventory: Path, markdown: str) -> list[str]:
    errors: list[str] = []
    for target in LINK_RE.findall(markdown):
        target = target.strip().split(maxsplit=1)[0].strip("<>")
        if target.startswith(("https://", "http://", "mailto:")):
            continue
        path_text, separator, fragment = target.partition("#")
        linked = inventory if not path_text else (inventory.parent / unquote(path_text))
        if not linked.is_file():
            errors.append(f"lien local introuvable: {target}")
            continue
        if separator:
            linked_text = linked.read_text(encoding="utf-8")
            if unquote(fragment) not in anchors(linked_text):
                errors.append(f"ancre locale introuvable: {target}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root", type=Path, default=Path(__file__).resolve().parents[1]
    )
    args = parser.parse_args()
    root = args.root.resolve()
    inventory = root / "docs" / "stability-inventory-current.md"
    public_surface = root / "docs" / "public-surface-0.5.json"
    errors: list[str] = []

    if not inventory.is_file():
        print(f"ERROR inventaire absent: {inventory}", file=sys.stderr)
        return 1
    markdown = inventory.read_text(encoding="utf-8")
    rows = ROW_RE.findall(markdown)
    statuses = dict(rows)
    if len(statuses) != len(rows):
        errors.append("une surface est inventoriée plusieurs fois")

    public = json.loads(public_surface.read_text(encoding="utf-8"))
    required = set(REQUIRED_GROUPS)
    required.update(
        str(module["name"])
        for module in public["stdlib_modules"]
        if module["status"] != "internal-detail"
    )
    required.update(
        f"cli.{command['name']}" for command in public["cli"]["commands"]
    )
    for missing in sorted(required - statuses.keys()):
        errors.append(f"surface publique sans statut: {missing}")

    if "stable-proposed" in markdown:
        errors.append("statut obsolète stable-proposed dans l'inventaire courant")

    errors.extend(check_links(root, inventory, markdown))
    for error in errors:
        print(f"ERROR {error}", file=sys.stderr)
    if errors:
        return 1
    experimental = sum(status == "experimental" for status in statuses.values())
    print(
        f"Inventaire courant vérifié: {len(statuses)} surfaces, "
        f"{experimental} expérimentales, liens locaux valides"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
