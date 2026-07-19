#!/usr/bin/env sh

set -eu

janus_bin=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -n "${LD_LIBRARY_PATH:-}" ]; then
    export LD_LIBRARY_PATH="${janus_bin}/../lib:${LD_LIBRARY_PATH}"
else
    export LD_LIBRARY_PATH="${janus_bin}/../lib"
fi

exec "${janus_bin}/clang.real" "$@"
