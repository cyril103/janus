#!/usr/bin/env python3
"""Structural and executable checks for the supported LLVM range."""

from __future__ import annotations

import re
import subprocess
import sys
import unittest
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(sys.argv.pop()) if len(sys.argv) > 1 else Path(__file__).parents[2]
VERSION_CHECK = ROOT / "cmake" / "validate_llvm_version.cmake"
WORKFLOW = ROOT / ".github" / "workflows" / "llvm-compatibility.yml"
MAIN_CI = ROOT / ".github" / "workflows" / "ci.yml"
INSTALL_LLVM_SHA = "ebc0426251bc40c7cd31162802432c68818ab8f0"
EXPECTED_TRIGGER_BLOCK = """on:
  push:
    branches: [main]
  pull_request:
  workflow_dispatch:
"""


@dataclass(frozen=True)
class Line:
    indent: int
    text: str


def yaml_lines(document: str) -> list[Line]:
    """Tokenize the indentation used by this workflow's YAML subset."""
    result = []
    for number, raw in enumerate(document.splitlines(), 1):
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        prefix = raw[: len(raw) - len(raw.lstrip(" "))]
        if "\t" in prefix:
            raise ValueError(f"line {number}: tabs are not valid indentation")
        result.append(Line(len(prefix), raw.strip()))
    return result


def child_block(lines: list[Line], key: str, parent_indent: int = -2) -> list[Line]:
    matches = [index for index, line in enumerate(lines)
               if line.indent == parent_indent + 2 and line.text == f"{key}:"]
    if len(matches) != 1:
        raise ValueError(f"expected exactly one {key!r} mapping")
    start = matches[0]
    end = next((index for index in range(start + 1, len(lines))
                if lines[index].indent <= lines[start].indent), len(lines))
    return lines[start + 1:end]


def scalar(block: list[Line], key: str, indent: int) -> str:
    prefix = f"{key}:"
    matches = [line.text[len(prefix):].strip() for line in block
               if line.indent == indent and line.text.startswith(prefix)]
    if len(matches) != 1:
        raise ValueError(f"expected exactly one {key!r} value")
    return matches[0].split(" #", 1)[0]


def supported_job(document: str) -> list[Line]:
    lines = yaml_lines(document)
    jobs = child_block(lines, "jobs")
    return child_block(jobs, "supported-bounds", 0)


def parse_steps(job: list[Line]) -> list[dict[str, str]]:
    steps_block = child_block(job, "steps", 2)
    starts = [index for index, line in enumerate(steps_block)
              if line.indent == 6 and line.text.startswith("- ")]
    steps: list[dict[str, str]] = []
    for position, start in enumerate(starts):
        end = starts[position + 1] if position + 1 < len(starts) else len(steps_block)
        item = steps_block[start:end]
        fields: dict[str, str] = {}
        first = item[0].text[2:]
        if ":" not in first:
            raise ValueError("step list items must start with a mapping key")
        first_key, first_value = first.split(":", 1)
        fields[first_key] = first_value.strip()
        parent = ""
        for index, line in enumerate(item[1:], 1):
            if line.indent == 8 and line.text.endswith(":"):
                parent = line.text[:-1]
                continue
            if line.indent == 10 and parent and ":" in line.text:
                key, value = line.text.split(":", 1)
                fields[f"{parent}.{key}"] = value.strip().split(" #", 1)[0]
                continue
            if line.indent != 8 or ":" not in line.text:
                continue
            parent = ""
            key, value = line.text.split(":", 1)
            value = value.strip()
            if value in ("|", ">", "|-", ">-"):
                following = []
                for continuation in item[index + 1:]:
                    if continuation.indent <= line.indent:
                        break
                    following.append(continuation.text)
                value = " ".join(following)
            fields[key] = value.split(" #", 1)[0]
        steps.append(fields)
    return steps


def normalized(command: str) -> str:
    return " ".join(command.split())


