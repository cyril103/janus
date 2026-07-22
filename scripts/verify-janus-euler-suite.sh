#!/bin/sh
set -eu

usage() {
  cat >&2 <<'EOF'
usage: verify-janus-euler-suite.sh --project DIR --config FILE (--problem N|--all) [options]

options:
  --janus PATH             Janus executable to use (default: janus)
  --release                Pass --release to janus build
  --timeout SECONDS        Per-problem timeout in seconds (default: 60)
  --global-timeout SECONDS Global timeout in seconds
  --json-out FILE          Also write the JSON report to FILE
  --artifacts-dir DIR      Persist run artifacts under DIR (default: PROJECT/target/verify)
  --no-artifacts           Do not persist run artifacts

config format:
  N|source.janus|expected-output-file
  Empty lines and lines starting with # are ignored.

Each JSON result keeps problem/status/stage/exit_code/duration/message and adds
compile_ok, run_ok, segfault, timeout, and output_mismatch booleans. Harness
timeouts set timeout=true; a command that exits 124 by itself does not.
EOF
}

die() {
  echo "verify: $*" >&2
  exit 2
}

json_escape() {
  # Encode byte-by-byte so JSON stays valid even when compiler diagnostics
  # contain terminal escapes or arbitrary non-UTF-8/control bytes.
  LC_ALL=C od -An -v -t u1 "$1" | while IFS= read -r bytes; do
    for byte in $bytes; do
      case "$byte" in
        8) printf '\\b' ;;
        9) printf '\\t' ;;
        10) printf '\\n' ;;
        12) printf '\\f' ;;
        13) printf '\\r' ;;
        34) printf '\\"' ;;
        92) printf '\\\\' ;;
        *)
          if [ "$byte" -ge 32 ] && [ "$byte" -le 126 ]; then
            printf "\\$(printf '%03o' "$byte")"
          else
            printf '\\u00%02x' "$byte"
          fi
          ;;
      esac
    done
  done
}

json_escape_text() {
  tmp="$WORK/json-text"
  printf '%s' "$1" > "$tmp"
  json_escape "$tmp"
}

normalize_output() {
  input="$1"
  output="$2"
  sed 's/[[:space:]]*$//' "$input" > "$output"
}

is_number() {
  case "$1" in
    ''|*[!0-9]*) return 1 ;;
    *) return 0 ;;
  esac
}

now_seconds() {
  date +%s
}

run_id_timestamp() {
  date -u +%Y%m%dT%H%M%SZ
}

remaining_timeout() {
  deadline="$1"
  now="$(now_seconds)"
  remaining=$((deadline - now))
  if [ "$remaining" -le 0 ]; then
    echo 0
  else
    echo "$remaining"
  fi
}

problem_timeout_left() {
  case_deadline="$1"
  remaining="$(remaining_timeout "$case_deadline")"
  if [ -n "$GLOBAL_DEADLINE" ]; then
    global_remaining="$(remaining_timeout "$GLOBAL_DEADLINE")"
    if [ "$global_remaining" -lt "$remaining" ]; then
      remaining="$global_remaining"
    fi
  fi
  echo "$remaining"
}

