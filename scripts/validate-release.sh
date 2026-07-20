#!/usr/bin/env bash

set -euo pipefail

root_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-"$root_dir/build-release"}"
temporary_dir="$(mktemp -d)"

cleanup() {
    rm -rf "$temporary_dir"
}
trap cleanup EXIT

if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
    echo "Build directory is not configured: $build_dir" >&2
    exit 1
fi

cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure
cmake --build "$build_dir" --target dist

archive="$(find "$build_dir" -maxdepth 1 -type f \
    -name 'janus-*.tar.gz' -printf '%T@ %p\n' \
    | sort -nr \
    | head -n 1 \
    | cut -d' ' -f2-)"
if [[ -z "$archive" ]]; then
    echo "No Janus distribution archive found in $build_dir" >&2
    exit 1
fi
"$root_dir/scripts/smoke-test-package.sh" "$archive"

(
    cd "$root_dir/editors/vscode"
    npm ci
    npm run package -- --out "$temporary_dir/janus-language.vsix"
)

echo "Release validation completed successfully."
