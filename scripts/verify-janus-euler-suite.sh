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
  --safe-fallback FILE     Run failed primary cases from fallback config
  --json-out FILE          Also write the JSON report to FILE
  --artifacts-dir DIR      Persist run artifacts under DIR (default: PROJECT/target/verify)
  --no-artifacts           Do not persist run artifacts

config format:
  N|source.janus|expected-output-file
  Empty lines and lines starting with # are ignored.

Each JSON result keeps problem/status/stage/exit_code/duration/message and adds
compile_ok, run_ok, segfault, timeout, and output_mismatch booleans. Harness
timeouts set timeout=true; a command that exits 124 by itself does not. Process
failures also include termination, signal name/number (when observed), the
conventional exit code, source/executable mapping, and stdout/stderr log paths.
In safe fallback mode, the primary attempt and fallback attempt share one
per-problem timeout budget; the fallback receives only the remaining time. The
global timeout, when set, is still shared by the whole verifier run.
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
  process_status_file="$5"
  shift 5

  : > "$stdout_file"
  : > "$stderr_file"

  timeout_file="$WORK/timeout.$$"
  timeout_marker="${RUN_TIMEOUT_MARKER:-}"
  rm -f "$timeout_file"
  if [ -n "$timeout_marker" ]; then
    rm -f "$timeout_marker"
  fi
  rm -f "$process_status_file"

  # Always start the command in its own session. A recursive process-tree
  # fallback is inherently racy once children are reparented, so use Perl's
  # POSIX::setsid on platforms (notably macOS) without a setsid executable.
  if command -v setsid >/dev/null 2>&1; then
    if command -v perl >/dev/null 2>&1; then
      set -- perl "$PROCESS_OBSERVER" "$process_status_file" "$@"
    fi
    setsid sh -c 'cd "$1" || exit 125; shift; exec "$@"' sh "$workdir" "$@" \
      > "$stdout_file" 2> "$stderr_file" &
    command_pid=$!
  elif command -v perl >/dev/null 2>&1; then
    # The Perl fallback is both the session supervisor and process observer.
    # Keeping those roles in one process avoids a nested process-group wrapper
    # on macOS and preserves the raw wait status before converting it to the
    # conventional shell exit code.
    perl -MPOSIX -MConfig -e '
      my $status_file = shift @ARGV;
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
        my ($termination, $exit_code, $signal_number, $signal_name);
        if ($status & 127) {
          $termination = "signal";
          $signal_number = $status & 127;
          my @names = split /\s+/, ($Config::Config{sig_name} // "");
          $signal_name = $names[$signal_number] || "SIGNAL$signal_number";
          $signal_name = "SIG$signal_name" unless $signal_name =~ /^SIG/;
          $exit_code = 128 + $signal_number;
        } else {
          $termination = "exit";
          $exit_code = $status >> 8;
          $signal_number = 0;
          $signal_name = "";
        }
        my $temporary = "$status_file.$$";
        open my $handle, ">", $temporary or die "status: $!\n";
        printf {$handle} "termination=%s\nexit_code=%d\nsignal_number=%d\nsignal_name=%s\n",
          $termination, $exit_code, $signal_number, $signal_name;
        close $handle or die "status close: $!\n";
        rename $temporary, $status_file or die "status publish: $!\n";
        exit $exit_code;
      }
      chdir $dir or exit 125;
      POSIX::setsid() >= 0 or die "setsid: $!\n";
      exec @cmd or die "exec: $!\n";
    ' -- \
      "$process_status_file" "$workdir" "$@" > "$stdout_file" 2> "$stderr_file" &
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
  if [ ! -f "$process_status_file" ]; then
    # A portable shell only exposes the conventional status here. Without the
    # observer, never guess that an intentional exit 139 was a signal.
    printf 'termination=unknown\nexit_code=%s\nsignal_number=0\nsignal_name=\n' \
      "$status" > "$process_status_file"
  fi
  rm -f "$timeout_file"
  ACTIVE_COMMAND_PID=
  ACTIVE_TIMER_PID=
  return "$status"
}

