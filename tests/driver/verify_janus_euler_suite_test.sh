#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <source-dir>" >&2
  exit 2
fi

SOURCE_DIR="$(cd "$1" && pwd)"
SCRIPT="$SOURCE_DIR/scripts/verify-janus-euler-suite.sh"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

FAKE_BIN="$WORK/bin"
PROJECT="$WORK/project"
CONFIG="$WORK/euler-suite.txt"
mkdir -p "$FAKE_BIN" "$PROJECT/src" "$PROJECT/expected" "$PROJECT/bin" \
  "$PROJECT/log"

cat > "$PROJECT/janus.toml" <<'EOF'
[package]
name = "euler-suite"
version = "0.1.0"
entry = "src/problem1.janus"
EOF

touch "$PROJECT/src/problem1.janus" "$PROJECT/src/problem2.janus" \
  "$PROJECT/src/problem3.janus" "$PROJECT/src/problem4.janus" \
  "$PROJECT/src/problem5.janus" "$PROJECT/src/problem6.janus" \
  "$PROJECT/src/problem7.janus"
printf '233168\n' > "$PROJECT/expected/problem1.txt"
printf '4613732\n' > "$PROJECT/expected/problem2.txt"
printf '6857\n' > "$PROJECT/expected/problem3.txt"
printf '906609\n' > "$PROJECT/expected/problem4.txt"
printf '0\n' > "$PROJECT/expected/problem5.txt"

cat > "$CONFIG" <<'EOF'
1|src/problem1.janus|expected/problem1.txt
2|src/problem2.janus|expected/problem2.txt
3|src/problem3.janus|expected/problem3.txt
4|src/problem4.janus|expected/problem4.txt
5|src/problem5.janus|expected/problem5.txt
6|src/problem6.janus|expected/problem1.txt
7|src/problem7.janus|expected/problem1.txt
EOF

cat > "$FAKE_BIN/janus" <<'EOF'
#!/bin/sh
set -eu
project="${JANUS_FAKE_PROJECT:?}"
log="$project/log/janus.log"
printf '%s\n' "$*" >> "$log"
command="$1"
shift
case "$command" in
  check)
    case "$1" in
      "$project"/bin/slow-check)
        if [ -n "${JANUS_SLOW_CHECK_PID:-}" ]; then
          printf '%s\n' "$$" > "$JANUS_SLOW_CHECK_PID"
        fi
        sleep "${JANUS_SLOW_SECONDS:=30}"
        exit 0
        ;;
      *problem3.janus) exit 7 ;;
      *) exit 0 ;;
    esac
    ;;
  build)
    source="$1"
    shift
    output=
    release=0
    while [ "$#" -gt 0 ]; do
      case "$1" in
        -o) output="$2"; shift 2 ;;
        --release) release=1; shift ;;
        *) shift ;;
      esac
    done
    if [ -z "$output" ]; then
      echo "fake janus: missing -o" >&2
      exit 2
    fi
    case "$source" in
      "$project"/bin/slow-build)
        if [ -n "${JANUS_SLOW_BUILD_PID:-}" ]; then
          printf '%s\n' "$$" > "$JANUS_SLOW_BUILD_PID"
        fi
        sleep "${JANUS_SLOW_SECONDS:=30}"
        cat > "$output" <<'RUNNER'
#!/bin/sh
printf '233168\n'
RUNNER
        chmod +x "$output"
        exit 0
        ;;
      *problem4.janus) exit 8 ;;
    esac
    case "$source" in
      *problem5.janus)
      cat > "$output" <<'RUNNER'
#!/bin/sh
exit 13
RUNNER
      chmod +x "$output"
      exit 0
      ;;
      *problem6.janus)
      cat > "$output" <<'RUNNER'
#!/bin/sh
exit 124
RUNNER
      chmod +x "$output"
      exit 0
      ;;
      *problem7.janus)
      cat > "$output" <<'RUNNER'
#!/bin/sh
printf '\033bad\001\n' >&2
exit 13
RUNNER
      chmod +x "$output"
      exit 0
      ;;
    esac
    if [ "$source" = "$project/bin/slow" ]; then
      cat > "$output" <<'RUNNER'
#!/bin/sh
: "${JANUS_SLOW_SECONDS:=2}"
if [ -n "${JANUS_SLOW_RUN_PID:-}" ]; then
  printf '%s\n' "$$" > "$JANUS_SLOW_RUN_PID"
