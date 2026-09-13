from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[2] / 'scripts/prototype_total_functions.py'
spec = importlib.util.spec_from_file_location('total_functions_prototype', SCRIPT)
p = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = p
spec.loader.exec_module(p)


class TotalFunctionsTests(unittest.TestCase):
    def test_nontrivial_proofs_and_five_required_counterexamples(self):
        result = p.analyze(p.corpus())
        self.assertTrue({'countdown', 'list.walk', 'bounded', 'safe.get', 'safe.divide',
                         'choose', 'option.isSome', 'result.isOk'} <= set(result['accepted']))
        for name in ('zero.divide', 'unsafe.index', 'panic.wrapper', 'infinite',
                     'mutual.a', 'mutual.b'):
            self.assertIn(name, result['diagnostics'])
        self.assertEqual(['panic.wrapper:return -> panic.leaf',
                          'panic.leaf:return: unproved operation panic'],
                         result['diagnostics']['panic.wrapper'])

    def test_proofs_require_guard_strict_subterm_and_all_branches(self):
        graph = p.corpus()
        graph.update({
            'bad.branch': p.Function(('if_positive', p.ZERO, ('call', 'bad.branch', ('dec',))), 'int'),
            'bad.match': p.Function(('match', {'Some': p.TRUE}), 'option'),
            'ordinary': p.Function(p.X, annotated=False),
            'caller': p.Function(('call', 'ordinary', p.X)),
            'bad.bound': p.Function(('bounded', -1, p.ZERO)),
            'overflow': p.Function(('add', p.X, ('literal', 1))),
        })
        result = p.analyze(graph)
        for name in ('bad.branch', 'bad.match', 'caller', 'bad.bound', 'overflow',
                     'unguarded', 'same.list', 'dead.panic', 'missing.return',
                     'unknown.call', 'cleanup.wrapper', 'ffi'):
            self.assertIn(name, result['diagnostics'])

    def test_no_panic_is_not_termination(self):
        graph = p.corpus()
        accepted = set(p.analyze(graph, 'no_panic')['accepted'])
        self.assertTrue({'infinite', 'mutual.a', 'mutual.b', 'same.list'} <= accepted)
        self.assertNotIn('panic.wrapper', accepted)
        self.assertNotIn('zero.divide', accepted)

    def test_generic_callback_substitution_and_cleanup(self):
        graph = p.corpus()
        accepted = p.analyze(graph)['accepted']
        for typ in ('int', 'string'):
            self.assertIn(f'apply[{typ},identity]', accepted)
            self.assertNotIn(f'apply[{typ},panic.leaf]', accepted)
        graph['identity'] = p.Function(('call', 'panic.leaf', p.X))
        after = p.analyze(graph)
        self.assertNotIn('apply[int,identity]', after['accepted'])
        self.assertEqual(3, len(after['diagnostics']['apply[int,identity]']))

    def test_large_cycles_have_bounded_deterministic_diagnostics(self):
        graph = {str(i): p.Function(('call', str((i + 1) % 1500), p.X))
                 for i in range(1500)}
        result = p.analyze(graph)
        self.assertEqual([], result['accepted'])
        self.assertTrue(all(len(path) == 1 for path in result['diagnostics'].values()))
        self.assertEqual(result, p.analyze(dict(reversed(list(graph.items())))))
        self.assertEqual(1500, len(p.analyze(graph, 'no_panic')['accepted']))

    def test_sccs_against_independent_reachability_oracle(self):
        import random
        rng = random.Random(317)
        for _ in range(20):
            edges = {str(i): rng.sample([str(j) for j in range(15)], rng.randrange(4))
                     for i in range(15)}
            reach = {}
            for n in edges:
                seen, pending = set(), [n]
                while pending:
                    v = pending.pop()
                    if v not in seen:
                        seen.add(v)
                        pending.extend(edges[v])
                reach[n] = seen
            actual = p.components(edges)
            for n in edges:
                self.assertEqual(tuple(sorted(v for v in edges if v in reach[n] and n in reach[v])), actual[n])

    def test_stdlib_measurements_are_explicit_and_reproducible(self):
        report = p.report(1)
        sample = report['stdlib_manual_sample']
        self.assertEqual(['math.fabs', 'math.gcd'], sample['false_negatives'])
        self.assertEqual([], sample['false_positives'])
        self.assertEqual(6, len(sample['accepted']))
        self.assertEqual(4, len(report['source_sha256']))
        # Source drift requires re-auditing hand-lowered examples and measurements.
        import json
        saved = json.loads((SCRIPT.parents[1] / 'docs/design/total-functions-measurements.json').read_text())
        self.assertEqual(saved['source_sha256'], report['source_sha256'])
        self.assertEqual(saved['stdlib_manual_sample'], sample)

    def test_source_fingerprints_ignore_checkout_line_endings(self):
        expected = p.report(1)['source_sha256']
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for relative in expected:
                target = root / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                text = (SCRIPT.parents[1] / relative).read_text(encoding='utf-8')
                target.write_bytes(text.replace('\n', '\r\n').encode('utf-8'))
            with patch.object(p, '__file__', str(root / 'scripts/prototype_total_functions.py')):
                self.assertEqual(expected, p.report(1)['source_sha256'])


if __name__ == '__main__':
    unittest.main()
