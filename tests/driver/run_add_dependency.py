#!/usr/bin/env python3
"""Exercise optional dependency versions through the CLI without a network."""

import argparse
import os
import pathlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--janus", required=True, type=pathlib.Path)
    args = parser.parse_args()
    janus = str(args.janus.resolve())
    with tempfile.TemporaryDirectory(prefix="janus-add-") as temporary:
        root = pathlib.Path(temporary)
        env = {**os.environ, "PATH": str(pathlib.Path(janus).parent)
               + os.pathsep + os.environ.get("PATH", ""),
               "JANUS_CACHE": str(root / "cache"),
               "JANUS_REGISTRY": str(root / "registry"),
               "GIT_ALLOW_PROTOCOL": "file"}

        def run(*command, cwd=root, error=None):
            result = subprocess.run(
                [janus, *command], cwd=cwd, env=env,
                capture_output=True, text=True, timeout=10)
            output = result.stdout + result.stderr
            assert (result.returncode == 0) == (error is None), output
            if error is not None:
                assert error in output, output

        run("new", "dep")
        revision = "0123456789abcdefABCDEF0123456789abcdefAB"
        url = "https://example.invalid/dep.git"
        sources = [
            (["--path", "../dep"], 'path = "../dep"'),
            (["--git", url, "--rev", revision],
             f'git = "{url}", rev = "{revision}"'),
            ([], None),
            (["--registry", "https://example.invalid/registry"],
             'registry = "https://example.invalid/registry"'),
        ]
        for index, (options, location) in enumerate(sources):
            run("new", f"app{index}")
            app = root / f"app{index}"
            manifest = app / "janus.toml"
            for requirement in (None, "^0.1.0", "not-a-version", 'bad"version',
                                "bad\nversion", "bad\rversion"):
                original = manifest.read_bytes()
                version_args = ([] if requirement is None
                                else ["--version", requirement])
                invalid = requirement not in (None, "^0.1.0")
                run("add", "dep", *options, *version_args, cwd=app,
                    error="invalid version requirement" if invalid else None)
                if invalid:
                    assert manifest.read_bytes() == original, (
                        "rejected version changed the original manifest")
                    continue

                version = requirement if requirement is not None else "*"
                if index < 2:
                    fields = location
                    if requirement is not None:
                        fields += f', version = "{requirement}"'
                    expected = f"dep = {{ {fields} }}"
                elif location is None:
                    expected = f'dep = "{version}"'
                else:
                    expected = f'dep = {{ version = "{version}", {location} }}'
                assert expected in manifest.read_text(encoding="utf-8").splitlines()
                # remove reloads the saved manifest, including the offline Git case.
                run("remove", "dep", cwd=app)
                assert expected not in manifest.read_text(encoding="utf-8")


if __name__ == "__main__":
    main()
