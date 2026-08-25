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
    "numeric-conversions.md",
    "text.md",
    "tooling.md",
    "testing.md",
    "registry-protocol-v1.md",
    "compiler-performance.md",
    "api-documentation.md",
    "doctests.md",
    "diagnostics.md",
    "stdlib-reference.md",
    "graphics.md",
    "stability-contract.md",
    "stability-inventory-0.8.md",
    "audit-0.17.md",
    "roadmap-1.0.md",
    "release-severity-policy.md",
    "development.md",
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
    "std.numeric",
    "std.option",
    "std.path",
    "std.process",
    "std.random",
    "std.range",
    "std.result",
    "std.slice",
    "std.system",
    "std.text",
    "std.testing",
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
    "search",
    "test",
}
BOOK = [f"{number:02d}-" for number in range(1, 16)]
EXPECTED_KEYWORDS = {
    "module",
    "import",
    "as",
    "extern",
    "tailrec",
    "def",
    "trait",
    "extends",
    "enum",
    "class",
    "struct",
    "derives",
    "new",
    "move",
    "borrow",
    "consume",
    "defer",
    "delete",
    "destructor",
    "private",
    "internal",
    "if",
    "else",
    "match",
    "for",
    "in",
    "while",
    "break",
    "continue",
    "return",
    "const",
    "val",
    "var",
    "true",
    "false",
}


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
            "docs/tutorials/derives-copy-debug.md",
            "docs/tutorials/propriete-move-consume.md",
            "docs/tutorials/atelier-graphisme-2d.md",
            "docs/tutorials/snake-graphique.md",
            "docs/reference/index.md",
            "docs/reference/generated/diagnostics.md",
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
        self.assertEqual(15, len(chapters))
        names = [chapter.name for chapter in chapters]
        for prefix in BOOK:
            self.assertTrue(any(name.startswith(prefix) for name in names), prefix)

    def test_public_content_states_version_and_experimental_status(self):
        docs = WEBSITE / "docs"
        home = (docs / "index.md").read_text(encoding="utf-8")
        self.assertIn("0.19.0", home)
        self.assertRegex(home.lower(), r"expérimental")
        self.assertNotIn("0.6.1", home)

        stale = [
            str(path.relative_to(docs))
            for path in sorted(docs.rglob("*.md"))
            if "0.14.0" in path.read_text(encoding="utf-8")
        ]
        self.assertEqual([], stale, "public current-version pages must not advertise 0.14.0")
        for relative in ("reference/index.md", "book/index.md", "tutorials/index.md"):
            text = (docs / relative).read_text(encoding="utf-8")
            self.assertIn("0.19.0", text, relative)

        metadata = (WEBSITE / "overrides" / "main.html").read_text(encoding="utf-8")
        self.assertIn("Janus 0.19.0", metadata)
        self.assertNotIn("Janus 0.14.0", metadata)

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

    def test_keyword_reference_covers_every_reserved_word(self):
        reference = (
            WEBSITE / "docs" / "book" / "14-reference-mots-cles.md"
        ).read_text(encoding="utf-8")
        documented = set(re.findall(r"\| `([a-z]+)` \|", reference))
        lexer = (REPOSITORY / "src" / "frontend" / "lexer.cpp").read_text(
            encoding="utf-8"
        )
        reserved = set(re.findall(r'lexeme == "([a-z]+)"', lexer))
        self.assertEqual(EXPECTED_KEYWORDS, reserved)
        self.assertEqual(reserved, documented)
        self.assertEqual(35, len(documented))
        self.assertIn("tailrec", documented)
        self.assertIn("`owned` est un qualificateur contextuel", reference)
        self.assertNotIn("owned", documented)

    def test_recent_language_surface_is_taught(self):
        book = WEBSITE / "docs" / "book"
        values = (book / "02-valeurs-types.md").read_text(encoding="utf-8")
        modeling = (book / "04-modeliser-donnees.md").read_text(encoding="utf-8")
        collections = (book / "06-collections-iterateurs.md").read_text(
            encoding="utf-8"
        )
        ownership = (book / "09-propriete-avancee.md").read_text(encoding="utf-8")
        ffi = (book / "10-modules-visibilite-ffi.md").read_text(encoding="utf-8")
        stdlib = (book / "11-bibliotheque-standard.md").read_text(encoding="utf-8")
        tooling = (book / "13-projets-tests-outils.md").read_text(encoding="utf-8")
        self.assertIn("numericCast", values)
        self.assertIn("`0b`/`0B`", values)
        self.assertIn("Motifs littéraux et gardes", modeling)
        self.assertIn("generateArray", collections)
        self.assertIn("sortWith", collections)
        self.assertIn("borrow val", ownership)
        self.assertIn(": owned Ptr[byte]", ffi)
        self.assertIn("hypot(3.0, 4.0)", stdlib)
        self.assertIn("removeDirectoryAll", stdlib)
        self.assertIn("JANA0022", tooling)
        self.assertIn("generated/diagnostics.md", tooling)
        self.assertIn("/// @test", tooling)
        self.assertIn("TestTemporaryDirectory.cleanup()", tooling)
        self.assertIn("janus run -- 10 -2 4 8", tooling)
        self.assertIn("janus --version --json", tooling)
        self.assertIn("LLVM prises en charge sont 18 à", tooling)

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
    def test_sync_does_not_rewrite_code_fences(self):
        module = load_sync_module()
        source = REPOSITORY / "docs" / "stdlib-reference.md"
        content = (
            "[guide sur deux\n lignes](../README.md)\n"
            "```janus\ncheckedCast[ubyte](255)\n```\n"
        )
        rewritten = module.rewrite_links(content, source, REPOSITORY)
        self.assertIn(
            "https://github.com/cyril103/janus/blob/v0.19.0/README.md",
            rewritten,
        )
        self.assertIn("checkedCast[ubyte](255)", rewritten)

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
                "github.com/cyril103/janus/blob/v0.19.0/docs/registry-protocol-v1.md",
                registry,
            )
            self.assertIn(
                "github.com/cyril103/janus/tree/v0.19.0/docs/schemas/registry-v1",
                registry,
            )

    def test_sync_rewrites_links_outside_published_reference(self):
        module = load_sync_module()
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary)
            module.sync(REPOSITORY, destination)
            language = (destination / "language-guide.md").read_text(encoding="utf-8")
            self.assertIn(
                "https://github.com/cyril103/janus/tree/v0.19.0/stdlib/std",
                language,
            )
            self.assertIn(
                "https://github.com/cyril103/janus/tree/v0.19.0/examples",
                language,
            )
            graphics = (destination / "graphics.md").read_text(encoding="utf-8")
            self.assertIn(
                "https://github.com/cyril103/janus/tree/v0.19.0/examples/snake",
                graphics,
            )
            local_links = re.findall(r"\[[^]]+\]\((?!https?://|#|mailto:)([^)]+)\)", language)
            for target in local_links:
                self.assertFalse(target.startswith("../"), target)


if __name__ == "__main__":
    unittest.main()
