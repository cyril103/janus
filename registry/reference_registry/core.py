from __future__ import annotations

import base64
import contextlib
import datetime as dt
import gzip
import hashlib
import hmac
import io
import json
import os
import pathlib
import re
import shutil
import sqlite3
import tarfile
import tempfile
from collections.abc import Iterable
from typing import Any


MEDIA_TYPE = "application/vnd.janus.registry.v1+json"
PACKAGE_RE = re.compile(r"^[a-z][a-z0-9_-]{0,63}/[a-z][a-z0-9_-]{0,63}$")
VERSION_RE = re.compile(
    r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
    r"(?:-(?:0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*)"
    r"(?:\.(?:0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*))*)?"
    r"(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$"
)
DATETIME_RE = re.compile(
    r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z$"
)
REGISTRY_RE = re.compile(
    r"^https://(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)*"
    r"[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?"
    r"(?:/(?!\.{1,2}(?:/|$))[A-Za-z0-9._~+-]+)*$"
)
SHA_RE = re.compile(r"^[0-9a-f]{64}$")
PATH_RE = re.compile(
    r"^(?:(?:src|tests|examples|docs)/(?!\.{1,2}(?:/|$))"
    r"[A-Za-z0-9._-]+(?:/(?!\.{1,2}(?:/|$))[A-Za-z0-9._-]+)*"
    r"|janus\.toml|README\.md|LICENSE|NOTICE)$"
)
MAX_REQUEST = 130 * 1024 * 1024
MAX_ARCHIVE = 128 * 1024 * 1024
MAX_FILE = 32 * 1024 * 1024
MAX_EXTRACTED = 256 * 1024 * 1024
MAX_ENTRIES = 10_000
SCHEMA_VERSION = 1


class UnsafeArchiveError(ValueError):
    pass


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace(
        "+00:00", "Z"
    )


def canonical_json(value: Any) -> bytes:
    return json.dumps(
        value, ensure_ascii=False, separators=(",", ":"), sort_keys=True
    ).encode("utf-8")


def sha256(contents: bytes) -> str:
    return hashlib.sha256(contents).hexdigest()


def token_digest(token: str) -> str:
    return hashlib.sha256(("janus-registry-token-v1\0" + token).encode()).hexdigest()


def semver_key(version: str) -> tuple[int, int, int, int, tuple[tuple[int, Any], ...]]:
    match = VERSION_RE.fullmatch(version)
    if match is None:
        raise ValueError("invalid semantic version")
    core_and_pre = version.split("+", 1)[0]
    core, separator, prerelease = core_and_pre.partition("-")
    major, minor, patch = (int(part) for part in core.split("."))
    identifiers: list[tuple[int, Any]] = []
    if separator:
        for item in prerelease.split("."):
            identifiers.append((0, int(item)) if item.isdigit() else (1, item))
        stable = 0
    else:
        stable = 1
    return major, minor, patch, stable, tuple(identifiers)


def _object(value: Any, expected: set[str], required: set[str], label: str) -> dict:
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be an object")
    if not required <= value.keys() or not value.keys() <= expected:
        raise ValueError(f"{label} has an invalid shape")
    return value


def _canonical_object(contents: bytes, label: str) -> dict:
    try:
        value = json.loads(contents)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"{label} is not valid UTF-8 JSON") from error
    if canonical_json(value) != contents:
        raise ValueError(f"{label} is not canonical JSON")
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be an object")
    return value


