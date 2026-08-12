# Benchmarks

This directory contains deterministic runtime microbenchmarks for local
comparison and canonical small/medium compiler workloads under `compilation/`.
Wall-clock times are informational only and are not CI gates. See
[`docs/compiler-performance.md`](../docs/compiler-performance.md) for the
compiler timing workflow and its non-blocking trend dashboard.

## `array_sort.janus`

This benchmark sorts 100,000 deterministic pseudo-random integers through
`Array.sortWith`. It verifies both ordering and a checksum, so the sort remains
observable. It also sorts the same 2,048 reverse-ordered integers with
`Array.sortWith` and a deliberately naive insertion sort, counts comparator
calls, and requires the hybrid sort to use fewer comparisons. Comparator calls
are a deterministic, machine-independent proxy for the algorithmic improvement;
wall-clock time remains informational and is not a noisy CI gate. The runtime
test `benchmarks.array_sort_smoke` locks the combined result to `true`; local
timings can use the same optimized build and `/usr/bin/time` workflow as the
benchmarks below.

## `prime_factors.janus`

This benchmark exercises `std.math.prime_factors` through the public API over
fixed representative workloads: `0`, `1`, a small prime, small composites with
multiplicity, a power of two, a medium prime, `2147483647`, and the canonical
Euler 3 value `usize(600851475143.0)`. Every returned array is deleted. The
program prints a checksum so the factorization work is observable.

Expected checksum:

```text
17619440360357435768
```

Build and run with the Janus driver in optimized mode:

```bash
cmake --build build --target janus janus_runtime
build/janus build benchmarks/prime_factors.janus --release \
  -o /tmp/janus-prime-factors-bench
/tmp/janus-prime-factors-bench
```

For repeatable local timing, use an external timer and keep the checksum check
separate from the timing result:

```bash
/usr/bin/time -p /tmp/janus-prime-factors-bench
```

To exercise the same lower-level compiler/runtime artifacts with optimization enabled:

```bash
cmake --build build --target janusc janus_runtime
build/janusc benchmarks/prime_factors.janus > /tmp/janus-prime-factors-bench.ll
clang -O3 /tmp/janus-prime-factors-bench.ll \
  build/libjanus_runtime.a \
  -o /tmp/janus-prime-factors-bench
/tmp/janus-prime-factors-bench
```