fi
sleep "$JANUS_SLOW_SECONDS"
RUNNER
      chmod +x "$output"
      exit 0
    fi
    case "$source" in
      "$project"/bin/leaky)
      cat > "$output" <<RUNNER
#!/bin/sh
trap '' HUP TERM
"$source" &
wait
RUNNER
      chmod +x "$output"
      exit 0
      ;;
    esac
    case "$source" in
      *problem1.janus) value=233168 ;;
      *problem2.janus) value=wrong ;;
      *) value=unexpected ;;
    esac
    cat > "$output" <<RUNNER
#!/bin/sh
printf '%s\n' '$value'
RUNNER
    chmod +x "$output"
    if [ "$release" -eq 1 ]; then
      touch "$project/log/release-was-used"
    fi
    ;;
  *)
    exit 9
    ;;
esac
EOF
chmod +x "$FAKE_BIN/janus"

assert_artifact_pointers_are_durable() {
  artifact_root="$1"
  if [ -f "$artifact_root/latest" ]; then
    latest_rel="$(cat "$artifact_root/latest")"
    if [ ! -f "$artifact_root/$latest_rel/report.json" ]; then
      echo "verify test: latest points at a missing artifact report" >&2
      exit 1
    fi
  fi
  [ -f "$artifact_root/index.tsv" ] || return 0
  while IFS='	' read -r run_id unix_time exit_code duration report_path; do
    [ "$run_id" = run_id ] && continue
    [ -n "$run_id" ] || continue
    if [ ! -f "$artifact_root/$report_path" ]; then
      echo "verify test: index points at a missing artifact report" >&2
      exit 1
    fi
  done < "$artifact_root/index.tsv"
}

PATH="$FAKE_BIN:/usr/bin:/bin" \
JANUS_FAKE_PROJECT="$PROJECT" \
  "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --problem 1 \
  > "$WORK/problem1.out" 2> "$WORK/problem1.err"

if ! grep -q 'problem 1 .* ok' "$WORK/problem1.err"; then
  echo "verify test: readable output did not report problem 1 as ok" >&2
  exit 1
fi
if ! grep -q '"problem":1' "$WORK/problem1.out" ||
   ! grep -q '"status":"ok"' "$WORK/problem1.out"; then
  echo "verify test: JSON output did not report problem 1 as ok" >&2
  exit 1
fi
if ! grep -q "check .*problem1.janus" "$PROJECT/log/janus.log" ||
   ! grep -q "build .*problem1.janus -o " "$PROJECT/log/janus.log"; then
  echo "verify test: janus check/build were not called for problem 1" >&2
  exit 1
fi
VERIFY_ARTIFACT_ROOT="$PROJECT/target/verify"
if [ ! -f "$VERIFY_ARTIFACT_ROOT/latest" ]; then
  echo "verify test: latest artifact pointer was not written" >&2
  exit 1
fi
first_run_rel="$(cat "$VERIFY_ARTIFACT_ROOT/latest")"
first_run_dir="$VERIFY_ARTIFACT_ROOT/$first_run_rel"
if [ ! -d "$first_run_dir" ] || [ ! -f "$first_run_dir/report.json" ]; then
  echo "verify test: default artifact run directory is incomplete" >&2
  exit 1
fi
if ! grep -q "\"run_id\":\"$first_run_rel\"" "$first_run_dir/report.json" ||
   ! grep -q "\"report\":\"$first_run_rel/report.json\"" "$first_run_dir/report.json"; then
  echo "verify test: report.json is not self-identifying" >&2
  exit 1
fi
if ! cmp -s "$WORK/problem1.out" "$first_run_dir/report.json"; then
  echo "verify test: persisted report.json differs from stdout JSON" >&2
  exit 1
fi
for artifact_file in \
  "$first_run_dir/problems/1/check.stdout" \
  "$first_run_dir/problems/1/check.stderr" \
  "$first_run_dir/problems/1/build.stdout" \
  "$first_run_dir/problems/1/build.stderr" \
  "$first_run_dir/problems/1/run.stdout" \
  "$first_run_dir/problems/1/run.stderr"
do
  if [ ! -f "$artifact_file" ]; then
    echo "verify test: missing per-step log artifact $artifact_file" >&2
    exit 1
  fi
done
if command -v python3 >/dev/null 2>&1; then
  python3 -m json.tool "$first_run_dir/report.json" >/dev/null
