from __future__ import annotations

import importlib.util
import json
import re
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "audit_stdlib.py"
REPORT = ROOT / "docs" / "audits" / "stdlib-0.7.4.md"
PUBLIC_SURFACE = ROOT / "docs" / "public-surface-0.5.json"
STDLIB_REFERENCE = ROOT / "docs" / "stdlib-reference.md"
STDLIB_ROOT = ROOT / "stdlib" / "std"
API_INDEX = ROOT / "website" / "docs" / "reference" / "stdlib" / "api-index.json"
API_HTML = ROOT / "website" / "docs" / "reference" / "stdlib" / "index.html"

GENERIC_DOCUMENTATION_PHRASES = (
    "Fournit la sémantique de",
    "Valeur utilisée par cette opération",
    "Résultat de type",
    "correspondant à cette valeur précise",
    "Définit [[",
)


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
        self.assertEqual(30, len(model.modules))
        self.assertEqual(936, len(model.symbols))
        self.assertTrue(all(module.owner for module in model.modules.values()))
        self.assertTrue(all(symbol.owner for symbol in model.symbols.values()))
        self.assertTrue(all(symbol.decision for symbol in model.symbols.values()))

    def test_public_documentation_is_semantic_not_tautological(self):
        occurrences = []
        for source in sorted(STDLIB_ROOT.rglob("*.janus")):
            text = source.read_text(encoding="utf-8")
            for phrase in GENERIC_DOCUMENTATION_PHRASES:
                if phrase in text:
                    occurrences.append(f"{source.relative_to(ROOT)}: {phrase}")
        self.assertEqual([], occurrences)

    def test_published_api_anchors_are_unique(self):
        api_index = json.loads(API_INDEX.read_text(encoding="utf-8"))
        anchors = [entry["anchor"] for entry in api_index["symbols"]]
        self.assertEqual(len(anchors), len(set(anchors)))

    def test_module_usage_examples_are_published_in_html_and_json(self):
        expected_modules = set()
        for source in sorted(STDLIB_ROOT.rglob("*.janus")):
            text = source.read_text(encoding="utf-8")
            module = re.search(r"^module\s+(\S+)", text, flags=re.MULTILINE)
            if module is not None and "@example" in text:
                expected_modules.add(module.group(1))

        api_index = json.loads(API_INDEX.read_text(encoding="utf-8"))
        indexed_modules = {
            entry["name"] for entry in api_index["modules"] if entry["examples"]
        }
        html = API_HTML.read_text(encoding="utf-8")

        self.assertEqual(expected_modules, indexed_modules)
        self.assertEqual(28, len(expected_modules))
        self.assertEqual(
            len(expected_modules),
            html.count('class="module-example doc-section"'),
        )

    def test_committed_report_is_deterministic_and_current(self):
        expected = self.audit.render_report(self.audit.collect_audit(ROOT))
        self.assertEqual(expected, REPORT.read_text(encoding="utf-8"))

    def test_stale_report_is_rejected(self):
        expected = self.audit.render_report(self.audit.collect_audit(ROOT))
        with tempfile.TemporaryDirectory() as directory:
            stale = Path(directory) / "stdlib-audit.md"
            stale.write_text(expected + "\n<!-- stale -->\n", encoding="utf-8")
            self.assertFalse(self.audit.report_is_current(stale, expected))

    def test_every_module_has_success_and_structured_error_doctests(self):
        text = STDLIB_REFERENCE.read_text(encoding="utf-8")
        sections = {}
        for match in re.finditer(
            r"^### `(?P<module>std(?:\.[a-z_]+)+)`\n"
            r"(?P<body>.*?)(?=^### `std|\Z)",
            text,
            flags=re.MULTILINE | re.DOTALL,
        ):
            sections[match.group("module")] = match.group("body")

        expected_modules = {
            entry["name"] for entry in self.inventory["stdlib_modules"]
        }
        self.assertEqual(expected_modules, set(sections))
        for module, body in sections.items():
            with self.subTest(module=module):
                self.assertIn("// doctest: doctest", body)

        structured_errors = {
            "std.option",
            "std.result",
            "std.system",
            "std.path",
            "std.fs",
            "std.io",
            "std.process",
            "std.text",
        }
        for module in structured_errors:
            with self.subTest(structured_error_module=module):
                self.assertIn("// doctest: compile_fail=", sections[module])


if __name__ == "__main__":
    unittest.main()
