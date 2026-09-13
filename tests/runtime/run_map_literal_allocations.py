"""Check every allocation failure during map literal construction under ASan/UBSan."""
import argparse
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser()
    for name in ("janusc", "clang", "source", "runtime", "shim", "output"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    compilation = subprocess.run([args.janusc, args.source], capture_output=True, text=True, check=True)
    llvm = output / "program.ll"
    llvm.write_text(compilation.stdout)
    executable = output / "program"
    subprocess.run([args.clang, "-fsanitize=address,undefined", str(llvm), args.shim,
                    args.runtime, "-Wl,--wrap=janus_alloc,--wrap=janus_realloc,"
                    "--wrap=janus_free,--wrap=abort", "-o", str(executable)], check=True)
    env = {**os.environ, "MAP_FAIL_AT": "0", "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1"}
    normal = subprocess.run([str(executable)], capture_output=True, text=True, env=env)
    match = re.fullmatch(r"cleanup ok attempts=(\d+)\n", normal.stdout)
    if normal.returncode or not match or normal.stderr:
        raise RuntimeError(f"normal construction: {normal.stdout}\n{normal.stderr}")
    count = int(match[1])
    for failure in range(1, count + 1):
        env["MAP_FAIL_AT"] = str(failure)
        result = subprocess.run([str(executable)], capture_output=True, text=True, env=env)
        # An unused initial storage reservation can fail without preventing construction.
        if (result.returncode not in (0, 134) or "cleanup ok" not in result.stdout
                or (result.returncode == 134 and "allocation failed" not in result.stderr)
                or "Sanitizer" in result.stderr or "runtime error:" in result.stderr):
            raise RuntimeError(f"allocation {failure}: {result.returncode}\n{result.stdout}\n{result.stderr}")
    print(f"{count} allocation failure points cleaned completely")


if __name__ == "__main__":
    main()
