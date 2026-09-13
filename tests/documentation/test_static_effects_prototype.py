from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / 'scripts/prototype_static_effects.py'
spec = importlib.util.spec_from_file_location('static_effects_prototype', SCRIPT)
p = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = p
spec.loader.exec_module(p)


class StaticEffectsTests(unittest.TestCase):
    def test_generic_callbacks_remain_independent(self):
        effects, _ = p.solve(p.corpus())
        self.assertEqual((), effects['pipeline[int]/relay<identity[int]>'])
        self.assertEqual(('ffi', 'io', 'panic'), effects['pipeline[int]/relay<fs.readFile>'])
        self.assertEqual(('panic',), effects['pipeline[int]/relay<time.durationSince>'])
        self.assertLessEqual(set(effects['time.durationSince']), p.PURE_ALLOWED)
        self.assertFalse(set(effects['fs.readFile']) <= p.PURE_ALLOWED)

    def test_cycles_converge_independent_of_declaration_order(self):
        graph = p.corpus()
        expected, _ = p.solve(graph)
        actual, _ = p.solve(dict(reversed(list(graph.items()))))
        self.assertEqual(expected, actual)
        self.assertEqual(('clock', 'ffi'), actual['cycle.a'])
        self.assertEqual(actual['cycle.a'], actual['cycle.b'])
        self.assertEqual((), actual['pureCycle.a'])

    def test_diagnostic_reaches_source_through_callback_and_cleanup(self):
        graph = p.corpus()
        self.assertEqual(['pipeline[int]/relay<fs.readFile>',
                          'pipeline[int]/apply<fs.readFile>', 'fs.readFile',
                          'fs.nativeRead'],
                         p.witness(graph, 'pipeline[int]/relay<fs.readFile>', 'io'))
        self.assertEqual(['scope.exit', 'cleanup', 'fs.nativeRead'],
                         p.witness(graph, 'scope.exit', 'io'))
        self.assertEqual([], p.witness(graph, 'pureCycle.a', 'io'))
        self.assertEqual(['cycle.a', 'cycle.b', 'time.now'],
                         p.witness(graph, 'cycle.a', 'clock'))

    def test_canonical_public_keys_and_transitive_invalidation(self):
        self.assertEqual(p.fingerprint('f', ['io', 'panic', 'io']),
                         p.fingerprint('f', ['panic', 'io']))
        graph = p.corpus()
        before, _ = p.solve(graph)
        graph['identity[int]'] = p.Node(('io',))
        after, _ = p.solve(graph)
        changed = {name for name in graph if p.fingerprint(name, before[name]) !=
                   p.fingerprint(name, after[name])}
        self.assertEqual({'identity[int]', 'pipeline[int]/apply<identity[int]>',
                          'pipeline[int]/relay<identity[int]>'}, changed)

    def test_unknown_calls_never_silently_become_pure(self):
        with self.assertRaisesRegex(ValueError, 'missing summary'):
            p.solve({'f': p.Node(calls=('external',))})
        with self.assertRaisesRegex(ValueError, 'unknown effects'):
            p.canonical(['typo'])

    def test_solver_matches_independent_reachability_oracle(self):
        # Cross edges, self loops and SCCs; an independent oracle unions all
        # reachable local sources instead of sharing the solver worklist.
        import random
        rng = random.Random(316)
        for _ in range(20):
            names = [str(i) for i in range(20)]
            graph = {name: p.Node(tuple(rng.sample(sorted(p.ATOMS), 2)),
                                  tuple(rng.sample(names, 3))) for name in names}
            actual, _ = p.solve(graph)
            for start in names:
                seen, pending, effects = set(), [start], set()
                while pending:
                    name = pending.pop()
                    if name in seen:
                        continue
                    seen.add(name)
                    effects.update(graph[name].local)
                    pending.extend(graph[name].calls)
                self.assertEqual(tuple(sorted(effects)), actual[start])


if __name__ == '__main__':
    unittest.main()
