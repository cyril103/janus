#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <janus-linux-archive.tar.gz>" >&2
  exit 2
fi

ARCHIVE="$(realpath "$1")"
CHECKSUM="${ARCHIVE}.sha256"

if [ ! -f "$ARCHIVE" ]; then
  echo "smoke test: archive not found: $ARCHIVE" >&2
  exit 1
fi
if [ ! -f "$CHECKSUM" ]; then
  echo "smoke test: checksum not found: $CHECKSUM" >&2
  exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
  (cd "$(dirname "$ARCHIVE")" && sha256sum -c "$(basename "$CHECKSUM")")
elif command -v shasum >/dev/null 2>&1; then
  (cd "$(dirname "$ARCHIVE")" &&
    shasum -a 256 -c "$(basename "$CHECKSUM")")
else
  echo "smoke test: no SHA-256 tool is available" >&2
  exit 1
fi

sha256_digest() {
  if command -v sha256sum >/dev/null 2>&1; then
    digest_output="$(sha256sum "$1")"
  else
    digest_output="$(shasum -a 256 "$1")"
  fi
  printf '%s\n' "${digest_output%% *}"
}

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
mkdir -p "$WORK/package" "$WORK/home" "$WORK/cache" "$WORK/registry"
tar -xzf "$ARCHIVE" -C "$WORK/package"

PACKAGE_ROOT="$(find "$WORK/package" -mindepth 1 -maxdepth 1 -type d \
  -name 'janus-*' -print -quit)"
if [ -z "$PACKAGE_ROOT" ] || [ ! -x "$PACKAGE_ROOT/bin/janus" ]; then
  echo "smoke test: invalid Janus archive layout" >&2
  exit 1
fi
if command -v ldd >/dev/null 2>&1 &&
   ldd "$PACKAGE_ROOT/bin/clang" | grep -q 'not found'; then
  echo "smoke test: bundled Clang has unresolved shared libraries" >&2
  ldd "$PACKAGE_ROOT/bin/clang" >&2
  exit 1
fi

JANUS="$PACKAGE_ROOT/bin/janus"
"$PACKAGE_ROOT/bin/janus-lsp" --version
mkdir -p "$WORK/stdlib-reference"
"$JANUS" doc --stdlib --offline -o "$WORK/stdlib-reference"
for reference_file in api-index.json index.html; do
  generated_digest="$(
    sha256_digest "$WORK/stdlib-reference/$reference_file"
  )"
  packaged_digest="$(
    sha256_digest \
      "$PACKAGE_ROOT/share/doc/janus/stdlib-reference/$reference_file"
  )"
  if [ "$generated_digest" != "$packaged_digest" ]; then
    echo "smoke test: stdlib reference drift in $reference_file" >&2
    exit 1
  fi
done
export HOME="$WORK/home"
export JANUS_CACHE="$WORK/cache"
export JANUS_REGISTRY="$WORK/registry"

"$JANUS" --version
"$JANUS" new "$WORK/hello"

(
  cd "$WORK/hello"
  "$JANUS" check
  "$JANUS" fmt --check
  "$JANUS" build
  "$JANUS" build --release
  OUTPUT="$("$JANUS" run)"
  case "$OUTPUT" in
    *"Hello from Janus!"*) ;;
    *)
      echo "smoke test: unexpected program output: $OUTPUT" >&2
      exit 1
      ;;
  esac

  mkdir -p tests
  {
    echo 'def main() : int {'
    echo '    return 0'
    echo '}'
  } > tests/basic.janus
  "$JANUS" test
)

echo "smoke test: packaged Janus toolchain is operational"
