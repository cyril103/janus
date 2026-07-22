# Project Euler fixtures

This corpus covers canonical Project Euler answers for problems 1 through 20.

The `smoke.txt` profile is the stable CI gate. It compiles, builds, and runs
twenty tiny Janus programs through `scripts/verify-janus-euler-suite.sh`; each
program prints one canonical answer and exits. CTest runs this profile with a
short per-problem timeout so compiler/runtime regressions are caught without
making CI depend on long algorithms.

The `production.txt` profile is for manual algorithm checks. It contains one
source per problem and uses direct Janus algorithms over the canonical inputs.

Commands:

```sh
scripts/verify-janus-euler-suite.sh \
  --project tests/fixtures/project-euler \
  --config tests/fixtures/project-euler/smoke.txt \
  --janus /path/to/janus \
  --all \
  --timeout 5 \
  --global-timeout 120 \
  --artifacts-dir /tmp/janus-euler-smoke-artifacts

scripts/verify-janus-euler-suite.sh \
  --project tests/fixtures/project-euler \
  --config tests/fixtures/project-euler/production.txt \
  --janus /path/to/janus \
  --all \
  --timeout 60 \
  --global-timeout 900 \
  --artifacts-dir /tmp/janus-euler-production-artifacts
```

Each artifact run writes `latest`, `index.tsv`, `report.json`, and per-problem
check/build/run stdout and stderr logs under the selected artifact directory.
Use `--no-artifacts` for disposable local runs.
