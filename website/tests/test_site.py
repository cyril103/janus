from __future__ import annotations

import importlib.util
import json
import re
import tempfile
import unittest
from pathlib import Path


WEBSITE = Path(__file__).resolve().parents[1]
REPOSITORY = WEBSITE.parent
EXPECTED_REFERENCE = {
    "getting-started.md",
    "language-guide.md",
    "text.md",
    "tooling.md",
    "registry-protocol-v1.md",
    "compiler-performance.md",
    "api-documentation.md",
    "doctests.md",
    "stdlib-reference.md",
    "graphics.md",
    "stability-contract.md",
    "development.md",
    "migration-0.5-to-0.6.md",
}
EXPECTED_MODULES = {
    "std.array",
    "std.array_builder",
    "std.builder",
    "std.c",
    "std.fs",
    "std.graphics",
    "std.graphics.audio",
    "std.graphics.drawing",
    "std.graphics.input",
    "std.graphics.resources",
    "std.graphics.types",
    "std.hash_probe",
    "std.hashing",
    "std.hashmap",
    "std.hashset",
    "std.io",
    "std.iterator",
    "std.math",
    "std.option",
    "std.path",
    "std.process",
    "std.random",
    "std.range",
    "std.result",
    "std.system",
    "std.text",
    "std.time",
    "std.wall_time",
}
EXPECTED_COMMANDS = {
    "--help",
    "--version",
    "add",
    "build",
    "check",
    "clean",
    "doc",
    "fmt",
    "init",
    "new",
    "publish",
    "remove",
    "run",
    "test",
}
BOOK = [f"{number:02d}-" for number in range(1, 9)]