run_with_timeout() {
  seconds="$1"
  stdout_file="$2"
  stderr_file="$3"
  workdir="$4"
  shift 4

  : > "$stdout_file"
  : > "$stderr_file"

  timeout_file="$WORK/timeout.$$"
  timeout_marker="${RUN_TIMEOUT_MARKER:-}"
  rm -f "$timeout_file"
  if [ -n "$timeout_marker" ]; then
    rm -f "$timeout_marker"
  fi

  # Always start the command in its own session. A recursive process-tree
  # fallback is inherently racy once children are reparented, so use Perl's
  # POSIX::setsid on platforms (notably macOS) without a setsid executable.
  if command -v setsid >/dev/null 2>&1; then
    setsid sh -c 'cd "$1" || exit 125; shift; exec "$@"' sh "$workdir" "$@" \
      > "$stdout_file" 2> "$stderr_file" &
    command_pid=$!
  elif command -v perl >/dev/null 2>&1; then
    perl -MPOSIX -e '
      my $dir = shift @ARGV;
      my @cmd = @ARGV;
      my $pid = fork();
      defined $pid or die "fork: $!\n";
      if ($pid) {
        my %exit_for = (HUP => 129, INT => 130, TERM => 143);
        for my $signal (keys %exit_for) {
          $SIG{$signal} = sub {
            kill $signal, -$pid;
            kill $signal, $pid;
            # The shell timeout worker also escalates after one second. Kill
            # the child session immediately here so it cannot kill this
            # forwarding parent before descendants are reaped.
            kill "KILL", -$pid;
            kill "KILL", $pid;
            exit $exit_for{$signal};
          };
        }
        waitpid($pid, 0);
        my $status = $?;
        exit 127 if $status == -1;
        exit 128 + ($status & 127) if $status & 127;
        exit($status >> 8);
      }
      chdir $dir or exit 125;
      POSIX::setsid() >= 0 or die "setsid: $!\n";
      exec @cmd or die "exec: $!\n";
    ' -- \
      "$workdir" "$@" > "$stdout_file" 2> "$stderr_file" &
    command_pid=$!
  else
    printf '%s\n' 'timeout support requires setsid or perl' > "$stderr_file"
    return 125
  fi
  ACTIVE_COMMAND_PID="$command_pid"

  (
    trap - EXIT HUP INT TERM
    sleep "$seconds"
    if kill -0 "$command_pid" 2>/dev/null; then
      printf timeout > "$timeout_file"
      if [ -n "$timeout_marker" ]; then
        printf timeout > "$timeout_marker"
      fi
      kill -TERM "-$command_pid" 2>/dev/null || true
      kill -TERM "$command_pid" 2>/dev/null || true
      sleep 1
      kill -KILL "-$command_pid" 2>/dev/null || true
      kill -KILL "$command_pid" 2>/dev/null || true
    fi
  ) &
  timer_pid=$!
  ACTIVE_TIMER_PID="$timer_pid"

  set +e
  wait "$command_pid"
  wait_status=$?
  if [ -f "$timeout_file" ]; then
    # Let the timeout worker finish its TERM/KILL process-group cleanup.
    wait "$timer_pid" 2>/dev/null || true
  else
    kill "$timer_pid" 2>/dev/null || true
    wait "$timer_pid" 2>/dev/null || true
  fi

  if [ -f "$timeout_file" ]; then
    rm -f "$timeout_file"
    ACTIVE_COMMAND_PID=
    ACTIVE_TIMER_PID=
    return 124
  fi
  status="$wait_status"
  rm -f "$timeout_file"
  ACTIVE_COMMAND_PID=
  ACTIVE_TIMER_PID=
  return "$status"
}

cd_physical() {
  cd_path="$1"
  case "$cd_path" in
    -*) cd_path="./$cd_path" ;;
  esac
  CDPATH= cd "$cd_path" && pwd
}

resolve_file_path() {
  path="$1"
  case "$path" in
    /*) printf '%s\n' "$path" ;;
    */*)
      dir_part=${path%/*}
      base_part=${path##*/}
      printf '%s/%s\n' "$(cd_physical "$dir_part")" "$base_part"
      ;;
    *) printf '%s/%s\n' "$(pwd)" "$path" ;;
  esac
}

copy_if_exists() {
  source_file="$1"
  target_file="$2"
  if [ -f "$source_file" ]; then
    cp "$source_file" "$target_file"
  fi
}

is_live_pid() {
  pid="$1"
  is_number "$pid" || return 1
  [ "$pid" -gt 0 ] || return 1
  kill -0 "$pid" 2>/dev/null
}

PROJECT=
CONFIG=
JANUS=${JANUS:-janus}
MODE=
REQUESTED_PROBLEM=
PROBLEM_SELECTED=0
ALL_SELECTED=0
RELEASE=0
CASE_TIMEOUT=60
GLOBAL_TIMEOUT=
JSON_OUT=
ARTIFACTS_DIR=
ARTIFACTS_ENABLED=1

