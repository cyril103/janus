#!/usr/bin/env python3
"""Aggregate five timed Janus builds per versioned benchmark project."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import math
import pathlib
import shutil
import statistics
import subprocess
import sys
from typing import Any


REQUIRED_PHASES = {
    "loading",
    "parsing",
    "analysis",
    "llvm_generation",
    "optimization",
    "link",
    "overhead",
}
CANONICAL_PROJECTS = {"small", "medium"}


def validate_timing_report(report: Any) -> dict[str, Any]:
    if not isinstance(report, dict) or report.get("schema_version") != 1:
        raise RuntimeError(f"unsupported timing schema: {report!r}")
    if report.get("command") != "build" or report.get("unit") != "milliseconds":
        raise RuntimeError(f"unexpected timing metadata: {report!r}")
    phases = report.get("phases")
    if not isinstance(phases, dict) or set(phases) != REQUIRED_PHASES:
        raise RuntimeError(f"unexpected timing phases: {phases!r}")
    values = [report.get("total_ms"), *phases.values()]
    if any(
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not math.isfinite(float(value))
        or float(value) < 0.0
        for value in values
    ):
        raise RuntimeError(f"invalid timing values: {report!r}")
    total = float(report["total_ms"])
    if not math.isclose(
        sum(float(value) for value in phases.values()),
        total,
        rel_tol=0.0,
        abs_tol=0.05,
    ):
        raise RuntimeError(f"timing phases do not explain total: {report!r}")
    return report


def run_checked(command: list[str], cwd: pathlib.Path) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def benchmark_project(
    janus: pathlib.Path,
    project: pathlib.Path,
    samples: int,
    work_root: pathlib.Path,
) -> dict[str, object]:
    reports: list[dict[str, Any]] = []
    project_work = work_root / project.name
    project_work.mkdir(parents=True, exist_ok=True)
    executable = project_work / ("benchmark.exe" if sys.platform == "win32" else "benchmark")
    for _ in range(samples):
        result = run_checked(
            [
                str(janus),
                "build",
                "--release",
                "--timings=json",
                "-o",
                str(executable),
            ],
            project,
        )
        if result.stderr:
            raise RuntimeError(f"timed build polluted stderr: {result.stderr}")
        report = validate_timing_report(json.loads(result.stdout))
        reports.append(report)
    totals = [float(report["total_ms"]) for report in reports]
    return {
        "name": project.name,
        "sample_count": samples,
        "median_ms": round(statistics.median(totals), 3),
        "min_ms": round(min(totals), 3),
        "max_ms": round(max(totals), 3),
        "samples": reports,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--janus", required=True, type=pathlib.Path)
    parser.add_argument("--benchmarks", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--samples", type=int, default=5)
    args = parser.parse_args()
    if args.samples < 1:
        parser.error("--samples must be positive")

    janus = args.janus.resolve()
    benchmarks = args.benchmarks.resolve()
    output = args.output.resolve()
    projects = sorted(
        path for path in benchmarks.iterdir() if (path / "janus.toml").is_file()
    )
    if not projects:
        raise RuntimeError(f"no benchmark projects under {benchmarks}")
    project_names = {project.name for project in projects}
    if project_names != CANONICAL_PROJECTS:
        raise RuntimeError(
            "canonical benchmarks must be exactly small and medium; "
            f"found {sorted(project_names)}"
        )

    output.parent.mkdir(parents=True, exist_ok=True)
    work_root = output.parent / "compiler-benchmark-work"
    shutil.rmtree(work_root, ignore_errors=True)
    try:
        version = run_checked([str(janus), "--version"], benchmarks).stdout.strip()
        report = {
            "schema_version": 1,
            "generated_at": dt.datetime.now(dt.timezone.utc).isoformat(),
            "compiler": version,
            "sample_count": args.samples,
            "projects": [
                benchmark_project(janus, project, args.samples, work_root)
                for project in projects
            ],
        }
        output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    finally:
        shutil.rmtree(work_root, ignore_errors=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
