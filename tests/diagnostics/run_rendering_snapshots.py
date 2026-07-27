#!/usr/bin/env python3
"""Exercise human/JSON diagnostic rendering and parser recovery."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import subprocess
import sys


def run(
    janus: pathlib.Path,
    fixture_dir: pathlib.Path,
    arguments: list[str],
    columns: int = 80,
) -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    environment["COLUMNS"] = str(columns)
    environment["NO_COLOR"] = "1"
    return subprocess.run(
        [str(janus), *arguments],
        cwd=fixture_dir,
        env=environment,
        capture_output=True,
        text=True,
        timeout=10,
        check=False,
    )


def compare_snapshot(
    name: str, actual: str, snapshot: pathlib.Path, failures: list[str]
) -> None:
    expected = snapshot.read_text(encoding="utf-8").replace("\r\n", "\n")
    normalized = actual.replace("\r\n", "\n")
    if normalized != expected:
        failures.append(
            f"{name} snapshot differs\n--- expected ---\n{expected}"
            f"--- actual ---\n{normalized}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--janus", required=True, type=pathlib.Path)
    parser.add_argument("--fixtures", required=True, type=pathlib.Path)
    parser.add_argument("--schema", required=True, type=pathlib.Path)
    args = parser.parse_args()
    args.janus = args.janus.resolve()
    args.fixtures = args.fixtures.resolve()

    failures: list[str] = []
    source = args.fixtures / "render-invalid.janus"
    original = source.read_bytes()

    for width in (80, 120):
        arguments = ["check", source.name]
        if width == 80:
            arguments.extend(["--diagnostic-format", "human"])
        result = run(args.janus, args.fixtures, arguments, width)
        if result.returncode != 1 or result.stdout:
            failures.append(
                f"human-{width}: status={result.returncode}, "
                f"stdout={result.stdout!r}"
            )
        compare_snapshot(
            f"human-{width}",
            result.stderr,
            args.fixtures / f"human-{width}.txt",
            failures,
        )

    check_json = run(
        args.janus,
        args.fixtures,
        ["check", source.name, "--diagnostic-format", "json"],
    )
    build_json = run(
        args.janus,
        args.fixtures,
        [
            "build",
            source.name,
            "--diagnostic-format",
            "json",
            "-o",
            "must-not-exist",
        ],
    )
    expected_json = json.loads(
        (args.fixtures / "diagnostic.json").read_text(encoding="utf-8")
    )
    for name, result in (("check-json", check_json), ("build-json", build_json)):
        if result.returncode != 1 or result.stdout:
            failures.append(
                f"{name}: status={result.returncode}, stdout={result.stdout!r}"
            )
            continue
        try:
            actual_json = json.loads(result.stderr)
        except json.JSONDecodeError as error:
            failures.append(f"{name}: invalid JSON: {error}")
            continue
        if actual_json != expected_json:
            failures.append(f"{name}: JSON fixture differs")

    recovery = run(
        args.janus,
        args.fixtures,
        ["check", "recovery-invalid.janus", "--diagnostic-format", "json"],
    )
    try:
        recovered = json.loads(recovery.stderr)
    except json.JSONDecodeError as error:
        failures.append(f"recovery: invalid JSON: {error}")
    else:
        diagnostics = recovered.get("diagnostics", [])
        if recovery.returncode != 1 or len(diagnostics) != 2:
            failures.append(
                f"recovery: status={recovery.returncode}, "
                f"diagnostics={len(diagnostics)}"
            )
        if any(item.get("code") != "JPAR0001" for item in diagnostics):
            failures.append("recovery: unexpected diagnostic code")

    schema = json.loads(args.schema.read_text(encoding="utf-8"))
    if schema.get("$id", "").endswith("diagnostic-0.5.2.schema.json") is False:
        failures.append("schema: missing versioned identifier")
    if set(schema.get("required", [])) != {"schemaVersion", "diagnostics"}:
        failures.append("schema: top-level required fields changed")
    if expected_json.get("schemaVersion") != "0.5.2":
        failures.append("fixture: unexpected schema version")
    if source.read_bytes() != original:
        failures.append("suggestion rendering modified the source")
    if (args.fixtures / "must-not-exist").exists():
        failures.append("failed build created its requested output")

    if failures:
        print("\n\n".join(failures), file=sys.stderr)
        return 1
    print("validated human snapshots, JSON fixture, schema and recovery")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