load_process_status() {
  status_file="$1"
  fallback_status="$2"
  PROCESS_TERMINATION=unknown
  PROCESS_EXIT_CODE="$fallback_status"
  PROCESS_SIGNAL_NUMBER=0
  PROCESS_SIGNAL_NAME=
  [ -f "$status_file" ] || return 0
  while IFS='=' read -r key value; do
    case "$key" in
      termination) PROCESS_TERMINATION="$value" ;;
      exit_code) PROCESS_EXIT_CODE="$value" ;;
      signal_number) PROCESS_SIGNAL_NUMBER="$value" ;;
      signal_name) PROCESS_SIGNAL_NAME="$value" ;;
    esac
  done < "$status_file"
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
SAFE_FALLBACK=
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
CAPTURE_ATTEMPT_FILE=
CAPTURE_ATTEMPT_STATUS=
CAPTURE_ATTEMPT_STAGE=
CAPTURE_ATTEMPT_EXIT_CODE=
CAPTURE_ATTEMPT_DURATION=
CAPTURE_ATTEMPT_FAILED=0
COUNT_RESULTS=1
ATTEMPT_SUFFIX=
CHECK_EXTRA_ARGS=
FALLBACK_USED=0
PRIMARY_FAILED=0
case "$0" in
  */*) SCRIPT_DIR="$(cd_physical "${0%/*}")" ;;
  *) SCRIPT_DIR="$(pwd)" ;;
esac
PROCESS_OBSERVER="$SCRIPT_DIR/observe-process.pl"

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
    --safe-fallback)
      [ "$#" -ge 2 ] || die "--safe-fallback requires a file"
      SAFE_FALLBACK="$2"
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
[ -z "$SAFE_FALLBACK" ] || [ -f "$SAFE_FALLBACK" ] || die "fallback config file not found: $SAFE_FALLBACK"
[ -f "$PROCESS_OBSERVER" ] || die "process observer not found: $PROCESS_OBSERVER"

JANUS_INPUT="$JANUS"
case "$JANUS_INPUT" in
  */*) JANUS="$(resolve_file_path "$JANUS_INPUT")" ;;
esac
if [ -n "$ARTIFACTS_DIR" ]; then
  ARTIFACTS_DIR="$(resolve_file_path "$ARTIFACTS_DIR")"
fi
PROJECT="$(cd_physical "$PROJECT")"
CONFIG="$(resolve_file_path "$CONFIG")"
if [ -n "$SAFE_FALLBACK" ]; then
  SAFE_FALLBACK="$(resolve_file_path "$SAFE_FALLBACK")"
fi
WORK="$(mktemp -d)"
FALLBACK_INDEX="$WORK/fallback-index.txt"
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
PROCESS_TERMINATION=unknown
PROCESS_EXIT_CODE=0
PROCESS_SIGNAL_NUMBER=0
PROCESS_SIGNAL_NAME=
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
  telemetry_source="${9:-}"
  telemetry_executable="${10:-}"
  telemetry_stdout="${11:-}"
  telemetry_stderr="${12:-}"
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
    [ -n "$CAPTURE_ATTEMPT_FILE" ] || FIRST=0
  else
    [ -n "$CAPTURE_ATTEMPT_FILE" ] || printf ',\n' >> "$TMP_REPORT"
  fi

  result_target="$TMP_REPORT"
  if [ -n "$CAPTURE_ATTEMPT_FILE" ]; then
    result_target="$CAPTURE_ATTEMPT_FILE"
    CAPTURE_ATTEMPT_STATUS="$status"
    CAPTURE_ATTEMPT_STAGE="$stage"
    CAPTURE_ATTEMPT_EXIT_CODE="$exit_code"
    CAPTURE_ATTEMPT_DURATION="$duration"
    case "$status" in
      ok) CAPTURE_ATTEMPT_FAILED=0 ;;
      *) CAPTURE_ATTEMPT_FAILED=1 ;;
    esac
    : > "$result_target"
  fi

  printf '{"problem":%s,"status":"%s","stage":"%s","exit_code":%s,"duration_seconds":%s,"message":"%s","compile_ok":%s,"run_ok":%s,"segfault":%s,"timeout":%s,"output_mismatch":%s' \
    "$problem" "$status" "$stage" "$exit_code" "$duration" \
    "$(json_escape_text "$message")" "$compile_ok" "$run_ok" \
    "$segfault" "$timeout" "$output_mismatch" >> "$result_target"
  if [ -n "$actual_file" ] && [ -f "$actual_file" ]; then
    printf ',"actual":"%s"' "$(json_escape "$actual_file")" >> "$result_target"
  fi
  if [ -n "$expected_file" ] && [ -f "$expected_file" ]; then
    printf ',"expected":"%s"' "$(json_escape "$expected_file")" >> "$result_target"
  fi
  if [ -n "$CAPTURE_ATTEMPT_FILE" ]; then
    CAPTURE_ATTEMPT_ACTUAL_FILE="$actual_file"
    CAPTURE_ATTEMPT_EXPECTED_FILE="$expected_file"
    CAPTURE_ATTEMPT_TELEMETRY_SOURCE=
    CAPTURE_ATTEMPT_TELEMETRY_EXECUTABLE=
    CAPTURE_ATTEMPT_TELEMETRY_STDOUT=
    CAPTURE_ATTEMPT_TELEMETRY_STDERR=
    CAPTURE_ATTEMPT_TERMINATION=
    CAPTURE_ATTEMPT_SIGNAL_NUMBER=
    CAPTURE_ATTEMPT_SIGNAL_NAME=
    CAPTURE_ATTEMPT_STACK_FILE=
  fi
  if [ -n "$telemetry_source" ]; then
    append_telemetry_fields "$result_target" "$PROCESS_TERMINATION" \
      "$PROCESS_EXIT_CODE" "$PROCESS_SIGNAL_NUMBER" "$PROCESS_SIGNAL_NAME" \
      "$telemetry_source" "$telemetry_executable" "$telemetry_stdout" \
      "$telemetry_stderr" "$actual_file"
    if [ -n "$CAPTURE_ATTEMPT_FILE" ]; then
      CAPTURE_ATTEMPT_TELEMETRY_SOURCE="$telemetry_source"
      CAPTURE_ATTEMPT_TELEMETRY_EXECUTABLE="$telemetry_executable"
      CAPTURE_ATTEMPT_TELEMETRY_STDOUT="$telemetry_stdout"
      CAPTURE_ATTEMPT_TELEMETRY_STDERR="$telemetry_stderr"
      CAPTURE_ATTEMPT_TERMINATION="$PROCESS_TERMINATION"
      CAPTURE_ATTEMPT_SIGNAL_NUMBER="$PROCESS_SIGNAL_NUMBER"
      CAPTURE_ATTEMPT_SIGNAL_NAME="$PROCESS_SIGNAL_NAME"
      CAPTURE_ATTEMPT_STACK_FILE="$actual_file"
    fi
  fi
  printf '}' >> "$result_target"
}

