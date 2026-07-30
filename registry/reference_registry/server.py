from __future__ import annotations

import argparse
import email.parser
import email.policy
import http
import json
import os
import pathlib
import re
import signal
import sys
import urllib.parse
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from .core import (
    MAX_REQUEST,
    MEDIA_TYPE,
    PACKAGE_RE,
    VERSION_RE,
    RegistryStore,
    UnsafeArchiveError,
    canonical_json,
)


class RegistryHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(
        self,
        address: tuple[str, int],
        store: RegistryStore,
        origin: str,
    ):
        super().__init__(address, RegistryHandler)
        self.store = store
        self.origin = origin
        self.api_base = origin + "/v1"


class RegistryHandler(BaseHTTPRequestHandler):
    server_version = "JanusReferenceRegistry/1"
    protocol_version = "HTTP/1.1"

    @property
    def registry(self) -> RegistryHTTPServer:
        return self.server  # type: ignore[return-value]

    def log_message(self, format: str, *args: object) -> None:
        # Les requêtes sont tracées dans le journal signé sans en-têtes sensibles.
        pass

    def _request_id(self) -> str:
        supplied = self.headers.get("X-Request-ID", "")
        if re.fullmatch(r"[A-Za-z0-9._-]{8,128}", supplied):
            return supplied
        return str(uuid.uuid4())

    def _send_bytes(
        self,
        status: int,
        contents: bytes,
        content_type: str,
        etag: str | None = None,
        request_id: str | None = None,
    ) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(contents)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        if etag:
            self.send_header("ETag", f'"{etag}"')
        if request_id:
            self.send_header("X-Request-ID", request_id)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(contents)

    def _send_json(
        self, status: int, value: object, request_id: str | None = None
    ) -> None:
        self._send_bytes(status, canonical_json(value), MEDIA_TYPE, request_id=request_id)

    def _error(self, status: int, message: str, request_id: str) -> None:
        self._send_json(
            status,
            {"error": message, "protocolVersion": "1", "requestId": request_id},
            request_id,
        )

    def _segments(self) -> tuple[list[str], urllib.parse.SplitResult]:
        parsed = urllib.parse.urlsplit(self.path)
        raw_segments = parsed.path.split("/")
        segments: list[str] = []
        for raw in raw_segments:
            if not raw:
                continue
            segment = urllib.parse.unquote(raw, errors="strict")
            if (
                segment in {".", ".."}
                or "/" in segment
                or "\\" in segment
                or "\0" in segment
            ):
                raise ValueError("unsafe URL path")
            segments.append(segment)
        return segments, parsed

    def _require_accept(self, discovery: bool = False) -> bool:
        accepted = self.headers.get("Accept", "*/*")
        allowed = (
            ("application/json", MEDIA_TYPE, "*/*")
            if discovery
            else (MEDIA_TYPE, "*/*")
        )
        return any(item in accepted for item in allowed)

    def _bearer(self) -> str | None:
        authorization = self.headers.get("Authorization", "")
        prefix = "Bearer "
        if not authorization.startswith(prefix):
            return None
        token = authorization[len(prefix) :]
        if not token or any(character.isspace() for character in token):
            return None
        return token

    def _authorize(
        self,
        scope: str,
        action: str,
        request_id: str,
        package: str | None = None,
        version: str | None = None,
    ) -> str | None:
        token = self._bearer()
        identity = self.registry.store.identity(token) if token is not None else None
        if identity is None:
            self.registry.store.audit(
                "anonymous",
                action,
                "denied",
                request_id,
                package,
                version,
                "authentication or authorization failed",
            )
            self._error(http.HTTPStatus.UNAUTHORIZED, "unauthorized", request_id)
            return None
        subject, scopes = identity
        if scope not in scopes:
            self.registry.store.audit(
                subject,
                action,
                "denied",
                request_id,
                package,
                version,
                "scope is not authorized",
            )
            self._error(http.HTTPStatus.FORBIDDEN, "forbidden", request_id)
            return None
        return subject

    def _body(self) -> bytes:
        raw_length = self.headers.get("Content-Length")
        if raw_length is None:
            raise ValueError("Content-Length is required")
        try:
            length = int(raw_length)
        except ValueError as error:
            raise ValueError("Content-Length is invalid") from error
        if length < 0 or length > MAX_REQUEST:
            raise OverflowError("request body is too large")
        body = self.rfile.read(length)
        if len(body) != length:
            raise ValueError("request body is incomplete")
        return body

    def _multipart(self) -> dict[str, bytes]:
        content_type = self.headers.get("Content-Type", "")
        if not content_type.lower().startswith("multipart/related;"):
            raise TypeError("unsupported publication media type")
        body = self._body()
        envelope = (
            f"Content-Type: {content_type}\r\nMIME-Version: 1.0\r\n\r\n".encode()
            + body
        )
        message = email.parser.BytesParser(
            policy=email.policy.default
        ).parsebytes(envelope)
        if not message.is_multipart():
            raise ValueError("publication is not multipart")
        parts: dict[str, bytes] = {}
        allowed_types = {
            "metadata": MEDIA_TYPE,
            "archive-manifest": MEDIA_TYPE,
            "archive": "application/gzip",
        }
        for part in message.iter_parts():
            content_id = part.get("Content-ID", "").strip().strip("<>")
            if (
                content_id not in allowed_types
                or content_id in parts
                or part.get_content_type() != allowed_types[content_id]
                or part.is_multipart()
            ):
                raise ValueError("publication contains an invalid part")
            payload = part.get_payload(decode=True)
            if payload is None:
                raise ValueError("publication part cannot be decoded")
            parts[content_id] = payload
        if parts.keys() != allowed_types.keys():
            raise ValueError("publication must contain exactly three parts")
        return parts

    def do_GET(self) -> None:
        request_id = self._request_id()
        try:
            segments, parsed = self._segments()
            if segments == [".well-known", "janus-registry"]:
                if not self._require_accept(discovery=True):
                    self._error(406, "unsupported response media type", request_id)
                    return
                self._send_bytes(
                    200,
                    canonical_json(
                        {
                            "protocolVersion": "1",
                            "versions": [
                                {
                                    "apiBase": self.registry.api_base,
                                    "protocolVersion": "1",
                                }
                            ],
                        }
                    ),
                    "application/json",
                    request_id=request_id,
                )
                return
            if segments == ["healthz"]:
                self._send_bytes(200, b"ok\n", "text/plain; charset=utf-8", request_id=request_id)
                return
            if not self._require_accept():
                self._error(406, "unsupported response media type", request_id)
                return
            if segments == ["v1", "search"]:
                query = urllib.parse.parse_qs(
                    parsed.query, keep_blank_values=True, strict_parsing=True
                ).get("q", [])
                if len(query) != 1 or not query[0] or len(query[0]) > 256:
                    self._error(400, "search query is invalid", request_id)
                    return
                self._send_json(200, self.registry.store.search(query[0]), request_id)
                return
            if segments == ["v1", "audit"]:
                subject = self._authorize("audit", "read-audit", request_id)
                if subject is None:
                    return
                query = urllib.parse.parse_qs(parsed.query, keep_blank_values=True)
                package = query.get("package", [None])[0]
                if package is not None and PACKAGE_RE.fullmatch(package) is None:
                    self._error(400, "package is invalid", request_id)
                    return
                self._send_json(
                    200,
                    {
                        "events": self.registry.store.audit_records(package),
                        "protocolVersion": "1",
                    },
                    request_id,
                )
                return
            if len(segments) == 4 and segments[:2] == ["v1", "packages"]:
                package = "/".join(segments[2:4])
                if PACKAGE_RE.fullmatch(package) is None:
                    raise ValueError("package is invalid")
                index = self.registry.store.package_index(
                    package, self.registry.api_base
                )
                if index is None:
                    self._error(404, "package not found", request_id)
                    return
                self._send_json(200, index, request_id)
                return
            if len(segments) == 6 and segments[:2] == ["v1", "packages"]:
                package = "/".join(segments[2:4])
                version, resource = segments[4:]
                if (
                    PACKAGE_RE.fullmatch(package) is None
                    or VERSION_RE.fullmatch(version) is None
                ):
                    raise ValueError("package or version is invalid")
                release = self.registry.store.release(package, version)
                if release is None:
                    self._error(404, "release not found", request_id)
                    return
                resources = {
                    "metadata": (release["metadata_sha256"], MEDIA_TYPE),
                    "archive-manifest": (
                        release["manifest_sha256"],
                        MEDIA_TYPE,
                    ),
                    "archive.tar.gz": (
                        release["archive_sha256"],
                        "application/gzip",
                    ),
                }
                if resource == "provenance":
                    contents = bytes(release["provenance"])
                    self._send_bytes(
                        200,
                        contents,
                        MEDIA_TYPE,
                        request_id=request_id,
                    )
                    return
                selected = resources.get(resource)
                if selected is None:
                    self._error(404, "resource not found", request_id)
                    return
                digest, content_type = selected
                self._send_bytes(
                    200,
                    self.registry.store.read_blob(digest),
                    content_type,
                    digest,
                    request_id,
                )
                return
            self._error(404, "resource not found", request_id)
        except (UnicodeError, ValueError) as error:
            self._error(400, str(error), request_id)
        except Exception:
            self._error(500, "internal registry error", request_id)

    def do_PUT(self) -> None:
        request_id = self._request_id()
        package = version = None
        subject = None
        try:
            segments, _ = self._segments()
            if len(segments) != 5 or segments[:2] != ["v1", "packages"]:
                self._error(404, "resource not found", request_id)
                return
            package = "/".join(segments[2:4])
            version = segments[4]
            if (
                PACKAGE_RE.fullmatch(package) is None
                or VERSION_RE.fullmatch(version) is None
            ):
                raise ValueError("package or version is invalid")
            namespace = package.split("/", 1)[0]
            subject = self._authorize(
                f"publish:{namespace}", "publish", request_id, package, version
            )
            if subject is None:
                return
            if self.headers.get("If-None-Match") != "*":
                self._error(428, "If-None-Match: * is required", request_id)
                return
            parts = self._multipart()
            self.registry.store.publish(
                package,
                version,
                parts["metadata"],
                parts["archive-manifest"],
                parts["archive"],
                subject,
                request_id,
                self.registry.api_base,
            )
            self._send_json(
                201,
                {
                    "package": package,
                    "protocolVersion": "1",
                    "requestId": request_id,
                    "version": version,
                },
                request_id,
            )
        except TypeError as error:
            self._error(415, str(error), request_id)
        except OverflowError as error:
            self._error(413, str(error), request_id)
        except FileExistsError:
            self.registry.store.audit(
                subject or "authenticated",
                "publish",
                "conflict",
                request_id,
                package,
                version,
                "immutable release already exists",
            )
            self._error(409, "package version already exists", request_id)
        except UnsafeArchiveError as error:
            self.registry.store.audit(
                subject or "authenticated",
                "publish",
                "rejected",
                request_id,
                package,
                version,
                "invalid publication",
            )
            self._error(422, str(error), request_id)
        except (UnicodeError, ValueError) as error:
            self.registry.store.audit(
                subject or "authenticated",
                "publish",
                "rejected",
                request_id,
                package,
                version,
                "incoherent publication",
            )
            self._error(400, str(error), request_id)
        except Exception:
            self._error(500, "internal registry error", request_id)

    def _set_yank(self, yanked: bool) -> None:
        request_id = self._request_id()
        try:
            segments, _ = self._segments()
            if (
                len(segments) != 6
                or segments[:2] != ["v1", "packages"]
                or segments[5] != "yank"
            ):
                self._error(404, "resource not found", request_id)
                return
            package = "/".join(segments[2:4])
            version = segments[4]
            if (
                PACKAGE_RE.fullmatch(package) is None
                or VERSION_RE.fullmatch(version) is None
            ):
                raise ValueError("package or version is invalid")
            namespace = package.split("/", 1)[0]
            subject = self._authorize(
                f"yank:{namespace}",
                "yank" if yanked else "unyank",
                request_id,
                package,
                version,
            )
            if subject is None:
                return
            reason = ""
            if yanked:
                body = self._body()
                try:
                    value = json.loads(body)
                except (UnicodeDecodeError, json.JSONDecodeError) as error:
                    raise ValueError("yank body is invalid JSON") from error
                if (
                    canonical_json(value) != body
                    or not isinstance(value, dict)
                    or value.keys() != {"reason"}
                    or not isinstance(value["reason"], str)
                    or not value["reason"]
                    or len(value["reason"]) > 512
                ):
                    raise ValueError("yank reason is invalid")
                reason = value["reason"]
            if not self.registry.store.set_yanked(
                package, version, yanked, reason, subject, request_id
            ):
                self._error(404, "release not found", request_id)
                return
            self._send_json(
                200,
                {
                    "package": package,
                    "protocolVersion": "1",
                    "version": version,
                    "yanked": yanked,
                },
                request_id,
            )
        except (UnicodeError, ValueError) as error:
            self._error(400, str(error), request_id)
        except OverflowError as error:
            self._error(413, str(error), request_id)
        except Exception:
            self._error(500, "internal registry error", request_id)

    def do_POST(self) -> None:
        self._set_yank(True)

    def do_DELETE(self) -> None:
        self._set_yank(False)


