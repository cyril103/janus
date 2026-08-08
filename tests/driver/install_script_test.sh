#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <source-dir>" >&2
  exit 2
fi

SOURCE_DIR="$1"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
REAL_TAR=$(command -v tar)
FAKE_BIN="$WORK/bin"
NO_GH_BIN="$WORK/no-gh-bin"
DIST="$WORK/dist"
PACKAGE_NAME="janus-test-Linux-x86_64"
mkdir -p "$FAKE_BIN" "$NO_GH_BIN" "$DIST/$PACKAGE_NAME/bin" "$WORK/home"

cat > "$DIST/$PACKAGE_NAME/bin/janusup" <<'EOF'
#!/bin/sh
set -eu
touch "$TEST_TRACE_EXECUTED"
mkdir -p "$JANUSUP_HOME/bin"
touch "$JANUSUP_HOME/bin/janus"
EOF
chmod +x "$DIST/$PACKAGE_NAME/bin/janusup"
tar -czf "$DIST/$PACKAGE_NAME.tar.gz" -C "$DIST" "$PACKAGE_NAME"
(
  cd "$DIST"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$PACKAGE_NAME.tar.gz" > "$PACKAGE_NAME.tar.gz.sha256"
  else
    shasum -a 256 "$PACKAGE_NAME.tar.gz" > "$PACKAGE_NAME.tar.gz.sha256"
  fi
)

cat > "$FAKE_BIN/uname" <<'EOF'
#!/bin/sh
case "${1:-}" in
  -s) echo Linux ;;
  -m) echo x86_64 ;;
  *) exit 2 ;;
esac
EOF
cat > "$FAKE_BIN/curl" <<EOF
#!/bin/sh
set -eu
output=
url=
while [ "\$#" -gt 0 ]; do
  case "\$1" in
    -o) output="\$2"; shift 2 ;;
    http*) url="\$1"; shift ;;
    *) shift ;;
  esac
done
case "\$url" in
  *.sha256*) source="$DIST/$PACKAGE_NAME.tar.gz.sha256" ;;
  *) source="$DIST/$PACKAGE_NAME.tar.gz" ;;
esac
cp "\$source" "\$output"
EOF
cat > "$FAKE_BIN/gh" <<'EOF'
#!/bin/sh
set -eu
if [ "${1:-}" = "attestation" ] && [ "${2:-}" = "--help" ]; then
  exit "${TEST_GH_HELP_STATUS:-0}"
fi
touch "$TEST_TRACE_ATTESTED"
exit "${TEST_GH_VERIFY_STATUS:-0}"
EOF
cat > "$FAKE_BIN/tar" <<'EOF'
#!/bin/sh
set -eu
touch "$TEST_TRACE_EXTRACTED"
exec "$TEST_REAL_TAR" "$@"
EOF
chmod +x "$FAKE_BIN/uname" "$FAKE_BIN/curl" "$FAKE_BIN/gh" "$FAKE_BIN/tar"
ln -s "$FAKE_BIN/uname" "$NO_GH_BIN/uname"
ln -s "$FAKE_BIN/curl" "$NO_GH_BIN/curl"
ln -s "$FAKE_BIN/tar" "$NO_GH_BIN/tar"
for tool in mktemp rm tr mkdir touch cp gzip; do
  tool_path=$(command -v "$tool")
  ln -s "$tool_path" "$NO_GH_BIN/$tool"
  ln -s "$tool_path" "$FAKE_BIN/$tool"
done
if command -v sha256sum >/dev/null 2>&1; then
  HASH_TOOL=sha256sum
else
  HASH_TOOL=shasum
fi
tool_path=$(command -v "$HASH_TOOL")
ln -s "$tool_path" "$NO_GH_BIN/$HASH_TOOL"
ln -s "$tool_path" "$FAKE_BIN/$HASH_TOOL"

