# Project Euler fixtures

This corpus covers canonical Project Euler problems 1 through 20. The
`production.txt` manifest is the sole canonical profile: every Janus source
computes its answer dynamically from the problem inputs, and the corresponding
file under `expected/` remains the output oracle.

CTest keeps the historical `project_euler.smoke` test name for compatibility,
but that gate runs the dynamic production corpus. Each problem has a five-second
budget and the complete run has a 120-second budget.

Run the same corpus directly with:

```sh
scripts/verify-janus-euler-suite.sh \
  --project tests/fixtures/project-euler \
  --config tests/fixtures/project-euler/production.txt \
  --janus /absolute/path/to/janus \
  --all \
  --timeout 5 \
  --global-timeout 120 \
  --artifacts-dir /tmp/janus-euler-artifacts
```

Each artifact run writes `latest`, `index.tsv`, `report.json`, and per-problem
check/build/run stdout and stderr logs under the selected artifact directory.
Use `--no-artifacts` for disposable local runs.

For stage isolation, status interpretation, segfault triage, and fallback
semantics, see the
[Project Euler troubleshooting playbook](../../../docs/project-euler-troubleshooting.md).
