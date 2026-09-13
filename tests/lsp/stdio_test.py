#!/usr/bin/env python3
"""Exercise the real stdio boundary, including EOF and queued traffic."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


BINARY = str(Path(sys.argv.pop(1)).resolve())
MAX_MESSAGE = 16 * 1024 * 1024
MAX_HEADERS = 8 * 1024


def request(identifier=1, method="shutdown"):
    return json.dumps(
        {"jsonrpc": "2.0", "id": identifier, "method": method, "params": {}},
        separators=(",", ":"),
    ).encode()


def frame(body, newline=b"\r\n"):
    return b"Content-Length: " + str(len(body)).encode() + newline * 2 + body


def responses(data):
    result = []
    while data:
        headers, data = data.split(b"\r\n\r\n", 1)
        name, length = headers.split(b": ")
        if name != b"Content-Length":
            raise AssertionError(f"Unexpected stdout header: {headers!r}")
        length = int(length)
        if len(data) < length:
            raise AssertionError("Truncated response")
        result.append(json.loads(data[:length]))
        data = data[length:]
    return result


class StdioTest(unittest.TestCase):
    def run_server(self, data, status=0):
        # No project discovery or workspace writes in the checkout.
        with tempfile.TemporaryDirectory(prefix="janus-lsp-stdio-") as directory:
            process = subprocess.run(
                [BINARY], input=data, capture_output=True, cwd=directory, timeout=15
            )
        self.assertEqual(process.returncode, status, process.stderr.decode())
        if status:
            self.assertIn(b"janus-lsp: invalid stdio frame:", process.stderr)
        else:
            self.assertEqual(process.stderr, b"")
        return responses(process.stdout)

    def test_syntax_depth_diagnostic_and_recovery(self):
        uri = "file:///tmp/janus-depth-regression.janus"

        def message(method, params, identifier=None):
            value = {"jsonrpc": "2.0", "method": method, "params": params}
            if identifier is not None:
                value["id"] = identifier
            return frame(json.dumps(value).encode())

        source = "def main() : int { return " + "(" * 5000 + "0" + ")" * 5000 + " }"
        data = frame(request(1, "initialize"))
        data += message("textDocument/didOpen", {"textDocument": {
            "uri": uri, "languageId": "janus", "version": 1, "text": source}})
        data += message("textDocument/documentSymbol", {"textDocument": {"uri": uri}}, 2)
        data += message("textDocument/didChange", {
            "textDocument": {"uri": uri, "version": 2},
            "contentChanges": [{"text": "def main() : int { return 0 }"}]})
        data += message("textDocument/documentSymbol", {"textDocument": {"uri": uri}}, 3)
        data += frame(request(4))
        data += message("exit", {})
        result = self.run_server(data)
        diagnostics = [r["params"]["diagnostics"] for r in result
                       if r.get("method") == "textDocument/publishDiagnostics"]
        self.assertGreaterEqual(len(diagnostics), 2)
        self.assertEqual(diagnostics[0][0]["code"], "JPAR0005")
        self.assertIn("maximum syntax depth exceeded", diagnostics[0][0]["message"])
        self.assertEqual(diagnostics[-1], [])
        replies = {r["id"]: r for r in result if "id" in r}
        self.assertIn("result", replies[2])
        self.assertEqual(replies[3]["result"][0]["name"], "main")
        self.assertIsNone(replies[4]["result"])

    def test_named_struct_aliases_and_homonyms(self):
        with tempfile.TemporaryDirectory(prefix="janus-named-lsp-") as directory:
            root = Path(directory).resolve()
            (root / "a.janus").write_text("module a\nstruct Point[T](val x : T) {}\n")
            (root / "b.janus").write_text("module b\nstruct Point(val x : bool) {}\n")
            source = ("import a.{Point as P}\nimport b as b\n"
                      "def main() : int {\n    val x = 1\n"
                      "    val first = new P[int] { x }\n"
                      "    val second = new b.Point { x: true }\n"
                      "    return first.x\n}\n")
            uri = (root / "main.janus").as_uri()

            def message(method, params, identifier=None):
                value = {"jsonrpc": "2.0", "method": method, "params": params}
                if identifier is not None:
                    value["id"] = identifier
                return frame(json.dumps(value).encode())

            def at(spelling):
                offset = source.index(spelling)
                return {"textDocument": {"uri": uri}, "position": {
                    "line": source[:offset].count("\n"),
                    "character": offset - source.rfind("\n", 0, offset) - 1}}

            rename = at("x }")
            rename["newName"] = "horizontal"
            data = message("initialize", {"rootUri": root.as_uri()}, 1)
            data += message("textDocument/didOpen", {"textDocument": {
                "uri": uri, "version": 1, "languageId": "janus", "text": source}})
            data += message("textDocument/definition", at("x }"), 2)
            data += message("textDocument/definition", at("x: true"), 3)
            data += message("textDocument/rename", rename, 4)
            data += frame(request(5))
            data += message("exit", {})
            process = subprocess.run([BINARY], input=data, capture_output=True,
                                     cwd=directory, timeout=30)
            self.assertEqual(process.returncode, 0, process.stderr.decode())
            replies = {r["id"]: r for r in responses(process.stdout) if "id" in r}
            self.assertIn((root / "a.janus").as_uri(), json.dumps(replies[2]))
            self.assertIn((root / "b.janus").as_uri(), json.dumps(replies[3]))
            self.assertNotIn("error", replies[4])
            changes = replies[4]["result"]["documentChanges"]
            self.assertEqual({change["textDocument"]["uri"] for change in changes},
                             {uri, (root / "a.janus").as_uri()})
            self.assertIn("horizontal: x", json.dumps(changes))

    def test_empty_stream(self):
        self.assertEqual(self.run_server(b""), [])

    def test_initialize_shutdown_crlf_and_lf(self):
        for newline in (b"\r\n", b"\n"):
            with self.subTest(newline=newline):
                result = self.run_server(
                    frame(request(1, "initialize"), newline)
                    + frame(request(2), newline)
                    + frame(b'{"jsonrpc":"2.0","method":"exit"}', newline)
                )
                self.assertEqual([reply["id"] for reply in result], [1, 2])
                self.assertIn("capabilities", result[0]["result"])
                self.assertIsNone(result[1]["result"])

    def test_invalid_lengths(self):
        for length in (
            b"", b"nope", b"-1", b"+1", b"1junk", b"1 2", b"1.5", b"0",
            b"999999999999999999999999999999999", str(MAX_MESSAGE + 1).encode(),
        ):
            with self.subTest(length=length):
                self.assertEqual(self.run_server(
                    b"Content-Length: " + length + b"\r\n\r\n", 1
                ), [])

    def test_missing_duplicate_and_malformed_headers(self):
        for headers in (
            b"\r\n", b"Content-Type: application/vscode-jsonrpc\r\n\r\n",
            b"Content-Length: 2\r\nContent-Length: 2\r\n\r\n",
            b"Content-Length: 2\r\ncontent-length: 3\r\n\r\n",
            b"Content-Length 2\r\n\r\n", b": 2\r\n\r\n",
        ):
            with self.subTest(headers=headers):
                self.assertEqual(self.run_server(headers + b"{}", 1), [])

    def test_truncated_headers(self):
        for headers in (b"C", b"Content-Length: 2", b"Content-Length: 2\r\n"):
            with self.subTest(headers=headers):
                self.assertEqual(self.run_server(headers, 1), [])

    def test_truncated_body_is_never_dispatched(self):
        body = request()
        # Even syntactically complete JSON is not a complete frame here.
        data = b"Content-Length: " + str(len(body) + 1).encode() + b"\r\n\r\n"
        self.assertEqual(self.run_server(data + body, 1), [])
        self.assertEqual(self.run_server(data, 1), [])

    def test_error_drains_only_preceding_complete_messages(self):
        result = self.run_server(
            frame(request(1)) + b"Content-Length: nope\r\n\r\n" + frame(request(2)), 1
        )
        self.assertEqual([reply["id"] for reply in result], [1])

    def test_case_insensitive_length_and_optional_headers(self):
        body = request()
        result = self.run_server(
            b"Content-Type: application/vscode-jsonrpc; charset=utf-8\r\n"
            b"cOnTeNt-LeNgTh:\t" + str(len(body)).encode() + b" \t\r\n\r\n" + body
        )
        self.assertEqual(result[0]["id"], 1)

    def test_header_limit(self):
        body = request()
        prefix = b"Content-Length: " + str(len(body)).encode() + b"\r\nX-Padding: "
        headers = prefix + b"a" * (MAX_HEADERS - len(prefix) - 4) + b"\r\n\r\n"
        self.assertEqual(self.run_server(headers + body)[0]["id"], 1)
        self.assertEqual(self.run_server(b"X" * (MAX_HEADERS + 1), 1), [])
        self.assertEqual(self.run_server(b"X: a\n" * (MAX_HEADERS // 5 + 1), 1), [])

    def test_maximum_body_and_byte_queue_traffic(self):
        # More than 32 MiB in one stream; exercise the byte capacity and drain.
        body = request()
        body += b" " * (MAX_MESSAGE - len(body))
        result = self.run_server(frame(body) * 3)
        self.assertEqual([reply["id"] for reply in result], [1, 1, 1])

    def test_message_queue_traffic_preserves_order(self):
        result = self.run_server(b"".join(frame(request(i)) for i in range(512)))
        self.assertEqual([reply["id"] for reply in result], list(range(512)))


if __name__ == "__main__":
    unittest.main()
