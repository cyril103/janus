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

config format:
  N|source.janus|expected-output-file
  Empty lines and lines starting with # are ignored.
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
  shift 3

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
    setsid "$@" > "$stdout_file" 2> "$stderr_file" &
    command_pid=$!
  elif command -v perl >/dev/null 2>&1; then
    perl -MPOSIX -e 'POSIX::setsid() >= 0 or die "setsid: $!\n"; exec @ARGV or die "exec: $!\n"' -- \
      "$@" > "$stdout_file" 2> "$stderr_file" &
    command_pid=$!
  else
    printf '%s\n' 'timeout support requires setsid or perl' > "$stderr_file"
    return 125
  fi

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
    return 124
  fi
  status="$wait_status"
  rm -f "$timeout_file"
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
PROJECT="$(cd_physical "$PROJECT")"
CONFIG="$(resolve_file_path "$CONFIG")"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

REPORT="$WORK/report.json"
TMP_REPORT="$WORK/results.json"
: > "$TMP_REPORT"

TOTAL=0
PASSED=0
FAILED=0
FIRST=1
EXIT_STATUS=0
GLOBAL_DEADLINE=
if [ -n "$GLOBAL_TIMEOUT" ]; then
  GLOBAL_DEADLINE=$(($(now_seconds) + GLOBAL_TIMEOUT))
fi

append_result() {
  problem="$1"
  status="$2"
  stage="$3"
  exit_code="$4"
  duration="$5"
  message="$6"
  actual_file="$7"
  expected_file="$8"

  if [ "$FIRST" -eq 1 ]; then
    FIRST=0
  else
    printf ',\n' >> "$TMP_REPORT"
  fi

  printf '{"problem":%s,"status":"%s","stage":"%s","exit_code":%s,"duration_seconds":%s,"message":"%s"' \
    "$problem" "$status" "$stage" "$exit_code" "$duration" \
    "$(json_escape_text "$message")" >> "$TMP_REPORT"
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
  (
    cd "$PROJECT" &&
      RUN_TIMEOUT_MARKER="$check_timeout" \
        run_with_timeout "$timeout_left" "$check_out" "$check_err" \
          "$JANUS" check "$source"
  )
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
    (
      cd "$PROJECT" &&
        RUN_TIMEOUT_MARKER="$build_timeout" \
          run_with_timeout "$timeout_left" "$build_out" "$build_err" \
            "$JANUS" build "$source" -o "$executable" --release
    )
  else
    (
      cd "$PROJECT" &&
        RUN_TIMEOUT_MARKER="$build_timeout" \
          run_with_timeout "$timeout_left" "$build_out" "$build_err" \
            "$JANUS" build "$source" -o "$executable"
    )
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
    run_with_timeout "$timeout_left" "$run_out" "$run_err" "$executable"
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
  source_rel="$2"
  expected_rel="$3"
  is_number "$number" || die "invalid problem number in config: $number"
  if [ "$MODE" = all ] || [ "$number" = "$REQUESTED_PROBLEM" ]; then
    FOUND=1
    run_problem "$number" "$source_rel" "$expected_rel"
  fi
done < "$CONFIG"

if [ "$FOUND" -eq 0 ]; then
  die "problem not found in config: $REQUESTED_PROBLEM"
fi

printf '{"summary":{"total":%s,"ok":%s,"failed":%s},"results":[\n' \
  "$TOTAL" "$PASSED" "$FAILED" > "$REPORT"
cat "$TMP_REPORT" >> "$REPORT"
printf '\n]}\n' >> "$REPORT"

cat "$REPORT"
if [ -n "$JSON_OUT" ]; then
  cp "$REPORT" "$JSON_OUT"
fi
echo "summary: $PASSED/$TOTAL ok, $FAILED failed" >&2

exit "$EXIT_STATUS"
