#!/usr/bin/env python3
"""Conservative, isolated totality experiment for #317 on hand-lowered typed IR.

This is not a Janus parser, type checker or a certificate for source programs.
Every function has one immutable argument `x` and an expression return body.
Unknown constructs fail closed. No compiler, ABI or optimization changes.
"""
from __future__ import annotations

import argparse
from collections import deque
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import platform
import statistics
import time


@dataclass(frozen=True)
class Function:
    body: tuple
    kind: str = 'scalar'
    annotated: bool = True


X = ('x',)
ZERO = ('literal', 0)
TRUE = ('literal', True)
FALSE = ('literal', False)


def components(edges):
    """Iterative Kosaraju: bounded stack even for large call cycles."""
    seen, order = set(), []
    reverse = {n: [] for n in edges}
    for n, targets in edges.items():
        for target in targets:
            reverse[target].append(n)
    for root in sorted(edges):
        if root in seen:
            continue
        stack = [(root, False)]
        while stack:
            n, done = stack.pop()
            if done:
                order.append(n)
            elif n not in seen:
                seen.add(n)
                stack.append((n, True))
                stack.extend((t, False) for t in edges[n] if t not in seen)
    groups, seen = {}, set()
    for root in reversed(order):
        if root in seen:
            continue
        group, stack = [], [root]
        while stack:
            n = stack.pop()
            if n in seen:
                continue
            seen.add(n)
            group.append(n)
            stack.extend(reverse[n])
        canonical = tuple(sorted(group))
        for n in group:
            groups[n] = canonical
    return groups


def analyze(graph, contract='total'):
    if contract not in ('total', 'no_panic'):
        raise ValueError('unknown contract')
    local = {n: [] for n in graph}
    calls = {n: [] for n in graph}

    def reject(n, path, reason):
        local[n].append(f'{n}:{path}: {reason}')

    def walk(n, expr, path='return', positive=False, cons=False):
        # The IR is pretyped; no arbitrary user-supplied proof flags exist.
        if not isinstance(expr, tuple) or not expr:
            reject(n, path, 'unknown or missing return expression')
            return
        op, *args = expr
        kind = graph[n].kind
        if op == 'literal' and len(args) == 1 and type(args[0]) in (int, bool):
            if not -(2**63) <= args[0] < 2**63:
                reject(n, path, 'integer literal out of range')
            return
        if op == 'x' and not args:
            return
        if op == 'dec' and not args and kind == 'int' and positive:
            return  # x > 0 implies x - 1 is representable and nonnegative.
        if op == 'tail' and not args and kind == 'list' and cons:
            return  # Borrowed strict subterm of a finite inductive list.
        if op == 'field' and args == ['length'] and kind == 'persistent_list':
            return
        if op == 'if_positive' and len(args) == 2 and kind == 'int':
            walk(n, args[0], path + '.positive', True, cons)
            walk(n, args[1], path + '.nonpositive', False, cons)
            return
        variants = {'list': ('Nil', 'Cons'), 'option': ('None', 'Some'),
                    'result': ('Ok', 'Error')}
        if op == 'match' and len(args) == 1 and isinstance(args[0], dict):
            branches = args[0]
            if kind not in variants or set(branches) != set(variants[kind]):
                reject(n, path, 'non-exhaustive or unsupported match')
            for tag, body in sorted(branches.items()):
                walk(n, body, path + '.' + tag, positive, kind == 'list' and tag == 'Cons')
            return
        if op == 'if' and len(args) == 3:
            for i, body in enumerate(args):
                walk(n, body, f'{path}.if[{i}]', positive, cons)
            return
        if op == 'call' and len(args) == 2 and isinstance(args[0], str):
            target, arg = args
            walk(n, arg, path + '.argument', positive, cons)
            if target not in graph or not graph[target].annotated:
                reject(n, path, f'unverified callee {target}')
                return
            decreasing = (target == n and
                          ((kind == 'int' and positive and arg == ('dec',)) or
                           (kind == 'list' and cons and arg == ('tail',))))
            calls[n].append((target, path, decreasing))
            return
        if op in ('equal', 'checked_div', 'checked_add', 'checked_cast', 'get_option') and len(args) == 2:
            # Built-in IR primitives, never a whitelist of same-named Janus APIs.
            for i, body in enumerate(args):
                walk(n, body, f'{path}.{op}[{i}]', positive, cons)
            return
        if op == 'bounded' and len(args) == 2 and type(args[0]) is int and 0 <= args[0] < 2**63:
            walk(n, args[1], path + '.iteration', positive, cons)
            return
        if op == 'loop' and len(args) == 1:
            if contract == 'total':
                reject(n, path, 'loop has no proven finite bound')
            walk(n, args[0], path + '.iteration', positive, cons)
            return
        if op == 'seq' and args:
            for i, body in enumerate(args):
                walk(n, body, f'{path}.seq[{i}]', positive, cons)
            return
        reject(n, path, f'unproved operation {op}')

    for n in sorted(graph):
        walk(n, graph[n].body)
    edges = {n: sorted({c[0] for c in cs}) for n, cs in calls.items()}
    groups = components(edges)
    if contract == 'total':
        for n in sorted(graph):
            for target, path, decreasing in calls[n]:
                if groups[n] == groups[target] and (n != target or not decreasing):
                    reject(n, path, f'recursive edge {n} -> {target} has no proven decrease')
    # Propagate one shortest witness, not unbounded sets of paths through SCCs.
    reverse = {n: [] for n in graph}
    for n in sorted(graph):
        for target, path, _ in calls[n]:
            reverse[target].append((n, path))
    witness = {n: [sorted(errors)[0]] for n, errors in local.items() if errors}
    queue = deque(sorted(witness))
    while queue:
        target = queue.popleft()
        for caller, path in reverse[target]:
            if caller not in witness:
                witness[caller] = [f'{caller}:{path} -> {target}'] + witness[target]
                queue.append(caller)
    return {'accepted': sorted(set(graph) - witness.keys()),
            'diagnostics': dict(sorted(witness.items())),
            'nodes': len(graph), 'edges': sum(map(len, edges.values())),
            'recursive_components': sorted({g for n, g in groups.items()
                                            if len(g) > 1 or n in edges[n]})}