append_telemetry_fields() {
  result_target="$1"
  process_termination="$2"
  process_exit_code="$3"
  process_signal_number="$4"
  process_signal_name="$5"
  telemetry_source="$6"
  telemetry_executable="$7"
  telemetry_stdout="$8"
  telemetry_stderr="$9"
  stack_file="${10}"

  printf ',"termination":"%s","conventional_exit_code":%s' \
    "$(json_escape_text "$process_termination")" "$process_exit_code" >> "$result_target"
  if [ "$process_termination" = signal ]; then
    printf ',"signal_number":%s,"signal_name":"%s"' \
      "$process_signal_number" "$(json_escape_text "$process_signal_name")" >> "$result_target"
  else
    printf ',"signal_number":null,"signal_name":null' >> "$result_target"
  fi
  printf ',"source":"%s","executable":"%s","stdout_log":"%s","stderr_log":"%s"' \
    "$(json_escape_text "$telemetry_source")" \
    "$(json_escape_text "$telemetry_executable")" \
    "$(json_escape_text "$telemetry_stdout")" \
    "$(json_escape_text "$telemetry_stderr")" >> "$result_target"
  if [ "$process_termination" = signal ] && [ -n "$stack_file" ] && [ -s "$stack_file" ]; then
    printf ',"stack_excerpt":"%s"' "$(json_escape "$stack_file")" >> "$result_target"
  fi
}

crash_log_path() {
  problem="$1"
  stage="$2"
  stream="$3"
  problem_id="$problem$ATTEMPT_SUFFIX"
  if [ "$ARTIFACTS_ENABLED" -eq 1 ] && [ -n "$ARTIFACT_RUN_DIR" ]; then
    printf '%s/problems/%s/%s.%s\n' "$ARTIFACT_RUN_DIR" "$problem_id" "$stage" "$stream"
  else
    printf '%s/%s-%s.%s\n' "$WORK" "$stage" "$problem_id" \
      "$( [ "$stream" = stdout ] && printf out || printf err )"
  fi
}

