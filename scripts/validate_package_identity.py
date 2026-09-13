#!/usr/bin/env python3
"""Check the actual distribution contents before publishing its checksum."""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import tarfile
import tempfile
import zipfile
from pathlib import Path


def validate(archive: Path, version: str, platform: str,
             bindir: str = "bin", datadir: str = "share") -> None:
    basename = f"janus-{version}-{platform}"
    extension = ".zip" if platform.startswith("Windows-") else ".tar.gz"
    if archive.name != basename + extension:
        raise ValueError("archive name does not match configured package version/platform")
    with tempfile.TemporaryDirectory(prefix="janus-dist-") as directory:
        root = Path(directory).resolve()
        # CPack archives contain only regular files and directories. Check paths
        # before extraction and materialize files without accepting archive links.
        def destination(name: str) -> Path:
            target = (root / name).resolve()
            if not target.is_relative_to(root / basename):
                raise ValueError(f"unexpected archive entry: {name}")
            return target

        if extension == ".zip":
            with zipfile.ZipFile(archive) as source:
                for entry in source.infolist():
                    target = destination(entry.filename)
                    if entry.is_dir():
                        target.mkdir(parents=True, exist_ok=True)
                    else:
                        target.parent.mkdir(parents=True, exist_ok=True)
                        with source.open(entry) as contents, target.open("wb") as output:
                            shutil.copyfileobj(contents, output)
        else:
            with tarfile.open(archive, "r:gz") as source:
                for entry in source:
                    target = destination(entry.name)
                    if entry.isdir():
                        target.mkdir(parents=True, exist_ok=True)
                    elif entry.isfile():
                        target.parent.mkdir(parents=True, exist_ok=True)
                        with source.extractfile(entry) as contents, target.open("wb") as output:
                            shutil.copyfileobj(contents, output)
                        target.chmod(entry.mode & 0o777)
                    else:
                        raise ValueError(f"unsupported archive entry: {entry.name}")
        package = root / basename
        manifest = json.loads((package / datadir / "janus/build-identity.json").read_text())
        if manifest.get("schema_version") != 1 or manifest.get("version") != version:
            raise ValueError("packaged manifest version does not match archive name")
        suffix = ".exe" if extension == ".zip" else ""
        for tool in ("janus", "janus-lsp", "janusup"):
            result = subprocess.run(
                [str(package / bindir / (tool + suffix)), "--version", "--json"],
                check=True, capture_output=True, text=True, timeout=30)
            if json.loads(result.stdout) != manifest:
                raise ValueError(f"packaged {tool} identity differs from manifest")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--bindir", default="bin")
    parser.add_argument("--datadir", default="share")
    args = parser.parse_args()
    validate(args.archive, args.version, args.platform, args.bindir, args.datadir)
    print(f"Package identity verified: {args.archive.name}")


if __name__ == "__main__":
    main()