def _validate_manifest(
    contents: bytes, package: str, version: str
) -> tuple[dict, dict[str, tuple[int, str]]]:
    manifest = _canonical_object(contents, "archive manifest")
    _object(
        manifest,
        {"protocolVersion", "package", "version", "entries"},
        {"protocolVersion", "package", "version", "entries"},
        "archive manifest",
    )
    if (
        manifest["protocolVersion"] != "1"
        or manifest["package"] != package
        or manifest["version"] != version
        or not isinstance(manifest["entries"], list)
        or not manifest["entries"]
        or len(manifest["entries"]) > MAX_ENTRIES
    ):
        raise ValueError("archive manifest identity or entries are invalid")
    entries: dict[str, tuple[int, str]] = {}
    total = 0
    for raw in manifest["entries"]:
        entry = _object(
            raw,
            {"path", "sha256", "size"},
            {"path", "sha256", "size"},
            "archive manifest entry",
        )
        path, digest, size = entry["path"], entry["sha256"], entry["size"]
        if (
            not isinstance(path, str)
            or PATH_RE.fullmatch(path) is None
            or path in entries
            or not isinstance(digest, str)
            or SHA_RE.fullmatch(digest) is None
            or not isinstance(size, int)
            or isinstance(size, bool)
            or size < 0
            or size > MAX_FILE
        ):
            raise UnsafeArchiveError("archive manifest contains an unsafe entry")
        total += size
        if total > MAX_EXTRACTED:
            raise UnsafeArchiveError("archive manifest exceeds extracted size limit")
        entries[path] = (size, digest)
    if "janus.toml" not in entries or not any(path.startswith("src/") for path in entries):
        raise UnsafeArchiveError("archive must contain janus.toml and src/")
    return manifest, entries


def _validate_archive(contents: bytes, entries: dict[str, tuple[int, str]]) -> None:
    if not contents or len(contents) > MAX_ARCHIVE:
        raise UnsafeArchiveError("archive size is invalid")
    actual: set[str] = set()
    try:
        with tarfile.open(fileobj=io.BytesIO(contents), mode="r:gz") as archive:
            members = archive.getmembers()
            if len(members) != len(entries) or len(members) > MAX_ENTRIES:
                raise UnsafeArchiveError("archive entries differ from manifest")
            for member in members:
                name = member.name.removeprefix("./")
                if (
                    not member.isfile()
                    or name not in entries
                    or name in actual
                    or "\\" in name
                    or pathlib.PurePosixPath(name).is_absolute()
                    or ".." in pathlib.PurePosixPath(name).parts
                ):
                    raise UnsafeArchiveError("archive contains an unsafe entry")
                expected_size, expected_sha = entries[name]
                if member.size != expected_size or member.size > MAX_FILE:
                    raise UnsafeArchiveError(
                        "archive entry size differs from manifest"
                    )
                stream = archive.extractfile(member)
                if stream is None:
                    raise UnsafeArchiveError("archive entry cannot be read")
                digest = hashlib.sha256()
                consumed = 0
                while chunk := stream.read(64 * 1024):
                    consumed += len(chunk)
                    if consumed > expected_size:
                        raise UnsafeArchiveError(
                            "archive entry exceeds declared size"
                        )
                    digest.update(chunk)
                if consumed != expected_size or digest.hexdigest() != expected_sha:
                    raise UnsafeArchiveError(
                        "archive entry checksum differs from manifest"
                    )
                actual.add(name)
    except (tarfile.TarError, OSError, EOFError) as error:
        raise UnsafeArchiveError("archive is not a valid gzip tar file") from error
    if actual != entries.keys():
        raise UnsafeArchiveError("archive entries differ from manifest")


