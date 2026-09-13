#!/usr/bin/env python3
"""Check depth diagnostics through the compiler and semantic analysis boundary."""

from pathlib import Path
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="janus-syntax-depth-") as directory:
    source = Path(directory) / "main.janus"
    for depth, accepted in ((32, True), (126, True), (127, False), (5000, False)):
        programs = [
            f"def value() : {result_type} {{ return {expression} }}\n"
            "def main() : int { return 0 }\n"
            for expression, result_type in (
                ("(" * depth + "0" + ")" * depth, "int"),
                ("!" * depth + "true", "bool"),
                ("0" + " + 0" * depth, "int"),
            )
        ]
        programs.append("def main() : int { " + "while false { " * depth +
                        "return 0" + " }" * depth + " return 0 }")
        for program in programs:
            source.write_text(program)
            result = subprocess.run([binary, "check", str(source)],
                                    capture_output=True, timeout=15)
            expected = 0 if accepted else 1
            if result.returncode != expected:
                raise AssertionError((depth, result.returncode, result.stderr.decode()))
            if not accepted and b"JPAR0005" not in result.stderr:
                raise AssertionError(result.stderr.decode())