elif command -v python >/dev/null 2>&1; then
  python -m json.tool "$first_run_dir/report.json" >/dev/null
fi

PATH="$FAKE_BIN:/usr/bin:/bin" \
JANUS_FAKE_PROJECT="$PROJECT" \
  "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --problem 1 \
  > "$WORK/problem1-second.out" 2> "$WORK/problem1-second.err"
second_run_rel="$(cat "$VERIFY_ARTIFACT_ROOT/latest")"
if [ "$second_run_rel" = "$first_run_rel" ]; then
  echo "verify test: two artifact runs overwrote the same directory" >&2
  exit 1
fi
if [ ! -f "$VERIFY_ARTIFACT_ROOT/$first_run_rel/report.json" ] ||
   [ ! -f "$VERIFY_ARTIFACT_ROOT/$second_run_rel/report.json" ]; then
  echo "verify test: two artifact runs were not both retained" >&2
  exit 1
fi
if [ ! -f "$VERIFY_ARTIFACT_ROOT/index.tsv" ] ||
   ! grep -q "$first_run_rel" "$VERIFY_ARTIFACT_ROOT/index.tsv" ||
   ! grep -q "$second_run_rel" "$VERIFY_ARTIFACT_ROOT/index.tsv"; then
  echo "verify test: artifact index does not list retained runs" >&2
  exit 1
fi
assert_artifact_pointers_are_durable "$VERIFY_ARTIFACT_ROOT"
if [ -d "$VERIFY_ARTIFACT_ROOT/.publish.lock" ]; then
  echo "verify test: artifact lock must be a regular file, not a directory" >&2
  exit 1
fi

CONCURRENT_ARTIFACTS="$WORK/concurrent-artifacts"
PATH="$FAKE_BIN:/usr/bin:/bin" JANUS_FAKE_PROJECT="$PROJECT" \
  "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --problem 1 \
    --artifacts-dir "$CONCURRENT_ARTIFACTS" \
  > "$WORK/concurrent-1.out" 2> "$WORK/concurrent-1.err" &
concurrent_pid_1=$!
PATH="$FAKE_BIN:/usr/bin:/bin" JANUS_FAKE_PROJECT="$PROJECT" \
  "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --problem 1 \
    --artifacts-dir "$CONCURRENT_ARTIFACTS" \
  > "$WORK/concurrent-2.out" 2> "$WORK/concurrent-2.err" &
concurrent_pid_2=$!
wait "$concurrent_pid_1"
wait "$concurrent_pid_2"
concurrent_lines=$(grep -c '^run-' "$CONCURRENT_ARTIFACTS/index.tsv")
if [ "$concurrent_lines" -ne 2 ]; then
  echo "verify test: concurrent artifact index lost a run" >&2
  exit 1
fi
latest_concurrent_rel="$(cat "$CONCURRENT_ARTIFACTS/latest")"
latest_concurrent_report="$CONCURRENT_ARTIFACTS/$latest_concurrent_rel/report.json"
if [ ! -s "$latest_concurrent_report" ] ||
   ! grep -q "\"run_id\":\"$latest_concurrent_rel\"" "$latest_concurrent_report" ||
   ! grep -q '"summary":{"total":1,"ok":1,"failed":0}' "$latest_concurrent_report"; then
  echo "verify test: concurrent latest did not point at a complete report" >&2
  exit 1
fi
assert_artifact_pointers_are_durable "$CONCURRENT_ARTIFACTS"

ORPHAN_ARTIFACTS="$WORK/orphan-artifacts"
mkdir -p "$ORPHAN_ARTIFACTS/.publish.lock"
printf '%s\n' 0 > "$ORPHAN_ARTIFACTS/.publish.lock/pid"
if PATH="$FAKE_BIN:/usr/bin:/bin" \
   JANUS_FAKE_PROJECT="$PROJECT" \
   ARTIFACT_LOCK_TIMEOUT_SECONDS=1 \
     "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --problem 1 \
       --artifacts-dir "$ORPHAN_ARTIFACTS" \
       > "$WORK/orphan-lock.out" 2> "$WORK/orphan-lock.err"; then
  echo "verify test: stale legacy artifact lock should fail after bounded wait" >&2
  exit 1
fi
if ! grep -q "remove .*publish.lock manually" "$WORK/orphan-lock.err"; then
  echo "verify test: stale legacy artifact lock error was not manual-removal guidance" >&2
  exit 1