def validate_publication(
    metadata_bytes: bytes,
    manifest_bytes: bytes,
    archive_bytes: bytes,
    package: str,
    version: str,
    api_base: str,
) -> tuple[dict, dict]:
    if PACKAGE_RE.fullmatch(package) is None or VERSION_RE.fullmatch(version) is None:
        raise ValueError("package or version is invalid")
    metadata = _canonical_object(metadata_bytes, "metadata")
    _object(
        metadata,
        {
            "protocolVersion",
            "package",
            "version",
            "publishedAt",
            "archive",
            "dependencies",
        },
        {
            "protocolVersion",
            "package",
            "version",
            "publishedAt",
            "archive",
            "dependencies",
        },
        "metadata",
    )
    if (
        metadata["protocolVersion"] != "1"
        or metadata["package"] != package
        or metadata["version"] != version
        or not isinstance(metadata["publishedAt"], str)
        or DATETIME_RE.fullmatch(metadata["publishedAt"]) is None
        or not isinstance(metadata["dependencies"], list)
    ):
        raise ValueError("metadata identity is invalid")
    archive = _object(
        metadata["archive"],
        {"url", "sha256", "size", "manifestSha256"},
        {"url", "sha256", "size", "manifestSha256"},
        "metadata archive",
    )
    expected_url = f"{api_base}/packages/{package}/{version}/archive.tar.gz"
    if (
        archive["url"] != expected_url
        or archive["sha256"] != sha256(archive_bytes)
        or archive["manifestSha256"] != sha256(manifest_bytes)
        or archive["size"] != len(archive_bytes)
    ):
        raise ValueError("metadata archive identity or checksum is invalid")
    for dependency in metadata["dependencies"]:
        item = _object(
            dependency,
            {"package", "requirement", "registry"},
            {"package", "requirement", "registry"},
            "dependency",
        )
        if (
            not isinstance(item["package"], str)
            or PACKAGE_RE.fullmatch(item["package"]) is None
            or not isinstance(item["requirement"], str)
            or not item["requirement"]
            or len(item["requirement"]) > 128
            or not isinstance(item["registry"], str)
            or REGISTRY_RE.fullmatch(item["registry"]) is None
        ):
            raise ValueError("metadata dependency is invalid")
    manifest, entries = _validate_manifest(manifest_bytes, package, version)
    _validate_archive(archive_bytes, entries)
    return metadata, manifest


