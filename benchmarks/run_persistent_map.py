"""Compare 32 retained insertions into 1024 associations; no timing threshold."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time


def read_rss(path):
    value = path.read_text().strip()
    if not re.fullmatch(r"[0-9]+", value):
        raise ValueError(f"invalid RSS measurement: {value!r}")
    return int(value)


def discover_time(configured):
    # An explicit override is authoritative, including when it is unavailable.
    candidates = [configured] if configured else ["gtime", "time"]
    failures = []
    for candidate in candidates:
        executable = shutil.which(candidate)
        if executable is None:
            failures.append(f"{candidate!r}: executable not found")
            continue
        try:
            with tempfile.TemporaryDirectory(prefix="janus-rss-") as directory:
                rss = Path(directory) / "rss.txt"
                result = subprocess.run(
                    [executable, "-f", "%M", "-o", str(rss),
                     sys.executable, "-c", "print('janus-rss-probe')"],
                    text=True, capture_output=True, check=True, timeout=10)
                read_rss(rss)
                if result.stdout != "janus-rss-probe\n" or result.stderr:
                    raise ValueError("collector changed child output")
            return executable, None
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            failures.append(f"{candidate!r}: incompatible collector ({error})")
    return None, "; ".join(failures)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--gnu-time", default=os.environ.get("JANUS_GNU_TIME"),
                        help="GNU time executable path (or JANUS_GNU_TIME); otherwise search PATH")
    parser.add_argument("--require-rss", action="store_true",
                        help="fail before compilation if GNU time is unavailable")
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error("--repeats must be positive")
    collector, reason = discover_time(args.gnu_time)
    if collector is None:
        diagnostic = (f"RSS unavailable: {reason}. Install GNU time "
                      "(Debian/Ubuntu: sudo apt-get install time) or configure "
                      "--gnu-time '/path/to/time' / JANUS_GNU_TIME.")
        if args.require_rss:
            parser.error(diagnostic)
        print(diagnostic, file=sys.stderr)
    root = Path(__file__).resolve().parents[1]
    build = args.build_dir.resolve()
    output = build / "persistent_map_benchmark"
    output.mkdir(parents=True, exist_ok=True)
    # Do not leave a previous successful report behind after a failed run.
    (output / "measurements.json").unlink(missing_ok=True)
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
            rss_file.unlink(missing_ok=True)
            command = ([collector, "-f", "%M", "-o", str(rss_file)]
                       if collector else []) + [str(binary)]
            result = subprocess.run(command, env={**os.environ, "VECTOR_MODE": str(mode)},
                                    text=True, capture_output=True, check=True)
            samples.append(time.perf_counter() - start)
            match = re.fullmatch(r"523776\n32\nallocations=(\d+) requested_bytes=(\d+) "
                                 r"peak_live_bytes=(\d+) live_bytes=0\n", result.stdout)
            if not match or result.stderr:
                raise RuntimeError(f"{name}: unexpected benchmark output: " + result.stdout + result.stderr)
            rss_kib = read_rss(rss_file) if collector else None
        record = dict(zip(("allocations", "requested_bytes", "peak_live_bytes"),
                          map(int, match.groups())))
        record.update(rss_kib=rss_kib, rss_status="available" if collector else "unavailable",
                      version_entries=1025, collection=name, seconds_min=min(samples), seconds_samples=samples)
        measurements.append(record)
    report = {"elements": 1024, "retained_edits": 32, "measurements": measurements}
    (output / "measurements.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"persistent_map benchmark failed: {error}", file=sys.stderr)
        sys.exit(1)