run_install() {
  case_name="$1"
  url="$2"
  gh_state="$3"
  shift 3
  rm -f "$WORK/$case_name-attested" "$WORK/$case_name-extracted" \
    "$WORK/$case_name-executed"
  test_bin=$FAKE_BIN
  if [ "$gh_state" = "absent" ]; then test_bin=$NO_GH_BIN; fi
  env PATH="$test_bin" HOME="$WORK/home" \
    JANUS_VERSION=test JANUS_DIST_URL="$url" \
    JANUSUP_HOME="$WORK/$case_name-home" \
    TEST_TRACE_ATTESTED="$WORK/$case_name-attested" \
    TEST_TRACE_EXTRACTED="$WORK/$case_name-extracted" \
    TEST_REAL_TAR="$REAL_TAR" \
    TEST_TRACE_EXECUTED="$WORK/$case_name-executed" "$@" \
    "$SOURCE_DIR/scripts/install.sh" >"$WORK/$case_name-output" \
      2>"$WORK/$case_name-error"
}

OFFICIAL_URL="https://github.com/cyril103/janus/releases/download/vtest/$PACKAGE_NAME.tar.gz"
PRIVATE_URL="https://packages.example.invalid/janus/$PACKAGE_NAME.tar.gz"

assert_stopped_before_extraction() {
  case_name="$1"
  if [ -e "$WORK/$case_name-extracted" ] ||
     [ -e "$WORK/$case_name-executed" ]; then
    echo "install test: $case_name reached extraction or execution" >&2
    exit 1
  fi
}

for gh_state in absent old invalid; do
  case "$gh_state" in
    absent) gh_args= ;;
    old) gh_args="TEST_GH_HELP_STATUS=1" ;;
    invalid) gh_args="TEST_GH_VERIFY_STATUS=23" ;;
  esac
  # shellcheck disable=SC2086
  if run_install "official-$gh_state" "$OFFICIAL_URL" "$gh_state" \
       JANUS_ALLOW_UNVERIFIED_PRIVATE_MIRROR=1 $gh_args; then
    echo "install test: official archive accepted gh state $gh_state" >&2
    exit 1
  fi
  assert_stopped_before_extraction "official-$gh_state"
done

official_variant=0
for url in \
  "HTTPS://GITHUB.COM/CYRIL103/JANUS/RELEASES/DOWNLOAD/vtest/$PACKAGE_NAME.tar.gz" \
  "https://github.com:443/cyril103/janus/releases/download/vtest/$PACKAGE_NAME.tar.gz" \
  "https://user:secret@GitHub.Com/cyril103/janus/releases/download/vtest/$PACKAGE_NAME.tar.gz?download=1"; do
  official_variant=$((official_variant + 1))
  case_name="official-variant-$official_variant"
  if run_install "$case_name" "$url" old \
       JANUS_ALLOW_UNVERIFIED_PRIVATE_MIRROR=1 TEST_GH_HELP_STATUS=1; then
    echo "install test: official URL variant accepted the opt-out: $url" >&2
    exit 1
  fi
  assert_stopped_before_extraction "$case_name"
done

if [ ! -e "$WORK/official-invalid-attested" ]; then
  echo "install test: invalid attestation was not attempted" >&2
  exit 1
fi

if run_install private-default "$PRIVATE_URL" old TEST_GH_HELP_STATUS=1; then
  echo "install test: private mirror silently skipped attestation" >&2
  exit 1
fi
if [ -e "$WORK/private-default-extracted" ]; then
  echo "install test: private mirror failure happened after extraction" >&2
  exit 1
fi

run_install private-optout "$PRIVATE_URL" old TEST_GH_HELP_STATUS=1 \
  JANUS_ALLOW_UNVERIFIED_PRIVATE_MIRROR=1
if [ ! -f "$WORK/private-optout-home/bin/janus" ]; then
  echo "install test: explicit private-mirror opt-out did not install" >&2
  exit 1
fi
if ! grep -qi "private.*unverified\|non vérifié.*privé" \
    "$WORK/private-optout-error"; then
  echo "install test: private-mirror opt-out was not loudly logged" >&2
  exit 1
fi
