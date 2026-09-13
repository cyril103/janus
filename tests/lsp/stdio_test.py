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
