#!/usr/bin/env python3
"""Measure reproducible cold LSP completion latency samples."""

from __future__ import annotations

import argparse
import json
import math
import statistics
import subprocess
import time
from pathlib import Path


def percentile(values: list[float], percentage: float) -> float:
    ordered = sorted(values)
    index = max(0, math.ceil(len(ordered) * percentage) - 1)
    return ordered[index]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--janus-lsp", type=Path, required=True)
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--document", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--line", type=int, default=0)
    parser.add_argument("--character", type=int, default=0)
    parser.add_argument("--samples", type=int, default=10)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.samples < 2:
        parser.error("--samples must be at least 2")

    samples: list[float] = []
    command = [str(args.janus_lsp), "--completion", str(args.workspace),
               str(args.document), str(args.snapshot), str(args.line),
               str(args.character)]
    for _ in range(args.samples):
        started = time.perf_counter_ns()
        completed = subprocess.run(command, check=True, capture_output=True, text=True)
        elapsed = (time.perf_counter_ns() - started) / 1_000_000
        json.loads(completed.stdout)
        samples.append(elapsed)

    report = {
        "schema_version": 1,
        "operation": "cold_start_and_completion",
        "unit": "milliseconds",
        "samples": samples,
        "p50": statistics.median(samples),
        "p95": percentile(samples, 0.95),
    }
    rendered = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