def validate_workflow(document: str) -> list[str]:
    errors: list[str] = []
    if document.count(EXPECTED_TRIGGER_BLOCK) != 1:
        errors.append("workflow must run on pull requests, main pushes, and workflow_dispatch")
    try:
        job = supported_job(document)
        runner = scalar(job, "runs-on", 4)
        strategy = child_block(job, "strategy", 2)
        matrix = child_block(strategy, "matrix", 4)
        include = child_block(matrix, "include", 6)
        versions = []
        for line in include:
            match = re.fullmatch(r"- llvm_version:\s*[\"']?([0-9.]+)[\"']?", line.text)
            if line.indent == 10 and match:
                versions.append(match.group(1))
            elif line.indent >= 10:
                errors.append("matrix entries may only contain llvm_version")
        steps = parse_steps(job)
    except ValueError as error:
        return [str(error)]

    if runner != "ubuntu-24.04":
        errors.append("supported-bounds must run on ubuntu-24.04")
    direct_job_keys = {
        line.text.split(":", 1)[0]
        for line in job
        if line.indent == 4 and ":" in line.text
    }
    if "if" in direct_job_keys:
        errors.append("supported-bounds job must not have an if key")
    if "continue-on-error" in direct_job_keys:
        errors.append("supported-bounds job must not have continue-on-error")
    if versions != ["18", "21.1.8"]:
        errors.append("matrix must contain exactly LLVM 18 and 21.1.8")

    names = [step.get("name", "") for step in steps]
    expected_names = [
        "Check out Janus",
        "Install LLVM 18 distribution packages",
        "Install pinned LLVM 21 toolchain",
        "Select coherent LLVM root",
        "Configure with coherent LLVM, Clang and LLD",
        "Build representative backend targets",
        "Exercise backend, constants, object emission and linkage",
        "Build distribution",
        "Smoke test packaged toolchain",
    ]
    if names != expected_names:
        errors.append("required steps must each appear exactly once and in order")
        return errors
    expected_conditions = {
        expected_names[1]: "matrix.llvm_version == '18'",
        expected_names[2]: "matrix.llvm_version == '21.1.8'",
    }
    for step in steps:
        expected_if = expected_conditions.get(step.get("name", ""))
        if step.get("if") != expected_if:
            errors.append(f"unexpected if condition in step {step.get('name', '')!r}")
    if any("continue-on-error" in step for step in steps):
        errors.append("required steps must not have continue-on-error")

    expected_runs = {
        expected_names[1]: normalized("""sudo apt-get update
            sudo apt-get install --yes clang-18 lld-18 llvm-18-dev ninja-build"""),
        expected_names[3]: " ".join((
            'if [[ "${{ matrix.llvm_version }}" == "18" ]]; then',
            "llvm_root=/usr/lib/llvm-18",
            "else",
            'llvm_root="$LLVM_PATH"',
            "fi",
            'test -x "$llvm_root/bin/clang"',
            'test -x "$llvm_root/bin/ld.lld"',
            'echo "LLVM_ROOT=$llvm_root" >> "$GITHUB_ENV"',
        )),
        expected_names[4]: " ".join((
            "cmake -S . -B build-llvm -G Ninja \\",
            "-DCMAKE_BUILD_TYPE=Release \\",
            '-DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld" \\',
            '-DCMAKE_C_COMPILER="$LLVM_ROOT/bin/clang" \\',
            '-DCMAKE_CXX_COMPILER="$LLVM_ROOT/bin/clang++" \\',
            '-DLLVM_DIR="$LLVM_ROOT/lib/cmake/llvm" \\',
            '-DJANUS_CLANG_EXECUTABLE="$LLVM_ROOT/bin/clang" \\',
            '-DJANUS_LLD_EXECUTABLE="$LLVM_ROOT/bin/ld.lld" \\',
            '-DJANUS_SOURCE_SHA="$GITHUB_SHA"',
        )),
        expected_names[5]: normalized("""cmake --build build-llvm --parallel
            --target janus_llvm_backend compile_time_constant_test
            backend_link_test janus"""),
        expected_names[6]: normalized("""ctest --test-dir build-llvm --output-on-failure
            --tests-regex
            '^(language\\.compile_time_constants|backend\\.link_contract|cli\\.emit_llvm|cli\\.emit_object|cli\\.build|cli\\.run)$'"""),
        expected_names[7]: "cmake --build build-llvm --parallel --target dist",
        expected_names[8]: "scripts/smoke-test-package.sh build-llvm/janus-*.tar.gz",
    }
    by_name = {step["name"]: step for step in steps}
    checkout = by_name[expected_names[0]].get("uses", "")
    if not re.fullmatch(r"actions/checkout@[0-9a-f]{40}", checkout):
        errors.append("checkout must be pinned to a full commit SHA")
    install = by_name[expected_names[2]]
    if install.get("uses") != f"KyleMayes/install-llvm-action@{INSTALL_LLVM_SHA}":
        errors.append("install-llvm-action must use the approved exact SHA")
    expected_install_inputs = {
        "with.version": "${{ matrix.llvm_version }}",
        "with.directory": "${{ runner.temp }}/llvm-${{ matrix.llvm_version }}",
        "with.env": "false",
    }
    if {key: install.get(key) for key in expected_install_inputs} != expected_install_inputs:
        errors.append("install-llvm-action inputs must select the pinned matrix toolchain")
    if by_name[expected_names[3]].get("shell") != "bash":
        errors.append("LLVM root selection step must use bash")
    if by_name[expected_names[4]].get("shell") != "bash":
        errors.append("configure step must use bash")
    if by_name[expected_names[8]].get("shell") != "bash":
        errors.append("package smoke step must use bash")
    for name, command in expected_runs.items():
        if normalized(by_name[name].get("run", "")) != command:
            errors.append(f"unexpected command in step {name!r}")
    return errors