def corpus():
    graph = {
        'identity': Function(X),
        'choose': Function(('if', TRUE, X, ZERO)),
        'countdown': Function(('if_positive', ('call', 'countdown', ('dec',)), ZERO), 'int'),
        'list.walk': Function(('match', {'Nil': TRUE, 'Cons': ('call', 'list.walk', ('tail',))}), 'list'),
        'bounded': Function(('bounded', 100, ('call', 'identity', X))),
        'safe.get': Function(('get_option', X, ZERO)),
        'safe.divide': Function(('checked_div', X, ZERO)),
        'safe.cast': Function(('checked_cast', X, ZERO)),
        'zero.divide': Function(('div', X, ZERO)),
        'unsafe.index': Function(('index', X, ZERO)),
        'panic.leaf': Function(('panic',)),
        'panic.wrapper': Function(('call', 'panic.leaf', X)),
        'infinite': Function(('loop', ZERO)),
        'mutual.a': Function(('call', 'mutual.b', X)),
        'mutual.b': Function(('call', 'mutual.a', X)),
        'unguarded': Function(('call', 'unguarded', ('dec',)), 'int'),
        'same.list': Function(('match', {'Nil': TRUE, 'Cons': ('call', 'same.list', X)}), 'list'),
        'cleanup.wrapper': Function(('seq', X, ('call', 'panic.leaf', X))),
        'dead.panic': Function(('if', FALSE, ('panic',), ZERO)),
        'ffi': Function(('ffi',)),
        'missing.return': Function(()),
        'unknown.call': Function(('call', 'not_present', X)),
    }
    for kind, names, tags in (
        ('option', ('option.isSome', 'option.isNone'), ('None', 'Some')),
        ('result', ('result.isOk', 'result.isError'), ('Error', 'Ok')),
    ):
        for i, name in enumerate(names):
            graph[name] = Function(('match', dict(zip(tags, (FALSE, TRUE) if i == 0 else (TRUE, FALSE)))), kind)
    graph.update({
        'persistent_list.size': Function(('field', 'length'), 'persistent_list'),
        'persistent_list.isEmpty': Function(('equal', ('field', 'length'), ZERO), 'persistent_list'),
        # Conservatively opaque source bodies; not fabricated successful proofs.
        'math.gcd': Function(('unsupported_euclidean_loop',)),
        'math.lcm': Function(('seq', ('call', 'math.gcd', X), ('panic',))),
        'math.fabs': Function(('ffi',)),
    })
    for typ in ('int', 'string'):
        for callback in ('identity', 'panic.leaf'):
            graph[f'apply[{typ},{callback}]'] = Function(('call', callback, X))
    return graph


def measure(graph, repeats, contract):
    times = []
    for _ in range(repeats):
        start = time.perf_counter_ns()
        result = analyze(graph, contract)
        times.append((time.perf_counter_ns() - start) / 1e6)
    return {'median_ms': statistics.median(times), **result}


def report(repeats):
    graph = corpus()
    root = Path(__file__).resolve().parents[1]
    sources = ('option', 'result', 'persistent_list', 'math')
    snapshots = {f'stdlib/std/{name}.janus': hashlib.sha256(
        (root / f'stdlib/std/{name}.janus').read_text(encoding='utf-8').encode('utf-8')).hexdigest() for name in sources}
    # Manual oracle from the selected source bodies, under RFC machine model.
    # fabs is mathematically total but excluded by the uncertified FFI rule.
    total_oracle = {'option.isSome', 'option.isNone', 'result.isOk', 'result.isError',
                    'persistent_list.size', 'persistent_list.isEmpty', 'math.gcd', 'math.fabs'}
    stdlib = total_oracle | {'math.lcm'}
    results = {c: measure(graph, repeats, c) for c in ('total', 'no_panic')}
    accepted = set(results['total']['accepted'])
    generic = {n: f for n, f in graph.items() if n.startswith('apply[')}
    specialized = {'identity': graph['identity'], 'panic.leaf': graph['panic.leaf'], **generic}
    return {'python': platform.python_version(), 'platform': platform.platform(),
            'repeats': repeats, 'source_sha256': snapshots, 'contracts': results,
            'stdlib_manual_sample': {'functions': sorted(stdlib),
                'known_total': sorted(total_oracle),
                'accepted': sorted(accepted & stdlib),
                'false_negatives': sorted(total_oracle - accepted),
                'false_positives': sorted((accepted & stdlib) - total_oracle)},
            'generic_specializations': measure(specialized, repeats, 'total'),
            'stdlib_timing': {c: measure({n: graph[n] for n in stdlib}, repeats, c)
                              for c in ('total', 'no_panic')},
            'limitations': ['Hand-lowered typed IR; no source inference or compiler timing.',
                            'Manual oracle; no downstream project evaluated.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repeats', type=int, default=21)
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error('--repeats must be positive')
    print(json.dumps(report(args.repeats), indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
