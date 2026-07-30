from __future__ import annotations

import json
import re
import unittest
from pathlib import Path
from typing import cast
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[2]
DOC = ROOT / "docs" / "registry-protocol-v1.md"
SCHEMAS = ROOT / "docs" / "schemas" / "registry-v1"
FIXTURES = ROOT / "tests" / "fixtures" / "registry-v1"
SHA256 = re.compile(r"^[0-9a-f]{64}$")
PACKAGE = re.compile(r"^[a-z][a-z0-9_-]{0,63}/[a-z][a-z0-9_-]{0,63}$")
VERSION = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$")
SAFE_ARCHIVE_PATH = re.compile(r"^(?:src|tests|examples|docs)/[A-Za-z0-9._/-]+$|^(?:janus\.toml|README\.md|LICENSE|NOTICE)$")


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def assert_https_url(test: unittest.TestCase, value: str) -> None:
    parsed = urlparse(value)
    test.assertEqual("https", parsed.scheme)
    test.assertTrue(parsed.netloc)
    test.assertFalse(parsed.username)
    test.assertFalse(parsed.password)
    test.assertFalse(parsed.fragment)


def walk_schema(node: object):
    yield node
    if isinstance(node, dict):
        for value in node.values():
            yield from walk_schema(value)
    elif isinstance(node, list):
        for value in node:
            yield from walk_schema(value)


def validate_schema(
    test: unittest.TestCase, instance: object, schema: dict, root: dict | None = None
) -> None:
    root = schema if root is None else root
    if "$ref" in schema:
        target: object = root
        for component in schema["$ref"].removeprefix("#/").split("/"):
            target = target[component]  # type: ignore[index]
        validate_schema(test, instance, target, root)  # type: ignore[arg-type]
        return

    if "const" in schema:
        test.assertEqual(schema["const"], instance)
    if "enum" in schema:
        test.assertIn(instance, schema["enum"])

    expected_type = schema.get("type")
    if expected_type == "object":
        test.assertIsInstance(instance, dict)
        mapping = cast(dict, instance)
        properties = schema.get("properties", {})
        for required in schema.get("required", []):
            test.assertIn(required, mapping)
        if schema.get("additionalProperties") is False:
            test.assertLessEqual(set(mapping), set(properties))
        for key, value in mapping.items():
            if key in properties:
                validate_schema(test, value, properties[key], root)
    elif expected_type == "array":
        test.assertIsInstance(instance, list)
        values = cast(list, instance)
        test.assertGreaterEqual(len(values), schema.get("minItems", 0))
        if schema.get("uniqueItems"):
            canonical = [json.dumps(value, sort_keys=True) for value in values]
            test.assertEqual(len(canonical), len(set(canonical)))
        if "items" in schema:
            for value in values:
                validate_schema(test, value, schema["items"], root)
        if "contains" in schema:
            matches = 0
            for value in values:
                try:
                    validate_schema(test, value, schema["contains"], root)
                    matches += 1
                except AssertionError:
                    pass
            test.assertGreaterEqual(matches, 1)
    elif expected_type == "string":
        test.assertIsInstance(instance, str)
        text = cast(str, instance)
        test.assertGreaterEqual(len(text), schema.get("minLength", 0))
        if "maxLength" in schema:
            test.assertLessEqual(len(text), schema["maxLength"])
        if "pattern" in schema:
            test.assertIsNotNone(re.search(schema["pattern"], text))
        if schema.get("format") == "date-time":
            test.assertRegex(
                text, r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z$"
            )
    elif expected_type == "integer":
        test.assertIsInstance(instance, int)
        number = cast(int, instance)
        test.assertNotIsInstance(number, bool)
        test.assertGreaterEqual(number, schema.get("minimum", number))
    elif expected_type == "boolean":
        test.assertIsInstance(instance, bool)


def validate_metadata(test: unittest.TestCase, metadata: dict) -> None:
    test.assertEqual("1", metadata["protocolVersion"])
    test.assertRegex(metadata["package"], PACKAGE)
    test.assertRegex(metadata["version"], VERSION)
    archive = metadata["archive"]
    assert_https_url(test, archive["url"])
    test.assertRegex(archive["sha256"], SHA256)
    test.assertGreater(archive["size"], 0)
    for dependency in metadata["dependencies"]:
        test.assertRegex(dependency["package"], PACKAGE)
        assert_https_url(test, dependency["registry"])


def validate_archive_manifest(test: unittest.TestCase, manifest: dict) -> None:
    test.assertEqual("1", manifest["protocolVersion"])
    test.assertRegex(manifest["package"], PACKAGE)
    test.assertRegex(manifest["version"], VERSION)
    seen: set[str] = set()
    for entry in manifest["entries"]:
        path = entry["path"]
        test.assertRegex(path, SAFE_ARCHIVE_PATH)
        components = path.split("/")
        test.assertNotIn("", components)
        test.assertNotIn(".", components)
        test.assertNotIn("..", components)
        test.assertFalse(path.startswith(("/", "\\")))
        test.assertNotIn(path, seen)
        seen.add(path)
        test.assertRegex(entry["sha256"], SHA256)
        test.assertGreaterEqual(entry["size"], 0)


def validate_resolution(test: unittest.TestCase, resolution: dict) -> None:
    requested = resolution["requested"]
    selected = resolution["selected"]
    test.assertTrue(requested["requirement"])
    test.assertEqual(requested["package"], selected["package"])
    test.assertEqual(requested["registry"], selected["registry"])
    assert_https_url(test, selected["registry"])
    test.assertRegex(selected["metadataSha256"], SHA256)
    test.assertRegex(selected["archiveSha256"], SHA256)


