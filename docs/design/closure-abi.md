# Closure ABI and allocation

Function values use the following internal LLVM aggregate:

```text
{ code: ptr, environment: ptr, owns_environment: i1 }
```

`code` points to a function whose first argument is `environment`. The remaining
arguments follow the declared Janus function signature. `owns_environment`
controls deterministic cleanup: deleting the function value calls `janus_free`
only for an owned environment. This is an internal compiler ABI and is not part
of the source-language compatibility contract.

## Representations

| Source value | Environment | Ownership bit | Allocation |
| --- | --- | --- | --- |
| Direct call to a named function | none | n/a | none |
| Lambda without captures | null | false | none |
| Capturing lambda passed directly to a `scoped` parameter | caller stack | false | one `alloca` |
| Capturing lambda that may escape | `janus_alloc` storage | true | one heap allocation |

The semantic analyzer is conservative. A function parameter marked `scoped`
cannot be external and promises that the callback is neither stored nor
returned. The promise is checked in the receiver body and follows moved aliases
and capturing closures. Returning or storing the value, or forwarding it to a
non-`scoped` parameter, is rejected with `JANA0026`; forwarding to another
verified `scoped` parameter remains valid. Borrow-capturing lambdas are accepted
only in such a bounded context.
The stack optimization currently applies to a lambda literal passed directly as
that argument. Other expressions retain the owned representation.

The ownership bit keeps cleanup uniform. A callee may use `delete` or
`defer delete` on a `scoped` callback; normal return, early return, `?`, and
panic cleanup all observe the bit and leave a stack environment untouched.
Heap environments continue to be released exactly once.

## Allocation baseline

The deterministic IR checks in `language.borrowed_calls_closures` provide the
baseline below. Counts are per evaluated lambda expression and exclude unrelated
program allocations.

| Case | Before | After |
| --- | ---: | ---: |
| Lambda without captures | 0 heap allocations | 0 heap allocations |
| Capturing lambda passed directly to `scoped` | 1 heap allocation | 0 heap allocations, 1 stack slot |
| Escaping capturing lambda | 1 heap allocation | 1 heap allocation |

Reproduce the checks with:

```bash
cmake --build build --target borrowed_calls_closures_test
ctest --test-dir build -R language.borrowed_calls_closures --output-on-failure
```

Wall-clock closure and iterator pipeline measurements remain informational: the
allocator-call count above is the stable regression signal, while LLVM may
inline or eliminate the stack slot in optimized builds.
