#!/usr/bin/env python3
"""Enforce the VSIX handoff and GitHub Release workflow contract."""

from __future__ import annotations

import argparse
import re
import shlex
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class Step:
    values: dict[str, str] = field(default_factory=dict)
    with_values: dict[str, str] = field(default_factory=dict)


@dataclass
class Job:
    values: dict[str, str] = field(default_factory=dict)
    steps: list[Step] = field(default_factory=list)


def _value(lines: list[str], index: int, indent: int, value: str) -> tuple[str, int]:
    value = value.strip()
    if value not in ("|", ">", "|-", ">-"):
        return value, index
    block: list[str] = []
    index += 1
    while index < len(lines):
        line = lines[index]
        if line.strip() and len(line) - len(line.lstrip()) <= indent:
            break
        block.append(line[indent + 2 :] if line.strip() else "")
        index += 1
    return "\n".join(block), index - 1


def _continued_plain_value(lines: list[str], index: int, indent: int) -> tuple[str, int]:
    parts: list[str] = []
    index += 1
    while index < len(lines):
        line = lines[index]
        if line.strip() and len(line) - len(line.lstrip()) <= indent:
            break
        if line.strip():
            parts.append(line.strip())
        index += 1
    return " ".join(parts), index - 1


def parse_jobs(text: str, source: str) -> dict[str, Job]:
    """Parse the deliberately stable subset of YAML used by these workflows."""
    lines = text.splitlines()
    try:
        jobs_line = next(i for i, line in enumerate(lines) if line == "jobs:")
    except StopIteration as exc:
        raise ValueError(f"{source}: missing top-level jobs mapping") from exc
    jobs: dict[str, Job] = {}
    current: Job | None = None
    step: Step | None = None
    in_steps = False
    in_with = False
    i = jobs_line + 1
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        indent = len(line) - len(line.lstrip())
        if stripped and indent == 0:
            break
        job_match = re.fullmatch(r"  ([A-Za-z0-9_-]+):", line)
        if job_match:
            current = Job()
            jobs[job_match.group(1)] = current
            step = None
            in_steps = in_with = False
            i += 1
            continue
        if current is None or not stripped or stripped.startswith("#"):
            i += 1
            continue
        if indent == 4 and stripped == "steps:":
            in_steps = True
            step = None
            i += 1
            continue
        if indent == 4:
            in_steps = False
            step = None
            match = re.fullmatch(r"([A-Za-z0-9_-]+):\s*(.*)", stripped)
            if match:
                if match.group(1) == "needs" and not match.group(2):
                    value, i = _continued_plain_value(lines, i, indent)
                else:
                    value, i = _value(lines, i, indent, match.group(2))
                current.values[match.group(1)] = value
            i += 1
            continue
        if in_steps and indent == 6 and stripped.startswith("- "):
            step = Step()
            current.steps.append(step)
            in_with = False
            match = re.fullmatch(r"- ([A-Za-z0-9_-]+):\s*(.*)", stripped)
            if match:
                value, i = _value(lines, i, indent, match.group(2))
                step.values[match.group(1)] = value
            i += 1
            continue
        if in_steps and step is not None and indent == 8:
            match = re.fullmatch(r"([A-Za-z0-9_-]+):\s*(.*)", stripped)
            if match:
                key = match.group(1)
                if key == "with" and not match.group(2):
                    in_with = True
                else:
                    in_with = False
                    value, i = _value(lines, i, indent, match.group(2))
                    step.values[key] = value
            i += 1
            continue
        if in_steps and step is not None and in_with and indent == 10:
            match = re.fullmatch(r"([A-Za-z0-9_-]+):\s*(.*)", stripped)
            if match:
                value, i = _value(lines, i, indent, match.group(2))
                step.with_values[match.group(1)] = value
        i += 1
    return jobs


def _active_lines(script: str) -> list[str]:
    """Return non-comment shell source with backslash continuations joined."""
    result: list[str] = []
    pending = ""
    for raw in script.splitlines():
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            continue
        line = stripped
        if pending:
            line = pending + " " + line
        if line.endswith("\\"):
            pending = line[:-1].rstrip()
        else:
            result.append(line)
            pending = ""
    if pending:
        result.append(pending)
    return result