while [ "$#" -gt 0 ]; do
  case "$1" in
    --project)
      [ "$#" -ge 2 ] || die "--project requires a directory"
      PROJECT="$2"
      shift 2
      ;;
    --config)
      [ "$#" -ge 2 ] || die "--config requires a file"
      CONFIG="$2"
      shift 2
      ;;
    --janus)
      [ "$#" -ge 2 ] || die "--janus requires an executable"
      JANUS="$2"
      shift 2
      ;;
    --problem)
      [ "$#" -ge 2 ] || die "--problem requires a number"
      [ "$ALL_SELECTED" -eq 0 ] || die "--problem and --all are mutually exclusive"
      MODE=problem
      PROBLEM_SELECTED=1
      REQUESTED_PROBLEM="$2"
      shift 2
      ;;
    --all)
      [ "$PROBLEM_SELECTED" -eq 0 ] || die "--problem and --all are mutually exclusive"
      MODE=all
      ALL_SELECTED=1
      shift
      ;;
    --release)
      RELEASE=1
      shift
      ;;
    --timeout)
      [ "$#" -ge 2 ] || die "--timeout requires seconds"
      CASE_TIMEOUT="$2"
      shift 2
      ;;
    --global-timeout)
      [ "$#" -ge 2 ] || die "--global-timeout requires seconds"
      GLOBAL_TIMEOUT="$2"
      shift 2
      ;;
    --json-out)
      [ "$#" -ge 2 ] || die "--json-out requires a file"
      JSON_OUT="$2"
      shift 2
      ;;
    --artifacts-dir)
      [ "$#" -ge 2 ] || die "--artifacts-dir requires a directory"
      ARTIFACTS_DIR="$2"
      ARTIFACTS_ENABLED=1
      shift 2
      ;;
    --no-artifacts)
      ARTIFACTS_ENABLED=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown option '$1'"
      ;;
  esac
done

[ -n "$PROJECT" ] || die "--project is required"
[ -n "$CONFIG" ] || die "--config is required"
[ -n "$MODE" ] || die "use --problem N or --all"
if [ "$MODE" = problem ] && ! is_number "$REQUESTED_PROBLEM"; then
  die "--problem requires a numeric value"
fi
is_number "$CASE_TIMEOUT" || die "--timeout requires a positive integer"
[ "$CASE_TIMEOUT" -gt 0 ] || die "--timeout requires a positive integer"
if [ -n "$GLOBAL_TIMEOUT" ]; then
  is_number "$GLOBAL_TIMEOUT" || die "--global-timeout requires a positive integer"
  [ "$GLOBAL_TIMEOUT" -gt 0 ] || die "--global-timeout requires a positive integer"
fi
[ -d "$PROJECT" ] || die "project directory not found: $PROJECT"
[ -f "$CONFIG" ] || die "config file not found: $CONFIG"

