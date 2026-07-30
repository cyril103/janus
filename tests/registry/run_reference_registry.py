#!/usr/bin/env python3

from __future__ import annotations

import argparse
import base64
import concurrent.futures
import hashlib
import hmac
import json
import os
import pathlib
import shutil
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request


TOKEN = "reference-registry-test-token-000000000000"
BAD_TOKEN = "reference-registry-invalid-token-000000000"
AUDIT_TOKEN = "reference-registry-audit-only-token-000000000"
SIGNING_KEY = b"reference-registry-signing-key-for-tests-000000000000"
MEDIA_TYPE = "application/vnd.janus.registry.v1+json"


def canonical(value: object) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, separators=(",", ":"), sort_keys=True
    ).encode()


def free_port() -> int:
    with socket.socket() as candidate:
        candidate.bind(("127.0.0.1", 0))
        return candidate.getsockname()[1]


def run(
    arguments: list[str],
    *,
    cwd: pathlib.Path,
    env: dict[str, str],
    success: bool = True,
    input_text: str | None = None,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        arguments,
        cwd=cwd,
        env=env,
        input=input_text,
        text=True,
        capture_output=True,
        timeout=60,
    )
    output = result.stdout + result.stderr
    if (result.returncode == 0) != success:
        raise AssertionError(
            f"{' '.join(arguments)} returned {result.returncode}\n{output}"
        )
    if (
        TOKEN in output
        or BAD_TOKEN in output
        or AUDIT_TOKEN in output
        or SIGNING_KEY.decode() in output
    ):
        raise AssertionError("registry secret leaked into process output")
    return result


def request(
    url: str,
    *,
    method: str = "GET",
    token: str | None = None,
    value: object | None = None,
    expected: int = 200,
) -> tuple[bytes, dict[str, str]]:
    body = canonical(value) if value is not None else None
    headers = {"Accept": MEDIA_TYPE}
    if body is not None:
        headers["Content-Type"] = MEDIA_TYPE
    if token is not None:
        headers["Authorization"] = f"Bearer {token}"
    operation = urllib.request.Request(
        url, data=body, headers=headers, method=method
    )
    try:
        with urllib.request.urlopen(operation, timeout=10) as response:
            if response.status != expected:
                raise AssertionError(f"{method} {url}: expected {expected}, got {response.status}")
            return response.read(), dict(response.headers)
    except urllib.error.HTTPError as error:
        contents = error.read()
        if error.code != expected:
            raise AssertionError(
                f"{method} {url}: expected {expected}, got {error.code}: {contents!r}"
            ) from error
        return contents, dict(error.headers)


