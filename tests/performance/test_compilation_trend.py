#!/usr/bin/env python3
import importlib.util
import math
import pathlib
import unittest


SCRIPT = pathlib.Path(__file__).parents[2] / "scripts" / "compilation-trend.py"
spec = importlib.util.spec_from_file_location("compilation_trend", SCRIPT)
if spec is None or spec.loader is None:
    raise RuntimeError(f"cannot load {SCRIPT}")
trend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trend)


def current(*measurements):
    return {
        "schema_version": 1,
        "sample_count": 5,
        "projects": [
            {
                "name": name,
                "sample_count": 5,
                "median_ms": median,
                "min_ms": median,
                "max_ms": median,
                "samples": [{"total_ms": median} for _ in range(5)],
            }
            for name, median in measurements
        ],
    }


def previous(*projects):
    return {
        "schema_version": 1,
        "threshold_ratio": 0.15,
        "required_consecutive_jobs": 2,
        "projects": {
            name: {
                "baseline_median_ms": baseline,
                "consecutive_regressions": consecutive,
            }
            for name, baseline, consecutive in projects
        },
    }


class CompilationTrendTests(unittest.TestCase):
    def test_first_fifteen_percent_regression_is_only_a_candidate(self):
        result = trend.evaluate_trend(
            current(("small", 115.0)), previous(("small", 100.0, 0))
        )

        project = result["projects"]["small"]
        self.assertEqual(project["consecutive_regressions"], 1)
        self.assertFalse(project["alert"])
        self.assertEqual(project["baseline_median_ms"], 100.0)
        self.assertFalse(result["alert"])

    def test_second_consecutive_regression_triggers_alert(self):
        result = trend.evaluate_trend(
            current(("small", 117.0)), previous(("small", 100.0, 1))
        )

        self.assertTrue(result["projects"]["small"]["alert"])
        self.assertTrue(result["alert"])

    def test_recovery_resets_counter_and_moves_baseline(self):
        result = trend.evaluate_trend(
            current(("small", 108.0)), previous(("small", 100.0, 1))
        )

        project = result["projects"]["small"]
        self.assertEqual(project["consecutive_regressions"], 0)
        self.assertEqual(project["baseline_median_ms"], 108.0)
        self.assertFalse(project["alert"])

    def test_new_project_establishes_baseline_without_alert(self):
        result = trend.evaluate_trend(current(("medium", 250.0)), None)

        project = result["projects"]["medium"]
        self.assertEqual(project["baseline_median_ms"], 250.0)
        self.assertEqual(project["consecutive_regressions"], 0)
        self.assertFalse(result["alert"])

    def test_current_report_requires_exactly_five_samples(self):
        report = current(("small", 100.0))
        report["sample_count"] = 4
        with self.assertRaisesRegex(ValueError, "exactly five"):
            trend.evaluate_trend(report, None)

    def test_current_report_rejects_duplicate_projects(self):
        report = current(("small", 100.0), ("small", 101.0))
        with self.assertRaisesRegex(ValueError, "unique"):
            trend.evaluate_trend(report, None)

    def test_current_report_rejects_non_finite_median(self):
        report = current(("small", 100.0))
        report["projects"][0]["median_ms"] = math.nan
        with self.assertRaisesRegex(ValueError, "invalid median"):
            trend.evaluate_trend(report, None)

    def test_current_report_rejects_missing_raw_sample(self):
        report = current(("small", 100.0))
        report["projects"][0]["samples"].pop()
        with self.assertRaisesRegex(ValueError, "five raw samples"):
            trend.evaluate_trend(report, None)

    def test_previous_report_must_match_schema_and_project_set(self):
        report = previous(("medium", 200.0, 0))
        with self.assertRaisesRegex(ValueError, "project set"):
            trend.evaluate_trend(current(("small", 100.0)), report)
        report = previous(("small", 100.0, 0))
        report["schema_version"] = 2
        with self.assertRaisesRegex(ValueError, "schema_version"):
            trend.evaluate_trend(current(("small", 100.0)), report)


if __name__ == "__main__":
    unittest.main()