JANUS_INPUT="$JANUS"
case "$JANUS_INPUT" in
  */*) JANUS="$(resolve_file_path "$JANUS_INPUT")" ;;
esac
if [ -n "$ARTIFACTS_DIR" ]; then
  ARTIFACTS_DIR="$(resolve_file_path "$ARTIFACTS_DIR")"
fi
PROJECT="$(cd_physical "$PROJECT")"
CONFIG="$(resolve_file_path "$CONFIG")"
WORK="$(mktemp -d)"
ARTIFACT_RUN_DIR=
ARTIFACT_RUN_REL=
ARTIFACT_REPORT_REL=
ARTIFACTS_FINALIZED=0
ARTIFACTS_DURABLE=0
ARTIFACT_LOCK_DIR=
ARTIFACT_LOCK_FILE=
ARTIFACT_LOCK_OWNER_FILE=
ARTIFACT_LOCK_HELD=0
CLEANED_UP=0
ACTIVE_COMMAND_PID=
ACTIVE_TIMER_PID=
ARTIFACT_LOCK_TIMEOUT_SECONDS=${ARTIFACT_LOCK_TIMEOUT_SECONDS:-30}
ARTIFACT_LOCK_PID_GRACE_SECONDS=${ARTIFACT_LOCK_PID_GRACE_SECONDS:-2}

cleanup() {
  [ "$CLEANED_UP" -eq 0 ] || return 0
  CLEANED_UP=1
  if [ "$ARTIFACT_LOCK_HELD" -eq 1 ]; then
    release_artifact_lock
  fi
  if [ "$ARTIFACTS_DURABLE" -eq 0 ] && [ -n "$ARTIFACT_RUN_DIR" ]; then
    rm -rf "$ARTIFACT_RUN_DIR"
  fi
  rm -rf "$WORK"
}

terminate_active_work() {
  if [ -n "$ACTIVE_TIMER_PID" ]; then
    kill "$ACTIVE_TIMER_PID" 2>/dev/null || true
  fi
  if [ -n "$ACTIVE_COMMAND_PID" ]; then
    kill -TERM "-$ACTIVE_COMMAND_PID" 2>/dev/null || true
    kill -TERM "$ACTIVE_COMMAND_PID" 2>/dev/null || true
    sleep 1
    kill -KILL "-$ACTIVE_COMMAND_PID" 2>/dev/null || true
    kill -KILL "$ACTIVE_COMMAND_PID" 2>/dev/null || true
  fi
}

handle_signal() {
  signal="$1"
  case "$signal" in
    HUP) signal_status=129 ;;
    INT) signal_status=130 ;;
    TERM) signal_status=143 ;;
    *) signal_status=1 ;;
  esac
  trap - EXIT HUP INT TERM
  terminate_active_work
  cleanup
  exit "$signal_status"
}

trap cleanup EXIT
trap 'handle_signal HUP' HUP
trap 'handle_signal INT' INT
trap 'handle_signal TERM' TERM

REPORT="$WORK/report.json"
TMP_REPORT="$WORK/results.json"
: > "$TMP_REPORT"

TOTAL=0
PASSED=0
FAILED=0
FIRST=1
EXIT_STATUS=0
RUN_START="$(now_seconds)"
GLOBAL_DEADLINE=
if [ -n "$GLOBAL_TIMEOUT" ]; then
  GLOBAL_DEADLINE=$(($(now_seconds) + GLOBAL_TIMEOUT))
fi
init_artifacts() {
  [ "$ARTIFACTS_ENABLED" -eq 1 ] || return 0
  if [ -z "$ARTIFACTS_DIR" ]; then
    ARTIFACTS_DIR="$PROJECT/target/verify"
  fi
  mkdir -p "$ARTIFACTS_DIR"
  base_id="run-$(run_id_timestamp)-$$"
  counter=0
  while :; do
    if [ "$counter" -eq 0 ]; then
      candidate="$base_id"
    else
      candidate="$base_id-$counter"
    fi
    if mkdir "$ARTIFACTS_DIR/$candidate" 2>/dev/null; then
      ARTIFACT_RUN_REL="$candidate"
      ARTIFACT_RUN_DIR="$ARTIFACTS_DIR/$candidate"
      ARTIFACT_REPORT_REL="$candidate/report.json"
      break
    fi
    counter=$((counter + 1))
  done
}

acquire_artifact_lock() {
  [ "$ARTIFACTS_ENABLED" -eq 1 ] || return 0
  ARTIFACT_LOCK_FILE="$ARTIFACTS_DIR/.publish.lock"
  ARTIFACT_LOCK_DIR="$ARTIFACT_LOCK_FILE"
  ARTIFACT_LOCK_OWNER_FILE="$ARTIFACTS_DIR/.publish.lock.owner.$$"
  lock_deadline=$(($(now_seconds) + ARTIFACT_LOCK_TIMEOUT_SECONDS))
  if ! command -v ln >/dev/null 2>&1; then
    echo "verify: artifact locking requires ln" >&2
    return 1
  fi
  rm -f "$ARTIFACT_LOCK_OWNER_FILE"
  if ! printf '%s\n' "$$" > "$ARTIFACT_LOCK_OWNER_FILE"; then
    echo "verify: failed to create artifact lock owner file: $ARTIFACT_LOCK_OWNER_FILE" >&2
    return 1
  fi
  while :; do
    lock_pid=
    lock_state=
    if ln "$ARTIFACT_LOCK_OWNER_FILE" "$ARTIFACT_LOCK_FILE" 2>/dev/null; then
      lock_pid="$(sed -n '1p' "$ARTIFACT_LOCK_FILE" 2>/dev/null || true)"
      if [ "$lock_pid" = "$$" ]; then
        ARTIFACT_LOCK_HELD=1
        rm -f "$ARTIFACT_LOCK_OWNER_FILE"
        ARTIFACT_LOCK_OWNER_FILE=
        return 0
      fi
      lock_state="regular-file"
    elif [ -d "$ARTIFACT_LOCK_FILE" ]; then
      lock_state="legacy-directory"
      if [ -f "$ARTIFACT_LOCK_FILE/pid" ]; then
        lock_pid="$(sed -n '1p' "$ARTIFACT_LOCK_FILE/pid" 2>/dev/null || true)"
      fi
    elif [ -f "$ARTIFACT_LOCK_FILE" ]; then
      lock_state="regular-file"
      lock_pid="$(sed -n '1p' "$ARTIFACT_LOCK_FILE" 2>/dev/null || true)"
    else
      continue
    fi
    if [ "$(now_seconds)" -ge "$lock_deadline" ]; then
      rm -f "$ARTIFACT_LOCK_OWNER_FILE"
      ARTIFACT_LOCK_OWNER_FILE=
      if is_live_pid "$lock_pid"; then
        echo "verify: artifact lock is still held by live pid $lock_pid after ${ARTIFACT_LOCK_TIMEOUT_SECONDS}s: $ARTIFACT_LOCK_FILE; remove .publish.lock manually only if that process is gone" >&2
      elif [ "$lock_state" = "legacy-directory" ]; then
        echo "verify: artifact lock is a stale or malformed legacy directory after ${ARTIFACT_LOCK_TIMEOUT_SECONDS}s: $ARTIFACT_LOCK_FILE; remove .publish.lock manually after confirming no publisher is active" >&2
      else
        echo "verify: artifact lock is stale or malformed after ${ARTIFACT_LOCK_TIMEOUT_SECONDS}s: $ARTIFACT_LOCK_FILE; remove .publish.lock manually after confirming no publisher is active" >&2
      fi
      return 1
    fi
    sleep 1
  done
}

release_artifact_lock() {
  if [ "$ARTIFACT_LOCK_HELD" -eq 1 ] && [ -n "$ARTIFACT_LOCK_FILE" ]; then
    lock_pid="$(sed -n '1p' "$ARTIFACT_LOCK_FILE" 2>/dev/null || true)"
    if [ "$lock_pid" = "$$" ]; then
      rm -f "$ARTIFACT_LOCK_FILE"
    fi
    ARTIFACT_LOCK_HELD=0
  fi
  if [ -n "$ARTIFACT_LOCK_OWNER_FILE" ]; then
    rm -f "$ARTIFACT_LOCK_OWNER_FILE"
    ARTIFACT_LOCK_OWNER_FILE=
  fi
}

persist_artifacts() {
  [ "$ARTIFACTS_ENABLED" -eq 1 ] || return 0
  [ -n "$ARTIFACT_RUN_DIR" ] || return 0

  if ! cp "$REPORT" "$ARTIFACT_RUN_DIR/report.json"; then
    echo "verify: failed to persist artifact report: $ARTIFACT_RUN_DIR/report.json" >&2
    return 1
  fi

  for stage in check build run; do
    for stream in out err; do
      for source_file in "$WORK/$stage"-*."$stream"; do
        [ -f "$source_file" ] || continue
        name=${source_file##*/}
        problem=${name#"$stage"-}
        problem=${problem%."$stream"}
        problem_dir="$ARTIFACT_RUN_DIR/problems/$problem"
        if ! mkdir -p "$problem_dir"; then
          echo "verify: failed to persist artifact log directory: $problem_dir" >&2
          return 1
        fi
        case "$stream" in
          out) target_stream=stdout ;;
          err) target_stream=stderr ;;
        esac
        if ! copy_if_exists "$source_file" "$problem_dir/$stage.$target_stream"; then
          echo "verify: failed to persist artifact log: $problem_dir/$stage.$target_stream" >&2
          return 1
        fi
      done
    done
  done
  ARTIFACTS_DURABLE=1

  if ! acquire_artifact_lock; then
    return 1
  fi
  if [ ! -f "$ARTIFACTS_DIR/index.tsv" ]; then
    if ! printf 'run_id\tunix_time\texit_code\tduration_seconds\treport\n' \
      > "$ARTIFACTS_DIR/index.tsv"; then
      release_artifact_lock
      echo "verify: failed to persist artifact index: $ARTIFACTS_DIR/index.tsv" >&2
      return 1
    fi
  fi
  run_duration=$(($(now_seconds) - RUN_START))
  if ! printf '%s\t%s\t%s\t%s\t%s\n' \
    "$ARTIFACT_RUN_REL" "$(now_seconds)" "$EXIT_STATUS" "$run_duration" \
    "$ARTIFACT_RUN_REL/report.json" >> "$ARTIFACTS_DIR/index.tsv"; then
    release_artifact_lock
    echo "verify: failed to persist artifact index: $ARTIFACTS_DIR/index.tsv" >&2
    return 1
  fi
  latest_tmp="$ARTIFACTS_DIR/.latest.$$"
  if ! printf '%s\n' "$ARTIFACT_RUN_REL" > "$latest_tmp"; then
    release_artifact_lock
    echo "verify: failed to persist latest artifact pointer: $ARTIFACTS_DIR/latest" >&2
    return 1
  fi
  if ! mv "$latest_tmp" "$ARTIFACTS_DIR/latest"; then
    rm -f "$latest_tmp"
    release_artifact_lock
    echo "verify: failed to persist latest artifact pointer: $ARTIFACTS_DIR/latest" >&2
    return 1
  fi
  release_artifact_lock
  ARTIFACTS_FINALIZED=1
}

