#!/usr/bin/env python3
"""Validate the machine-readable Janus 1.0 release gates."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


REQUIRED = {
    "surface-inventory",
    "semantic-core",
    "diagnostics-zero-j0000",
    "lsp-performance",
    "stdlib-review",
    "ecosystem-validation",
    "release-candidate",
}
STATUSES = {"pending", "in-progress", "complete"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root.resolve()
    path = root / "docs" / "release-gates-1.0.json"
    payload = json.loads(path.read_text(encoding="utf-8"))
    errors: list[str] = []
    gates = payload.get("gates", [])
    ids = [gate.get("id") for gate in gates]
    if len(ids) != len(set(ids)):
        errors.append("identifiant de gate dupliqué")
    for missing in sorted(REQUIRED - set(ids)):
        errors.append(f"gate requise absente: {missing}")
    for gate in gates:
        gate_id = gate.get("id", "<sans id>")
        status = gate.get("status")
        if status not in STATUSES:
            errors.append(f"{gate_id}: statut invalide {status!r}")
        proof = gate.get("proof")
        if not isinstance(proof, list):
            errors.append(f"{gate_id}: proof doit être une liste")
            continue
        if status == "complete" and not proof:
            errors.append(f"{gate_id}: gate complète sans preuve")
        for relative in proof:
            if not (root / relative).is_file():
                errors.append(f"{gate_id}: preuve introuvable: {relative}")
    for error in errors:
        print(f"ERROR {error}", file=sys.stderr)
    if errors:
        return 1
    complete = sum(gate["status"] == "complete" for gate in gates)
    print(f"Gates 1.0 vérifiées: {complete}/{len(gates)} complètes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
