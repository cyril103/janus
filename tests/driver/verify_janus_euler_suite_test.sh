#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <source-dir>" >&2
  exit 2
fi

SOURCE_DIR="$1"
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
sleep 2
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

if PATH="$FAKE_BIN:/usr/bin:/bin" \
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

FALLBACK_BIN="$WORK/fallback-bin"
mkdir -p "$FALLBACK_BIN"
for tool in awk cat chmod cmp cp date grep mkdir mktemp od perl ps rm sed sleep; do
  if tool_path="$(command -v "$tool")"; then
    ln -s "$tool_path" "$FALLBACK_BIN/$tool"
  fi
done
ln -s "$FAKE_BIN/janus" "$FALLBACK_BIN/janus"

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