persist_json_out() {
  [ -n "$JSON_OUT" ] || return 0
  if ! cp "$REPORT" "$JSON_OUT"; then
    echo "verify: failed to persist JSON report: $JSON_OUT" >&2
    return 1
  fi
}

append_result() {
  problem="$1"
  status="$2"
  stage="$3"
  exit_code="$4"
  duration="$5"
  message="$6"
  actual_file="$7"
  expected_file="$8"
  compile_ok=false
  run_ok=false
  segfault=false
  timeout=false
  output_mismatch=false

  case "$stage" in
    run) compile_ok=true ;;
  esac
  case "$status" in
    ok|mismatch)
      if [ "$stage" = run ] && [ "$exit_code" -eq 0 ]; then
        run_ok=true
      fi
      ;;
  esac
  case "$status" in
    segfault) segfault=true ;;
    timeout) timeout=true ;;
    mismatch) output_mismatch=true ;;
  esac

  if [ "$FIRST" -eq 1 ]; then
    FIRST=0
  else
    printf ',\n' >> "$TMP_REPORT"
  fi

  printf '{"problem":%s,"status":"%s","stage":"%s","exit_code":%s,"duration_seconds":%s,"message":"%s","compile_ok":%s,"run_ok":%s,"segfault":%s,"timeout":%s,"output_mismatch":%s' \
    "$problem" "$status" "$stage" "$exit_code" "$duration" \
    "$(json_escape_text "$message")" "$compile_ok" "$run_ok" \
    "$segfault" "$timeout" "$output_mismatch" >> "$TMP_REPORT"
  if [ -n "$actual_file" ] && [ -f "$actual_file" ]; then
    printf ',"actual":"%s"' "$(json_escape "$actual_file")" >> "$TMP_REPORT"
  fi
  if [ -n "$expected_file" ] && [ -f "$expected_file" ]; then
    printf ',"expected":"%s"' "$(json_escape "$expected_file")" >> "$TMP_REPORT"
  fi
  printf '}' >> "$TMP_REPORT"
}