class RegistryStore:
    def __init__(self, root: pathlib.Path | str, signing_key: bytes, key_id: str):
        self.root = pathlib.Path(root)
        self.database = self.root / "registry.sqlite3"
        self.blobs = self.root / "blobs" / "sha256"
        self.signing_key = signing_key
        self.key_id = key_id
        if len(signing_key) < 32:
            raise ValueError("registry signing key must contain at least 32 bytes")
        if not key_id or len(key_id) > 128:
            raise ValueError("registry signing key id is invalid")
        self.root.mkdir(parents=True, exist_ok=True)
        self.blobs.mkdir(parents=True, exist_ok=True)
        self._initialize()

    def connect(self, database: pathlib.Path | None = None) -> sqlite3.Connection:
        connection = sqlite3.connect(
            database or self.database, timeout=30, isolation_level=None
        )
        connection.row_factory = sqlite3.Row
        connection.execute("PRAGMA foreign_keys = ON")
        connection.execute("PRAGMA busy_timeout = 30000")
        return connection

    def _initialize(self) -> None:
        with self.connect() as connection:
            connection.executescript(
                """
                PRAGMA journal_mode = WAL;
                PRAGMA synchronous = FULL;
                CREATE TABLE IF NOT EXISTS schema_info (
                  version INTEGER NOT NULL
                );
                INSERT INTO schema_info(version)
                SELECT 1 WHERE NOT EXISTS (SELECT 1 FROM schema_info);
                CREATE TABLE IF NOT EXISTS tokens (
                  digest TEXT PRIMARY KEY,
                  subject TEXT NOT NULL,
                  scopes TEXT NOT NULL,
                  created_at TEXT NOT NULL,
                  revoked_at TEXT
                );
                CREATE TABLE IF NOT EXISTS releases (
                  package TEXT NOT NULL,
                  version TEXT NOT NULL,
                  metadata_sha256 TEXT NOT NULL,
                  manifest_sha256 TEXT NOT NULL,
                  archive_sha256 TEXT NOT NULL,
                  archive_size INTEGER NOT NULL,
                  published_at TEXT NOT NULL,
                  publisher TEXT NOT NULL,
                  yanked INTEGER NOT NULL DEFAULT 0,
                  yank_reason TEXT,
                  provenance BLOB NOT NULL,
                  PRIMARY KEY(package, version)
                );
                CREATE TABLE IF NOT EXISTS audit (
                  sequence INTEGER PRIMARY KEY AUTOINCREMENT,
                  timestamp TEXT NOT NULL,
                  subject TEXT NOT NULL,
                  action TEXT NOT NULL,
                  package TEXT,
                  version TEXT,
                  result TEXT NOT NULL,
                  request_id TEXT NOT NULL,
                  details TEXT NOT NULL,
                  previous_digest TEXT NOT NULL,
                  digest TEXT NOT NULL UNIQUE,
                  key_id TEXT NOT NULL,
                  signature TEXT NOT NULL
                );
                """
            )
            version = connection.execute(
                "SELECT version FROM schema_info"
            ).fetchone()[0]
            if version != SCHEMA_VERSION:
                raise RuntimeError(f"unsupported registry schema version {version}")

    def provision_token(self, token: str, subject: str, scopes: Iterable[str]) -> None:
        scope_set = sorted(set(scopes))
        if (
            len(token) < 24
            or not subject
            or len(subject) > 128
            or not scope_set
            or any(
                scope != "audit"
                and re.fullmatch(r"(?:publish|yank):[a-z][a-z0-9_-]{0,63}", scope)
                is None
                for scope in scope_set
            )
        ):
            raise ValueError("token subject or scopes are invalid")
        with self.connect() as connection:
            connection.execute(
                """
                INSERT INTO tokens(digest, subject, scopes, created_at, revoked_at)
                VALUES(?, ?, ?, ?, NULL)
                ON CONFLICT(digest) DO UPDATE SET
                  subject=excluded.subject, scopes=excluded.scopes, revoked_at=NULL
                """,
                (token_digest(token), subject, json.dumps(scope_set), utc_now()),
            )

    def revoke_token(self, digest: str) -> bool:
        if SHA_RE.fullmatch(digest) is None:
            raise ValueError("token digest is invalid")
        with self.connect() as connection:
            cursor = connection.execute(
                "UPDATE tokens SET revoked_at=? WHERE digest=? AND revoked_at IS NULL",
                (utc_now(), digest),
            )
            return cursor.rowcount == 1

    def authenticate(self, token: str, required_scope: str) -> str | None:
        identity = self.identity(token)
        if identity is None:
            return None
        subject, scopes = identity
        return subject if required_scope in scopes else None

    def identity(self, token: str) -> tuple[str, set[str]] | None:
        digest = token_digest(token)
        with self.connect() as connection:
            rows = connection.execute(
                "SELECT digest, subject, scopes FROM tokens WHERE revoked_at IS NULL"
            ).fetchall()
        for row in rows:
            if hmac.compare_digest(row["digest"], digest):
                return row["subject"], set(json.loads(row["scopes"]))
        return None

    def _blob_path(self, digest: str) -> pathlib.Path:
        if SHA_RE.fullmatch(digest) is None:
            raise ValueError("blob digest is invalid")
        return self.blobs / digest[:2] / digest[2:]

    def _write_blob(self, contents: bytes) -> str:
        digest = sha256(contents)
        destination = self._blob_path(digest)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists():
            if sha256(destination.read_bytes()) != digest:
                raise RuntimeError("existing registry blob is corrupt")
            return digest
        descriptor, temporary_name = tempfile.mkstemp(
            dir=destination.parent, prefix=".new-"
        )
        temporary = pathlib.Path(temporary_name)
        try:
            with os.fdopen(descriptor, "wb") as output:
                output.write(contents)
                output.flush()
                os.fsync(output.fileno())
            os.replace(temporary, destination)
        finally:
            temporary.unlink(missing_ok=True)
        return digest

    def read_blob(self, digest: str) -> bytes:
        contents = self._blob_path(digest).read_bytes()
        if sha256(contents) != digest:
            raise RuntimeError("registry blob checksum mismatch")
        return contents

    def _sign(self, contents: bytes) -> str:
        return base64.urlsafe_b64encode(
            hmac.new(self.signing_key, contents, hashlib.sha256).digest()
        ).rstrip(b"=").decode()

    def audit(
        self,
        subject: str,
        action: str,
        result: str,
        request_id: str,
        package: str | None = None,
        version: str | None = None,
        details: str = "",
        connection: sqlite3.Connection | None = None,
    ) -> None:
        owned = connection is None
        connection = connection or self.connect()
        try:
            if owned:
                connection.execute("BEGIN IMMEDIATE")
            previous = connection.execute(
                "SELECT digest FROM audit ORDER BY sequence DESC LIMIT 1"
            ).fetchone()
            previous_digest = previous["digest"] if previous else "0" * 64
            record = {
                "action": action,
                "details": details,
                "package": package,
                "previousDigest": previous_digest,
                "requestId": request_id,
                "result": result,
                "subject": subject,
                "timestamp": utc_now(),
                "version": version,
            }
            encoded = canonical_json(record)
            digest = sha256(encoded)
            connection.execute(
                """
                INSERT INTO audit(
                  timestamp, subject, action, package, version, result, request_id,
                  details, previous_digest, digest, key_id, signature
                ) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    record["timestamp"],
                    subject,
                    action,
                    package,
                    version,
                    result,
                    request_id,
                    details,
                    previous_digest,
                    digest,
                    self.key_id,
                    self._sign(encoded),
                ),
            )
            if owned:
                connection.execute("COMMIT")
        except Exception:
            if owned:
                connection.execute("ROLLBACK")
            raise
        finally:
            if owned:
                connection.close()

    def publish(
        self,
        package: str,
        version: str,
        metadata: bytes,
        manifest: bytes,
        archive: bytes,
        publisher: str,
        request_id: str,
        api_base: str,
    ) -> dict:
        parsed_metadata, _ = validate_publication(
            metadata, manifest, archive, package, version, api_base
        )
        metadata_sha = self._write_blob(metadata)
        manifest_sha = self._write_blob(manifest)
        archive_sha = self._write_blob(archive)
        statement = {
            "_type": "https://janus-lang.org/provenance/registry-receipt/v1",
            "acceptedAt": utc_now(),
            "archiveSha256": archive_sha,
            "manifestSha256": manifest_sha,
            "metadataSha256": metadata_sha,
            "package": package,
            "publishedAt": parsed_metadata["publishedAt"],
            "publisher": publisher,
            "requestId": request_id,
            "version": version,
        }
        receipt = {
            "keyId": self.key_id,
            "signature": self._sign(canonical_json(statement)),
            "statement": statement,
        }
        with self.connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
            try:
                connection.execute(
                    """
                    INSERT INTO releases(
                      package, version, metadata_sha256, manifest_sha256,
                      archive_sha256, archive_size, published_at, publisher,
                      provenance
                    ) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)
                    """,
                    (
                        package,
                        version,
                        metadata_sha,
                        manifest_sha,
                        archive_sha,
                        len(archive),
                        parsed_metadata["publishedAt"],
                        publisher,
                        canonical_json(receipt),
                    ),
                )
                self.audit(
                    publisher,
                    "publish",
                    "allowed",
                    request_id,
                    package,
                    version,
                    connection=connection,
                )
                connection.execute("COMMIT")
            except sqlite3.IntegrityError as error:
                connection.execute("ROLLBACK")
                raise FileExistsError("package version already exists") from error
            except Exception:
                connection.execute("ROLLBACK")
                raise
        return receipt

    def release(self, package: str, version: str) -> sqlite3.Row | None:
        with self.connect() as connection:
            return connection.execute(
                "SELECT * FROM releases WHERE package=? AND version=?",
                (package, version),
            ).fetchone()

    def package_index(self, package: str, api_base: str) -> dict | None:
        with self.connect() as connection:
            rows = connection.execute(
                "SELECT * FROM releases WHERE package=?", (package,)
            ).fetchall()
        if not rows:
            return None
        rows = sorted(
            rows, key=lambda row: (semver_key(row["version"]), row["version"])
        )
        releases = []
        for row in rows:
            item = {
                "metadataSha256": row["metadata_sha256"],
                "metadataUrl": (
                    f"{api_base}/packages/{package}/{row['version']}/metadata"
                ),
                "version": row["version"],
                "yanked": bool(row["yanked"]),
            }
            if row["yank_reason"]:
                item["yankReason"] = row["yank_reason"]
            releases.append(item)
        return {"package": package, "protocolVersion": "1", "releases": releases}

    def search(self, query: str) -> dict:
        query = query.casefold()
        with self.connect() as connection:
            rows = connection.execute(
                "SELECT package, version FROM releases WHERE yanked=0"
            ).fetchall()
        grouped: dict[str, list[str]] = {}
        for row in rows:
            if query in row["package"].casefold():
                grouped.setdefault(row["package"], []).append(row["version"])
        packages = [
            {
                "latestVersion": max(
                    versions, key=lambda version: (semver_key(version), version)
                ),
                "package": package,
            }
            for package, versions in sorted(grouped.items())
        ]
        return {"packages": packages, "protocolVersion": "1"}

    def set_yanked(
        self,
        package: str,
        version: str,
        yanked: bool,
        reason: str,
        subject: str,
        request_id: str,
    ) -> bool:
        with self.connect() as connection:
            connection.execute("BEGIN IMMEDIATE")
            cursor = connection.execute(
                """
                UPDATE releases SET yanked=?, yank_reason=?
                WHERE package=? AND version=?
                """,
                (int(yanked), reason if yanked else None, package, version),
            )
            if cursor.rowcount:
                self.audit(
                    subject,
                    "yank" if yanked else "unyank",
                    "allowed",
                    request_id,
                    package,
                    version,
                    reason,
                    connection,
                )
            connection.execute("COMMIT")
            return cursor.rowcount == 1

    def audit_records(self, package: str | None = None) -> list[dict]:
        query = "SELECT * FROM audit"
        parameters: tuple[str, ...] = ()
        if package:
            query += " WHERE package=?"
            parameters = (package,)
        query += " ORDER BY sequence"
        with self.connect() as connection:
            rows = connection.execute(query, parameters).fetchall()
        return [
            {
                "action": row["action"],
                "details": row["details"],
                "digest": row["digest"],
                "keyId": row["key_id"],
                "package": row["package"],
                "previousDigest": row["previous_digest"],
                "requestId": row["request_id"],
                "result": row["result"],
                "sequence": row["sequence"],
                "signature": row["signature"],
                "subject": row["subject"],
                "timestamp": row["timestamp"],
                "version": row["version"],
            }
            for row in rows
        ]

    def create_backup(self, destination: pathlib.Path | str) -> pathlib.Path:
        destination = pathlib.Path(destination)
        with tempfile.TemporaryDirectory(prefix="janus-registry-backup-") as temporary:
            snapshot_root = pathlib.Path(temporary)
            snapshot_database = snapshot_root / "registry.sqlite3"
            source = self.connect()
            target = sqlite3.connect(snapshot_database)
            try:
                source.backup(target)
            finally:
                target.close()
                source.close()
            with contextlib.closing(self.connect(snapshot_database)) as snapshot:
                rows = snapshot.execute(
                    """
                    SELECT metadata_sha256, manifest_sha256, archive_sha256
                    FROM releases
                    """
                ).fetchall()
            digests = sorted(
                {
                    digest
                    for row in rows
                    for digest in (
                        row["metadata_sha256"],
                        row["manifest_sha256"],
                        row["archive_sha256"],
                    )
                }
            )
            files: dict[str, str] = {}
            files["registry.sqlite3"] = sha256(snapshot_database.read_bytes())
            for digest in digests:
                source_path = self._blob_path(digest)
                if sha256(source_path.read_bytes()) != digest:
                    raise RuntimeError("cannot back up a corrupt registry blob")
                relative = pathlib.Path("blobs") / "sha256" / digest[:2] / digest[2:]
                target_path = snapshot_root / relative
                target_path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source_path, target_path)
                files[relative.as_posix()] = digest
            manifest = {
                "createdAt": utc_now(),
                "files": files,
                "schemaVersion": SCHEMA_VERSION,
            }
            manifest_bytes = canonical_json(manifest)
            (snapshot_root / "backup-manifest.json").write_bytes(manifest_bytes)
            files_to_archive = ["backup-manifest.json", *sorted(files)]
            destination.parent.mkdir(parents=True, exist_ok=True)
            descriptor, temporary_name = tempfile.mkstemp(
                dir=destination.parent, prefix=".backup-", suffix=".tar.gz"
            )
            os.close(descriptor)
            try:
                with open(temporary_name, "wb") as raw:
                    with gzip.GzipFile(fileobj=raw, mode="wb", mtime=0) as compressed:
                        with tarfile.open(fileobj=compressed, mode="w") as archive:
                            for relative in files_to_archive:
                                source_path = snapshot_root / relative
                                size = source_path.stat().st_size
                                info = tarfile.TarInfo(relative)
                                info.size = size
                                info.mode = 0o600
                                info.mtime = 0
                                info.uid = info.gid = 0
                                info.uname = info.gname = ""
                                with source_path.open("rb") as contents:
                                    archive.addfile(info, contents)
                os.replace(temporary_name, destination)
            finally:
                pathlib.Path(temporary_name).unlink(missing_ok=True)
        return destination

    @staticmethod
    def restore_backup(
        archive_path: pathlib.Path | str, destination: pathlib.Path | str
    ) -> pathlib.Path:
        archive_path, destination = pathlib.Path(archive_path), pathlib.Path(destination)
        if destination.exists():
            raise FileExistsError("restore destination already exists")
        destination.parent.mkdir(parents=True, exist_ok=True)
        staging = pathlib.Path(
            tempfile.mkdtemp(dir=destination.parent, prefix=f".{destination.name}.new-")
        )
        try:
            with tarfile.open(archive_path, "r:gz") as archive:
                members: dict[str, tarfile.TarInfo] = {}
                for member in archive.getmembers():
                    pure = pathlib.PurePosixPath(member.name)
                    if (
                        not member.isfile()
                        or pure.is_absolute()
                        or ".." in pure.parts
                        or "\\" in member.name
                        or member.name != pure.as_posix()
                        or member.name in members
                    ):
                        raise ValueError("backup contains an unsafe entry")
                    members[member.name] = member
                manifest_member = members.get("backup-manifest.json")
                if manifest_member is None or manifest_member.size > 8 * 1024 * 1024:
                    raise ValueError("backup manifest is missing or too large")
                manifest_stream = archive.extractfile(manifest_member)
                if manifest_stream is None:
                    raise ValueError("backup manifest cannot be read")
                manifest_bytes = manifest_stream.read()
                manifest = _canonical_object(manifest_bytes, "backup manifest")
                if manifest.get("schemaVersion") != SCHEMA_VERSION or not isinstance(
                    manifest.get("files"), dict
                ):
                    raise ValueError("backup schema is unsupported")
                expected_paths = {"backup-manifest.json", *manifest["files"].keys()}
                if members.keys() != expected_paths:
                    raise ValueError("backup contains unexpected or missing files")
                for relative, digest in manifest["files"].items():
                    valid_path = relative == "registry.sqlite3" or re.fullmatch(
                        r"blobs/sha256/[0-9a-f]{2}/[0-9a-f]{62}", relative
                    )
                    if (
                        not valid_path
                        or not isinstance(digest, str)
                        or SHA_RE.fullmatch(digest) is None
                    ):
                        raise ValueError("backup manifest contains an invalid file")
                    member = members[relative]
                    stream = archive.extractfile(member)
                    if stream is None:
                        raise ValueError("backup entry cannot be read")
                    target = staging.joinpath(*pathlib.PurePosixPath(relative).parts)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    calculated = hashlib.sha256()
                    with target.open("wb") as output:
                        while chunk := stream.read(64 * 1024):
                            calculated.update(chunk)
                            output.write(chunk)
                    if calculated.hexdigest() != digest:
                        raise ValueError("backup file checksum mismatch")
            database = staging / "registry.sqlite3"
            connection = sqlite3.connect(database)
            connection.row_factory = sqlite3.Row
            try:
                version = connection.execute(
                    "SELECT version FROM schema_info"
                ).fetchone()[0]
                if version != SCHEMA_VERSION:
                    raise ValueError("backup database schema is unsupported")
                rows = connection.execute(
                    """
                    SELECT metadata_sha256, manifest_sha256, archive_sha256
                    FROM releases
                    """
                ).fetchall()
            finally:
                connection.close()
            for row in rows:
                for digest in row:
                    blob = staging / "blobs" / "sha256" / digest[:2] / digest[2:]
                    if not blob.is_file() or sha256(blob.read_bytes()) != digest:
                        raise ValueError("backup database references a missing blob")
            os.replace(staging, destination)
        except Exception:
            shutil.rmtree(staging, ignore_errors=True)
            raise
        return destination
