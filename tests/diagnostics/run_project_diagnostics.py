#!/usr/bin/env python3
"""Validate exhaustive, structured, denied, cached and contextual diagnostics."""

import argparse
import json
import pathlib
import shutil
import subprocess
import tempfile


def run(janus: pathlib.Path, project: pathlib.Path, *arguments: str):
    return subprocess.run(
        [str(janus), *arguments],
        cwd=project,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--janus", type=pathlib.Path, required=True)
    parser.add_argument("--fixture", type=pathlib.Path, required=True)
    arguments = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="janus-project-diagnostics-") as tmp:
        project = pathlib.Path(tmp) / "project"
        shutil.copytree(arguments.fixture, project)

        regular = run(arguments.janus, project, "check")
        assert regular.returncode == 0, regular.stderr
        assert "JANA0014" not in regular.stderr

        human = run(arguments.janus, project, "check", "--all")
        assert human.returncode == 0, human.stderr
        assert human.stderr.count("JANA0014") == 1, human.stderr
        assert "shared.janus" in human.stderr, human.stderr

        structured = run(
            arguments.janus,
            project,
            "check",
            "--all",
            "--diagnostic-format",
            "json",
        )
        assert structured.returncode == 0, structured.stderr
        payload = json.loads(structured.stderr)
        codes = [diagnostic["code"] for diagnostic in payload["diagnostics"]]
        assert codes.count("JANA0014") == 1, payload

        denied = run(arguments.janus, project, "check", "--all", "--deny-warnings")
        assert denied.returncode == 1, denied.stderr
        assert denied.stderr.count("JANA0014") == 1, denied.stderr

        isolated = run(arguments.janus, project, "check", "src/left.janus")
        assert isolated.returncode == 0, isolated.stderr

        built = run(arguments.janus, project, "build")
        assert built.returncode == 0, built.stderr
        denied_cached = run(arguments.janus, project, "build", "--deny-warnings")
        assert denied_cached.returncode == 1, denied_cached.stderr
        assert denied_cached.stderr.count("JANA0014") == 1, denied_cached.stderr

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