run_problem() {
  number="$1"
  source_rel="$2"
  expected_rel="$3"

  TOTAL=$((TOTAL + 1))
  start="$(now_seconds)"
  case_deadline=$((start + CASE_TIMEOUT))
  source="$PROJECT/$source_rel"
  expected="$PROJECT/$expected_rel"
  executable="$WORK/problem-$number"
  check_out="$WORK/check-$number.out"
  check_err="$WORK/check-$number.err"
  check_timeout="$WORK/check-$number.timeout"
  build_out="$WORK/build-$number.out"
  build_err="$WORK/build-$number.err"
  build_timeout="$WORK/build-$number.timeout"
  run_out="$WORK/run-$number.out"
  run_err="$WORK/run-$number.err"
  run_timeout="$WORK/run-$number.timeout"
  actual_norm="$WORK/actual-$number.txt"
  expected_norm="$WORK/expected-$number.txt"

  if [ -n "$GLOBAL_DEADLINE" ] && [ "$(remaining_timeout "$GLOBAL_DEADLINE")" -le 0 ]; then
    FAILED=$((FAILED + 1))
    EXIT_STATUS=1
    echo "problem $number ... timeout (global)" >&2
    append_result "$number" timeout global 124 0 "global timeout exceeded" "" ""
    return
  fi
  if [ ! -f "$source" ]; then
    FAILED=$((FAILED + 1))
    EXIT_STATUS=1
    echo "problem $number ... error (missing source)" >&2
    append_result "$number" error source 1 0 "source not found: $source_rel" "" ""
    return
  fi
  if [ ! -f "$expected" ]; then
    FAILED=$((FAILED + 1))
    EXIT_STATUS=1
    echo "problem $number ... error (missing expected output)" >&2
    append_result "$number" error expected 1 0 "expected output not found: $expected_rel" "" ""
    return
  fi

  timeout_left="$(problem_timeout_left "$case_deadline")"
  if [ "$timeout_left" -le 0 ]; then
    FAILED=$((FAILED + 1))
    EXIT_STATUS=1
    echo "problem $number ... timeout" >&2
    append_result "$number" timeout check 124 "$CASE_TIMEOUT" "problem timeout exceeded before check" "" ""
    return
  fi
  set +e
  RUN_TIMEOUT_MARKER="$check_timeout" \
    run_with_timeout "$timeout_left" "$check_out" "$check_err" "$PROJECT" \
      "$JANUS" check "$source"
  status=$?
  set -e
  if [ -f "$check_timeout" ]; then
    FAILED=$((FAILED + 1))
    EXIT_STATUS=1
    duration=$(($(now_seconds) - start))
    echo "problem $number ... timeout (check)" >&2
    append_result "$number" timeout check 124 "$duration" "janus check timed out" "" ""
    return
  fi
  if [ "$status" -eq 139 ]; then
    FAILED=$((FAILED + 1))
    EXIT_STATUS=1
    duration=$(($(now_seconds) - start))
    echo "problem $number ... segfault (check exit 139)" >&2
    append_result "$number" segfault check 139 "$duration" "janus check segfaulted" "$check_err" ""
    return
  fi
  if [ "$status" -ne 0 ]; then
    FAILED=$((FAILED + 1))
    EXIT_STATUS=1
    duration=$(($(now_seconds) - start))
    echo "problem $number ... error (check exit $status)" >&2
    append_result "$number" error check "$status" "$duration" "janus check failed" "$check_err" ""
    return
  fi

  timeout_left="$(problem_timeout_left "$case_deadline")"
  if [ "$timeout_left" -le 0 ]; then
    FAILED=$((FAILED + 1))
    EXIT_STATUS=1
    echo "problem $number ... timeout" >&2
    append_result "$number" timeout build 124 "$CASE_TIMEOUT" "problem timeout exceeded before build" "" ""
    return
  fi
  set +e
  if [ "$RELEASE" -eq 1 ]; then
        RUN_TIMEOUT_MARKER="$build_timeout" \
          run_with_timeout "$timeout_left" "$build_out" "$build_err" "$PROJECT" \
            "$JANUS" build "$source" -o "$executable" --release
  else
        RUN_TIMEOUT_MARKER="$build_timeout" \
          run_with_timeout "$timeout_left" "$build_out" "$build_err" "$PROJECT" \
            "$JANUS" build "$source" -o "$executable"
  fi
  status=$?
  set -e
  if [ -f "$build_timeout" ]; then
    FAILED=$((FAILED + 1))
    EXIT_STATUS=1
    duration=$(($(now_seconds) - start))
    echo "problem $number ... timeout (build)" >&2
    append_result "$number" timeout build 124 "$duration" "janus build timed out" "" ""
    return
  fi
  if [ "$status" -eq 139 ]; then
    FAILED=$((FAILED + 1))
    EXIT_STATUS=1
    duration=$(($(now_seconds) - start))
    echo "problem $number ... segfault (build exit 139)" >&2
    append_result "$number" segfault build 139 "$duration" "janus build segfaulted" "$build_err" ""
    return
  fi
  if [ "$status" -ne 0 ]; then
    FAILED=$((FAILED + 1))
    EXIT_STATUS=1
    duration=$(($(now_seconds) - start))
    echo "problem $number ... error (build exit $status)" >&2
    append_result "$number" error build "$status" "$duration" "janus build failed" "$build_err" ""
    return
  fi

  timeout_left="$(problem_timeout_left "$case_deadline")"
  if [ "$timeout_left" -le 0 ]; then
    FAILED=$((FAILED + 1))
    EXIT_STATUS=1
    echo "problem $number ... timeout" >&2
    append_result "$number" timeout run 124 "$CASE_TIMEOUT" "problem timeout exceeded before run" "" ""
    return
  fi
  set +e
  RUN_TIMEOUT_MARKER="$run_timeout" \
    run_with_timeout "$timeout_left" "$run_out" "$run_err" "$PROJECT" "$executable"
  status=$?
  set -e
  duration=$(($(now_seconds) - start))
  if [ -f "$run_timeout" ]; then
    FAILED=$((FAILED + 1))
    EXIT_STATUS=1
    echo "problem $number ... timeout (run)" >&2
    append_result "$number" timeout run 124 "$duration" "executable timed out" "" ""
    return
  fi
  if [ "$status" -eq 139 ]; then
    FAILED=$((FAILED + 1))
    EXIT_STATUS=1
    echo "problem $number ... segfault (run exit 139)" >&2
    append_result "$number" segfault run 139 "$duration" "executable segfaulted" "$run_err" ""
    return
  fi
  if [ "$status" -ne 0 ]; then
    FAILED=$((FAILED + 1))
    EXIT_STATUS=1
    echo "problem $number ... crash (exit $status)" >&2
    append_result "$number" crash run "$status" "$duration" "executable failed" "$run_err" ""
    return
  fi

  normalize_output "$run_out" "$actual_norm"
  normalize_output "$expected" "$expected_norm"
  if cmp -s "$actual_norm" "$expected_norm"; then
    PASSED=$((PASSED + 1))
    echo "problem $number ... ok" >&2
    append_result "$number" ok run 0 "$duration" "" "$actual_norm" "$expected_norm"
  else
    FAILED=$((FAILED + 1))
    EXIT_STATUS=1
    echo "problem $number ... mismatch" >&2
    append_result "$number" mismatch run 0 "$duration" "output did not match expected result" "$actual_norm" "$expected_norm"
  fi
}