fi
if [ ! -d "$ORPHAN_ARTIFACTS/.publish.lock" ]; then
  echo "verify test: stale legacy artifact lock was removed automatically" >&2
  exit 1
fi
if [ -f "$ORPHAN_ARTIFACTS/latest" ] || [ -f "$ORPHAN_ARTIFACTS/index.tsv" ]; then
  echo "verify test: stale legacy artifact lock failure published pointers" >&2
  exit 1
fi
orphan_reports=$(find "$ORPHAN_ARTIFACTS" -mindepth 2 -maxdepth 2 -name report.json | wc -l)
if [ "$orphan_reports" -ne 1 ]; then
  echo "verify test: stale legacy artifact lock failure did not retain one complete unreferenced run" >&2
  exit 1
fi

LIVE_LOCK_ARTIFACTS="$WORK/live-lock-artifacts"
mkdir -p "$LIVE_LOCK_ARTIFACTS"
printf '%s\n' "$$" > "$LIVE_LOCK_ARTIFACTS/.publish.lock"
if PATH="$FAKE_BIN:/usr/bin:/bin" \
   JANUS_FAKE_PROJECT="$PROJECT" \
   ARTIFACT_LOCK_TIMEOUT_SECONDS=1 \
     "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --problem 1 \
       --artifacts-dir "$LIVE_LOCK_ARTIFACTS" \
       > "$WORK/live-lock.out" 2> "$WORK/live-lock.err"; then
  echo "verify test: live artifact lock should fail after bounded wait" >&2
  exit 1
fi
if ! grep -q "artifact lock is still held by live pid" "$WORK/live-lock.err"; then
  echo "verify test: live artifact lock error was not clear" >&2
  exit 1
fi
rm -f "$LIVE_LOCK_ARTIFACTS/.publish.lock"

MALFORMED_LOCK_ARTIFACTS="$WORK/malformed-lock-artifacts"
mkdir -p "$MALFORMED_LOCK_ARTIFACTS"
printf '%s\n' "not-a-pid" > "$MALFORMED_LOCK_ARTIFACTS/.publish.lock"
if PATH="$FAKE_BIN:/usr/bin:/bin" \
   JANUS_FAKE_PROJECT="$PROJECT" \
   ARTIFACT_LOCK_TIMEOUT_SECONDS=1 \
     "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --problem 1 \
       --artifacts-dir "$MALFORMED_LOCK_ARTIFACTS" \
       > "$WORK/malformed-lock.out" 2> "$WORK/malformed-lock.err"; then
  echo "verify test: malformed regular-file lock should fail after bounded wait" >&2
  exit 1
fi
if ! grep -q "remove .*publish.lock manually" "$WORK/malformed-lock.err"; then
  echo "verify test: malformed regular-file lock error was not manual-removal guidance" >&2
  exit 1
fi
if [ ! -f "$MALFORMED_LOCK_ARTIFACTS/.publish.lock" ]; then
  echo "verify test: malformed regular-file lock was removed automatically" >&2
  exit 1
fi
if [ -f "$MALFORMED_LOCK_ARTIFACTS/latest" ] ||
   [ -f "$MALFORMED_LOCK_ARTIFACTS/index.tsv" ]; then
  echo "verify test: malformed regular-file lock failure published pointers" >&2
  exit 1
fi
malformed_reports=$(find "$MALFORMED_LOCK_ARTIFACTS" -mindepth 2 -maxdepth 2 -name report.json | wc -l)
if [ "$malformed_reports" -ne 1 ]; then
  echo "verify test: malformed regular-file lock failure did not retain one complete unreferenced run" >&2
  exit 1
fi

CUSTOM_ARTIFACTS="$WORK/custom-artifacts"
PATH="$FAKE_BIN:/usr/bin:/bin" \
JANUS_FAKE_PROJECT="$PROJECT" \
  "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --problem 1 \
    --artifacts-dir "$CUSTOM_ARTIFACTS" \
  > "$WORK/problem1-custom.out" 2> "$WORK/problem1-custom.err"
custom_run_dir="$CUSTOM_ARTIFACTS/$(cat "$CUSTOM_ARTIFACTS/latest")"
if [ ! -f "$custom_run_dir/report.json" ]; then
  echo "verify test: explicit artifact destination was not used" >&2
  exit 1
fi

