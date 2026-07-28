from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "audit_stdlib.py"
REPORT = ROOT / "docs" / "audits" / "stdlib-0.7.10.md"
PUBLIC_SURFACE = ROOT / "docs" / "public-surface-0.5.json"


def load_audit_module():
    spec = importlib.util.spec_from_file_location("audit_stdlib", SCRIPT)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Impossible de charger {SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class StdlibAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.audit = load_audit_module()
        cls.inventory = json.loads(PUBLIC_SURFACE.read_text(encoding="utf-8"))

    def test_audit_covers_every_module_and_public_symbol(self):
        model = self.audit.collect_audit(ROOT)
        expected_modules = {
            entry["name"] for entry in self.inventory["stdlib_modules"]
        }
        expected_symbols = {
            (entry["name"], symbol)
            for entry in self.inventory["stdlib_modules"]
            for symbol in entry["symbols"]
        }

        self.assertEqual(expected_modules, set(model.modules))
        self.assertEqual(expected_symbols, set(model.symbols))
        self.assertEqual(28, len(model.modules))
        self.assertEqual(637, len(model.symbols))
        self.assertTrue(all(module.owner for module in model.modules.values()))
        self.assertTrue(all(symbol.owner for symbol in model.symbols.values()))
        self.assertTrue(all(symbol.decision for symbol in model.symbols.values()))

    def test_committed_report_is_deterministic_and_current(self):
        expected = self.audit.render_report(self.audit.collect_audit(ROOT))
        self.assertEqual(expected, REPORT.read_text(encoding="utf-8"))

    def test_stale_report_is_rejected(self):
        expected = self.audit.render_report(self.audit.collect_audit(ROOT))
        with tempfile.TemporaryDirectory() as directory:
            stale = Path(directory) / "stdlib-audit.md"
            stale.write_text(expected + "\n<!-- stale -->\n", encoding="utf-8")
            self.assertFalse(self.audit.report_is_current(stale, expected))


if __name__ == "__main__":
    unittest.main()