append_process_failure() {
  problem="$1"
  stage="$2"
  status="$3"
  duration="$4"
  source="$5"
  executable="$6"
  stderr_file="$7"
  load_process_status "$8" "$status"
  stdout_log="$(crash_log_path "$problem" "$stage" stdout)"
  stderr_log="$(crash_log_path "$problem" "$stage" stderr)"
  if [ "$PROCESS_TERMINATION" = signal ]; then
    case "$PROCESS_SIGNAL_NAME" in
      SIGSEGV) result_status=segfault ;;
      *) result_status=crash ;;
    esac
    message="$executable terminated by $PROCESS_SIGNAL_NAME ($PROCESS_SIGNAL_NUMBER)"
    echo "problem $problem ... $result_status ($stage: $PROCESS_SIGNAL_NAME/$PROCESS_SIGNAL_NUMBER, exit $PROCESS_EXIT_CODE)" >&2
  else
    case "$stage" in
      check|build) result_status=error ;;
      *) result_status=crash ;;
    esac
    message="$executable exited with code $PROCESS_EXIT_CODE"
    echo "problem $problem ... $result_status ($stage exit $PROCESS_EXIT_CODE)" >&2
  fi
  echo "  source: $source" >&2
  echo "  executable: $executable" >&2
  echo "  logs: stdout=$stdout_log stderr=$stderr_log" >&2
  append_result "$problem" "$result_status" "$stage" "$PROCESS_EXIT_CODE" \
    "$duration" "$message" "$stderr_file" "" "$source" "$executable" \
    "$stdout_log" "$stderr_log"
}

record_total() {
  [ "$COUNT_RESULTS" -eq 1 ] || return 0
  TOTAL=$((TOTAL + 1))
}

record_pass() {
  [ "$COUNT_RESULTS" -eq 1 ] || return 0
  PASSED=$((PASSED + 1))
}

record_fail() {
  [ "$COUNT_RESULTS" -eq 1 ] || return 0
  FAILED=$((FAILED + 1))
  EXIT_STATUS=1
}