def _tokens(line: str) -> list[str]:
    try:
        return shlex.split(line, comments=True, posix=True)
    except ValueError:
        return []


def _has_command(script: str, expected: tuple[str, ...]) -> bool:
    """Match an active command invocation, not a comment or arbitrary text."""
    for line in _active_lines(script):
        tokens = _tokens(line)
        if tuple(tokens[: len(expected)]) == expected:
            return True
    return False


def _has_exact_command(script: str, expected: tuple[str, ...]) -> bool:
    """Match one complete active command, with no ignored shell suffix."""
    return any(tuple(_tokens(line)) == expected for line in _active_lines(script))


def _defines_or_aliases_command(script: str, command_name: str) -> bool:
    """Reject shell functions and aliases that can replace a required command."""
    escaped = re.escape(command_name)
    for line in _active_lines(script):
        if re.match(rf"^(?:function\s+{escaped}(?:\s|\()|{escaped}\s*\(\s*\))", line):
            return True
        tokens = _tokens(line)
        if tokens and tokens[0] == "alias" and any(
            token.startswith(f"{command_name}=") for token in tokens[1:]
        ):
            return True
    return False


def _validate_job_controls(
    job: Job, source: str, name: str, failures: list[str], required_if: str | None = None
) -> None:
    actual_if = job.values.get("if")
    if required_if is None:
        if actual_if is not None:
            failures.append(f"{source}: job {name!r} must not have an if key")
    elif actual_if != required_if:
        failures.append(f"{source}: job {name!r} must have exactly if: {required_if}")
    if "continue-on-error" in job.values:
        failures.append(f"{source}: job {name!r} must not have continue-on-error")


def _step(job: Job, name: str, source: str, failures: list[str]) -> Step | None:
    matches = [candidate for candidate in job.steps if candidate.values.get("name") == name]
    if len(matches) != 1:
        failures.append(f"{source}: expected exactly one step named {name!r}, found {len(matches)}")
        return None
    return matches[0]


def _required_step(job: Job, name: str, kind: str, source: str, failures: list[str]) -> Step | None:
    step = _step(job, name, source, failures)
    if step is None:
        return None
    if "if" in step.values:
        failures.append(f"{source}: mandatory step {name!r} must not have an if key")
    if "continue-on-error" in step.values:
        failures.append(f"{source}: mandatory step {name!r} must not have continue-on-error")
    if kind not in step.values or ({"uses", "run"} - {kind}) & step.values.keys():
        failures.append(f"{source}: mandatory step {name!r} must be a {kind} step")
    return step


def _require_action(job: Job, name: str, action: str, source: str, failures: list[str]) -> Step | None:
    step = _required_step(job, name, "uses", source, failures)
    # An inline YAML comment commonly records the human-readable action version.
    uses = step.values.get("uses", "").split(None, 1)[0] if step is not None else ""
    if step is not None and not re.fullmatch(re.escape(action) + r"@[0-9a-f]{40}", uses):
        failures.append(f"{source}: step {name!r} must use pinned action {action}")
    return step


def _require_commands(
    job: Job, name: str, commands: tuple[tuple[str, ...], ...], source: str, failures: list[str]
) -> Step | None:
    step = _required_step(job, name, "run", source, failures)
    if step is not None:
        script = step.values.get("run", "")
        for command in commands:
            if not _has_command(script, command):
                failures.append(f"{source}: step {name!r} lacks active command {' '.join(command)!r}")
    return step


def _require_exact_commands(
    job: Job, name: str, commands: tuple[tuple[str, ...], ...], source: str, failures: list[str]
) -> Step | None:
    """Require complete commands so shell suffixes cannot mask their failures."""
    step = _required_step(job, name, "run", source, failures)
    if step is not None:
        script = step.values.get("run", "")
        for command in commands:
            if not _has_exact_command(script, command):
                failures.append(
                    f"{source}: step {name!r} lacks exact command {' '.join(command)!r}"
                )
    return step


def _contains_lines(value: str, required: set[str]) -> bool:
    return required <= {line.strip() for line in value.splitlines() if line.strip()}