def start_server(
    source_root: pathlib.Path,
    data: pathlib.Path,
    key_file: pathlib.Path,
    origin: str,
    port: int,
    env: dict[str, str],
) -> subprocess.Popen[str]:
    process = subprocess.Popen(
        [
            sys.executable,
            "-m",
            "reference_registry",
            "--data",
            str(data),
            "--origin",
            origin,
            "--listen",
            "127.0.0.1",
            "--port",
            str(port),
            "--signing-key-file",
            str(key_file),
            "--key-id",
            "test-key-v1",
            "--allow-http",
        ],
        cwd=source_root,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        if process.poll() is not None:
            stdout, stderr = process.communicate()
            raise AssertionError(f"registry exited early\n{stdout}\n{stderr}")
        try:
            request(origin + "/healthz")
            return process
        except (OSError, urllib.error.URLError):
            time.sleep(0.05)
    process.terminate()
    raise AssertionError("registry did not become ready")


def stop_server(process: subprocess.Popen[str]) -> None:
    process.terminate()
    try:
        stdout, stderr = process.communicate(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        stdout, stderr = process.communicate(timeout=5)
    if (
        TOKEN in stdout + stderr
        or AUDIT_TOKEN in stdout + stderr
        or SIGNING_KEY.decode() in stdout + stderr
    ):
        raise AssertionError("registry secret leaked into server output")


def write_package(root: pathlib.Path) -> None:
    (root / "src").mkdir(parents=True)
    (root / "janus.toml").write_text(
        "[package]\n"
        'name = "acme/reference"\n'
        'version = "1.2.3"\n'
        'entry = "src/library.janus"\n',
        encoding="utf-8",
    )
    (root / "src/library.janus").write_text(
        "module library\ndef registry_value() : int { return 86 }\n",
        encoding="utf-8",
    )
    (root / "README.md").write_text("reference registry fixture\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--janus", required=True, type=pathlib.Path)
    parser.add_argument("--source-root", required=True, type=pathlib.Path)
    parser.add_argument("--work-dir", required=True, type=pathlib.Path)
    args = parser.parse_args()
    shutil.rmtree(args.work_dir, ignore_errors=True)
    args.work_dir.mkdir(parents=True)
    data = args.work_dir / "data"
    restored = args.work_dir / "restored"
    key_file = args.work_dir / "signing.key"
    key_file.write_bytes(SIGNING_KEY)
    try:
        key_file.chmod(0o600)
    except OSError:
        pass

    python_path = str(args.source_root / "registry")
    env = os.environ.copy()
    env["PYTHONPATH"] = python_path + os.pathsep + env.get("PYTHONPATH", "")
    admin = [sys.executable, "-m", "reference_registry.admin"]
    provisioned = run(
        [
            *admin,
            "provision-token",
            "--data",
            str(data),
            "--signing-key-file",
            str(key_file),
            "--key-id",
            "test-key-v1",
            "--subject",
            "publisher-ci",
            "--scope",
            "publish:acme",
            "--scope",
            "yank:acme",
            "--scope",
            "audit",
        ],
        cwd=args.source_root,
        env=env,
        input_text=TOKEN + "\n",
    )
    assert len(provisioned.stdout.strip()) == 64
    run(
        [
            *admin,
            "provision-token",
            "--data",
            str(data),
            "--signing-key-file",
            str(key_file),
            "--key-id",
            "test-key-v1",
            "--subject",
            "auditor-ci",
            "--scope",
            "audit",
        ],
        cwd=args.source_root,
        env=env,
        input_text=AUDIT_TOKEN + "\n",
    )
    assert TOKEN.encode() not in (data / "registry.sqlite3").read_bytes()
    assert AUDIT_TOKEN.encode() not in (data / "registry.sqlite3").read_bytes()

    port = free_port()
    origin = f"http://127.0.0.1:{port}"
    server = start_server(args.source_root, data, key_file, origin, port, env)
    client_env = env.copy()
    client_env.update(
        {
            "JANUS_CACHE": str(args.work_dir / "cache"),
            "JANUS_REGISTRY": origin,
            "JANUS_REGISTRY_ALLOW_HTTP": "1",
            "JANUS_REGISTRY_TOKEN": TOKEN,
        }
    )
    package = args.work_dir / "package"
    write_package(package)
    published = run(
        [str(args.janus), "publish"], cwd=package, env=client_env
    )
    assert "acme/reference 1.2.3" in published.stdout

    duplicate = run(
        [str(args.janus), "publish"],
        cwd=package,
        env=client_env,
        success=False,
    )
    assert "409" in duplicate.stderr
    unauthorized_env = client_env.copy()
    unauthorized_env["JANUS_REGISTRY_TOKEN"] = BAD_TOKEN
    unauthorized = run(
        [str(args.janus), "publish"],
        cwd=package,
        env=unauthorized_env,
        success=False,
    )
    assert "401" in unauthorized.stderr
    forbidden_env = client_env.copy()
    forbidden_env["JANUS_REGISTRY_TOKEN"] = AUDIT_TOKEN
    forbidden = run(
        [str(args.janus), "publish"],
        cwd=package,
        env=forbidden_env,
        success=False,
    )
    assert "403" in forbidden.stderr

    searched = run(
        [str(args.janus), "search", "reference"],
        cwd=args.work_dir,
        env=client_env,
    )
    assert "acme/reference" in searched.stdout and "1.2.3" in searched.stdout
    consumer = args.work_dir / "consumer"
    run(
        [str(args.janus), "new", str(consumer)],
        cwd=args.work_dir,
        env=client_env,
    )
    run(
        [
            str(args.janus),
            "add",
            "acme/reference@^1.0.0",
            "--registry",
            origin,
        ],
        cwd=consumer,
        env=client_env,
    )
    (consumer / "src/main.janus").write_text(
        "import library\ndef main() : int { return registry_value() - 86 }\n",
        encoding="utf-8",
    )
    run([str(args.janus), "check"], cwd=consumer, env=client_env)
    lock = (consumer / "janus.lock").read_text(encoding="utf-8")
    assert TOKEN not in lock and BAD_TOKEN not in lock

    provenance_bytes, _ = request(
        origin + "/v1/packages/acme/reference/1.2.3/provenance"
    )
    provenance = json.loads(provenance_bytes)
    statement = provenance["statement"]
    expected_signature = base64.urlsafe_b64encode(
        hmac.new(SIGNING_KEY, canonical(statement), hashlib.sha256).digest()
    ).rstrip(b"=").decode()
    assert provenance["keyId"] == "test-key-v1"
    assert hmac.compare_digest(provenance["signature"], expected_signature)
    assert statement["publisher"] == "publisher-ci"
    assert statement["acceptedAt"].endswith("Z")

    request(
        origin + "/v1/packages/acme/reference/1.2.3/yank",
        method="POST",
        token=TOKEN,
        value={"reason": "compromised test release"},
    )
    index_bytes, _ = request(origin + "/v1/packages/acme/reference")
    index = json.loads(index_bytes)
    assert index["releases"][0]["yanked"] is True
    assert index["releases"][0]["yankReason"] == "compromised test release"
    assert json.loads(request(origin + "/v1/search?q=reference")[0])["packages"] == []
    request(
        origin + "/v1/packages/acme/reference/1.2.3/yank",
        method="DELETE",
        token=TOKEN,
    )

    request(origin + "/v1/audit", token=BAD_TOKEN, expected=401)
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        denied_reads = [
            executor.submit(
                request, origin + "/v1/audit", token=BAD_TOKEN, expected=401
            )
            for _ in range(16)
        ]
        for denied_read in denied_reads:
            denied_read.result()
    audit = json.loads(request(origin + "/v1/audit", token=TOKEN)[0])
    actions = [(event["action"], event["result"]) for event in audit["events"]]
    assert ("publish", "allowed") in actions
    assert ("publish", "conflict") in actions
    assert ("publish", "denied") in actions
    assert ("yank", "allowed") in actions
    assert ("unyank", "allowed") in actions
    assert TOKEN not in json.dumps(audit) and BAD_TOKEN not in json.dumps(audit)
    assert AUDIT_TOKEN not in json.dumps(audit)

    backup = args.work_dir / "registry-backup.tar.gz"
    run(
        [
            *admin,
            "backup",
            "--data",
            str(data),
            "--signing-key-file",
            str(key_file),
            "--key-id",
            "test-key-v1",
            "--output",
            str(backup),
        ],
        cwd=args.source_root,
        env=env,
    )
    assert backup.is_file()
    corrupt_backup = args.work_dir / "registry-backup-corrupt.tar.gz"
    corrupted = bytearray(backup.read_bytes())
    corrupted[len(corrupted) // 2] ^= 0x01
    corrupt_backup.write_bytes(corrupted)
    corrupt_restore = args.work_dir / "corrupt-restore"
    run(
        [
            *admin,
            "restore",
            "--archive",
            str(corrupt_backup),
            "--data",
            str(corrupt_restore),
        ],
        cwd=args.source_root,
        env=env,
        success=False,
    )
    assert not corrupt_restore.exists()
    run(
        [
            *admin,
            "verify-audit",
            "--data",
            str(data),
            "--signing-key-file",
            str(key_file),
            "--key-id",
            "test-key-v1",
        ],
        cwd=args.source_root,
        env=env,
    )

    stop_server(server)
    run(
        [
            *admin,
            "restore",
            "--archive",
            str(backup),
            "--data",
            str(restored),
        ],
        cwd=args.source_root,
        env=env,
    )
    run(
        [str(args.janus), "check", "--locked", "--offline"],
        cwd=consumer,
        env=client_env,
    )

    server = start_server(args.source_root, restored, key_file, origin, port, env)
    try:
        restored_search = run(
            [str(args.janus), "search", "reference"],
            cwd=args.work_dir,
            env=client_env,
        )
        assert "acme/reference" in restored_search.stdout
        run(
            [
                *admin,
                "verify-audit",
                "--data",
                str(restored),
                "--signing-key-file",
                str(key_file),
                "--key-id",
                "test-key-v1",
            ],
            cwd=args.source_root,
            env=env,
        )
    finally:
        stop_server(server)

    for path in (*data.rglob("*"), *restored.rglob("*")):
        if path.is_file():
            contents = path.read_bytes()
            assert TOKEN.encode() not in contents
            assert BAD_TOKEN.encode() not in contents
            assert AUDIT_TOKEN.encode() not in contents


if __name__ == "__main__":
    main()
