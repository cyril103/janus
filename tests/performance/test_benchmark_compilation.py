#!/usr/bin/env python3
import copy
import importlib.util
import math
import pathlib
import unittest


SCRIPT = pathlib.Path(__file__).parents[2] / "scripts" / "benchmark-compilation.py"
spec = importlib.util.spec_from_file_location("benchmark_compilation", SCRIPT)
if spec is None or spec.loader is None:
    raise RuntimeError(f"cannot load {SCRIPT}")
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


def valid_report():
    return {
        "schema_version": 1,
        "command": "build",
        "unit": "milliseconds",
        "source": "src/main.janus",
        "total_ms": 10.0,
        "phases": {
            "loading": 1.0,
            "parsing": 1.0,
            "analysis": 1.0,
            "llvm_generation": 1.0,
            "optimization": 2.0,
            "link": 3.0,
            "overhead": 1.0,
        },
    }


class BenchmarkCompilationTests(unittest.TestCase):
    def test_accepts_complete_timing_report(self):
        report = valid_report()
        self.assertIs(benchmark.validate_timing_report(report), report)

    def test_rejects_unknown_schema(self):
        report = valid_report()
        report["schema_version"] = 2
        with self.assertRaisesRegex(RuntimeError, "schema"):
            benchmark.validate_timing_report(report)

    def test_rejects_missing_phase(self):
        report = valid_report()
        del report["phases"]["link"]
        with self.assertRaisesRegex(RuntimeError, "phases"):
            benchmark.validate_timing_report(report)

    def test_rejects_non_finite_values(self):
        report = valid_report()
        report["total_ms"] = math.inf
        with self.assertRaisesRegex(RuntimeError, "values"):
            benchmark.validate_timing_report(report)

    def test_rejects_phase_total_mismatch(self):
        report = copy.deepcopy(valid_report())
        report["phases"]["overhead"] = 3.0
        with self.assertRaisesRegex(RuntimeError, "explain total"):
            benchmark.validate_timing_report(report)


if __name__ == "__main__":
    unittest.main()
