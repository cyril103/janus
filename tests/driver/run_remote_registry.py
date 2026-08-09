#!/usr/bin/env python3

import argparse
import hashlib
import http.server
import json
import os
import pathlib
import shutil
import subprocess
import threading
import urllib.parse


MEDIA_TYPE = "application/vnd.janus.registry.v1+json"
SECRET = "registry-test-secret"


class Registry:
    releases: dict[tuple[str, str], dict[str, bytes]] = {}
    corrupt_archive = False
    interrupt_archive = False
    archive_gate: threading.Event | None = None
    archive_requests = 0
    archive_condition = threading.Condition()


def canonical_json(value: object) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, separators=(",", ":"), sort_keys=True
    ).encode()


class Handler(http.server.BaseHTTPRequestHandler):
    server_version = "JanusRegistryFixture/1"

    def log_message(self, *_args: object) -> None:
        pass

    @property
    def origin(self) -> str:
        return f"http://127.0.0.1:{self.server.server_port}"

    def json_response(self, value: object, status: int = 200) -> None:
        body = canonical_json(value)
        self.send_response(status)
        self.send_header("Content-Type", MEDIA_TYPE)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/.well-known/janus-registry":
            self.json_response(
                {
                    "protocolVersion": "1",
                    "versions": [
                        {"protocolVersion": "1", "apiBase": self.origin + "/v1"}
                    ],
                }
            )
            return
        if parsed.path == "/v1/search":
            query = urllib.parse.parse_qs(parsed.query).get("q", [""])[0]
            packages = sorted(
                {
                    package
                    for package, _version in Registry.releases
                    if query.lower() in package.lower()
                }
            )
            self.json_response(
                {
                    "protocolVersion": "1",
                    "packages": [
                        {
                            "package": package,
                            "latestVersion": max(
                                version
                                for candidate, version in Registry.releases
                                if candidate == package
                            ),
                            "description": "integration fixture",
                        }
                        for package in packages
                    ],
                }
            )
            return
        prefix = "/v1/packages/"
        if not parsed.path.startswith(prefix):
            self.send_error(404)
            return
        parts = parsed.path[len(prefix) :].split("/")
        if len(parts) == 2:
            package = "/".join(parts)
            releases = []
            for (candidate, version), stored in Registry.releases.items():
                if candidate != package:
                    continue
                releases.append(
                    {
                        "version": version,
                        "metadataUrl": (
                            f"{self.origin}/v1/packages/{package}/{version}/metadata"
                        ),
                        "metadataSha256": hashlib.sha256(
                            stored["metadata"]
                        ).hexdigest(),
                        "yanked": False,
                    }
                )
            self.json_response(
                {
                    "protocolVersion": "1",
                    "package": package,
                    "releases": releases,
                }
            )
            return
        if len(parts) != 4:
            self.send_error(404)
            return
        package = "/".join(parts[:2])
        version, resource = parts[2:]
        stored = Registry.releases.get((package, version))
        if stored is None:
            self.send_error(404)
            return
        names = {
            "metadata": "metadata",
            "archive-manifest": "manifest",
            "archive.tar.gz": "archive",
        }
        name = names.get(resource)
        if name is None:
            self.send_error(404)
            return
        body = stored[name]
        if name == "archive" and Registry.archive_gate is not None:
            with Registry.archive_condition:
                Registry.archive_requests += 1
                Registry.archive_condition.notify_all()
            if not Registry.archive_gate.wait(timeout=20):
                self.send_error(503)
                return
        if name == "archive" and Registry.interrupt_archive:
            self.send_response(200)
            self.send_header("Content-Length", str(len(body) + 100))
            self.end_headers()
            self.wfile.write(body[: max(1, len(body) // 2)])
            self.close_connection = True
            return
        if name == "archive" and Registry.corrupt_archive:
            body = bytes([body[0] ^ 0x01]) + body[1:]
        self.send_response(200)
        self.send_header(
            "Content-Type", "application/gzip" if name == "archive" else MEDIA_TYPE
        )
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_PUT(self) -> None:
        if self.headers.get("Authorization") != f"Bearer {SECRET}":
            self.send_error(401)
            return
        prefix = "/v1/packages/"
        if not self.path.startswith(prefix):
            self.send_error(404)
            return
        parts = self.path[len(prefix) :].split("/")
        if len(parts) != 3:
            self.send_error(404)
            return
        package = "/".join(parts[:2])
        version = parts[2]
        if (package, version) in Registry.releases:
            self.send_error(409)
            return
        content_type = self.headers.get("Content-Type", "")
        parameters = {}
        for component in content_type.split(";")[1:]:
            if "=" in component:
                key, value = component.split("=", 1)
                parameters[key.strip().lower()] = value.strip().strip('"')
        boundary = parameters.get("boundary", "").encode()
        body = self.rfile.read(int(self.headers["Content-Length"]))
        stored: dict[str, bytes] = {}
        for part in body.split(b"--" + boundary):
            if b"\r\n\r\n" not in part:
                continue
            headers, payload = part.split(b"\r\n\r\n", 1)
            payload = payload.removesuffix(b"\r\n")
            header_text = headers.decode(errors="replace").lower()
            if "content-id: metadata" in header_text:
                stored["metadata"] = payload
            elif "content-id: archive-manifest" in header_text:
                stored["manifest"] = payload
            elif "content-id: archive" in header_text:
                stored["archive"] = payload
        if set(stored) != {"metadata", "manifest", "archive"}:
            self.send_error(400)
            return
        metadata = json.loads(stored["metadata"])
        manifest = json.loads(stored["manifest"])
        if metadata["package"] != package or metadata["version"] != version:
            self.send_error(400)
            return
        if manifest["package"] != package or manifest["version"] != version:
            self.send_error(400)
            return
        if hashlib.sha256(stored["archive"]).hexdigest() != metadata["archive"]["sha256"]:
            self.send_error(422)
            return
        if hashlib.sha256(stored["manifest"]).hexdigest() != metadata["archive"]["manifestSha256"]:
            self.send_error(422)
            return
        Registry.releases[(package, version)] = stored
        self.send_response(201)
        self.end_headers()


def run(
    janus: pathlib.Path,
    cwd: pathlib.Path,
    env: dict[str, str],
    *arguments: str,
    success: bool = True,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(janus), *arguments],
        cwd=cwd,
        env=env,
        text=True,
        capture_output=True,
        timeout=40,
    )
    output = result.stdout + result.stderr
    if (result.returncode == 0) != success:
        raise AssertionError(
            f"janus {' '.join(arguments)} returned {result.returncode}\n{output}"
        )
    if SECRET in output:
        raise AssertionError("registry token leaked into CLI output")
    return result


def start(
    janus: pathlib.Path,
    cwd: pathlib.Path,
    env: dict[str, str],
    *arguments: str,
) -> subprocess.Popen[str]:
    return subprocess.Popen(
        [str(janus), *arguments],
        cwd=cwd,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def finish(
    process: subprocess.Popen[str], description: str
) -> subprocess.CompletedProcess[str]:
    stdout, stderr = process.communicate(timeout=40)
    result = subprocess.CompletedProcess(
        process.args, process.returncode, stdout, stderr
    )
    if SECRET in stdout + stderr:
        raise AssertionError("registry token leaked into CLI output")
    if result.returncode != 0:
        raise AssertionError(
            f"{description} returned {result.returncode}\n{stdout}{stderr}"
        )
    return result


def write_package(root: pathlib.Path, name: str, version: str, value: int) -> None:
    (root / "src").mkdir(parents=True)
    (root / "janus.toml").write_text(
        "[package]\n"
        f'name = "{name}"\n'
        f'version = "{version}"\n'
        'entry = "src/library.janus"\n',
        encoding="utf-8",
    )
    (root / "src/library.janus").write_text(
        f"module library\ndef registry_value() : int {{ return {value} }}\n",
        encoding="utf-8",
    )
    (root / "README.md").write_text("registry fixture\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--janus", required=True, type=pathlib.Path)
    parser.add_argument("--work-dir", required=True, type=pathlib.Path)
    args = parser.parse_args()
    shutil.rmtree(args.work_dir, ignore_errors=True)
    args.work_dir.mkdir(parents=True)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    registry = f"http://127.0.0.1:{server.server_port}"
    env = os.environ.copy()
    env.update(
        {
            "JANUS_CACHE": str(args.work_dir / "cache"),
            "JANUS_REGISTRY": registry,
            "JANUS_REGISTRY_ALLOW_HTTP": "1",
            "JANUS_REGISTRY_TOKEN": SECRET,
        }
    )
    package = args.work_dir / "package"
    write_package(package, "acme/math", "1.2.3", 42)
    published = run(args.janus, package, env, "publish")
    assert "acme/math 1.2.3" in published.stdout
    duplicate = run(args.janus, package, env, "publish", success=False)
    assert "already" in duplicate.stderr.lower() or "409" in duplicate.stderr
    searched = run(args.janus, args.work_dir, env, "search", "math")
    assert "acme/math" in searched.stdout and "1.2.3" in searched.stdout

    consumer = args.work_dir / "consumer"
    run(args.janus, args.work_dir, env, "new", str(consumer))
    run(
        args.janus,
        consumer,
        env,
        "add",
        "acme/math@^1.0.0",
        "--registry",
        registry,
    )
    (consumer / "src/main.janus").write_text(
        "import library\ndef main() : int { return registry_value() - 42 }\n",
        encoding="utf-8",
    )
    run(args.janus, consumer, env, "check")
    lock = (consumer / "janus.lock").read_text(encoding="utf-8")
    for expected in (
        f'registry = "{registry}"',
        'metadata-sha256 = "',
        'archive-sha256 = "',
    ):
        assert expected in lock
    assert SECRET not in lock
    run(args.janus, consumer, env, "check", "--locked", "--offline")

    (consumer / "janus.lock").write_text(
        "\n".join(
            line
            for line in lock.splitlines()
            if not line.startswith("metadata-sha256 = ")
        )
        + "\n",
        encoding="utf-8",
    )
    incomplete_lock = run(
        args.janus, consumer, env, "check", "--locked", success=False
    )
    assert "missing the verified registry record" in incomplete_lock.stderr
    (consumer / "janus.lock").write_text(lock, encoding="utf-8")

    shutil.rmtree(args.work_dir / "cache")
    stored = Registry.releases[("acme/math", "1.2.3")]
    original_metadata = stored["metadata"]
    original_manifest = stored["manifest"]
    hostile_manifest = json.loads(original_manifest)
    hostile_manifest["entries"][0]["path"] = "../escape"
    stored["manifest"] = canonical_json(hostile_manifest)
    hostile_metadata = json.loads(original_metadata)
    hostile_metadata["archive"]["manifestSha256"] = hashlib.sha256(
        stored["manifest"]
    ).hexdigest()
    stored["metadata"] = canonical_json(hostile_metadata)
    hostile = run(args.janus, consumer, env, "check", success=False)
    assert "unsafe archive manifest entry" in hostile.stderr, hostile.stderr
    assert not (args.work_dir / "escape").exists()
    assert not any((args.work_dir / "cache").rglob("package"))
    stored["metadata"] = original_metadata
    stored["manifest"] = original_manifest

    Registry.corrupt_archive = True
    bad = run(args.janus, consumer, env, "check", success=False)
    assert "checksum" in bad.stderr.lower(), bad.stderr
    Registry.corrupt_archive = False
    assert not any((args.work_dir / "cache").rglob("package"))

    Registry.interrupt_archive = True
    interrupted = run(args.janus, consumer, env, "check", success=False)
    assert "download" in interrupted.stderr.lower(), interrupted.stderr
    Registry.interrupt_archive = False
    assert not any((args.work_dir / "cache").rglob("package"))
    assert not any(".new-" in path.name for path in (args.work_dir / "cache").rglob("*"))

    race_package = args.work_dir / "race-package"
    write_package(race_package, "acme/race", "1.0.0", 7)
    run(args.janus, race_package, env, "publish")
    race_consumer = args.work_dir / "race-consumer"
    run(args.janus, args.work_dir, env, "new", str(race_consumer))
    run(
        args.janus,
        race_consumer,
        env,
        "add",
        "acme/race@^1.0.0",
        "--registry",
        registry,
    )
    run(args.janus, race_consumer, env, "check")
    old_race_lock = (race_consumer / "janus.lock").read_text(encoding="utf-8")

    del Registry.releases[("acme/race", "1.0.0")]
    (race_package / "src/library.janus").write_text(
        "module library\ndef registry_value() : int { return 8 }\n",
        encoding="utf-8",
    )
    run(args.janus, race_package, env, "publish")
    (race_consumer / "janus.lock").unlink()

    Registry.archive_requests = 0
    Registry.archive_gate = threading.Event()
    writer = start(args.janus, race_consumer, env, "check")
    with Registry.archive_condition:
        if not Registry.archive_condition.wait_for(
            lambda: Registry.archive_requests >= 1, timeout=20
        ):
            Registry.archive_gate.set()
            finish(writer, "registry cache writer")
            raise AssertionError("registry cache writer did not reach archive download")

    (race_consumer / "janus.lock").write_text(old_race_lock, encoding="utf-8")
    reader = subprocess.run(
        [str(args.janus), "check", "--locked", "--offline"],
        cwd=race_consumer,
        env=env,
        text=True,
        capture_output=True,
        timeout=40,
    )
    Registry.archive_gate.set()
    finish(writer, "registry cache writer")
    Registry.archive_gate = None
    if reader.returncode != 0:
        raise AssertionError(
            "a concurrent writer removed a cache generation already used by a reader\n"
            + reader.stdout
            + reader.stderr
        )

    del Registry.releases[("acme/race", "1.0.0")]
    (race_package / "src/library.janus").write_text(
        "module library\ndef registry_value() : int { return 9 }\n",
        encoding="utf-8",
    )
    run(args.janus, race_package, env, "publish")
    writer_roots = [args.work_dir / "writer-a", args.work_dir / "writer-b"]
    for writer_root in writer_roots:
        shutil.copytree(race_consumer, writer_root)
        (writer_root / "janus.lock").unlink(missing_ok=True)

    Registry.archive_requests = 0
    Registry.archive_gate = threading.Event()
    writers = [start(args.janus, root, env, "check") for root in writer_roots]
    with Registry.archive_condition:
        writers_ready = Registry.archive_condition.wait_for(
            lambda: Registry.archive_requests >= 2, timeout=20
        )
    Registry.archive_gate.set()
    if not writers_ready:
        for index, process in enumerate(writers):
            finish(process, f"concurrent registry cache writer {index + 1}")
        raise AssertionError("concurrent registry cache writers did not overlap")
    for index, process in enumerate(writers):
        finish(process, f"concurrent registry cache writer {index + 1}")
    Registry.archive_gate = None
    for writer_root in writer_roots:
        run(args.janus, writer_root, env, "check", "--locked", "--offline")
    (writer_roots[1] / "janus.lock").unlink()
    run(args.janus, writer_roots[1], env, "check", "--offline")
    race_markers = [
        marker
        for marker in (args.work_dir / "cache").rglob("verified.json")
        if json.loads(marker.read_text(encoding="utf-8"))["package"]
        == "acme/race"
    ]
    assert len(race_markers) == 3, "concurrent writers published duplicate generations"

    del Registry.releases[("acme/race", "1.0.0")]
    (race_package / "src/library.janus").write_text(
        "module library\ndef registry_value() : int { return 10 }\n",
        encoding="utf-8",
    )
    run(args.janus, race_package, env, "publish")
    interrupted_consumer = args.work_dir / "interrupted-consumer"
    shutil.copytree(race_consumer, interrupted_consumer)
    (interrupted_consumer / "janus.lock").unlink(missing_ok=True)
    Registry.interrupt_archive = True
    interrupted_writer = run(
        args.janus, interrupted_consumer, env, "check", success=False
    )
    Registry.interrupt_archive = False
    assert "download" in interrupted_writer.stderr.lower(), interrupted_writer.stderr
    run(args.janus, writer_roots[0], env, "check", "--locked", "--offline")
    assert len(
        [
            marker
            for marker in (args.work_dir / "cache").rglob("verified.json")
            if json.loads(marker.read_text(encoding="utf-8"))["package"]
            == "acme/race"
        ]
    ) == len(race_markers), "interrupted writer published a cache generation"
    assert not any(
        ".new-" in path.name
        for path in (args.work_dir / "cache").rglob("*")
    )

    server.shutdown()
    thread.join()


if __name__ == "__main__":
    main()