validate_config_selection() {
  FOUND=0
  while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in
      ''|'#'*) continue ;;
    esac
    old_ifs=$IFS
    IFS='|'
    set -- $line
    IFS=$old_ifs
    [ "$#" -eq 3 ] || die "invalid config line: $line"
    number="$1"
    is_number "$number" || die "invalid problem number in config: $number"
    if [ "$MODE" = all ] || [ "$number" = "$REQUESTED_PROBLEM" ]; then
      FOUND=1
    fi
  done < "$CONFIG"

  if [ "$FOUND" -eq 0 ]; then
    die "problem not found in config: $REQUESTED_PROBLEM"
  fi
}

validate_config_selection
init_artifacts

while IFS= read -r line || [ -n "$line" ]; do
  case "$line" in
    ''|'#'*) continue ;;
  esac
  old_ifs=$IFS
  IFS='|'
  set -- $line
  IFS=$old_ifs
  [ "$#" -eq 3 ] || die "invalid config line: $line"
  number="$1"
  source_rel="$2"
  expected_rel="$3"
  is_number "$number" || die "invalid problem number in config: $number"
  if [ "$MODE" = all ] || [ "$number" = "$REQUESTED_PROBLEM" ]; then
    FOUND=1
    run_problem "$number" "$source_rel" "$expected_rel"
  fi
done < "$CONFIG"

RUN_DURATION=$(($(now_seconds) - RUN_START))
RUN_MESSAGE=
if [ "$EXIT_STATUS" -ne 0 ]; then
  RUN_MESSAGE="one or more problems failed"
fi
printf '{"run":{"run_id":"%s","report":"%s","exit_code":%s,"duration_seconds":%s,"message":"%s"},"summary":{"total":%s,"ok":%s,"failed":%s},"results":[\n' \
  "$(json_escape_text "$ARTIFACT_RUN_REL")" \
  "$(json_escape_text "$ARTIFACT_REPORT_REL")" \
  "$EXIT_STATUS" "$RUN_DURATION" "$(json_escape_text "$RUN_MESSAGE")" \
  "$TOTAL" "$PASSED" "$FAILED" > "$REPORT"
cat "$TMP_REPORT" >> "$REPORT"
printf '\n]}\n' >> "$REPORT"

persist_artifacts
persist_json_out
cat "$REPORT"
echo "summary: $PASSED/$TOTAL ok, $FAILED failed" >&2

exit "$EXIT_STATUS"