PATH="$FAKE_BIN:/usr/bin:/bin" \
JANUS_FAKE_PROJECT="$PROJECT" \
  "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --problem 1 \
    --no-artifacts \
  > "$WORK/problem1-no-artifacts.out" 2> "$WORK/problem1-no-artifacts.err"
if [ "$(cat "$VERIFY_ARTIFACT_ROOT/latest")" != "$second_run_rel" ]; then
  echo "verify test: --no-artifacts unexpectedly updated artifacts" >&2
  exit 1
fi

INVALID_ARTIFACTS="$WORK/invalid-artifacts"
cat > "$WORK/invalid-config.txt" <<'EOF'
1|src/problem1.janus
EOF
if PATH="$FAKE_BIN:/usr/bin:/bin" \
   JANUS_FAKE_PROJECT="$PROJECT" \
     "$SCRIPT" --project "$PROJECT" --config "$WORK/invalid-config.txt" \
       --problem 1 --artifacts-dir "$INVALID_ARTIFACTS" \
       > "$WORK/invalid-config.out" 2> "$WORK/invalid-config.err"; then
  echo "verify test: invalid config should fail" >&2
  exit 1
fi
if find "$INVALID_ARTIFACTS" -mindepth 1 -maxdepth 1 -type d 2>/dev/null |
   grep . >/dev/null; then
  echo "verify test: invalid config left a partial artifact run directory" >&2
  exit 1
fi

BAD_JSON_OUT="$WORK/missing-json-parent/report.json"
if PATH="$FAKE_BIN:/usr/bin:/bin" \
   JANUS_FAKE_PROJECT="$PROJECT" \
     "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --problem 1 \
       --json-out "$BAD_JSON_OUT" --no-artifacts \
       > "$WORK/bad-json-out.out" 2> "$WORK/bad-json-out.err"; then
  echo "verify test: invalid --json-out destination should fail" >&2
  exit 1
fi
if [ -s "$WORK/bad-json-out.out" ]; then
  echo "verify test: stdout JSON was emitted before --json-out persistence failed" >&2
  exit 1
fi
if ! grep -q "failed to persist JSON report" "$WORK/bad-json-out.err"; then
  echo "verify test: invalid --json-out error was not clear" >&2
  exit 1
fi

set +e
(
  cd "$WORK"
  JANUS_FAKE_PROJECT="$PROJECT" \
    "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --janus bin/janus \
      --problem 1 > "$WORK/relative-janus.out" \
      2> "$WORK/relative-janus.err"
)
relative_janus_status=$?
set -e
if [ "$relative_janus_status" -ne 0 ] ||
   ! grep -q '"status":"ok"' "$WORK/relative-janus.out"; then
  echo "verify test: relative --janus was not resolved before cd" >&2
  exit 1
fi

cat > "$WORK/one-problem.txt" <<'EOF'
1|src/problem1.janus|expected/problem1.txt
EOF
set +e
PATH="$FAKE_BIN:/usr/bin:/bin" \
JANUS_FAKE_PROJECT="$PROJECT" \
  "$SCRIPT" --project "$PROJECT" --config "$WORK/one-problem.txt" \
    --problem 1 --all > "$WORK/problem-and-all.out" \
    2> "$WORK/problem-and-all.err"
problem_and_all_status=$?
set -e
if [ "$problem_and_all_status" -ne 2 ] ||
   ! grep -q "mutually exclusive" "$WORK/problem-and-all.err"; then
  echo "verify test: --problem and --all were not rejected together" >&2
  exit 1
fi

if PATH="$FAKE_BIN:/usr/bin:/bin" \
   JANUS_FAKE_PROJECT="$PROJECT" \
     "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --problem 2 \
     > "$WORK/problem2.out" 2> "$WORK/problem2.err"; then
  echo "verify test: mismatch should fail" >&2
  exit 1
fi
if ! grep -q '"status":"mismatch"' "$WORK/problem2.out"; then
  echo "verify test: mismatch status missing from JSON" >&2
  exit 1
fi
mismatch_run_rel="$(cat "$VERIFY_ARTIFACT_ROOT/latest")"
mismatch_run_dir="$VERIFY_ARTIFACT_ROOT/$mismatch_run_rel"
if [ ! -f "$mismatch_run_dir/report.json" ] ||
   [ ! -f "$mismatch_run_dir/problems/2/run.stdout" ] ||
   [ ! -f "$mismatch_run_dir/problems/2/run.stderr" ]; then
  echo "verify test: failed run artifacts were not persisted" >&2
  exit 1