run_problem() {
  number="$1"
  source_rel="$2"
  expected_rel="$3"

  record_total
  start="$(now_seconds)"
  if [ -n "${CASE_DEADLINE_OVERRIDE:-}" ]; then
    case_deadline="$CASE_DEADLINE_OVERRIDE"
  else
    case_deadline=$((start + CASE_TIMEOUT))
  fi
  source="$PROJECT/$source_rel"
  expected="$PROJECT/$expected_rel"
  attempt_id="$number$ATTEMPT_SUFFIX"
  executable="$WORK/problem-$attempt_id"
  check_out="$WORK/check-$attempt_id.out"
  check_err="$WORK/check-$attempt_id.err"
  check_status="$WORK/check-$attempt_id.status"
  check_timeout="$WORK/check-$attempt_id.timeout"
  build_out="$WORK/build-$attempt_id.out"
  build_err="$WORK/build-$attempt_id.err"
  build_status="$WORK/build-$attempt_id.status"
  build_timeout="$WORK/build-$attempt_id.timeout"
  run_out="$WORK/run-$attempt_id.out"
  run_err="$WORK/run-$attempt_id.err"
  run_status="$WORK/run-$attempt_id.status"
  run_timeout="$WORK/run-$attempt_id.timeout"
  actual_norm="$WORK/actual-$attempt_id.txt"
  expected_norm="$WORK/expected-$attempt_id.txt"

  if [ -n "$GLOBAL_DEADLINE" ] && [ "$(remaining_timeout "$GLOBAL_DEADLINE")" -le 0 ]; then
    record_fail
    echo "problem $number ... timeout (global)" >&2
    append_result "$number" timeout global 124 0 "global timeout exceeded" "" ""
    return
  fi
  if [ ! -f "$source" ]; then
    record_fail
    echo "problem $number ... error (missing source)" >&2
    append_result "$number" error source 1 0 "source not found: $source_rel" "" ""
    return
  fi
  if [ ! -f "$expected" ]; then
    record_fail
    echo "problem $number ... error (missing expected output)" >&2
    append_result "$number" error expected 1 0 "expected output not found: $expected_rel" "" ""
    return
  fi

  timeout_left="$(problem_timeout_left "$case_deadline")"
  if [ "$timeout_left" -le 0 ]; then
    record_fail
    duration=$(($(now_seconds) - start))
    echo "problem $number ... timeout" >&2
    append_result "$number" timeout check 124 "$duration" "problem timeout exceeded before check" "" ""
    return
  fi
  set +e
  if [ -n "$CHECK_EXTRA_ARGS" ]; then
    RUN_TIMEOUT_MARKER="$check_timeout" \
      run_with_timeout "$timeout_left" "$check_out" "$check_err" "$PROJECT" "$check_status" \
        "$JANUS" check "$source" $CHECK_EXTRA_ARGS
  else
    RUN_TIMEOUT_MARKER="$check_timeout" \
      run_with_timeout "$timeout_left" "$check_out" "$check_err" "$PROJECT" "$check_status" \
        "$JANUS" check "$source"
  fi
  status=$?
  set -e
  if [ -f "$check_timeout" ]; then
    record_fail
    duration=$(($(now_seconds) - start))
    echo "problem $number ... timeout (check)" >&2
    append_result "$number" timeout check 124 "$duration" "janus check timed out" "" ""
    return
  fi
  if [ "$status" -ne 0 ]; then
    record_fail
    duration=$(($(now_seconds) - start))
    append_process_failure "$number" check "$status" "$duration" "$source" \
      "$JANUS" "$check_err" "$check_status"
    return
  fi

  timeout_left="$(problem_timeout_left "$case_deadline")"
  if [ "$timeout_left" -le 0 ]; then
    record_fail
    duration=$(($(now_seconds) - start))
    echo "problem $number ... timeout" >&2
    append_result "$number" timeout build 124 "$duration" "problem timeout exceeded before build" "" ""
    return
  fi
  set +e
  if [ "$RELEASE" -eq 1 ]; then
        RUN_TIMEOUT_MARKER="$build_timeout" \
          run_with_timeout "$timeout_left" "$build_out" "$build_err" "$PROJECT" "$build_status" \
            "$JANUS" build "$source" -o "$executable" --release
  else
        RUN_TIMEOUT_MARKER="$build_timeout" \
          run_with_timeout "$timeout_left" "$build_out" "$build_err" "$PROJECT" "$build_status" \
            "$JANUS" build "$source" -o "$executable"
  fi
  status=$?
  set -e
  if [ -f "$build_timeout" ]; then
    record_fail
    duration=$(($(now_seconds) - start))
    echo "problem $number ... timeout (build)" >&2
    append_result "$number" timeout build 124 "$duration" "janus build timed out" "" ""
    return
  fi
  if [ "$status" -ne 0 ]; then
    record_fail
    duration=$(($(now_seconds) - start))
    append_process_failure "$number" build "$status" "$duration" "$source" \
      "$JANUS" "$build_err" "$build_status"
    return
  fi

  timeout_left="$(problem_timeout_left "$case_deadline")"
  if [ "$timeout_left" -le 0 ]; then
    record_fail
    duration=$(($(now_seconds) - start))
    echo "problem $number ... timeout" >&2
    append_result "$number" timeout run 124 "$duration" "problem timeout exceeded before run" "" ""
    return
  fi
  set +e
  RUN_TIMEOUT_MARKER="$run_timeout" \
    run_with_timeout "$timeout_left" "$run_out" "$run_err" "$PROJECT" "$run_status" "$executable"
  status=$?
  set -e
  duration=$(($(now_seconds) - start))
  if [ -f "$run_timeout" ]; then
    record_fail
    echo "problem $number ... timeout (run)" >&2
    append_result "$number" timeout run 124 "$duration" "executable timed out" "" ""
    return
  fi
  if [ "$status" -ne 0 ]; then
    record_fail
    append_process_failure "$number" run "$status" "$duration" "$source" \
      "$executable" "$run_err" "$run_status"
    return
  fi

  normalize_output "$run_out" "$actual_norm"
  normalize_output "$expected" "$expected_norm"
  if cmp -s "$actual_norm" "$expected_norm"; then
    record_pass
    echo "problem $number ... ok" >&2
    append_result "$number" ok run 0 "$duration" "" "$actual_norm" "$expected_norm"
  else
    record_fail
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

lookup_config_entry() {
  lookup_file="$1"
  lookup_number="$2"
  LOOKUP_SOURCE=
  LOOKUP_EXPECTED=
  while IFS= read -r lookup_line || [ -n "$lookup_line" ]; do
    case "$lookup_line" in
      ''|'#'*) continue ;;
    esac
    old_ifs=$IFS
    IFS='|'
    set -- $lookup_line
    IFS=$old_ifs
    [ "$#" -eq 3 ] || die "invalid config line: $lookup_line"
    is_number "$1" || die "invalid problem number in config: $1"
    if [ "$1" = "$lookup_number" ]; then
      LOOKUP_SOURCE="$2"
      LOOKUP_EXPECTED="$3"
      return 0
    fi
  done < "$lookup_file"
  return 1
}

validate_fallback_config() {
  [ -n "$SAFE_FALLBACK" ] || return 0
  : > "$FALLBACK_INDEX"
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
    if grep -q "^$number|" "$FALLBACK_INDEX"; then
      die "duplicate problem in fallback config: $number"
    fi
    printf '%s|%s|%s\n' "$number" "$2" "$3" >> "$FALLBACK_INDEX"
  done < "$SAFE_FALLBACK"
}