def _validate_download(step: Step, source: str, failures: list[str]) -> None:
    lines = _active_lines(step.values.get("run", ""))
    tokens = [_tokens(line) for line in lines]
    artifact_lists: list[list[str]] = []
    for index, line_tokens in enumerate(tokens):
        if line_tokens[:3] == ["for", "artifact", "in"]:
            values = line_tokens[3:]
            cursor = index + 1
            while "do" not in values and cursor < len(tokens):
                values.extend(tokens[cursor])
                cursor += 1
            if "do" in values:
                artifact_lists.append(values[: values.index("do")])
    if not any("janus-vscode" in artifacts for artifacts in artifact_lists):
        failures.append(f"{source}: Download distributions active artifact loop must include janus-vscode")
    script = step.values.get("run", "")
    expected = ("command", "gh", "run", "download", "$GITHUB_RUN_ID", "--name", "$artifact", "--dir", "dist/release")
    if not _has_exact_command(script, expected):
        failures.append(f"{source}: Download distributions lacks the exact active command gh run download line")
    if _defines_or_aliases_command(script, "gh") or _defines_or_aliases_command(script, "command"):
        failures.append(f"{source}: Download distributions must not redefine or alias gh/command")


def validate(root: Path) -> list[str]:
    handoff_path = root / ".github/workflows/publish-vscode.yml"
    ci_path = root / ".github/workflows/ci.yml"
    cmake_path = root / "CMakeLists.txt"
    failures: list[str] = []
    try:
        handoff_text = handoff_path.read_text(encoding="utf-8")
        handoff_jobs = parse_jobs(handoff_text, str(handoff_path.relative_to(root)))
        ci_jobs = parse_jobs(ci_path.read_text(encoding="utf-8"), str(ci_path.relative_to(root)))
        cmake_text = cmake_path.read_text(encoding="utf-8")
    except (OSError, ValueError) as exc:
        return [str(exc)]

    cmake_contract = """    add_test(
        NAME ci.vscode_release_workflow_contract
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/check-vscode-release-workflows.py"
                "${PROJECT_SOURCE_DIR}"
    )
    add_test(
        NAME ci.vscode_release_workflow_contract.guard_self_test
        COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/scripts/check-vscode-release-workflows.py"
                --self-test
                "${PROJECT_SOURCE_DIR}"
    )"""
    if cmake_text.count(cmake_contract) != 1:
        failures.append(
            "CMakeLists.txt: workflow contract tests must both receive the source root"
        )

    handoff_source = ".github/workflows/publish-vscode.yml"
    workflows: dict[Path, tuple[str, dict[str, Job]]] = {
        handoff_path: (handoff_text, handoff_jobs),
        ci_path: (ci_path.read_text(encoding="utf-8"), ci_jobs),
    }
    for workflow_path in sorted((root / ".github/workflows").glob("*.y*ml")):
        if workflow_path in workflows:
            continue
        try:
            workflow_text = workflow_path.read_text(encoding="utf-8")
            workflows[workflow_path] = (
                workflow_text,
                parse_jobs(workflow_text, str(workflow_path.relative_to(root))),
            )
        except (OSError, ValueError) as exc:
            failures.append(str(exc))

    for workflow_path, (workflow_text, workflow_jobs) in workflows.items():
        source = str(workflow_path.relative_to(root))
        lowered = workflow_text.lower()
        for forbidden in ("vsce_pat", "vscode-marketplace"):
            if forbidden in lowered:
                failures.append(f"{source}: workflow contains forbidden {forbidden!r}")
        if workflow_path == handoff_path and "secrets." in lowered:
            failures.append(f"{source}: workflow contains forbidden 'secrets.'")
        for job in workflow_jobs.values():
            for step in job.steps:
                for tokens in map(_tokens, _active_lines(step.values.get("run", ""))):
                    lowered_tokens = [token.lower() for token in tokens]
                    if any(
                        lowered_tokens[index : index + 2] == ["vsce", "publish"]
                        for index in range(len(lowered_tokens) - 1)
                    ):
                        failures.append(
                            f"{source}: workflow contains an active vsce publish command"
                        )
    handoff = handoff_jobs.get("handoff")
    if handoff is None:
        failures.append(f"{handoff_source}: missing handoff job")
    else:
        _validate_job_controls(handoff, handoff_source, "handoff", failures)
        resolve = _require_commands(handoff, "Resolve release tag", (("echo", "tag=$tag", ">>", "$GITHUB_OUTPUT"),), handoff_source, failures)
        if resolve is not None and "^v[0-9]+\\.[0-9]+\\.[0-9]+$" not in "\n".join(_active_lines(resolve.values.get("run", ""))):
            failures.append(f"{handoff_source}: Resolve release tag lacks the stable-tag check")
        checkout = _require_action(handoff, "Check out the exact tag", "actions/checkout", handoff_source, failures)
        if checkout is not None and checkout.with_values.get("ref") != "${{ steps.release.outputs.tag }}":
            failures.append(f"{handoff_source}: exact-tag checkout must use the resolved release tag")
        setup = _require_action(handoff, "Set up Node.js", "actions/setup-node", handoff_source, failures)
        if setup is not None and setup.with_values.get("cache-dependency-path") != "editors/vscode/package-lock.json":
            failures.append(f"{handoff_source}: Node setup must use the extension lockfile")
        verify = _require_commands(handoff, "Verify tag and extension versions", (), handoff_source, failures)
        if verify is not None:
            active = "\n".join(_active_lines(verify.values.get("run", "")))
            for fragment in ("require('./package.json').version", "require('./package-lock.json').version"):
                if fragment not in active:
                    failures.append(f"{handoff_source}: version verification lacks active {fragment!r}")
        _require_exact_commands(handoff, "Install locked dependencies", (("npm", "ci"),), handoff_source, failures)
        _require_exact_commands(handoff, "Test extension", (("npm", "test"),), handoff_source, failures)
        _require_exact_commands(handoff, "Package VSIX and list its content", (("npm", "run", "package", "--", "--out", "janus-language.vsix"), ("npx", "vsce", "ls", "--tree")), handoff_source, failures)
        _require_exact_commands(handoff, "Produce SHA-256 checksum", (("sha256sum", "janus-language.vsix", ">", "janus-language.vsix.sha256"),), handoff_source, failures)
        upload = _require_action(handoff, "Upload VSIX handoff artifact", "actions/upload-artifact", handoff_source, failures)
        if upload is not None:
            required = {"editors/vscode/janus-language.vsix", "editors/vscode/janus-language.vsix.sha256"}
            if not _contains_lines(upload.with_values.get("path", ""), required):
                failures.append(f"{handoff_source}: handoff upload path must contain the VSIX and checksum")
            if upload.with_values.get("if-no-files-found") != "error":
                failures.append(f"{handoff_source}: handoff upload must fail when files are missing")

    ci_source = ".github/workflows/ci.yml"
    extension = ci_jobs.get("vscode-extension")
    if extension is None:
        failures.append(f"{ci_source}: missing vscode-extension job")
    else:
        _validate_job_controls(extension, ci_source, "vscode-extension", failures)
        _require_action(extension, "Check out Janus", "actions/checkout", ci_source, failures)
        _require_action(extension, "Set up Node.js", "actions/setup-node", ci_source, failures)
        _require_exact_commands(extension, "Install extension dependencies", (("npm", "ci"),), ci_source, failures)
        _require_exact_commands(extension, "Test extension", (("npm", "test"),), ci_source, failures)
        _require_exact_commands(extension, "Package extension", (("npm", "run", "package", "--", "--out", "janus-language.vsix"), ("sha256sum", "janus-language.vsix", ">", "janus-language.vsix.sha256")), ci_source, failures)
        upload = _require_action(extension, "Upload extension", "actions/upload-artifact", ci_source, failures)
        if upload is not None:
            required = {"editors/vscode/janus-language.vsix", "editors/vscode/janus-language.vsix.sha256"}
            if upload.with_values.get("name") != "janus-vscode":
                failures.append(f"{ci_source}: extension upload artifact must be named janus-vscode")
            if not _contains_lines(upload.with_values.get("path", ""), required):
                failures.append(f"{ci_source}: extension upload path must contain the VSIX and checksum")
            if upload.with_values.get("if-no-files-found") != "error":
                failures.append(f"{ci_source}: extension upload must fail when files are missing")

    release = ci_jobs.get("release")
    if release is None:
        failures.append(f"{ci_source}: missing release job")
    else:
        _validate_job_controls(release, ci_source, "release", failures, "startsWith(github.ref, 'refs/tags/v')")
        needs = set(re.findall(r"[A-Za-z0-9_-]+", release.values.get("needs", "")))
        if "vscode-extension" not in needs:
            failures.append(f"{ci_source}: release job must need vscode-extension")
        _require_action(release, "Check out Janus", "actions/checkout", ci_source, failures)
        _require_commands(release, "Verify release version", (("cmake",),), ci_source, failures)
        download = _require_commands(release, "Download distributions", (("mkdir", "-p", "dist/release"),), ci_source, failures)
        if download is not None:
            _validate_download(download, ci_source, failures)
        _require_commands(release, "Downstream canaries from candidate archive", (("python3", "scripts/downstream_canary.py", "--archive", "$archive"),), ci_source, failures)
        _require_action(release, "Attest release provenance", "actions/attest", ci_source, failures)
        publish = _require_commands(release, "Publish GitHub release", (), ci_source, failures)
        if publish is not None:
            publish_script = publish.values.get("run", "")
            expected_publish = ("command", "gh", "release", "create", "$GITHUB_REF_NAME", "dist/release/*", "${options[@]}")
            if not _has_exact_command(publish_script, expected_publish):
                failures.append(f"{ci_source}: Publish GitHub release lacks the exact command gh release create line")
            if _defines_or_aliases_command(publish_script, "gh") or _defines_or_aliases_command(publish_script, "command"):
                failures.append(f"{ci_source}: Publish GitHub release must not redefine or alias gh/command")
        _require_commands(release, "Update release channel", (("cmake",),), ci_source, failures)
        if download is not None and publish is not None and release.steps.index(download) >= release.steps.index(publish):
            failures.append(f"{ci_source}: Download distributions must precede Publish GitHub release")
    return failures