fi
if ! grep -q '"status":"mismatch"' "$mismatch_run_dir/report.json"; then
  echo "verify test: failed artifact report missed mismatch status" >&2
  exit 1
fi
if ! grep -q 'wrong' "$mismatch_run_dir/problems/2/run.stdout"; then
  echo "verify test: failed artifact did not retain run stdout" >&2
  exit 1
fi

if PATH="$FAKE_BIN:/usr/bin:/bin" \
   JANUS_FAKE_PROJECT="$PROJECT" \
     "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --all --release \
     > "$WORK/all.out" 2> "$WORK/all.err"; then
  echo "verify test: --all should fail when any problem fails" >&2
  exit 1
fi
for status in ok mismatch error crash; do
  if ! grep -q "\"status\":\"$status\"" "$WORK/all.out"; then
    echo "verify test: --all JSON missed status $status" >&2
    exit 1
  fi
done
if ! grep -q '"status":"error".*"stage":"check"' "$WORK/all.out" ||
   ! grep -q '"status":"error".*"stage":"build"' "$WORK/all.out"; then
  echo "verify test: check/build errors are not explicit" >&2
  exit 1
fi
if [ ! -f "$PROJECT/log/release-was-used" ]; then
  echo "verify test: --release was not passed to janus build" >&2
  exit 1
fi

if PATH="$FAKE_BIN:/usr/bin:/bin" \
   JANUS_FAKE_PROJECT="$PROJECT" \
     "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --problem 6 \
     > "$WORK/exit124.out" 2> "$WORK/exit124.err"; then
  echo "verify test: exit 124 crash should fail" >&2
  exit 1
fi
if ! grep -q '"status":"crash"' "$WORK/exit124.out" ||
   ! grep -q '"exit_code":124' "$WORK/exit124.out"; then
  echo "verify test: real exit 124 was confused with timeout" >&2
  exit 1
fi

if PATH="$FAKE_BIN:/usr/bin:/bin" \
   JANUS_FAKE_PROJECT="$PROJECT" \
     "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --problem 7 \
     > "$WORK/control-json.out" 2> "$WORK/control-json.err"; then
  echo "verify test: control-character crash should fail" >&2
  exit 1
fi
if command -v python3 >/dev/null 2>&1; then
  python3 -m json.tool "$WORK/control-json.out" >/dev/null
elif command -v python >/dev/null 2>&1; then
  python -m json.tool "$WORK/control-json.out" >/dev/null
fi

cat > "$PROJECT/bin/slow" <<'EOF'
#!/bin/sh
sleep 2
EOF
chmod +x "$PROJECT/bin/slow"
cat > "$CONFIG" <<'EOF'
5|bin/slow|expected/problem1.txt
EOF

SIGNAL_ARTIFACTS="$WORK/signal-artifacts"
SIGNAL_RUN_PID="$WORK/signal-run.pid"
set +e
PATH="$FAKE_BIN:/usr/bin:/bin" \
JANUS_FAKE_PROJECT="$PROJECT" \
JANUS_SLOW_SECONDS=30 \
JANUS_SLOW_RUN_PID="$SIGNAL_RUN_PID" \
  "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --problem 5 \
    --timeout 60 --artifacts-dir "$SIGNAL_ARTIFACTS" \
    > "$WORK/signal-term.out" 2> "$WORK/signal-term.err" &
signal_pid=$!
set -e
signal_start="$(date +%s)"
while [ ! -f "$SIGNAL_RUN_PID" ]; do
  if ! kill -0 "$signal_pid" 2>/dev/null; then
    echo "verify test: signal target exited before runner started" >&2
    exit 1
  fi
  if [ $(( $(date +%s) - signal_start )) -gt 10 ]; then
    echo "verify test: signal target did not start runner" >&2
    kill -TERM "$signal_pid" 2>/dev/null || true
    wait "$signal_pid" 2>/dev/null || true
    exit 1
  fi
  sleep 1
done
signal_child_pid="$(cat "$SIGNAL_RUN_PID")"
kill -TERM "$signal_pid"
set +e
wait "$signal_pid"
signal_status=$?
set -e
signal_duration=$(( $(date +%s) - signal_start ))
if [ "$signal_status" -ne 143 ]; then
  echo "verify test: TERM should exit with 143, got $signal_status" >&2
  exit 1