validate_fallback_config_selection() {
  [ -n "$SAFE_FALLBACK" ] || return 0
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
      if ! lookup_config_entry "$FALLBACK_INDEX" "$number"; then
        die "fallback config missing problem: $number"
      fi
      [ -f "$PROJECT/$LOOKUP_SOURCE" ] || die "fallback source not found for problem $number: $LOOKUP_SOURCE"
      [ -f "$PROJECT/$LOOKUP_EXPECTED" ] || die "fallback expected output not found for problem $number: $LOOKUP_EXPECTED"
      if [ -f "$PROJECT/$expected_rel" ]; then
        primary_expected_norm="$WORK/validate-primary-$number.txt"
        fallback_expected_norm="$WORK/validate-fallback-$number.txt"
        normalize_output "$PROJECT/$expected_rel" "$primary_expected_norm"
        normalize_output "$PROJECT/$LOOKUP_EXPECTED" "$fallback_expected_norm"
        if ! cmp -s "$primary_expected_norm" "$fallback_expected_norm"; then
          die "fallback expected output does not match primary for problem $number"
        fi
      fi
    fi
  done < "$CONFIG"
}

append_json_separator() {
  if [ "$FIRST" -eq 1 ]; then
    FIRST=0
  else
    printf ',\n' >> "$TMP_REPORT"
  fi
}

append_safe_result() {
  number="$1"
  status="$2"
  stage="$3"
  exit_code="$4"
  duration="$5"
  message="$6"
  compile_ok="$7"
  run_ok="$8"
  segfault="$9"
  timeout="${10}"
  output_mismatch="${11}"
  fallback_used="${12}"
  primary_file="${13}"
  fallback_file="${14}"
  telemetry_source="${15:-}"
  telemetry_executable="${16:-}"
  telemetry_stdout="${17:-}"
  telemetry_stderr="${18:-}"
  telemetry_termination="${19:-}"
  telemetry_exit_code="${20:-0}"
  telemetry_signal_number="${21:-0}"
  telemetry_signal_name="${22:-}"
  telemetry_stack_file="${23:-}"
  actual_file="${24:-}"
  expected_file="${25:-}"

  append_json_separator
  printf '{"problem":%s,"status":"%s","stage":"%s","exit_code":%s,"duration_seconds":%s,"message":"%s","compile_ok":%s,"run_ok":%s,"segfault":%s,"timeout":%s,"output_mismatch":%s,"fallback_used":%s,"primary":' \
    "$number" "$status" "$stage" "$exit_code" "$duration" \
    "$(json_escape_text "$message")" "$compile_ok" "$run_ok" "$segfault" \
    "$timeout" "$output_mismatch" "$fallback_used" >> "$TMP_REPORT"
  cat "$primary_file" >> "$TMP_REPORT"
  if [ -n "$fallback_file" ] && [ -f "$fallback_file" ]; then
    printf ',"fallback":' >> "$TMP_REPORT"
    cat "$fallback_file" >> "$TMP_REPORT"
  else
    printf ',"fallback":null' >> "$TMP_REPORT"
  fi
  if [ -n "$actual_file" ] && [ -f "$actual_file" ]; then
    printf ',"actual":"%s"' "$(json_escape "$actual_file")" >> "$TMP_REPORT"
  fi
  if [ -n "$expected_file" ] && [ -f "$expected_file" ]; then
    printf ',"expected":"%s"' "$(json_escape "$expected_file")" >> "$TMP_REPORT"
  fi
  if [ -n "$telemetry_source" ]; then
    append_telemetry_fields "$TMP_REPORT" "$telemetry_termination" \
      "$telemetry_exit_code" "$telemetry_signal_number" "$telemetry_signal_name" \
      "$telemetry_source" "$telemetry_executable" "$telemetry_stdout" \
      "$telemetry_stderr" "$telemetry_stack_file"
  fi
  printf '}' >> "$TMP_REPORT"
}

attempt_problem() {
  attempt_file="$1"
  number="$2"
  source_rel="$3"
  expected_rel="$4"
  suffix="$5"
  check_args="$6"

  CAPTURE_ATTEMPT_FILE="$attempt_file"
  CAPTURE_ATTEMPT_STATUS=
  CAPTURE_ATTEMPT_STAGE=
  CAPTURE_ATTEMPT_EXIT_CODE=0
  CAPTURE_ATTEMPT_DURATION=0
  CAPTURE_ATTEMPT_FAILED=0
  CAPTURE_ATTEMPT_TELEMETRY_SOURCE=
  CAPTURE_ATTEMPT_TELEMETRY_EXECUTABLE=
  CAPTURE_ATTEMPT_TELEMETRY_STDOUT=
  CAPTURE_ATTEMPT_TELEMETRY_STDERR=
  CAPTURE_ATTEMPT_TERMINATION=
  CAPTURE_ATTEMPT_SIGNAL_NUMBER=
  CAPTURE_ATTEMPT_SIGNAL_NAME=
  CAPTURE_ATTEMPT_STACK_FILE=
  CAPTURE_ATTEMPT_ACTUAL_FILE=
  CAPTURE_ATTEMPT_EXPECTED_FILE=
  COUNT_RESULTS=0
  ATTEMPT_SUFFIX="$suffix"
  CHECK_EXTRA_ARGS="$check_args"
  run_problem "$number" "$source_rel" "$expected_rel"
  CAPTURE_ATTEMPT_FILE=
  COUNT_RESULTS=1
  ATTEMPT_SUFFIX=
  CHECK_EXTRA_ARGS=
}

