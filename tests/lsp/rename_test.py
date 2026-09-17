#!/usr/bin/env python3
"""Compile and execute rename fixtures on both sides of the stdio boundary."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from urllib.parse import unquote, urlparse

LSP, COMPILER = map(lambda arg: str(Path(arg).resolve()), sys.argv[1:3])
del sys.argv[1:3]


def message(method, params, identifier=None):
    body = {"jsonrpc": "2.0", "method": method, "params": params}
    if identifier is not None:
        body["id"] = identifier
    data = json.dumps(body).encode()
    return b"Content-Length: " + str(len(data)).encode() + b"\r\n\r\n" + data


def replies(data):
    while data:
        header, data = data.split(b"\r\n\r\n", 1)
        size = int(header.split(b": ")[1])
        yield json.loads(data[:size])
        data = data[size:]


def position(source, offset):
    prefix = source[:offset]
    return {"line": prefix.count("\n"),
            "character": len(prefix.rsplit("\n", 1)[-1].encode("utf-16-le")) // 2}


def offset(source, position):
    lines = source.splitlines(keepends=True)
    prefix = lines[position["line"]].encode("utf-16-le")[:position["character"] * 2]
    return sum(map(len, lines[:position["line"]])) + len(prefix.decode("utf-16-le"))


class RenameTest(unittest.TestCase):
    def exercise(self, source, spelling, new_name, accepted=False, extra=None,
                 invalid_result=None, untouched=(), expected_edits=None):
        with tempfile.TemporaryDirectory(prefix="janus-rename-") as directory:
            root = Path(directory)
            main = root / "main.janus"
            sources = {main.as_uri(): source}
            for name, text in (extra or {}).items():
                sources[(root / name).as_uri()] = text
            for uri, text in sources.items():
                Path(unquote(urlparse(uri).path)).write_text(text)

            def command(verb):
                return subprocess.run([COMPILER, verb, str(main)], cwd=root,
                                      capture_output=True, timeout=30)

            before_check = command("check")
            self.assertEqual(before_check.returncode, 0, before_check.stderr.decode())
            before_run = command("run")
            data = message("initialize", {"rootUri": root.as_uri()}, 1)
            data += message("textDocument/didOpen", {"textDocument": {
                "uri": main.as_uri(), "version": 1, "languageId": "janus", "text": source}})
            params = {"textDocument": {"uri": main.as_uri()},
                      "position": position(source, source.index(spelling)), "newName": new_name}
            data += message("textDocument/rename", params, 2)
            # Repeat in the same server: speculative analysis must not change buffers/cache.
            data += message("textDocument/rename", params, 3)
            data += message("shutdown", {}, 4) + message("exit", {})
            process = subprocess.run([LSP], input=data, capture_output=True,
                                     cwd=root, timeout=30)
            self.assertEqual(process.returncode, 0, process.stderr.decode())
            answers = {reply["id"]: reply for reply in replies(process.stdout) if "id" in reply}
            self.assertEqual({k: v for k, v in answers[2].items() if k != "id"},
                             {k: v for k, v in answers[3].items() if k != "id"})
            for uri, text in sources.items():
                self.assertEqual(Path(unquote(urlparse(uri).path)).read_text(), text)
            answer = answers[2]
            if not accepted:
                self.assertIn("error", answer)
                self.assertNotIn("result", answer)
                self.assertNotIn("changes", answer)
                self.assertNotIn("documentChanges", answer)
                if invalid_result:
                    main.write_text(invalid_result)
                    self.assertNotEqual(command("check").returncode, 0)
                return
            self.assertNotIn("error", answer)
            result = answer["result"]
            changes = result.get("changes", {})
            for change in result.get("documentChanges", []):
                changes[change["textDocument"]["uri"]] = change["edits"]
            self.assertTrue(changes)
            for name in untouched:
                self.assertNotIn((root / name).as_uri(), changes)
            if expected_edits is not None:
                self.assertEqual(sum(map(len, changes.values())), expected_edits)
            for uri, edits in changes.items():
                original = sources[uri]
                updated = original
                for edit in sorted(edits, key=lambda e: offset(original, e["range"]["start"]),
                                   reverse=True):
                    start = offset(original, edit["range"]["start"])
                    end = offset(original, edit["range"]["end"])
                    updated = updated[:start] + edit["newText"] + updated[end:]
                Path(unquote(urlparse(uri).path)).write_text(updated)
            after_check = command("check")
            self.assertEqual(after_check.returncode, 0, after_check.stderr.decode())
            after_run = command("run")
            self.assertEqual((after_run.returncode, after_run.stdout, after_run.stderr),
                             (before_run.returncode, before_run.stdout, before_run.stderr))

    def test_local_collisions(self):
        cases = [
            ("val b = 7\n if true { val a = 1\n println(a) }", "a =", "b"),
            ("val b = 7\n if true { val a = 1\n println(a)\n println(b) }", "a =", "b"),
            ("val b = 7\n val a = 1\n println(a)\n println(b)", "a =", "b"),
            ("val a = 1\n println(a)\n val b = 7\n println(b)", "a =", "b"),
            ("val a = 1\n if true { val b = 7\n println(b) }\n println(a)", "a =", "b"),
            ("val b = 7\n println(b)\n val a = 1\n println(a)\n println(b)", "a =", "b"),
            ("val café = 7\n if true { val a = 1\n println(a)\n println(café) }", "a =", "cafe\u0301"),
        ]
        for body, spelling, new_name in cases:
            with self.subTest(body=body, new_name=new_name):
                source = "def main() : int {\n " + body + "\n return 0\n}\n"
                self.exercise(source, spelling, new_name,
                              invalid_result=source.replace("val a", "val " + new_name)
                              .replace("println(a)", "println(" + new_name + ")"))

    def test_parameters(self):
        self.exercise("def f(b : int) : int { val a = 1\n return a + b }\n"
                      "def main() : int { return f(7) }\n", "a =", "b")
        self.exercise("def f(a : int) : int { val b = 1\n return a + b }\n"
                      "def main() : int { return f(7) }\n", "a :", "b")

    def test_import_collisions(self):
        library = {"a.janus": "module a\ndef f() : int { return 1 }\n"
                   "def g() : int { return 2 }\n"}
        for imports, new_name in (("f as x, g as y", "y"),
                                  ("g as y, f as x", "y"),
                                  ("f as x, g", "g"),
                                  ("f as x, g as café", "cafe\u0301")):
            with self.subTest(imports=imports):
                other = "café" if "café" in imports else new_name
                source = ("import a.{" + imports + "}\ndef main() : int { return x() + "
                          + other + "() }\n")
                self.exercise(source, "x()", new_name, extra=library,
                              invalid_result=source.replace("as x", "as " + new_name)
                              .replace("x()", new_name + "()"))

    def test_capture_imported_reference(self):
        self.exercise("import a.{g}\ndef main() : int {\n println(g())\n"
                      "val x = () => 1\n println(x())\n println(g())\n return 0 }\n",
                      "x =", "g", extra={"a.janus": "module a\ndef g() : int { return 7 }\n"})

    def test_capture_shorthand_value(self):
        self.exercise("import a.{y}\nstruct Point(val y : int) {}\n"
                      "def main() : int { val x = 1\n println(x)\n"
                      "val p = new Point { y }\n return p.y }\n",
                      "x =", "y", extra={"a.janus": "module a\nconst y : int = 7\n"})

    def test_safe_renames(self):
        self.exercise("def a() : int { return 1 }\n"
                      "def other() : int { val b = 7\n return b }\n"
                      "def main() : int { return a() + other() }\n",
                      "a()", "b", accepted=True)
        self.exercise("def main() : int { val a = 1\n println(a)\n return 0 }\n",
                      "a =", "cafe\u0301", accepted=True)
        self.exercise("def main() : int {\n if true { val a = 1\n println(a) }\n"
                      "if true { val b = 7\n println(b) }\n return 0 }\n",
                      "a =", "b", accepted=True)
        self.exercise("import a.{f as x, g as y}\ndef main() : int { return x() + y() }\n",
                      "x()", "z", accepted=True,
                      extra={"a.janus": "module a\ndef f() : int { return 1 }\n"
                             "def g() : int { return 2 }\n"})
        self.exercise("import a.{f}\ndef main() : int { return f() }\n",
                      "f()", "renamed", accepted=True,
                      extra={"a.janus": "module a\ndef f() : int { return 3 }\n"})
        self.exercise("import a.{Point as P}\nimport b as b\n"
                      "def main() : int { val x = 3\n"
                      "val p = new P { x }\nval q = new b.Point { x: 7 }\n"
                      "return p.x + q.x }\n", "x }", "horizontal", accepted=True,
                      extra={"a.janus": "module a\nstruct Point(val x : int) {}\n",
                             "b.janus": "module b\nstruct Point(val x : int) {}\n"},
                      untouched=("b.janus",))
        self.exercise("def main() : int {\n if true { val a = 1\n println(a) }\n"
                      "val b = 7\n println(b)\n return 0 }\n",
                      "a =", "b", accepted=True)
        self.exercise("struct Point(val x : int) {}\ndef main() : int { val x = 3\n"
                      "val p = new Point { x }\n return p.x }\n",
                      "x }", "horizontal", accepted=True)
        self.exercise("def other() : int { val a = 7\n return a }\n"
                      "def main() : int { val a = 1\n return a + other() }\n",
                      "a = 1", "renamed", accepted=True, expected_edits=2)


if __name__ == "__main__":
    unittest.main()