def _origin(value: str, allow_http: bool) -> str:
    pattern = (
        r"https://[a-z0-9](?:[a-z0-9.-]*[a-z0-9])?"
        r"(?:/[A-Za-z0-9._~+-]+)*"
    )
    if allow_http:
        pattern = (
            r"(?:https://[a-z0-9](?:[a-z0-9.-]*[a-z0-9])?"
            r"(?::[1-9][0-9]*)?|http://(?:127\.0\.0\.1|localhost):[1-9][0-9]*)"
            r"(?:/[A-Za-z0-9._~+-]+)*"
        )
    if re.fullmatch(pattern, value) is None or value.endswith("/"):
        raise ValueError("registry origin is not canonical")
    return value


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Janus reference registry")
    parser.add_argument(
        "--data",
        type=pathlib.Path,
        default=os.environ.get("JANUS_REGISTRY_DATA"),
    )
    parser.add_argument("--origin", default=os.environ.get("JANUS_REGISTRY_ORIGIN"))
    parser.add_argument(
        "--listen", default=os.environ.get("JANUS_REGISTRY_LISTEN", "127.0.0.1")
    )
    parser.add_argument(
        "--port", type=int, default=int(os.environ.get("JANUS_REGISTRY_PORT", "8080"))
    )
    parser.add_argument(
        "--signing-key-file",
        type=pathlib.Path,
        default=os.environ.get("JANUS_REGISTRY_SIGNING_KEY_FILE"),
    )
    parser.add_argument("--key-id", default=os.environ.get("JANUS_REGISTRY_KEY_ID"))
    parser.add_argument(
        "--allow-http",
        action="store_true",
        default=os.environ.get("JANUS_REGISTRY_ALLOW_HTTP") == "1",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.data is None or args.origin is None or args.signing_key_file is None:
        raise SystemExit(
            "--data, --origin and --signing-key-file (or their environment "
            "equivalents) are required"
        )
    if args.key_id is None:
        raise SystemExit("--key-id or JANUS_REGISTRY_KEY_ID is required")
    origin = _origin(args.origin, args.allow_http)
    signing_key = args.signing_key_file.read_bytes()
    store = RegistryStore(args.data, signing_key, args.key_id)
    server = RegistryHTTPServer((args.listen, args.port), store, origin)

    def stop(_signal: int, _frame: object) -> None:
        # shutdown() doit être appelé depuis un autre thread ; SIGTERM ferme
        # donc le socket, puis la boucle sort naturellement.
        server.server_close()
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, stop)
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
