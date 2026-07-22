#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <source-dir>" >&2
  exit 2
fi

SOURCE_DIR="$(cd "$1" && pwd)"
FIXTURE_DIR="$SOURCE_DIR/tests/fixtures/project-euler"
SMOKE_CONFIG="$FIXTURE_DIR/smoke.txt"
PRODUCTION_CONFIG="$FIXTURE_DIR/production.txt"

[ -d "$FIXTURE_DIR" ] || {
  echo "missing fixture directory: tests/fixtures/project-euler" >&2
  exit 1
}

validate_config() {
  profile="$1"
  config="$2"
  expected_source_dir="$3"
  expected_seen=
  line_count=0
  last_number=0
  seen=" "

  [ -f "$config" ] || {
    echo "missing $profile config: $config" >&2
    exit 1
  }

  while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in
      ''|'#'*) continue ;;
    esac
    old_ifs=$IFS
    IFS='|'
    set -- $line
    IFS=$old_ifs
    if [ "$#" -ne 3 ]; then
      echo "$profile config has invalid line: $line" >&2
      exit 1
    fi
    number="$1"
    source_rel="$2"
    expected_rel="$3"
    case "$number" in
      ''|*[!0-9]*)
        echo "$profile config has non-numeric problem: $number" >&2
        exit 1
        ;;
    esac
    line_count=$((line_count + 1))
    if [ "$number" -ne "$line_count" ]; then
      echo "$profile config must list exactly 1..20 in order" >&2
      exit 1
    fi
    case "$seen" in
      *" $number "*)
        echo "$profile config duplicates problem $number" >&2
        exit 1
        ;;
    esac
    seen="$seen$number "
    last_number="$number"
    case "$source_rel" in
      "$expected_source_dir"/problem"$number".janus) ;;
      *)
        echo "$profile problem $number uses unexpected source path: $source_rel" >&2
        exit 1
        ;;
    esac
    if [ "$expected_rel" != "expected/problem$number.txt" ]; then
      echo "$profile problem $number uses unexpected expected path: $expected_rel" >&2
      exit 1
    fi
    [ -f "$FIXTURE_DIR/$source_rel" ] || {
      echo "$profile problem $number missing source: $source_rel" >&2
      exit 1
    }
    [ -f "$FIXTURE_DIR/$expected_rel" ] || {
      echo "$profile problem $number missing expected: $expected_rel" >&2
      exit 1
    }
  done < "$config"

  if [ "$line_count" -ne 20 ] || [ "$last_number" -ne 20 ]; then
    echo "$profile config must contain exactly problems 1..20" >&2
    exit 1
  fi
}

validate_config smoke "$SMOKE_CONFIG" smoke
validate_config production "$PRODUCTION_CONFIG" production

number=1
while [ "$number" -le 20 ]; do
  smoke="$FIXTURE_DIR/smoke/problem$number.janus"
  production="$FIXTURE_DIR/production/problem$number.janus"
  expected="$FIXTURE_DIR/expected/problem$number.txt"
  [ -s "$expected" ] || {
    echo "problem $number expected answer must not be empty" >&2
    exit 1
  }
  if cmp -s "$smoke" "$production"; then
    echo "problem $number smoke and production sources must be distinct" >&2
    exit 1
  fi

  if ! grep -Eq '(^|[[:space:]])(while|for)([[:space:]]|$)' "$production"; then
    echo "problem $number production source must contain an algorithmic loop" >&2
    exit 1
  fi

  expected_answer="$(sed 's/[[:space:]]*$//' "$expected")"
  if grep -Eq "return[[:space:]]+$expected_answer(\\.0)?([[:space:]]|$)" "$production"; then
    echo "problem $number production source must not directly return the expected answer" >&2
    exit 1
  fi

  number=$((number + 1))
done

if grep -Eiq 'placeholder|deferred|canonical answer|winning window|winning diagonal|precomputed|hardcoded' "$FIXTURE_DIR"/production/problem*.janus; then
  echo "production sources must not contain placeholder or precomputed markers" >&2
  exit 1
fi

if ! grep -q 'NAME project_euler.smoke' "$SOURCE_DIR/CMakeLists.txt"; then
  echo "missing CTest entry project_euler.smoke" >&2
  exit 1
fi
if ! grep -q 'tests/fixtures/project-euler/smoke.txt' "$SOURCE_DIR/CMakeLists.txt"; then
  echo "project_euler.smoke must use the versioned smoke manifest" >&2
  exit 1
fi

echo "project-euler fixture structure is covered"
