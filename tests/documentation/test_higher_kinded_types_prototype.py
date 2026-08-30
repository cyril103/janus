from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "prototype_higher_kinded_types.py"


def load_prototype():
    spec = importlib.util.spec_from_file_location("hkt_prototype", SCRIPT)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class HigherKindedTypesPrototypeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.prototype = load_prototype()

    def test_partial_application_has_the_expected_kind(self):
        result = self.prototype.TypeConstructor("Result", 2)
        candidate = self.prototype.reference(
            result,
            self.prototype.HOLE,
            self.prototype.ConstructorArgument("IoError"),
        )

        self.prototype.require_kind(candidate, self.prototype.Kind(1))
        self.assertEqual("Result[int,IoError]", candidate.apply("int"))

    def test_wrong_kind_produces_bounded_diagnostic(self):
        result = self.prototype.TypeConstructor("Result", 2)
        candidate = self.prototype.reference(result)

        with self.assertRaisesRegex(TypeError, "has kind .* expected"):
            self.prototype.require_kind(candidate, self.prototype.Kind(1))

    def test_scenario_deduplicates_monomorphizations(self):
        report = self.prototype.run_scenario()

        self.assertEqual(6, report["kind_checks"])
        self.assertEqual(7, report["monomorphization_requests"])
        self.assertEqual(5, report["unique_specializations"])
        self.assertEqual(2, len(report["diagnostics"]))


if __name__ == "__main__":
    unittest.main()