def load_sync_module():
    path = WEBSITE / "scripts" / "sync_reference_docs.py"
    spec = importlib.util.spec_from_file_location("sync_reference_docs", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Impossible de charger {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class SiteStructureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        module = load_sync_module()
        module.sync(REPOSITORY, WEBSITE / "docs" / "reference" / "generated")

    def test_required_pages_and_assets_exist(self):
        required = [
            "mkdocs.yml",
            "requirements.txt",
            "Dockerfile",
            "docker-compose.yml",
            "nginx.conf",
            "README.md",
            "janus.toml",
            "janus.lock",
            "doctest-context.janus",
            "docs/index.md",
            "docs/book/index.md",
            "docs/tutorials/index.md",
            "docs/tutorials/cli-compteur.md",
            "docs/tutorials/collections.md",
            "docs/tutorials/gestion-erreurs.md",
            "docs/tutorials/snake-graphique.md",
            "docs/reference/index.md",
            "docs/reference/stdlib/api-index.json",
            "docs/reference/stdlib/index.html",
            "docs/assets/logo.svg",
            "docs/assets/favicon.svg",
            "docs/stylesheets/extra.css",
            "docs/javascripts/extra.js",
        ]
        missing = [path for path in required if not (WEBSITE / path).is_file()]
        self.assertEqual([], missing)

        chapters = list((WEBSITE / "docs" / "book").glob("[0-9][0-9]-*.md"))
        self.assertEqual(8, len(chapters))
        names = [chapter.name for chapter in chapters]
        for prefix in BOOK:
            self.assertTrue(any(name.startswith(prefix) for name in names), prefix)

    def test_public_content_states_version_and_experimental_status(self):
        home = (WEBSITE / "docs" / "index.md").read_text(encoding="utf-8")
        self.assertIn("0.7.6", home)
        self.assertRegex(home.lower(), r"expérimental")
        self.assertNotIn("0.6.1", home)

    def test_dark_palette_inherits_material_tokens(self):
        config = (WEBSITE / "mkdocs.yml").read_text(encoding="utf-8")
        css = (WEBSITE / "docs" / "stylesheets" / "extra.css").read_text(encoding="utf-8")
        self.assertIn("scheme: default", config)
        self.assertIn("scheme: slate", config)
        self.assertIn('[data-md-color-scheme="slate"]', css)
        self.assertIn("--md-typeset-color: #e8e5dc", css)
        self.assertIn("--md-code-bg-color: #182531", css)

    def test_every_lesson_has_learning_scaffolding(self):
        for chapter in sorted((WEBSITE / "docs" / "book").glob("[0-9][0-9]-*.md")):
            with self.subTest(chapter=chapter.name):
                text = chapter.read_text(encoding="utf-8")
                self.assertIn("## Objectifs", text)
                self.assertIn("## Exercice", text)
                self.assertIn("??? success \"Correction\"", text)
                self.assertIn("```janus", text)

    def test_lesson_navigation_uses_output_relative_urls(self):
        chapters = sorted((WEBSITE / "docs" / "book").glob("[0-9][0-9]-*.md"))
        for index, chapter in enumerate(chapters):
            text = chapter.read_text(encoding="utf-8")
            match = re.search(r'<div class="lesson-nav">(.*?)</div>', text, flags=re.DOTALL)
            if match is None:
                self.fail(f"Navigation absente dans {chapter.name}")
            links = re.findall(r'href="([^"]+)"', match.group(1))
            expected = []
            if index > 0:
                expected.append(f"../{chapters[index - 1].stem}/")
            if index + 1 < len(chapters):
                expected.append(f"../{chapters[index + 1].stem}/")
            else:
                expected.append("../../tutorials/")
            self.assertEqual(expected, links, chapter.name)

    def test_all_local_markdown_links_resolve(self):
        broken = []
        pattern = re.compile(r"(?<!!)\[[^]]+\]\(([^)]+)\)")
        for source in (WEBSITE / "docs").rglob("*.md"):
            text = source.read_text(encoding="utf-8")
            prose = re.sub(r"```.*?```", "", text, flags=re.DOTALL)
            prose = re.sub(r"`[^`]*`", "", prose)
            for raw_target in pattern.findall(prose):
                target = raw_target.split("#", 1)[0]
                if not target or target.startswith(("http://", "https://", "mailto:")):
                    continue
                candidate = (source.parent / target).resolve()
                alternatives = [candidate]
                if target.endswith("/"):
                    alternatives.extend([candidate.with_suffix(".md"), candidate / "index.md"])
                elif not candidate.suffix:
                    alternatives.extend([candidate.with_suffix(".md"), candidate / "index.md"])
                if not any(path.exists() for path in alternatives):
                    broken.append(f"{source.relative_to(WEBSITE)} -> {raw_target}")
        self.assertEqual([], broken)

    def test_public_surface_inventory_is_complete_and_traceable(self):
        inventory_path = REPOSITORY / "docs" / "public-surface-0.5.json"
        inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
        self.assertEqual(2, inventory["schema_version"])
        self.assertEqual("0.5.x", inventory["target"])

        modules = inventory["stdlib_modules"]
        self.assertEqual(EXPECTED_MODULES, {entry["name"] for entry in modules})
        for entry in modules:
            with self.subTest(module=entry["name"]):
                self.assertIn(
                    entry["status"],
                    {"stable-proposed", "experimental", "internal-detail"},
                )
                self.assertTrue(entry["documentation"])
                self.assertRegex(entry["signature_digest"], r"^[0-9a-f]{64}$")
                self.assertTrue(entry["symbols"] or entry.get("reexports"))
                self.assertTrue((REPOSITORY / entry["source"]).is_file())
                for document in entry["documentation"]:
                    self.assertTrue((REPOSITORY / document).is_file(), document)

    def test_generated_stdlib_reference_matches_the_public_inventory(self):
        inventory = json.loads(
            (REPOSITORY / "docs" / "public-surface-0.5.json").read_text(
                encoding="utf-8"
            )
        )
        reference = json.loads(
            (
                WEBSITE
                / "docs"
                / "reference"
                / "stdlib"
                / "api-index.json"
            ).read_text(encoding="utf-8")
        )
        expected_modules = {
            entry["name"] for entry in inventory["stdlib_modules"]
        }
        expected_symbols = {
            f"{entry['name']}.{symbol}"
            for entry in inventory["stdlib_modules"]
            for symbol in entry["symbols"]
        }
        self.assertEqual(
            expected_modules, {entry["name"] for entry in reference["modules"]}
        )
        self.assertEqual(
            expected_symbols, {entry["name"] for entry in reference["symbols"]}
        )
        self.assertTrue(
            all(entry["documentation"] for entry in reference["modules"])
        )
        self.assertTrue(
            all(entry["documentation"] for entry in reference["symbols"])
        )

        commands = inventory["cli"]["commands"]
        self.assertEqual(EXPECTED_COMMANDS, {entry["name"] for entry in commands})
        for entry in commands:
            with self.subTest(command=entry["name"]):
                self.assertIn(entry["status"], {"stable-proposed", "experimental"})
                self.assertTrue(entry["documentation"])
                self.assertTrue(entry["source"])
                self.assertTrue((REPOSITORY / entry["source"]).is_file())
                for document in entry["documentation"]:
                    self.assertTrue((REPOSITORY / document).is_file(), document)


class ReferenceSyncTests(unittest.TestCase):
    def test_sync_copies_exact_canonical_set_with_provenance(self):
        module = load_sync_module()
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary)
            module.sync(REPOSITORY, destination)
            generated = {path.name for path in destination.glob("*.md")}
            self.assertEqual(EXPECTED_REFERENCE, generated)
            self.assertEqual(
                (REPOSITORY / "docs" / "public-surface-0.5.json").read_bytes(),
                (destination / "public-surface-0.5.json").read_bytes(),
            )
            for path in destination.glob("*.md"):
                text = path.read_text(encoding="utf-8")
                self.assertIn("Documentation canonique", text)
                self.assertIn("Ne modifiez pas cette copie", text)
            registry = (destination / "registry-protocol-v1.md").read_text(
                encoding="utf-8"
            )
            self.assertIn(
                "github.com/cyril103/janus/blob/main/docs/registry-protocol-v1.md",
                registry,
            )
            self.assertIn(
                "github.com/cyril103/janus/tree/main/docs/schemas/registry-v1",
                registry,
            )

    def test_sync_rewrites_links_outside_published_reference(self):
        module = load_sync_module()
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary)
            module.sync(REPOSITORY, destination)
            language = (destination / "language-guide.md").read_text(encoding="utf-8")
            self.assertIn(
                "https://github.com/cyril103/janus/tree/v0.7.6/stdlib/std",
                language,
            )
            self.assertIn(
                "https://github.com/cyril103/janus/tree/v0.7.6/examples",
                language,
            )
            graphics = (destination / "graphics.md").read_text(encoding="utf-8")
            self.assertIn(
                "https://github.com/cyril103/janus/tree/v0.7.6/examples/snake",
                graphics,
            )
            local_links = re.findall(r"\[[^]]+\]\((?!https?://|#|mailto:)([^)]+)\)", language)
            for target in local_links:
                self.assertFalse(target.startswith("../"), target)


if __name__ == "__main__":
    unittest.main()
