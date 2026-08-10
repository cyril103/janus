#!/usr/bin/env python3
"""Generate small deterministic archives for the shared installer security tests."""

import io
import json
import stat
import sys
import tarfile
import zipfile
from pathlib import Path


ROOT = "janus-test-Linux-x86_64"


def tar_archive(path: Path, entries):
    with tarfile.open(path, "w:gz", format=tarfile.PAX_FORMAT) as archive:
        for name, kind, data in entries:
            info = tarfile.TarInfo(name)
            info.mtime = 0
            if kind in ("file", "owned_file"):
                if kind == "owned_file":
                    payload, info.uname, info.gname = data
                else:
                    payload = data
                payload = payload if isinstance(payload, bytes) else payload.encode()
                info.size = len(payload)
                info.mode = 0o755 if name.endswith("/janus") else 0o644
                archive.addfile(info, io.BytesIO(payload))
            elif kind == "dir":
                info.type = tarfile.DIRTYPE
                archive.addfile(info)
            elif kind == "symlink":
                info.type, info.linkname = tarfile.SYMTYPE, data
                archive.addfile(info)
            elif kind == "hardlink":
                info.type, info.linkname = tarfile.LNKTYPE, data
                archive.addfile(info)
            elif kind == "fifo":
                info.type = tarfile.FIFOTYPE
                archive.addfile(info)


def zip_archive(path: Path, entries):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, kind, data in entries:
            info = zipfile.ZipInfo(name)
            info.date_time = (1980, 1, 1, 0, 0, 0)
            info.create_system = 3
            if kind == "dir":
                info.filename = name.rstrip("/") + "/"
                info.external_attr = (stat.S_IFDIR | 0o755) << 16
                archive.writestr(info, b"")
            elif kind == "symlink":
                info.external_attr = (stat.S_IFLNK | 0o777) << 16
                archive.writestr(info, data.encode())
            else:
                info.external_attr = (stat.S_IFREG | 0o644) << 16
                archive.writestr(info, data if isinstance(data, bytes) else data.encode())


def main(output: Path):
    output.mkdir(parents=True, exist_ok=True)
    base = [(f"{ROOT}/", "dir", ""), (f"{ROOT}/bin/", "dir", ""),
            (f"{ROOT}/bin/janus", "file", "janus")]
    cases = {
        "valid": (True, base),
        "absolute": (False, base + [("/tmp/janus-escape", "file", "x")]),
        "traversal": (False, base + [(f"{ROOT}/../escape", "file", "x")]),
        "drive": (False, base + [("C:/janus-escape", "file", "x")]),
        "unc": (False, base + [("//server/share/escape", "file", "x")]),
        "backslash": (False, base + [(f"{ROOT}\\..\\escape", "file", "x")]),
        "symlink": (False, base + [(f"{ROOT}/link", "symlink", "bin/janus")]),
        "multi_root": (False, base + [("other-root/file", "file", "x")]),
        "dot_collision": (False, base + [(f"{ROOT}/bin/./janus", "file", "x")]),
        "case_collision": (False, base + [(f"{ROOT}/BIN/JANUS", "file", "x")]),
        "alternate_stream": (False, base + [(f"{ROOT}/bin/janus:payload", "file", "x")]),
        "trailing_dot": (False, base + [(f"{ROOT}/bin/janus.", "file", "x")]),
        "reserved_device": (False, base + [(f"{ROOT}/bin/CON.txt", "file", "x")]),
        "file_directory_collision": (False, base + [
            (f"{ROOT}/node", "file", "x"),
            (f"{ROOT}/node/child", "file", "x"),
        ]),
        "file_directory_collision_reverse": (False, base + [
            (f"{ROOT}/node/child", "file", "x"),
            (f"{ROOT}/node", "file", "x"),
        ]),
        "too_many": (False, base + [(f"{ROOT}/f{i}", "file", "x") for i in range(6)]),
        "too_large": (False, base + [(f"{ROOT}/large", "file", b"x" * 65)]),
        "too_large_total": (False, base + [(f"{ROOT}/a", "file", b"x" * 40),
                                            (f"{ROOT}/b", "file", b"x" * 40)]),
    }
    manifest = []
    for name, (accepted, entries) in cases.items():
        tar_archive(output / f"{name}.tar.gz", entries)
        zip_archive(output / f"{name}.zip", entries)
        manifest.append({"name": name, "accepted": accepted})
    tar_archive(output / "hardlink.tar.gz", base + [
        (f"{ROOT}/hard", "hardlink", f"{ROOT}/bin/janus")])
    tar_archive(output / "special.tar.gz", base + [(f"{ROOT}/pipe", "fifo", "")])
    tar_archive(output / "owner_names.tar.gz", base + [
        (f"{ROOT}/owned", "owned_file", (b"small", "owner with space", "group with space"))
    ])
    manifest.extend([{"name": "hardlink", "accepted": False, "formats": ["tar.gz"]},
                     {"name": "special", "accepted": False, "formats": ["tar.gz"]},
                     {"name": "owner_names", "accepted": True, "formats": ["tar.gz"]}])
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