run_problem_safe() {
  number="$1"
  source_rel="$2"
  expected_rel="$3"

  record_total
  safe_start="$(now_seconds)"
  CASE_DEADLINE_OVERRIDE=$((safe_start + CASE_TIMEOUT))
  primary_file="$WORK/primary-$number.json"
  fallback_file="$WORK/fallback-$number.json"
  attempt_problem "$primary_file" "$number" "$source_rel" "$expected_rel" ".primary" "--warn-high-growth-loops"
  primary_status="$CAPTURE_ATTEMPT_STATUS"
  primary_stage="$CAPTURE_ATTEMPT_STAGE"
  primary_exit="$CAPTURE_ATTEMPT_EXIT_CODE"
  primary_duration="$CAPTURE_ATTEMPT_DURATION"
  primary_failed="$CAPTURE_ATTEMPT_FAILED"
  primary_telemetry_source="$CAPTURE_ATTEMPT_TELEMETRY_SOURCE"
  primary_telemetry_executable="$CAPTURE_ATTEMPT_TELEMETRY_EXECUTABLE"
  primary_telemetry_stdout="$CAPTURE_ATTEMPT_TELEMETRY_STDOUT"
  primary_telemetry_stderr="$CAPTURE_ATTEMPT_TELEMETRY_STDERR"
  primary_telemetry_termination="$CAPTURE_ATTEMPT_TERMINATION"
  primary_telemetry_signal_number="$CAPTURE_ATTEMPT_SIGNAL_NUMBER"
  primary_telemetry_signal_name="$CAPTURE_ATTEMPT_SIGNAL_NAME"
  primary_telemetry_stack_file="$CAPTURE_ATTEMPT_STACK_FILE"
  primary_actual_file="$CAPTURE_ATTEMPT_ACTUAL_FILE"
  primary_expected_file="$CAPTURE_ATTEMPT_EXPECTED_FILE"
  if [ "$primary_failed" -eq 0 ]; then
    record_pass
    safe_duration=$(($(now_seconds) - safe_start))
    append_safe_result "$number" ok "$primary_stage" "$primary_exit" "$safe_duration" "" \
      true true false false false false "$primary_file" "" \
      "$primary_telemetry_source" "$primary_telemetry_executable" \
      "$primary_telemetry_stdout" "$primary_telemetry_stderr" \
      "$primary_telemetry_termination" "$primary_exit" \
      "$primary_telemetry_signal_number" "$primary_telemetry_signal_name" \
      "$primary_telemetry_stack_file" "$primary_actual_file" "$primary_expected_file"
    CASE_DEADLINE_OVERRIDE=
    return
  fi

  PRIMARY_FAILED=$((PRIMARY_FAILED + 1))
  echo "problem $number ... primary failed: $primary_status" >&2
  lookup_config_entry "$FALLBACK_INDEX" "$number" || die "fallback config missing problem: $number"
  fallback_source_rel="$LOOKUP_SOURCE"
  fallback_expected_rel="$LOOKUP_EXPECTED"
  attempt_problem "$fallback_file" "$number" "$fallback_source_rel" "$fallback_expected_rel" ".fallback" ""
  fallback_status="$CAPTURE_ATTEMPT_STATUS"
  fallback_stage="$CAPTURE_ATTEMPT_STAGE"
  fallback_exit="$CAPTURE_ATTEMPT_EXIT_CODE"
  fallback_duration="$CAPTURE_ATTEMPT_DURATION"
  fallback_failed="$CAPTURE_ATTEMPT_FAILED"
  fallback_telemetry_source="$CAPTURE_ATTEMPT_TELEMETRY_SOURCE"
  fallback_telemetry_executable="$CAPTURE_ATTEMPT_TELEMETRY_EXECUTABLE"
  fallback_telemetry_stdout="$CAPTURE_ATTEMPT_TELEMETRY_STDOUT"
  fallback_telemetry_stderr="$CAPTURE_ATTEMPT_TELEMETRY_STDERR"
  fallback_telemetry_termination="$CAPTURE_ATTEMPT_TERMINATION"
  fallback_telemetry_signal_number="$CAPTURE_ATTEMPT_SIGNAL_NUMBER"
  fallback_telemetry_signal_name="$CAPTURE_ATTEMPT_SIGNAL_NAME"
  fallback_telemetry_stack_file="$CAPTURE_ATTEMPT_STACK_FILE"
  fallback_actual_file="$CAPTURE_ATTEMPT_ACTUAL_FILE"
  fallback_expected_file="$CAPTURE_ATTEMPT_EXPECTED_FILE"
  FALLBACK_USED=$((FALLBACK_USED + 1))
  if [ "$fallback_failed" -eq 0 ]; then
    record_pass
    echo "problem $number ... fallback" >&2
    safe_duration=$(($(now_seconds) - safe_start))
    append_safe_result "$number" fallback "$fallback_stage" "$fallback_exit" \
      "$safe_duration" "primary failed; fallback succeeded" \
      true true false false false true "$primary_file" "$fallback_file" \
      "$fallback_telemetry_source" "$fallback_telemetry_executable" \
      "$fallback_telemetry_stdout" "$fallback_telemetry_stderr" \
      "$fallback_telemetry_termination" "$fallback_exit" \
      "$fallback_telemetry_signal_number" "$fallback_telemetry_signal_name" \
      "$fallback_telemetry_stack_file" "$fallback_actual_file" "$fallback_expected_file"
    CASE_DEADLINE_OVERRIDE=
    return
  fi

  record_fail
  fallback_compile_ok=false
  fallback_run_ok=false
  fallback_segfault=false
  fallback_timeout=false
  fallback_output_mismatch=false
  [ "$fallback_stage" = run ] && fallback_compile_ok=true
  if [ "$fallback_stage" = run ] && [ "$fallback_exit" -eq 0 ]; then
    fallback_run_ok=true
  fi
  case "$fallback_status" in
    segfault) fallback_segfault=true ;;
    timeout) fallback_timeout=true ;;
    mismatch) fallback_output_mismatch=true ;;
  esac
  safe_duration=$(($(now_seconds) - safe_start))
  append_safe_result "$number" "$fallback_status" "$fallback_stage" "$fallback_exit" \
    "$safe_duration" "primary and fallback failed" \
    "$fallback_compile_ok" "$fallback_run_ok" "$fallback_segfault" \
    "$fallback_timeout" "$fallback_output_mismatch" true "$primary_file" "$fallback_file" \
    "$fallback_telemetry_source" "$fallback_telemetry_executable" \
    "$fallback_telemetry_stdout" "$fallback_telemetry_stderr" \
    "$fallback_telemetry_termination" "$fallback_exit" \
    "$fallback_telemetry_signal_number" "$fallback_telemetry_signal_name" \
    "$fallback_telemetry_stack_file" "$fallback_actual_file" "$fallback_expected_file"
  CASE_DEADLINE_OVERRIDE=
}

