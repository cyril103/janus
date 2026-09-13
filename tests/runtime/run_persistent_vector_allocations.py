"""Sweep every explicit Shared storage/reallocation failure, under ASan/UBSan."""
import argparse
import os
from pathlib import Path
import re
import subprocess


def run(command, **kwargs):
    return subprocess.run(command, text=True, capture_output=True, **kwargs)


def main():
    parser = argparse.ArgumentParser()
    for name in ("janusc", "clang", "source", "runtime", "shim", "output"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    compilation = run([args.janusc, args.source])
    if compilation.returncode:
        raise RuntimeError(compilation.stderr)
    # Mark explicit alloc[T] expressions only: new class/closure allocations
    # currently have the runtime's fatal OOM contract, not a Janus panic.
    ir, sites = re.subn(r"(%alloc\d* = call ptr )@janus_alloc",
                       r"\1@vector_storage_alloc", compilation.stdout)
    if not sites:
        raise RuntimeError("no explicit storage allocation sites instrumented")
    ir, node_sites = re.subn(
        r"(  %PersistentVectorNode\.new\d* = call ptr @janus_alloc)",
        r"  call void @vector_node_created()\n\1", ir)
    if not node_sites:
        raise RuntimeError("no tree node allocation sites instrumented")
    llvm = output / "program.ll"
    llvm.write_text(ir + "\ndeclare ptr @vector_storage_alloc(i64)\ndeclare void @vector_node_created()\n")
    executable = output / "program"
    link = run([args.clang, "-fsanitize=address,undefined", str(llvm), args.shim,
                args.runtime, "-Wl,--wrap=janus_alloc,--wrap=janus_realloc,"
                "--wrap=janus_free,--wrap=abort", "-o", str(executable)])
    if link.returncode:
        raise RuntimeError(link.stderr)
    total = 0
    for mode in range(9):
        env = {**os.environ, "VECTOR_MODE": str(mode), "VECTOR_FAIL_AT": "0",
               "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1"}
        normal = run([str(executable)], env=env)
        match = re.fullmatch(r"cleanup ok eligible=(\d+) nodes=(\d+)\n", normal.stdout)
        if normal.returncode or not match or normal.stderr:
            raise RuntimeError(f"mode {mode}: {normal.stdout}\n{normal.stderr}")
        expected_nodes = (4, 2, 2, 2, 4, 3, 0, 0, 3)[mode]
        if int(match[2]) != expected_nodes:
            raise RuntimeError(f"mode {mode}: expected {expected_nodes} new tree nodes, got {match[2]}")
        count = int(match[1])
        for failure in range(1, count + 1):
            env["VECTOR_FAIL_AT"] = str(failure)
            result = run([str(executable)], env=env)
            if (result.returncode != 134 or "cleanup ok" not in result.stdout
                    or "allocation failed" not in result.stderr
                    or "Sanitizer" in result.stderr or "runtime error:" in result.stderr):
                raise RuntimeError(f"mode {mode}, failure {failure}: "
                                   f"{result.returncode}\n{result.stdout}\n{result.stderr}")
        total += count
        print(f"mode {mode}: {count} allocation failures cleaned completely")
    print(f"{total} failure points passed")


if __name__ == "__main__":
    main()
