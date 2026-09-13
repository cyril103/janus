"""Compare 32 retained insertions into 1024 associations; no timing threshold."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = args.build_dir.resolve()
    output = build / "persistent_map_benchmark"
    output.mkdir(parents=True, exist_ok=True)
    llvm = output / "program.ll"
    with llvm.open("w") as stream:
        subprocess.run([str(build / "janusc"), str(root / "benchmarks/persistent_map.janus")],
                       stdout=stream, check=True)
    binary = output / "program"
    subprocess.run([args.clang, "-O3", str(llvm),
                    str(root / "tests/runtime/persistent_vector_allocations.c"),
                    str(build / "libjanus_runtime.a"),
                    "-Wl,--wrap=janus_alloc,--wrap=janus_realloc,--wrap=janus_free,--wrap=abort",
                    "-o", str(binary)], check=True)
    measurements = []
    for mode, name in enumerate(("Array[Pair]", "HashMap", "PersistentMap", "HashSet", "PersistentSet")):
        samples = []
        for _ in range(args.repeats):
            start = time.perf_counter()
            rss_file = output / f"rss-{mode}.txt"
            result = subprocess.run(["/usr/bin/time", "-f", "%M", "-o", str(rss_file), str(binary)], env={**os.environ, "VECTOR_MODE": str(mode)},
                                    text=True, capture_output=True, check=True)
            samples.append(time.perf_counter() - start)
            match = re.fullmatch(r"523776\n32\nallocations=(\d+) requested_bytes=(\d+) "
                                 r"peak_live_bytes=(\d+) live_bytes=0\n", result.stdout)
            if not match or result.stderr:
                raise RuntimeError(result.stdout + result.stderr)
        record = dict(zip(("allocations", "requested_bytes", "peak_live_bytes"),
                          map(int, match.groups())))
        record.update(rss_kib=int(rss_file.read_text().strip()), version_entries=1025, collection=name, seconds_min=min(samples), seconds_samples=samples)
        measurements.append(record)
    report = {"elements": 1024, "retained_edits": 32, "measurements": measurements}
    (output / "measurements.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
