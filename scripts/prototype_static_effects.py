#!/usr/bin/env python3
"""Isolated effect-summary experiment for #316; never parses or changes Janus.

The hand-modelled corpus covers real API shapes, not complete source semantics.
Unknown/native summaries must be supplied conservatively by the caller.
"""
from __future__ import annotations

import argparse
from collections import deque
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import platform
import re
import statistics
import time

ATOMS = frozenset({'io', 'clock', 'random', 'state', 'panic', 'ffi'})
PURE_ALLOWED = frozenset({'panic'})


def canonical(effects):
    result = frozenset(effects)
    if not result <= ATOMS:
        raise ValueError(f'unknown effects: {sorted(result - ATOMS)}')
    return tuple(sorted(result))


@dataclass(frozen=True)
class Node:
    local: tuple[str, ...] = ()
    calls: tuple[str, ...] = ()


def solve(graph):
    """Least fixed point by reverse-edge worklist, including recursive SCCs."""
    effects = {name: set(canonical(node.local)) for name, node in graph.items()}
    callers = {name: set() for name in graph}
    for name, node in graph.items():
        for target in node.calls:
            if target not in graph:
                raise ValueError(f'{name}: missing summary for {target}')
            callers[target].add(name)
    queue = deque(sorted(graph))
    queued = set(queue)
    visits = 0
    while queue:
        name = queue.popleft()
        queued.remove(name)
        visits += 1
        joined = set(effects[name])
        for target in graph[name].calls:
            joined.update(effects[target])
        if joined != effects[name]:
            effects[name] = joined
            for caller in sorted(callers[name]):
                if caller not in queued:
                    queued.add(caller)
                    queue.append(caller)
    return {name: canonical(value) for name, value in effects.items()}, visits


def witness(graph, start, effect):
    """Deterministic shortest call chain ending at an explicit effect source."""
    queue = deque([start])
    parents = {start: None}
    while queue:
        name = queue.popleft()
        if effect in graph[name].local:
            path = []
            while name is not None:
                path.append(name)
                name = parents[name]
            return list(reversed(path))
        for target in sorted(graph[name].calls):
            if target not in parents:
                parents[target] = name
                queue.append(target)
    return []


def instantiate(graph, name, callback):
    """Two generic callback forwarding levels, specialized by callback identity.

    Symbolic template: forall e. relay[e](callback:e) has effect e.
    Ordinary type arguments are represented in name, e.g. relay[int].
    """
    inner = f'{name}/apply<{callback}>'
    outer = f'{name}/relay<{callback}>'
    graph[inner] = Node(calls=(callback,))
    graph[outer] = Node(calls=(inner,))
    return outer


def corpus():
    # These summaries are experimental contracts, not inferred compiler facts.
    graph = {
        'math.fabs': Node(),  # trusted pure native math
        'time.durationSince': Node(('panic',)),
        'time.now': Node(('clock', 'ffi')),
        'fs.nativeRead': Node(('io', 'ffi')),
        'fs.readFile': Node(('panic',), ('fs.nativeRead',)),
        'capability.read': Node(calls=('fs.readFile',)),
        'random.nextUSize': Node(('state', 'ffi')),
        'random.automaticRandom': Node(('random', 'ffi')),
        'identity[int]': Node(),
        'cycle.a': Node(calls=('cycle.b',)),
        'cycle.b': Node(calls=('cycle.a', 'time.now')),
        'pureCycle.a': Node(calls=('pureCycle.b',)),
        'pureCycle.b': Node(calls=('pureCycle.a',)),
        'cleanup': Node(calls=('fs.nativeRead',)),
        'scope.exit': Node(calls=('cleanup',)),
    }
    for callback in ('identity[int]', 'fs.readFile', 'time.durationSince'):
        instantiate(graph, 'pipeline[int]', callback)
    return graph


def encode(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':')).encode()


def api(name, effects):
    # Type signature/ownership are existing inputs, preserved outside this model.
    return {'name': name, 'effects': list(canonical(effects)), 'effect_schema': 1}


def fingerprint(name, effects):
    return hashlib.sha256(encode(api(name, effects))).hexdigest()


def measure(graph, repeats):
    times = []
    for _ in range(repeats):
        start = time.perf_counter_ns()
        summaries, visits = solve(graph)
        times.append((time.perf_counter_ns() - start) / 1e6)
    start = time.perf_counter_ns()
    records = [api(name, summaries[name]) for name in sorted(graph)]
    hashes = [fingerprint(name, summaries[name]) for name in sorted(graph)]
    displays = [f"{r['name']} !{{{','.join(r['effects'])}}}" for r in records]
    tooling_ms = (time.perf_counter_ns() - start) / 1e6
    baseline = encode([{'name': name} for name in sorted(graph)])
    return {'nodes': len(graph), 'edges': sum(len(n.calls) for n in graph.values()),
            'worklist_visits': visits, 'solve_median_ms': statistics.median(times),
            'api_bytes': len(encode(records)), 'baseline_api_bytes': len(baseline),
            'effect_api_delta_bytes': len(encode(records)) - len(baseline),
            'digest_bytes': len(hashes) * 32, 'display_bytes': len(encode(displays)),
            'tooling_ms': tooling_ms}


def report(repeats=21):
    graph = corpus()
    summaries, _ = solve(graph)
    changed = dict(graph)
    changed['identity[int]'] = Node(('io',))
    after, _ = solve(changed)
    invalidated = [name for name in sorted(graph)
                   if fingerprint(name, summaries[name]) != fingerprint(name, after[name])]
    scaled = {f'{i}/{name}': Node(node.local, tuple(f'{i}/{c}' for c in node.calls))
              for i in range(100) for name, node in graph.items()}
    root = Path(__file__).resolve().parents[1]
    sources = [root / p for p in ('stdlib/std/fs.janus', 'stdlib/std/time.janus',
                                 'stdlib/std/random.janus', 'stdlib/std/math.janus')]
    inventory = {}
    for path in sources:
        content = path.read_text()
        declarations = re.findall(r'^\s*(?:private\s+)?(?:pure\s+)?(?:borrow\s+)?(?:extern(?:\([^\n]*?\))?\s+)?def\b[^\n]*', content, re.M)
        inventory[str(path.relative_to(root))] = {
            'declarations': len(declarations),
            'pure_declarations': sum('pure ' in d for d in declarations)}
    return {'python': platform.python_version(), 'platform': platform.platform(),
            'repeats': repeats, 'corpus': measure(graph, repeats),
            'synthetic_100_copies': measure(scaled, repeats),
            'summaries': summaries, 'io_witness': witness(graph, 'pipeline[int]/relay<fs.readFile>', 'io'),
            'invalidated_after_identity_adds_io': invalidated,
            'migration_lexical_inventory': inventory}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repeats', type=int, default=21)
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error('--repeats must be positive')
    print(json.dumps(report(args.repeats), indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
