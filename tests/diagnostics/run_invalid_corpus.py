#!/usr/bin/env python3
"""Validate that every versioned invalid source produces its expected diagnostic."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--janus", required=True, type=pathlib.Path)
    parser.add_argument("--corpus", required=True, type=pathlib.Path)
    args = parser.parse_args()

    manifest = args.corpus / "manifest.tsv"
    failures: list[str] = []
    entries = [
        line.split("\t", 2)
        for line in manifest.read_text(encoding="utf-8").splitlines()
        if line and not line.startswith("#")
    ]

    for filename, code, message in entries:
        source = args.corpus / filename
        result = subprocess.run(
            [str(args.janus), "check", str(source)],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
        output = result.stdout + result.stderr
        location = re.escape(str(source)) + r":\d+:\d+: error:"
        if result.returncode != 1:
            failures.append(
                f"{filename}: expected status 1, got {result.returncode}"
            )
        if re.search(location, output) is None:
            failures.append(f"{filename}: missing file:line:column location")
        if f"[{code}]" not in output:
            failures.append(f"{filename}: missing code {code}")
        if message not in output:
            failures.append(f"{filename}: missing message fragment {message!r}")
        if code in {"JPAR0002", "JPAR0003"}:
            structured = subprocess.run(
                [
                    str(args.janus),
                    "check",
                    str(source),
                    "--diagnostic-format",
                    "json",
                ],
                capture_output=True,
                text=True,
                timeout=10,
                check=False,
            )
            try:
                payload = json.loads(structured.stderr)
                diagnostics = payload["diagnostics"]
            except (json.JSONDecodeError, KeyError, TypeError) as error:
                failures.append(f"{filename}: invalid JSON diagnostic: {error}")
            else:
                if structured.returncode != 1:
                    failures.append(
                        f"{filename}: JSON expected status 1, got "
                        f"{structured.returncode}"
                    )
                if len(diagnostics) != 1 or diagnostics[0].get("code") != code:
                    failures.append(
                        f"{filename}: JSON missing diagnostic code {code}"
                    )
                elif message not in diagnostics[0].get("message", ""):
                    failures.append(
                        f"{filename}: JSON missing message fragment {message!r}"
                    )

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"validated {len(entries)} invalid diagnostic fixtures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
