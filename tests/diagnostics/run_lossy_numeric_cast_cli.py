#!/usr/bin/env python3
"""Validate JANA0013 JSON output and --deny-warnings at IEEE boundaries."""

import argparse
import json
import pathlib
import subprocess
import tempfile


SOURCE = """\
def main() : int {
    val exactFloatBoundary : int = 16777216
    val adjacentFloatValue : int = 16777217
    val exactAsFloat : float = float(exactFloatBoundary)
    val adjacentAsFloat : float = float(adjacentFloatValue)
    val two : long = long(2)
    val exactDoubleBoundary : long = two << usize(52)
    val adjacentDoubleValue : long = exactDoubleBoundary + long(1)
    val exactAsDouble : double = double(exactDoubleBoundary)
    val adjacentAsDouble : double = double(adjacentDoubleValue)
    println(exactAsFloat)
    println(adjacentAsFloat)
    println(exactAsDouble)
    println(adjacentAsDouble)
    return 0
}
"""


def run(janus: pathlib.Path, source: pathlib.Path, *arguments: str):
    return subprocess.run(
        [str(janus), "check", str(source), *arguments],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def jana0013_codes(result: subprocess.CompletedProcess[str]) -> list[str]:
    payload = json.loads(result.stderr)
    return [
        diagnostic["code"]
        for diagnostic in payload["diagnostics"]
        if diagnostic["code"] == "JANA0013"
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--janus", type=pathlib.Path, required=True)
    arguments = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="janus-lossy-numeric-cast-") as tmp:
        source = pathlib.Path(tmp) / "boundaries.janus"
        source.write_text(SOURCE, encoding="utf-8")

        structured = run(
            arguments.janus, source, "--diagnostic-format", "json"
        )
        assert structured.returncode == 0, structured.stderr
        assert len(jana0013_codes(structured)) == 4, structured.stderr

        denied = run(
            arguments.janus,
            source,
            "--diagnostic-format",
            "json",
            "--deny-warnings",
        )
        assert denied.returncode == 1, denied.stderr
        assert len(jana0013_codes(denied)) == 4, denied.stderr

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