def self_test(root: Path) -> int:
    handoff = Path(".github/workflows/publish-vscode.yml")
    ci = Path(".github/workflows/ci.yml")
    cmake = Path("CMakeLists.txt")
    files = (handoff, ci, cmake)
    originals = {path: (root / path).read_text(encoding="utf-8") for path in files}
    mutations = {
        "disabled handoff job": (handoff, "  handoff:\n", "  handoff:\n    if: false\n"),
        "fallible handoff job": (handoff, "  handoff:\n", "  handoff:\n    continue-on-error: true\n"),
        "handoff job secret": (handoff, "    runs-on: ubuntu-24.04\n", "    runs-on: ubuntu-24.04\n    env:\n      VSCE_PAT: ${{ secrets.VSCE_PAT }}\n"),
        "handoff marketplace environment": (handoff, "    runs-on: ubuntu-24.04\n", "    runs-on: ubuntu-24.04\n    environment: vscode-marketplace\n"),
        "continued marketplace publish": (handoff, "      - name: Test extension\n", "      - name: Publish extension\n        run: |\n          npx vsce \\\n            publish --packagePath janus-language.vsix\n\n      - name: Test extension\n"),
        "command-prefixed marketplace publish": (handoff, "      - name: Test extension\n", "      - name: Publish extension\n        run: command npx vsce publish --packagePath janus-language.vsix\n\n      - name: Test extension\n"),
        "masked npm test failure": (handoff, "        run: npm test\n", "        run: npm test || true\n"),
        "arbitrary false handoff upload": (handoff, "      - name: Upload VSIX handoff artifact\n", "      - name: Upload VSIX handoff artifact\n        if: ${{ 1 == 2 }}\n"),
        "disabled handoff package": (handoff, "      - name: Package VSIX and list its content\n", "      - name: Package VSIX and list its content\n        if: false\n"),
        "disabled vscode-extension job": (ci, "  vscode-extension:\n", "  vscode-extension:\n    if: false\n"),
        "fallible vscode-extension job": (ci, "  vscode-extension:\n", "  vscode-extension:\n    continue-on-error: true\n"),
        "disabled release job": (ci, "    if: startsWith(github.ref, 'refs/tags/v')\n", "    if: false\n"),
        "fallible release job": (ci, "  release:\n", "  release:\n    continue-on-error: true\n"),
        "disabled release download": (ci, "      - name: Download distributions\n", "      - name: Download distributions\n        if: false\n"),
        "missing active gh download": (ci, "            command gh run download \"$GITHUB_RUN_ID\" \\\n", "            # command gh run download \"$GITHUB_RUN_ID\" \\\n"),
        "fallible gh download": (ci, "              --dir dist/release\n", "              --dir dist/release || true\n"),
        "missing janus-vscode artifact": (ci, "            janus-vscode\n", "            janus-editor\n"),
        "disabled release publish": (ci, "      - name: Publish GitHub release\n", "      - name: Publish GitHub release\n        if: false\n"),
        "comment-only publish command": (ci, "          command gh release create \"$GITHUB_REF_NAME\" dist/release/* \"${options[@]}\"\n", "          # command gh release create \"$GITHUB_REF_NAME\" dist/release/* \"${options[@]}\"\n"),
        "fallible gh release": (ci, "          command gh release create \"$GITHUB_REF_NAME\" dist/release/* \"${options[@]}\"\n", "          command gh release create \"$GITHUB_REF_NAME\" dist/release/* \"${options[@]}\" || true\n"),
        "no-op gh function": (ci, "          options=(--verify-tag --generate-notes)\n", "          gh() { :; }\n          options=(--verify-tag --generate-notes)\n"),
        "gh alias": (ci, "          options=(--verify-tag --generate-notes)\n", "          alias gh=true\n          options=(--verify-tag --generate-notes)\n"),
        "no-op command function": (ci, "          options=(--verify-tag --generate-notes)\n", "          command() { :; }\n          options=(--verify-tag --generate-notes)\n"),
        "missing handoff if-no-files-found": (handoff, "          if-no-files-found: error\n", ""),
        "disabled vscode package": (ci, "      - name: Package extension\n", "      - name: Package extension\n        if: ${{ always() && false }}\n"),
        "Marketplace publish hidden in main release": (
            ci,
            "      - name: Update release channel\n",
            "      - name: Publish extension to Marketplace\n        run: npx vsce publish --packagePath dist/release/janus-language.vsix\n\n      - name: Update release channel\n",
        ),
        "missing CTest self-test root": (
            cmake,
            '                --self-test\n                "${PROJECT_SOURCE_DIR}"\n    )\n    add_test(\n        NAME release.nightly_policy',
            '                --self-test\n    )\n    add_test(\n        NAME release.nightly_policy',
        ),
    }
    checker = Path(__file__).resolve()
    modes = ((sys.executable,), (sys.executable, "-O"))
    for label, (target, old, new) in mutations.items():
        if originals[target].count(old) != 1:
            print(f"self-test setup failed for {label}: mutation target is not unique")
            return 1
        with tempfile.TemporaryDirectory(prefix="vscode-workflow-self-test-") as temp:
            candidate = Path(temp)
            for path, text in originals.items():
                destination = candidate / path
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_text(text.replace(old, new, 1) if path == target else text, encoding="utf-8")
            for mode in modes:
                result = subprocess.run((*mode, str(checker), str(candidate)), capture_output=True, text=True, check=False)
                if result.returncode == 0:
                    print(f"self-test failed: validator accepted mutation under {' '.join(mode)}: {label}")
                    return 1
    print(f"VS Code release workflow mutation self-test passed ({len(mutations)} mutations rejected under Python normal and -O)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", nargs="?", default=".")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    root = Path(args.root).resolve()
    if args.self_test:
        return self_test(root)
    failures = validate(root)
    if failures:
        print("VS Code release workflow contract violations:")
        for failure in failures:
            print(f"- {failure}")
        return 1
    print("VS Code manual handoff and GitHub Release contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
