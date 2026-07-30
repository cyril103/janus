#!/usr/bin/env python3
"""Maintain a non-blocking compiler-performance alert state."""

from __future__ import annotations

import argparse
import json
import math
import pathlib
from typing import Any, cast


def _finite_number(value: Any) -> bool:
    return (
        not isinstance(value, bool)
        and isinstance(value, (int, float))
        and math.isfinite(float(value))
    )


def validate_current(report: Any) -> None:
    if not isinstance(report, dict) or report.get("schema_version") != 1:
        raise ValueError("current report has an unsupported schema_version")
    if report.get("sample_count") != 5:
        raise ValueError("current report must contain exactly five samples")
    measurements = report.get("projects")
    if not isinstance(measurements, list) or not measurements:
        raise ValueError("current report must contain projects")
    names: set[str] = set()
    for measurement in measurements:
        if not isinstance(measurement, dict):
            raise ValueError("current project measurement must be an object")
        name = measurement.get("name")
        if not isinstance(name, str) or not name or name in names:
            raise ValueError("current project names must be non-empty and unique")
        names.add(name)
        if measurement.get("sample_count") != 5:
            raise ValueError(f"project {name} must contain exactly five samples")
        samples = measurement.get("samples")
        if not isinstance(samples, list) or len(samples) != 5:
            raise ValueError(f"project {name} must provide five raw samples")
        if any(
            not isinstance(sample, dict)
            or not _finite_number(sample.get("total_ms"))
            or float(cast(float, sample.get("total_ms"))) <= 0.0
            for sample in samples
        ):
            raise ValueError(f"project {name} has invalid raw samples")
        for metric in ("median_ms", "min_ms", "max_ms"):
            if not _finite_number(measurement.get(metric)) or float(
                cast(float, measurement.get(metric))
            ) <= 0.0:
                raise ValueError(f"project {name} has an invalid {metric}")


def validate_previous(
    report: Any,
    current_names: set[str],
    threshold: float,
    confirmations: int,
) -> None:
    if not isinstance(report, dict) or report.get("schema_version") != 1:
        raise ValueError("previous trend has an unsupported schema_version")
    previous_threshold = report.get("threshold_ratio")
    if not _finite_number(previous_threshold) or not math.isclose(
        float(cast(float, previous_threshold)), threshold
    ):
        raise ValueError("previous trend has an incompatible threshold")
    if report.get("required_consecutive_jobs") != confirmations:
        raise ValueError("previous trend has an incompatible confirmation count")
    projects = report.get("projects")
    if not isinstance(projects, dict) or set(projects) != current_names:
        raise ValueError("previous trend project set does not match current report")
    for name, project in projects.items():
        if not isinstance(project, dict):
            raise ValueError(f"previous project {name} must be an object")
        baseline = project.get("baseline_median_ms")
        consecutive = project.get("consecutive_regressions")
        if not _finite_number(baseline) or float(
            cast(float, baseline)
        ) <= 0.0:
            raise ValueError(f"previous project {name} has an invalid baseline")
        if (
            isinstance(consecutive, bool)
            or not isinstance(consecutive, int)
            or consecutive < 0
        ):
            raise ValueError(f"previous project {name} has an invalid counter")


def evaluate_trend(
    current: dict[str, Any],
    previous: dict[str, Any] | None,
    threshold: float = 0.15,
    confirmations: int = 2,
) -> dict[str, Any]:
    validate_current(current)
    current_names = {measurement["name"] for measurement in current["projects"]}
    if previous is not None:
        validate_previous(previous, current_names, threshold, confirmations)
    old_projects = (previous or {}).get("projects", {})
    projects: dict[str, Any] = {}
    any_alert = False
    for measurement in current["projects"]:
        name = str(measurement["name"])
        median = float(measurement["median_ms"])
        old = old_projects.get(name)
        if old is None:
            baseline = median
            consecutive = 0
            increase = 0.0
        else:
            baseline = float(old["baseline_median_ms"])
            increase = median / baseline - 1.0 if baseline > 0.0 else 0.0
            if increase > threshold or math.isclose(
                increase, threshold, rel_tol=1e-12, abs_tol=1e-12
            ):
                consecutive = int(old.get("consecutive_regressions", 0)) + 1
            else:
                baseline = median
                consecutive = 0
        alert = consecutive >= confirmations
        any_alert = any_alert or alert
        projects[name] = {
            "median_ms": median,
            "baseline_median_ms": baseline,
            "increase_ratio": round(increase, 6),
            "consecutive_regressions": consecutive,
            "alert": alert,
        }
    return {
        "schema_version": 1,
        "threshold_ratio": threshold,
        "required_consecutive_jobs": confirmations,
        "alert": any_alert,
        "projects": projects,
    }


def markdown_summary(state: dict[str, Any]) -> str:
    lines = [
        "## Compiler performance dashboard",
        "",
        "| Project | Median | Baseline | Change | Consecutive | Status |",
        "|---|---:|---:|---:|---:|---|",
    ]
    for name, project in sorted(state["projects"].items()):
        status = "⚠️ alert" if project["alert"] else "✅ observed"
        lines.append(
            f"| {name} | {project['median_ms']:.3f} ms | "
            f"{project['baseline_median_ms']:.3f} ms | "
            f"{project['increase_ratio'] * 100:+.1f}% | "
            f"{project['consecutive_regressions']} | {status} |"
        )
    lines.extend(
        [
            "",
            "An alert requires a median increase of at least 15% across five "
            "samples, confirmed by two consecutive jobs. Alerts are informative "
            "and never fail CI.",
        ]
    )
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--current", required=True, type=pathlib.Path)
    parser.add_argument("--previous", type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--summary", required=True, type=pathlib.Path)
    args = parser.parse_args()

    current = json.loads(args.current.read_text(encoding="utf-8"))
    previous = None
    if args.previous is not None:
        if not args.previous.is_file():
            raise FileNotFoundError(f"previous trend does not exist: {args.previous}")
        previous = json.loads(args.previous.read_text(encoding="utf-8"))
    state = evaluate_trend(current, previous)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
    args.summary.write_text(markdown_summary(state), encoding="utf-8")
    if state["alert"]:
        print("::warning::Compiler performance regression confirmed by two jobs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