fi
if [ "$signal_duration" -gt 10 ]; then
  echo "verify test: TERM did not stop the verification run quickly" >&2
  exit 1
fi
sleep 1
signal_child_status="$(ps -p "$signal_child_pid" -o stat= 2>/dev/null || true)"
if [ -n "$signal_child_status" ] &&
   ! printf '%s\n' "$signal_child_status" | grep -q '^Z'; then
  echo "verify test: TERM left the active runner alive" >&2
  kill -9 "$signal_child_pid" 2>/dev/null || true
  exit 1
fi
if find "$SIGNAL_ARTIFACTS" -mindepth 1 -maxdepth 1 -type d 2>/dev/null |
   grep . >/dev/null; then
  echo "verify test: TERM left a partial artifact run directory" >&2
  exit 1
fi
if [ -f "$SIGNAL_ARTIFACTS/latest" ] || [ -f "$SIGNAL_ARTIFACTS/index.tsv" ]; then
  echo "verify test: TERM published partial artifacts" >&2
  exit 1
fi

for signal_stage in check build; do
  case "$signal_stage" in
    check)
      slow_source=bin/slow-check
      signal_pid_file="$WORK/signal-check.pid"
      pid_env=JANUS_SLOW_CHECK_PID
      ;;
    build)
      slow_source=bin/slow-build
      signal_pid_file="$WORK/signal-build.pid"
      pid_env=JANUS_SLOW_BUILD_PID
      ;;
  esac
  : > "$PROJECT/$slow_source"
  cat > "$CONFIG" <<EOF
5|$slow_source|expected/problem1.txt
EOF
  signal_artifacts="$WORK/signal-$signal_stage-artifacts"
  rm -f "$signal_pid_file"
  set +e
  env \
    PATH="$FAKE_BIN:/usr/bin:/bin" \
    JANUS_FAKE_PROJECT="$PROJECT" \
    JANUS_SLOW_SECONDS=30 \
    "$pid_env=$signal_pid_file" \
    "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --problem 5 \
      --timeout 60 --artifacts-dir "$signal_artifacts" \
      > "$WORK/signal-$signal_stage.out" 2> "$WORK/signal-$signal_stage.err" &
  signal_parent_pid=$!
  set -e
  signal_stage_start="$(date +%s)"
  while [ ! -f "$signal_pid_file" ]; do
    if ! kill -0 "$signal_parent_pid" 2>/dev/null; then
      echo "verify test: signal $signal_stage target exited before janus started" >&2
      exit 1
    fi
    if [ $(( $(date +%s) - signal_stage_start )) -gt 10 ]; then
      echo "verify test: signal $signal_stage target did not start janus" >&2
      kill -TERM "$signal_parent_pid" 2>/dev/null || true
      wait "$signal_parent_pid" 2>/dev/null || true
      exit 1
    fi
    sleep 1
  done
  signal_janus_pid="$(cat "$signal_pid_file")"
  kill -TERM "$signal_parent_pid"
  set +e
  wait "$signal_parent_pid"
  signal_parent_status=$?
  set -e
  signal_stage_duration=$(( $(date +%s) - signal_stage_start ))
  if [ "$signal_parent_status" -ne 143 ]; then
    echo "verify test: TERM during janus $signal_stage should exit with 143, got $signal_parent_status" >&2
    exit 1
  fi
  if [ "$signal_stage_duration" -gt 10 ]; then
    echo "verify test: TERM during janus $signal_stage was not prompt" >&2
    exit 1
  fi
  sleep 1
  signal_janus_status="$(ps -p "$signal_janus_pid" -o stat= 2>/dev/null || true)"
  if [ -n "$signal_janus_status" ] &&
     ! printf '%s\n' "$signal_janus_status" | grep -q '^Z'; then
    echo "verify test: TERM during janus $signal_stage left janus alive" >&2
    kill -9 "$signal_janus_pid" 2>/dev/null || true
    exit 1
  fi
  if find "$signal_artifacts" -mindepth 1 -maxdepth 1 -type d 2>/dev/null |
     grep . >/dev/null; then
    echo "verify test: TERM during janus $signal_stage left a partial artifact run directory" >&2
    exit 1
  fi
  if [ -f "$signal_artifacts/latest" ] || [ -f "$signal_artifacts/index.tsv" ]; then
    echo "verify test: TERM during janus $signal_stage published partial artifacts" >&2
    exit 1
  fi
done

cat > "$CONFIG" <<'EOF'
5|bin/slow|expected/problem1.txt
EOF

