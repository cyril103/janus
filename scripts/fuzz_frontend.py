#!/usr/bin/env python3
"""Run a time-bounded mutation campaign against a versioned Janus corpus."""

from __future__ import annotations

import argparse
import os
import pathlib
import random
import subprocess
import tempfile
import time


LEXER_ALPHABET = bytes(range(1, 256))
PARSER_TOKENS = (
    "def main val var return if else while for match class enum trait "
    "(){}[],:;=!?+-*/%&<> abc 0 1 \"text\" '\\n' \n"
).split(" ")
MODES = ("lexer", "parser", "manifest", "resolver")


def lexer_input(randomizer: random.Random) -> bytes:
    size = randomizer.randint(0, 512)
    return bytes(randomizer.choice(LEXER_ALPHABET) for _ in range(size))


def parser_input(randomizer: random.Random) -> bytes:
    count = randomizer.randint(0, 160)
    return " ".join(randomizer.choice(PARSER_TOKENS) for _ in range(count)).encode()


def mutate(seed: bytes, randomizer: random.Random) -> bytes:
    payload = bytearray(seed)
    for _ in range(randomizer.randint(1, 12)):
        operation = randomizer.choice(("insert", "replace", "delete"))
        if operation == "insert" or not payload:
            payload.insert(
                randomizer.randrange(len(payload) + 1),
                randomizer.randrange(1, 256),
            )
        elif operation == "replace":
            payload[randomizer.randrange(len(payload))] = randomizer.randrange(1, 256)
        else:
            del payload[randomizer.randrange(len(payload))]
    return bytes(payload[:4096])


def run_case(
    janus: pathlib.Path,
    mode: str,
    project: pathlib.Path,
    payload: bytes,
) -> subprocess.CompletedProcess[bytes]:
    source = project / "src" / "main.janus"
    manifest = project / "janus.toml"
    if mode in {"lexer", "parser"}:
        source.write_bytes(payload)
        command = [str(janus), "check", str(source)]
    elif mode == "manifest":
        manifest.write_bytes(payload)
        command = [str(janus), "check", "--offline"]
    else:
        manifest.write_bytes(
            b'[package]\nname = "fuzz-root"\nversion = "0.1.0"\n'
            b'entry = "src/main.janus"\n\n[dependencies]\n' + payload + b"\n"
        )
        command = [str(janus), "check", "--offline"]
    environment = os.environ.copy()
    environment.setdefault("ASAN_OPTIONS", "abort_on_error=1:detect_leaks=1")
    environment.setdefault("UBSAN_OPTIONS", "halt_on_error=1")
    return subprocess.run(
        command,
        cwd=project,
        env=environment,
        capture_output=True,
        timeout=5,
        check=False,
    )


def crashed(result: subprocess.CompletedProcess[bytes]) -> bool:
    combined = result.stdout + result.stderr
    return result.returncode < 0 or any(
        marker in combined
        for marker in (
            b"AddressSanitizer",
            b"UndefinedBehaviorSanitizer",
            b"runtime error:",
            b"terminate called",
            b"Segmentation fault",
        )
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--janus", required=True, type=pathlib.Path)
    parser.add_argument("--mode", required=True, choices=MODES)
    parser.add_argument("--duration", type=int, default=900)
    parser.add_argument("--seed", type=int, default=0x5A17)
    parser.add_argument(
        "--corpus",
        type=pathlib.Path,
        help="Corpus directory (defaults to tests/fuzz/corpus/<mode>)",
    )
    args = parser.parse_args()

    randomizer = random.Random(args.seed)
    root = pathlib.Path(__file__).resolve().parents[1]
    corpus = args.corpus or root / "tests" / "fuzz" / "corpus" / args.mode
    seeds = [path.read_bytes() for path in sorted(corpus.iterdir()) if path.is_file()]
    if not seeds:
        parser.error(f"empty corpus: {corpus}")
    deadline = time.monotonic() + args.duration
    cases = 0

    with tempfile.TemporaryDirectory(prefix=f"janus-{args.mode}-fuzz-") as tmp:
        project = pathlib.Path(tmp)
        (project / "src").mkdir()
        (project / "src" / "main.janus").write_text(
            "def main() : int { return 0 }\n", encoding="utf-8"
        )
        dependency = project / "deps" / "lib"
        (dependency / "src").mkdir(parents=True)
        (dependency / "src" / "lib.janus").write_text(
            "module lib\n\ndef value() : int { return 1 }\n", encoding="utf-8"
        )
        (dependency / "janus.toml").write_text(
            '[package]\nname = "lib"\nversion = "1.0.0"\n'
            'entry = "src/lib.janus"\n',
            encoding="utf-8",
        )
        while time.monotonic() < deadline:
            if cases < len(seeds):
                payload = seeds[cases]
            elif args.mode == "lexer":
                payload = lexer_input(randomizer)
            elif args.mode == "parser":
                payload = parser_input(randomizer)
            else:
                payload = mutate(randomizer.choice(seeds), randomizer)
            try:
                result = run_case(args.janus.resolve(), args.mode, project, payload)
            except subprocess.TimeoutExpired as error:
                raise RuntimeError(
                    f"reproducible timeout in {args.mode} case {cases}: "
                    f"{payload.hex()}"
                ) from error
            if crashed(result):
                repeated = run_case(args.janus.resolve(), args.mode, project, payload)
                if crashed(repeated):
                    raise RuntimeError(
                        f"reproducible crash in {args.mode} case {cases}: "
                        f"{payload.hex()}"
                    )
            cases += 1

    print(
        f"{args.mode}: {cases} cases in {args.duration} seconds "
        f"from {len(seeds)} versioned seeds"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
