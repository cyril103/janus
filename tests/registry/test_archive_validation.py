"""Small, bounded regression fixtures for publication archive budgets (#366)."""
from __future__ import annotations

import gzip
import io
import json
import pathlib
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "registry"))
from reference_registry import core


def packed(files, *, format=tarfile.PAX_FORMAT):
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode="w", format=format) as archive:
        for name, data in files:
            member = tarfile.TarInfo(name)
            member.size = len(data)
            archive.addfile(member, io.BytesIO(data))
    return gzip.compress(output.getvalue())


def entries_for(files):
    return {name: (len(data), core.sha256(data)) for name, data in files}


def extension(kind, data=b"", size=None):
    member = tarfile.TarInfo("extension")
    member.type = kind
    member.size = len(data) if size is None else size
    return member.tobuf() + data + b"\0" * (-len(data) % 512)


class ArchiveValidationTests(unittest.TestCase):
    files = [("janus.toml", b"abc"), ("src/main.janus", b"def")]

    def rejected(self, contents, entries=None, message=None):
        with self.assertRaisesRegex(core.UnsafeArchiveError, message or "."):
            core._validate_archive(contents, entries if entries is not None else entries_for(self.files))

    def test_first_mismatch_stops_before_remaining_members(self):
        contents = packed([(f"src/f{i}.janus", b"") for i in range(20000)])
        seen = set()
        original = tarfile.TarFile.next

        def counted(archive):
            member = original(archive)
            if member is not None:
                seen.add(member.name)
            return member

        with patch.object(tarfile.TarFile, "next", counted):
            self.rejected(contents)
        self.assertEqual(seen, {"src/f0.janus"})
        self.assertLess(len(contents), 128 * 1024)

    def test_file_count_and_total_limits_during_read(self):
        contents = packed(self.files)
        entries = entries_for(self.files)
        # At each exact boundary, a valid archive remains accepted.
        with patch.multiple(core, MAX_ENTRIES=2, MAX_FILE=3, MAX_EXTRACTED=6):
            core._validate_archive(contents, entries)
        for budget, limit, message in [
            ("MAX_ENTRIES", 1, "entries differ"),
            ("MAX_FILE", 2, "size limit"),
            ("MAX_EXTRACTED", 5, "extracted size limit"),
        ]:
            with self.subTest(budget=budget), patch.object(core, budget, limit):
                self.rejected(contents, entries, message)

    def test_oversized_header_rejected_before_payload_read(self):
        for kind, size in [(tarfile.REGTYPE, core.MAX_FILE + 1),
                           (tarfile.XHDTYPE, core.MAX_EXTENDED_HEADER + 1),
                           (tarfile.XGLTYPE, core.MAX_EXTENDED_HEADER + 1),
                           (tarfile.GNUTYPE_LONGNAME, core.MAX_EXTENDED_HEADER + 1),
                           (tarfile.GNUTYPE_LONGLINK, core.MAX_EXTENDED_HEADER + 1)]:
            # The claimed body is deliberately absent: limits must win over EOF.
            with self.subTest(kind=kind):
                self.rejected(gzip.compress(extension(kind, size=size)), message="limit")

    def test_extension_chain_and_cumulative_budget(self):
        header = extension(tarfile.XHDTYPE, b"12 path=foo\n")
        with patch.object(core, "MAX_EXTENDED_CHAIN", 2):
            self.rejected(gzip.compress(header * 3), message="extended header limit")
        raw = gzip.decompress(packed(self.files))
        # Two harmless GNU names separated by files test the cumulative budget.
        first = extension(tarfile.GNUTYPE_LONGNAME, b"janus.toml\0")
        second = extension(tarfile.GNUTYPE_LONGNAME, b"src/main.janus\0")
        archive = gzip.compress(first + raw[:1024] + second + raw[1024:])
        with patch.object(core, "MAX_EXTENDED_TOTAL", 2048):
            core._validate_archive(archive, entries_for(self.files))
        with patch.object(core, "MAX_EXTENDED_TOTAL", 2047):
            self.rejected(archive, message="extended header limit")

    def test_decompression_budget_includes_tail_and_concatenated_gzip(self):
        contents = packed(self.files)
        raw_size = len(gzip.decompress(contents))
        with patch.object(core, "MAX_DECOMPRESSED", raw_size):
            core._validate_archive(contents, entries_for(self.files))
            consumed = 0
            original = gzip.GzipFile.read

            def counted(stream, size=-1):
                nonlocal consumed
                data = original(stream, size)
                consumed += len(data)
                return data

            with patch.object(gzip.GzipFile, "read", counted):
                self.rejected(contents + gzip.compress(b"\0" * 65536), message="decompressed size limit")
            self.assertEqual(consumed, raw_size + 1)
        with patch.object(core, "MAX_DECOMPRESSED", 1024):
            self.rejected(contents, message="decompressed size limit")
        corrupt = bytearray(contents)
        corrupt[-8] ^= 1
        self.rejected(bytes(corrupt), message="valid gzip tar")

    def test_full_entry_budget_without_metadata_cache(self):
        files = [("janus.toml", b"")] + [(f"src/f{i}.janus", b"")
                                            for i in range(core.MAX_ENTRIES - 1)]
        original = tarfile.TarFile.next
        cached = []

        def counted(archive):
            cached.append(len(archive.members))
            return original(archive)

        contents = packed(files)
        with patch.object(tarfile.TarFile, "next", counted):
            core._validate_archive(contents, entries_for(files))
        self.assertLessEqual(max(cached), 1)

    def test_long_names_pax_and_gnu_remain_valid(self):
        files = [("janus.toml", b""), ("src/" + "a" * 150 + ".janus", b"source")]
        for format in (tarfile.PAX_FORMAT, tarfile.GNU_FORMAT):
            with self.subTest(format=format):
                core._validate_archive(packed(files, format=format), entries_for(files))

    def test_existing_integrity_checks(self):
        self.rejected(packed(self.files + [self.files[0]]))
        self.rejected(packed(self.files[:1]))
        self.rejected(packed([(self.files[0][0], b"xyz"), self.files[1]]), message="checksum")
        for name in ("../janus.toml", "/janus.toml", "src/../main.janus", "src\\main.janus"):
            with self.subTest(name=name):
                self.rejected(packed([(name, b"")]), {name: (0, core.sha256(b""))})
        for kind in (tarfile.SYMTYPE, tarfile.LNKTYPE, tarfile.DIRTYPE,
                     tarfile.CHRTYPE, tarfile.FIFOTYPE, tarfile.GNUTYPE_SPARSE):
            self.rejected(gzip.compress(extension(kind)), message="unsafe entry type")

    def test_pax_sparse_rejected_before_map_processing(self):
        output = io.BytesIO()
        with tarfile.open(fileobj=output, mode="w") as archive:
            member = tarfile.TarInfo("janus.toml")
            member.pax_headers = {"GNU.sparse.major": "1", "GNU.sparse.minor": "0"}
            archive.addfile(member)
        self.rejected(gzip.compress(output.getvalue()), message="sparse entry")

    def test_publication_rejects_without_persistent_artifacts(self):
        manifest = core.canonical_json({
            "protocolVersion": "1", "package": "acme/test", "version": "1.0.0",
            "entries": [{"path": name, "size": size, "sha256": digest}
                        for name, (size, digest) in entries_for(self.files).items()],
        })
        with tempfile.TemporaryDirectory() as root:
            store = core.RegistryStore(root, b"x" * 32, "test")
            before = {p.relative_to(root): p.read_bytes() for p in pathlib.Path(root).rglob("*") if p.is_file()}
            for contents in (packed([("src/unexpected.janus", b"")]),
                             gzip.compress(extension(tarfile.XHDTYPE, size=core.MAX_EXTENDED_HEADER + 1))):
                metadata = core.canonical_json({
                    "protocolVersion": "1", "package": "acme/test", "version": "1.0.0",
                    "publishedAt": "2026-09-13T00:00:00Z", "dependencies": [],
                    "archive": {"url": "https://example.test/v1/packages/acme/test/1.0.0/archive.tar.gz",
                                "sha256": core.sha256(contents), "size": len(contents),
                                "manifestSha256": core.sha256(manifest)},
                })
                with self.assertRaises(core.UnsafeArchiveError):
                    store.publish("acme/test", "1.0.0", metadata, manifest, contents,
                                  "publisher", "request", "https://example.test/v1")
                after = {p.relative_to(root): p.read_bytes() for p in pathlib.Path(root).rglob("*") if p.is_file()}
                self.assertEqual(before, after)
            # The same publication path accepts a valid archive at all three
            # logical boundaries, with budgets scaled down to keep fixtures tiny.
            contents = packed(self.files)
            valid_metadata = json.loads(metadata)
            valid_metadata["archive"].update(size=len(contents), sha256=core.sha256(contents))
            with patch.multiple(core, MAX_ENTRIES=2, MAX_FILE=3, MAX_EXTRACTED=6):
                store.publish("acme/test", "1.0.0", core.canonical_json(valid_metadata),
                              manifest, contents, "publisher", "request",
                              "https://example.test/v1")
            self.assertTrue(any(store.blobs.rglob("*")))


if __name__ == "__main__":
    unittest.main()