FALLBACK_BIN="$WORK/fallback-bin"
mkdir -p "$FALLBACK_BIN"
for tool in awk cat chmod cmp cp date grep ln mkdir mktemp mv od ps rm sed sleep; do
  if tool_path="$(command -v "$tool")"; then
    ln -s "$tool_path" "$FALLBACK_BIN/$tool"
  fi
done
REAL_PERL="$(command -v perl)"
cat > "$FALLBACK_BIN/perl" <<EOF
#!/bin/sh
exec "$REAL_PERL" -MPOSIX -e 'POSIX::setpgid(0, 0) or die "setpgid: \$!\n"; exec @ARGV or die "exec perl: \$!\n"' -- "$REAL_PERL" "\$@"
EOF
chmod +x "$FALLBACK_BIN/perl"
ln -s "$FAKE_BIN/janus" "$FALLBACK_BIN/janus"

if PATH="$FALLBACK_BIN" \
   JANUS_FAKE_PROJECT="$PROJECT" \
     "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --problem 5 \
     --timeout 1 > "$WORK/timeout.out" 2> "$WORK/timeout.err"; then
  echo "verify test: timeout should fail" >&2
  exit 1
fi
if ! grep -q '"status":"timeout"' "$WORK/timeout.out"; then
  echo "verify test: timeout status missing from JSON" >&2
  exit 1
fi
timeout_run_rel="$(cat "$VERIFY_ARTIFACT_ROOT/latest")"
timeout_run_dir="$VERIFY_ARTIFACT_ROOT/$timeout_run_rel"
if ! grep -q '"status":"timeout"' "$timeout_run_dir/report.json" ||
   [ ! -f "$timeout_run_dir/problems/5/run.stdout" ] ||
   [ ! -f "$timeout_run_dir/problems/5/run.stderr" ]; then
  echo "verify test: timeout artifacts did not retain status and logs" >&2
  exit 1
fi

LEAK_NAME="janus-verify-leaked-descendant-$$"
LEAK_PID="$WORK/leaked-descendant.pid"
cat > "$PROJECT/bin/$LEAK_NAME" <<'EOF'
#!/bin/sh
trap '' HUP TERM
printf '%s\n' "$$" > "${JANUS_LEAK_PID:?}"
while :; do
  sleep 1
done
EOF
chmod +x "$PROJECT/bin/$LEAK_NAME"
cat > "$PROJECT/bin/leaky" <<EOF
#!/bin/sh
trap '' HUP TERM
"$PROJECT/bin/$LEAK_NAME" &
wait
EOF
chmod +x "$PROJECT/bin/leaky"
cat > "$CONFIG" <<'EOF'
5|bin/leaky|expected/problem1.txt
EOF

if PATH="$FALLBACK_BIN" \
   JANUS_LEAK_PID="$LEAK_PID" \
   JANUS_FAKE_PROJECT="$PROJECT" \
     "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --problem 5 \
     --timeout 1 > "$WORK/fallback-timeout.out" \
       2> "$WORK/fallback-timeout.err"; then
  echo "verify test: fallback timeout should fail" >&2
  exit 1
fi
sleep 2
if [ -f "$LEAK_PID" ]; then
  leak_status="$(ps -p "$(cat "$LEAK_PID")" -o stat= 2>/dev/null || true)"
  if [ -n "$leak_status" ] &&
     ! printf '%s\n' "$leak_status" | grep -q '^Z'; then
    echo "verify test: fallback timeout leaked a descendant process" >&2
    kill -9 "$(cat "$LEAK_PID")" 2>/dev/null || true
    exit 1
  fi
fi

cat > "$CONFIG" <<'EOF'
5|bin/slow|expected/problem1.txt
EOF

if PATH="$FAKE_BIN:/usr/bin:/bin" \
   JANUS_FAKE_PROJECT="$PROJECT" \
     "$SCRIPT" --project "$PROJECT" --config "$CONFIG" --problem 5 \
     --timeout 5 --global-timeout 1 \
     > "$WORK/global-timeout.out" 2> "$WORK/global-timeout.err"; then
  echo "verify test: global timeout should fail" >&2
  exit 1
fi
if ! grep -q '"status":"timeout"' "$WORK/global-timeout.out"; then
  echo "verify test: global timeout status missing from JSON" >&2
  exit 1
fi

echo "verify-janus-euler-suite behavior is covered"