validate_config_selection
validate_fallback_config
validate_fallback_config_selection
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
    if [ -n "$SAFE_FALLBACK" ]; then
      run_problem_safe "$number" "$source_rel" "$expected_rel"
    else
      run_problem "$number" "$source_rel" "$expected_rel"
    fi
  fi
done < "$CONFIG"

RUN_DURATION=$(($(now_seconds) - RUN_START))
RUN_MESSAGE=
if [ "$EXIT_STATUS" -ne 0 ]; then
  RUN_MESSAGE="one or more problems failed"
fi
if [ -n "$SAFE_FALLBACK" ]; then
  SUMMARY_EXTRA=',"fallback_used":'"$FALLBACK_USED"',"primary_failed":'"$PRIMARY_FAILED"
else
  SUMMARY_EXTRA=
fi
printf '{"run":{"run_id":"%s","report":"%s","exit_code":%s,"duration_seconds":%s,"message":"%s"},"summary":{"total":%s,"ok":%s,"failed":%s%s},"results":[\n' \
  "$(json_escape_text "$ARTIFACT_RUN_REL")" \
  "$(json_escape_text "$ARTIFACT_REPORT_REL")" \
  "$EXIT_STATUS" "$RUN_DURATION" "$(json_escape_text "$RUN_MESSAGE")" \
  "$TOTAL" "$PASSED" "$FAILED" "$SUMMARY_EXTRA" > "$REPORT"
cat "$TMP_REPORT" >> "$REPORT"
printf '\n]}\n' >> "$REPORT"

persist_artifacts
persist_json_out
cat "$REPORT"
echo "summary: $PASSED/$TOTAL ok, $FAILED failed" >&2

exit "$EXIT_STATUS"
