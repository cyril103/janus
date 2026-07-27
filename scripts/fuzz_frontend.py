#!/usr/bin/env python3
"""Run a time-bounded lexer or parser mutation campaign against janus."""

from __future__ import annotations

import argparse
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


def lexer_input(randomizer: random.Random) -> bytes:
    size = randomizer.randint(0, 512)
    return bytes(randomizer.choice(LEXER_ALPHABET) for _ in range(size))


def parser_input(randomizer: random.Random) -> bytes:
    count = randomizer.randint(0, 160)
    return " ".join(randomizer.choice(PARSER_TOKENS) for _ in range(count)).encode()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--janus", required=True, type=pathlib.Path)
    parser.add_argument("--mode", required=True, choices=("lexer", "parser"))
    parser.add_argument("--duration", type=int, default=900)
    parser.add_argument("--seed", type=int, default=0x5A17)
    args = parser.parse_args()

    randomizer = random.Random(args.seed)
    generate = lexer_input if args.mode == "lexer" else parser_input
    deadline = time.monotonic() + args.duration
    cases = 0

    with tempfile.TemporaryDirectory(prefix=f"janus-{args.mode}-fuzz-") as tmp:
        source = pathlib.Path(tmp) / "fuzz.janus"
        while time.monotonic() < deadline:
            payload = generate(randomizer)
            source.write_bytes(payload)
            try:
                result = subprocess.run(
                    [str(args.janus), "check", str(source)],
                    capture_output=True,
                    timeout=5,
                    check=False,
                )
            except subprocess.TimeoutExpired as error:
                raise RuntimeError(
                    f"reproducible timeout in {args.mode} case {cases}: "
                    f"{payload.hex()}"
                ) from error
            combined = result.stdout + result.stderr
            crashed = result.returncode < 0 or any(
                marker in combined
                for marker in (
                    b"AddressSanitizer",
                    b"UndefinedBehaviorSanitizer",
                    b"terminate called",
                    b"Segmentation fault",
                )
            )
            if crashed:
                raise RuntimeError(
                    f"reproducible crash in {args.mode} case {cases}: "
                    f"{payload.hex()}"
                )
            cases += 1

    print(f"{args.mode}: {cases} cases in {args.duration} seconds")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
