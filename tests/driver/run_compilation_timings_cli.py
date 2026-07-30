#!/usr/bin/env python3
import argparse
import json
import pathlib
import subprocess
import sys


REQUIRED_PHASES = [
    "loading",
    "parsing",
    "analysis",
    "llvm_generation",
    "optimization",
    "link",
    "overhead",
]


def fail(message: str) -> None:
    raise AssertionError(message)


def run(command: list[str], cwd: pathlib.Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, text=True, capture_output=True, check=False)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--janus", required=True, type=pathlib.Path)
    parser.add_argument("--source", required=True, type=pathlib.Path)
    parser.add_argument("--work-dir", required=True, type=pathlib.Path)
    args = parser.parse_args()

    args.work_dir.mkdir(parents=True, exist_ok=True)
    executable = args.work_dir / ("timed.exe" if sys.platform == "win32" else "timed")

    json_result = run(
        [str(args.janus), "build", str(args.source), "-o", str(executable), "--timings=json"],
        args.work_dir,
    )
    if json_result.returncode != 0:
        fail(f"JSON timing build failed: {json_result.stderr}")
    if json_result.stderr:
        fail(f"JSON timing build polluted stderr: {json_result.stderr!r}")
    report = json.loads(json_result.stdout)
    if report.get("schema_version") != 1 or report.get("command") != "build":
        fail(f"unexpected report identity: {report}")
    if report.get("unit") != "milliseconds":
        fail(f"unexpected timing unit: {report}")
    phases = report.get("phases")
    if not isinstance(phases, dict) or list(phases) != REQUIRED_PHASES:
        fail(f"unexpected phases: {phases}")
    if any(not isinstance(value, (int, float)) or value < 0 for value in phases.values()):
        fail(f"invalid phase duration: {phases}")
    total = report.get("total_ms")
    if not isinstance(total, (int, float)) or total <= 0:
        fail(f"invalid total: {total}")
    if abs(sum(phases.values()) - total) > 0.05:
        fail(f"phases do not explain total: phases={sum(phases.values())}, total={total}")

    human_executable = args.work_dir / ("timed-human.exe" if sys.platform == "win32" else "timed-human")
    human_result = run(
        [str(args.janus), "build", str(args.source), "-o", str(human_executable), "--timings"],
        args.work_dir,
    )
    if human_result.returncode != 0:
        fail(f"human timing build failed: {human_result.stderr}")
    for phase in REQUIRED_PHASES:
        if phase not in human_result.stderr:
            fail(f"human timing output misses {phase}: {human_result.stderr}")
    if "total" not in human_result.stderr or "100.0%" not in human_result.stderr:
        fail(f"human timing output does not explain total: {human_result.stderr}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
