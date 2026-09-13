# Closure ABI and allocation

Function values use the following internal LLVM aggregate:

```text
{ code: ptr, environment: ptr, owns_environment: i1, drop_environment: ptr }
```

`code` points to a function whose first argument is `environment`. The remaining
arguments follow the declared Janus function signature. `owns_environment`
controls deterministic cleanup: deleting the function value first calls its
capture destructor, when present, then calls `janus_free` only for an owned
environment. `owningCapture` installs the destructor for its explicit owner.
Moving or destroying a capture clears its storage before transfer; pointer
leaves use null as the disarmed representation. Aggregate cleanup recursively
ignores these cleared leaves. This is an internal compiler ABI and is not part
of the source-language compatibility contract.

The [call-capabilities RFC](call-capabilities.md) defines the consuming-call
and owned-capture contract for issue #312.

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

## Call capabilities

Every function type explicitly declares `Fn`, `FnMut` or `FnOnce`.
`Fn` borrows its environment, `FnMut` requires exclusive access and `FnOnce`
consumes the closure. A consuming call transfers the closure into a temporary
cleanup scope after evaluating its arguments. Both normal return and panic
run this cleanup. The old closure slot is cleared, so a previously registered
`defer delete` cannot destroy it again. The analyzed call contract also applies
to generic bounds, even when the concrete callback has a stronger capability.

This is closure ABI version 2. Cached objects using ABI version 1 must be
rebuilt; public API indexes use format version 2.
