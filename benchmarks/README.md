# Benchmarks

Benchmarks are deterministic microbenchmarks for local comparison. Wall-clock
times are informational only and are not CI gates.

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