class RegistryProtocolV1Tests(unittest.TestCase):
    def test_normative_document_covers_protocol_and_security_contract(self):
        text = DOC.read_text(encoding="utf-8")
        for anchor in (
            "## Version et négociation",
            "## Noms et espaces de noms",
            "## Ressources HTTP v1",
            "## Publication immuable et yanking",
            "## Résolution et lockfile",
            "## Authentification et autorisation",
            "## Modèle de menaces",
            "## Sauvegarde et récupération",
        ):
            with self.subTest(anchor=anchor):
                self.assertIn(anchor, text)
        self.assertIn('If-None-Match: "*"', text)
        self.assertIn("409 Conflict", text)
        self.assertIn("dependency confusion", text)
        self.assertIn("path traversal", text)

    def test_v1_json_schemas_are_strict_and_versioned(self):
        expected = {
            "discovery.schema.json",
            "index.schema.json",
            "metadata.schema.json",
            "archive-manifest.schema.json",
            "resolution.schema.json",
        }
        self.assertEqual(expected, {path.name for path in SCHEMAS.glob("*.schema.json")})
        for name in expected:
            with self.subTest(schema=name):
                schema = load_json(SCHEMAS / name)
                self.assertEqual("https://json-schema.org/draft/2020-12/schema", schema["$schema"])
                self.assertIn("/registry/v1/", schema["$id"])
                self.assertFalse(schema["additionalProperties"])
                self.assertEqual("1", schema["properties"]["protocolVersion"]["const"])
                for node in walk_schema(schema):
                    pattern = node.get("pattern") if isinstance(node, dict) else None
                    if pattern and pattern.startswith("^(0|[1-9]"):
                        self.assertIsNotNone(
                            re.fullmatch(pattern, "1.2.3-alpha.1+build.5")
                        )
                        for invalid_version in (
                            "1.2.3-01",
                            "1.2.3-..",
                            "1.2.3-alpha..1",
                            "1.2.3+..",
                        ):
                            self.assertIsNone(
                                re.fullmatch(pattern, invalid_version), invalid_version
                            )
                    if pattern and pattern.startswith("^https://"):
                        for unsafe_url in (
                            "https://token@registry.example/v1",
                            "https://REGISTRY.example/v1",
                            "https://registry.example:443/v1",
                            "https://registry.example:8443/v1",
                            "https://registry.example/a/../v1",
                            "https://registry.example/%2e%2e/v1",
                            "https://registry.example/v1/",
                        ):
                            self.assertIsNone(
                                re.fullmatch(pattern, unsafe_url), unsafe_url
                            )

    def test_valid_fixtures_pin_registry_metadata_and_archive(self):
        for stem in ("discovery", "index", "metadata", "archive-manifest", "resolution"):
            with self.subTest(fixture=stem):
                validate_schema(
                    self,
                    load_json(FIXTURES / "valid" / f"{stem}.json"),
                    load_json(SCHEMAS / f"{stem}.schema.json"),
                )
        validate_schema(
            self,
            load_json(FIXTURES / "valid" / "yanked-index.json"),
            load_json(SCHEMAS / "index.schema.json"),
        )
        discovery = load_json(FIXTURES / "valid" / "discovery.json")
        self.assertEqual("1", discovery["protocolVersion"])
        self.assertEqual("1", discovery["versions"][0]["protocolVersion"])
        assert_https_url(self, discovery["versions"][0]["apiBase"])
        validate_metadata(self, load_json(FIXTURES / "valid" / "metadata.json"))
        validate_archive_manifest(self, load_json(FIXTURES / "valid" / "archive-manifest.json"))
        validate_resolution(self, load_json(FIXTURES / "valid" / "resolution.json"))
        index = load_json(FIXTURES / "valid" / "index.json")
        self.assertEqual("1", index["protocolVersion"])
        self.assertRegex(index["package"], PACKAGE)
        self.assertTrue(index["releases"])
        yanked = load_json(FIXTURES / "valid" / "yanked-index.json")
        self.assertTrue(yanked["releases"][0]["yanked"])
        self.assertEqual(
            yanked["releases"][0]["version"],
            load_json(FIXTURES / "valid" / "resolution.json")["selected"]["version"],
        )

    def test_dependency_confusion_fixture_is_rejected(self):
        fixture = load_json(FIXTURES / "invalid" / "dependency-confusion.json")
        with self.assertRaises(AssertionError):
            validate_resolution(self, fixture)

    def test_path_traversal_fixtures_are_rejected(self):
        schema = load_json(SCHEMAS / "archive-manifest.schema.json")
        schema_path_pattern = re.compile(
            schema["properties"]["entries"]["items"]["properties"]["path"]["pattern"]
        )
        for path in sorted((FIXTURES / "invalid").glob("path-traversal-*.json")):
            fixture = load_json(path)
            with self.subTest(path=path.name), self.assertRaises(AssertionError):
                validate_archive_manifest(self, fixture)
            with self.assertRaises(AssertionError):
                validate_schema(self, fixture, schema)
            self.assertIsNone(schema_path_pattern.fullmatch(fixture["entries"][0]["path"]))
        self.assertGreaterEqual(
            len(list((FIXTURES / "invalid").glob("path-traversal-*.json"))), 3
        )


if __name__ == "__main__":
    unittest.main()