class LlvmCMakeBoundsTest(unittest.TestCase):
    def check_version(self, major: int) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["cmake", f"-DLLVM_VERSION_MAJOR={major}", "-P", str(VERSION_CHECK)],
            cwd=ROOT, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, check=False,
        )

    def test_supported_bounds_are_accepted(self) -> None:
        for major in (18, 21):
            with self.subTest(major=major):
                result = self.check_version(major)
                self.assertEqual(result.returncode, 0, result.stdout)

    def test_versions_outside_the_range_are_rejected_clearly(self) -> None:
        for major in (17, 22):
            with self.subTest(major=major):
                result = self.check_version(major)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                output = " ".join(result.stdout.split())
                self.assertIn("Supported LLVM versions are 18 through 21 inclusive", output)
                self.assertIn(f"found {major}", output)

    def test_top_level_configuration_enforces_the_check(self) -> None:
        top_level = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        find_position = top_level.find("find_package(LLVM REQUIRED CONFIG)")
        include_position = top_level.find("include(cmake/validate_llvm_version.cmake)")
        self.assertGreaterEqual(find_position, 0)
        self.assertGreater(include_position, find_position)


class LlvmCompatibilityWorkflowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")

    def assertMutationRejected(self, old: str, new: str) -> None:
        self.assertIn(old, self.workflow)
        mutated = self.workflow.replace(old, new, 1)
        self.assertTrue(validate_workflow(mutated), "mutation unexpectedly accepted")

    def test_workflow_contract(self) -> None:
        self.assertEqual(validate_workflow(self.workflow), [])

    def test_rejects_if_false_on_multiple_required_steps(self) -> None:
        for name in ("Check out Janus", "Exercise backend, constants, object emission and linkage"):
            with self.subTest(name=name):
                self.assertMutationRejected(f"- name: {name}\n", f"- name: {name}\n        if: false\n")

    def test_rejects_if_expression_on_multiple_required_steps(self) -> None:
        for name in ("Build representative backend targets", "Smoke test packaged toolchain"):
            with self.subTest(name=name):
                self.assertMutationRejected(f"- name: {name}\n", f"- name: {name}\n        if: ${{{{ 1 == 2 }}}}\n")

    def test_rejects_job_and_step_failure_suppression(self) -> None:
        mutations = (
            ("  supported-bounds:\n", "  supported-bounds:\n    if: false\n"),
            ("    runs-on: ubuntu-24.04\n", "    runs-on: ubuntu-24.04\n    continue-on-error: true\n"),
            ("      - name: Build distribution\n", "      - name: Build distribution\n        continue-on-error: true\n"),
        )
        for old, new in mutations:
            with self.subTest(old=old):
                self.assertMutationRejected(old, new)

    def test_rejects_disabled_required_triggers(self) -> None:
        mutations = (
            (EXPECTED_TRIGGER_BLOCK, "on:\n  workflow_dispatch:\n"),
            ("  pull_request:\n", "  pull_request:\n    branches: [__disabled__]\n"),
        )
        for old, new in mutations:
            with self.subTest(old=old):
                self.assertMutationRejected(old, new)

    def test_rejects_removed_ctest_target_dist_and_smoke(self) -> None:
        mutations = (
            ("ctest --test-dir build-llvm", "true # removed ctest"),
            ("janus_llvm_backend compile_time_constant_test", "janus_llvm_backend"),
            ("cmake --build build-llvm --parallel --target dist", "true # removed dist"),
            ("scripts/smoke-test-package.sh build-llvm/janus-*.tar.gz", "true # removed smoke"),
        )
        for old, new in mutations:
            with self.subTest(old=old):
                self.assertMutationRejected(old, new)

    def test_rejects_bound_change(self) -> None:
        self.assertMutationRejected('llvm_version: "21.1.8"', 'llvm_version: "20.1.8"')

    def test_rejects_runner_or_install_action_change(self) -> None:
        mutations = (
            ("runs-on: ubuntu-24.04", "runs-on: ubuntu-latest"),
            (INSTALL_LLVM_SHA, "0" * 40),
            ("version: ${{ matrix.llvm_version }}", "version: 18.1.8"),
        )
        for old, new in mutations:
            with self.subTest(old=old):
                self.assertMutationRejected(old, new)

    def test_rejects_incoherent_llvm_clang_or_lld_path(self) -> None:
        mutations = (
            ('LLVM_DIR="$LLVM_ROOT/lib/cmake/llvm"', 'LLVM_DIR="/opt/other/lib/cmake/llvm"'),
            ('JANUS_CLANG_EXECUTABLE="$LLVM_ROOT/bin/clang"', 'JANUS_CLANG_EXECUTABLE="/usr/bin/clang"'),
            ('JANUS_LLD_EXECUTABLE="$LLVM_ROOT/bin/ld.lld"', 'JANUS_LLD_EXECUTABLE="/usr/bin/ld.lld"'),
        )
        for old, new in mutations:
            with self.subTest(old=old):
                self.assertMutationRejected(old, new)


    def test_main_macos_job_uses_supported_coherent_llvm_21(self) -> None:
        main_ci = MAIN_CI.read_text(encoding="utf-8")
        required = (
            "brew install llvm@21 lld@21 ninja",
            "/opt/homebrew/opt/llvm@21/lib/cmake/llvm",
            "/opt/homebrew/opt/llvm@21/bin/clang",
            "/opt/homebrew/opt/lld@21/bin/ld.lld",
            '-DJANUS_CLANG_EXECUTABLE="$MATRIX_JANUS_CLANG"',
            '-DJANUS_LLD_EXECUTABLE="$MATRIX_JANUS_LLD"',
        )
        for fragment in required:
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, main_ci)
        self.assertNotIn("brew install llvm lld ninja", main_ci)

    def test_main_windows_job_uses_pinned_coherent_llvm_21(self) -> None:
        main_ci = MAIN_CI.read_text(encoding="utf-8")
        required = (
            "Pin Windows LLVM stack to 21.1.8",
            "https://repo.msys2.org/mingw/clang64",
            "mingw-w64-clang-x86_64-llvm-libs-21.1.8-4-any.pkg.tar.zst",
            "mingw-w64-clang-x86_64-llvm-tools-21.1.8-4-any.pkg.tar.zst",
            "mingw-w64-clang-x86_64-llvm-21.1.8-4-any.pkg.tar.zst",
            "mingw-w64-clang-x86_64-compiler-rt-21.1.8-4-any.pkg.tar.zst",
            "mingw-w64-clang-x86_64-lld-21.1.8-4-any.pkg.tar.zst",
            "mingw-w64-clang-x86_64-clang-libs-21.1.8-4-any.pkg.tar.zst",
            "mingw-w64-clang-x86_64-clang-21.1.8-4-any.pkg.tar.zst",
            "-DLLVM_DIR=/clang64/lib/cmake/llvm",
            "-DJANUS_CLANG_EXECUTABLE=/clang64/bin/clang.exe",
            "-DJANUS_LLD_EXECUTABLE=/clang64/bin/ld.lld.exe",
        )
        for fragment in required:
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, main_ci)
        self.assertEqual(main_ci.count("-21.1.8-4-any.pkg.tar.zst"), 7)


if __name__ == "__main__":
    unittest.main(verbosity=2)
