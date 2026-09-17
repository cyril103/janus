"""Exercise the CLI with isolated fake compilers, benchmarks and RSS collectors."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / 'benchmarks/run_persistent_map.py'
GOOD_OUTPUT = '523776\n32\nallocations=7 requested_bytes=128 peak_live_bytes=64 live_bytes=0\n'


class HarnessTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='map harness ')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.env = {**os.environ, 'PATH': str(self.root)}
        self.env.pop('JANUS_GNU_TIME', None)
        self.executable('janusc', "print('; fake LLVM')")
        self.executable('clang', '''
import pathlib, shutil, sys
shutil.copyfile(pathlib.Path(__file__).with_name('benchmark'), sys.argv[-1])
pathlib.Path(sys.argv[-1]).chmod(0o755)
''')
        self.benchmark()

    def executable(self, name, code):
        path = self.root / name
        path.write_text(f'#!{sys.executable}\n' + code + '\n')
        path.chmod(0o755)
        return path

    def benchmark(self, output=GOOD_OUTPUT, status=0):
        self.executable('benchmark', f'import sys\nsys.stdout.write({output!r})\nsys.exit({status})')

    def collector(self, name='time', failure=None):
        return self.executable(name, f'''
import os, pathlib, subprocess, sys
args = sys.argv
is_benchmark = 'VECTOR_MODE' in os.environ
if is_benchmark and {failure == 'exit'!r}:
    sys.exit(9)
result = subprocess.run(args[5:])
if not (is_benchmark and {failure == 'missing'!r}):
    pathlib.Path(args[4]).write_text('bad' if is_benchmark and {failure == 'invalid'!r} else '2048\\n')
sys.exit(result.returncode)
''')

    def run_harness(self, *options, success=True):
        result = subprocess.run(
            [sys.executable, str(SCRIPT), '--build-dir', str(self.root),
             '--clang', str(self.root / 'clang'), '--repeats', '2', *options],
            env=self.env, capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, success, result.stderr)
        self.assertNotIn('Traceback', result.stderr)
        return result

    def check_report(self, result, available):
        report = json.loads(result.stdout)
        self.assertEqual(len(report['measurements']), 5)
        self.assertEqual(report, json.loads((self.root / 'persistent_map_benchmark/measurements.json').read_text()))
        for row in report['measurements']:
            self.assertEqual(row['rss_kib'], 2048 if available else None)
            self.assertEqual(row['rss_status'], 'available' if available else 'unavailable')
            self.assertEqual(len(row['seconds_samples']), 2)
            self.assertEqual(row['allocations'], 7)

    def test_available(self):
        self.collector()
        self.check_report(self.run_harness('--require-rss'), True)

    def test_absent(self):
        result = self.run_harness()
        self.check_report(result, False)
        self.assertIn('RSS unavailable', result.stderr)

    def test_strict_absent_before_compilation(self):
        (self.root / 'janusc').unlink()
        result = self.run_harness('--require-rss', success=False)
        self.assertIn('sudo apt-get install time', result.stderr)
        self.assertIn('--gnu-time', result.stderr)
        self.assertFalse((self.root / 'persistent_map_benchmark').exists())

    def test_incompatible(self):
        self.executable('time', 'import sys; sys.exit(2)')
        self.check_report(self.run_harness(), False)
        self.run_harness('--require-rss', success=False)

    def test_probe_requires_measurement_and_child_output(self):
        for body in ["pass", "import pathlib, sys; pathlib.Path(sys.argv[4]).write_text('12')"]:
            with self.subTest(body=body):
                self.executable('time', body)
                self.run_harness('--require-rss', success=False)

    def test_explicit_path_with_spaces_and_precedence(self):
        collector = self.collector('GNU time with spaces')
        self.env['JANUS_GNU_TIME'] = str(self.root / 'absent')
        self.check_report(self.run_harness('--gnu-time', str(collector), '--require-rss'), True)
        self.env['JANUS_GNU_TIME'] = str(collector)
        self.check_report(self.run_harness('--require-rss'), True)
        self.collector()
        self.run_harness('--gnu-time', str(self.root / 'absent'), '--require-rss', success=False)

    def test_gtime_search(self):
        self.executable('gtime', 'import sys; sys.exit(2)')
        self.collector()
        self.check_report(self.run_harness('--require-rss'), True)

    def test_collector_failure_never_falls_back(self):
        for failure in ['exit', 'missing', 'invalid']:
            with self.subTest(failure=failure):
                self.collector(failure=failure)
                self.run_harness(success=False)
                self.assertFalse((self.root / 'persistent_map_benchmark/measurements.json').exists())

    def test_benchmark_failure_with_and_without_collector(self):
        for available in [False, True]:
            if available:
                self.collector()
            for output, status in [(GOOD_OUTPUT, 4), (GOOD_OUTPUT.replace('live_bytes=0', 'live_bytes=1'), 0), ('wrong\n', 0)]:
                with self.subTest(available=available, output=output, status=status):
                    self.benchmark(output, status)
                    self.run_harness(success=False)

    def test_nonpositive_repeats(self):
        self.run_harness('--repeats', '0', success=False)

    def test_stderr_fails_and_removes_previous_report(self):
        self.run_harness()
        self.executable('benchmark',
                        f'import sys\nsys.stdout.write({GOOD_OUTPUT!r})\n'
                        "sys.stderr.write('unexpected diagnostic')")
        self.run_harness(success=False)
        self.assertFalse((self.root / 'persistent_map_benchmark/measurements.json').exists())


if __name__ == '__main__':
    unittest.main()
