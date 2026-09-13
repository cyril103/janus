#!/usr/bin/env python3
"""Validate every edge to shared path/Git packages, without external network."""

import argparse
import json
import os
import pathlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--janus", required=True, type=pathlib.Path)
    args = parser.parse_args()
    janus = str(args.janus.resolve())
    with tempfile.TemporaryDirectory(prefix="janus-constraints-") as temporary:
        root = pathlib.Path(temporary)
        env = {**os.environ, "JANUS_CACHE": str(root / "cache"),
               "GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": os.devnull}

        def project(path, name, dependencies=""):
            (path / "src").mkdir(parents=True, exist_ok=True)
            (path / "src/main.janus").write_text(
                "def main() : int { return 0 }\n", encoding="utf-8")
            (path / "janus.toml").write_text(
                f'[package]\nname = "{name}"\nversion = "1.0.0"\n'
                'entry = "src/main.janus"\n[dependencies]\n' + dependencies,
                encoding="utf-8")
            return path

        shared = project(root / "shared", "shared")

        def git(*command):
            return subprocess.run(
                ["git", "-C", str(shared), *command], env=env, check=True,
                capture_output=True, text=True).stdout.strip()

        git("init", "--quiet")
        git("add", ".")
        git("-c", "user.name=Janus Test", "-c", "user.email=test@example.invalid",
            "-c", "commit.gpgsign=false", "commit", "--quiet", "-m", "fixture")
        revision = git("rev-parse", "HEAD")

        def check(app, *, locked=False, error=None):
            result = subprocess.run(
                [janus, "check", *(["--locked"] if locked else [])],
                cwd=app, env=env, capture_output=True, text=True)
            output = result.stdout + result.stderr
            assert (result.returncode == 0) == (error is None), output
            if error:
                for fragment in error:
                    assert fragment in output, output

        for source in ("path", "git"):
            location = ('path = "../../shared"' if source == "path"
                        else f"git = {json.dumps(shared.as_uri())}, rev = \"{revision}\"")

            def edge(requirement):
                version = (f', version = "{requirement}"'
                           if requirement is not None else "")
                return f"shared = {{ {location}{version} }}\n"

            for reverse in (False, True):
                case = root / f"{source}-{reverse}"
                consumer = project(case / "consumer", "consumer", edge("2.0.0"))
                dependencies = [edge("1.0.0"), 'consumer = { path = "../consumer" }\n']
                if reverse:
                    dependencies.reverse()
                app = project(case / "app", "app", "".join(dependencies))
                lock = app / "janus.lock"
                diagnostic = ["shared", "1.0.0", "2.0.0", "consumer"]
                check(app, error=diagnostic)
                assert not lock.exists(), "invalid graph created a lockfile"

                # Compatible and omitted constraints must deduplicate in both orders.
                for requirement in ("^1.0.0", None):
                    project(consumer, "consumer", edge(requirement))
                    check(app)
                    contents = lock.read_bytes()
                    assert contents.count(b'name = "shared"') == 1, contents
                    check(app, locked=True)
                    assert lock.read_bytes() == contents

                # Keep a valid lock, then invalidate only a transitive constraint.
                project(consumer, "consumer", edge("2.0.0"))
                for locked in (False, True):
                    check(app, locked=locked, error=diagnostic)
                    assert lock.read_bytes() == contents, "failure changed the lockfile"

        # The deduplication change must preserve cycle and source-conflict checks.
        project(shared, "shared", 'shared = { path = "." }\n')
        app = project(root / "cycle", "app",
                      'shared = { path = "../shared" }\n')
        check(app, error=["cyclic dependency", "shared"])
        project(shared, "shared")
        project(root / "other", "shared")
        project(root / "consumer", "consumer",
                'shared = { path = "../other" }\n')
        app = project(root / "conflict", "app",
                      'shared = { path = "../shared" }\n'
                      'consumer = { path = "../consumer" }\n')
        check(app, error=["source conflict", "shared"])


if __name__ == "__main__":
    main()
