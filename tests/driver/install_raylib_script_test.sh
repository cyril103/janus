#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <source-dir>" >&2
  exit 2
fi

SOURCE_DIR="$1"
OUTPUT="$(
  JANUS_RAYLIB_VERSION=test-version \
  JANUS_RAYLIB_REVISION=test-revision \
    "$SOURCE_DIR/scripts/install-raylib.sh" \
      --prefix /tmp/janus-raylib-test \
      --skip-dependencies \
      --dry-run
)"

echo "$OUTPUT" | grep -q "raylib version: test-version"
echo "$OUTPUT" | grep -q "raylib revision: test-revision"
echo "$OUTPUT" | grep -q "prefix: /tmp/janus-raylib-test"
echo "$OUTPUT" | grep -q "install dependencies: 0"

if "$SOURCE_DIR/scripts/install-raylib.sh" --unknown >/dev/null 2>&1; then
  echo "raylib install test: an unknown option was accepted" >&2
  exit 1
fi
