#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <source-dir>" >&2
  exit 2
fi

SOURCE_DIR="$1"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
FAKE_BIN="$WORK/bin"
DIST="$WORK/dist"
PACKAGE_NAME="janus-test-Linux-x86_64"
mkdir -p "$FAKE_BIN" "$DIST/$PACKAGE_NAME/bin" "$WORK/home"

cat > "$DIST/$PACKAGE_NAME/bin/janusup" <<'EOF'
#!/bin/sh
set -eu
mkdir -p "$JANUSUP_HOME/bin"
touch "$JANUSUP_HOME/bin/janus"
EOF
chmod +x "$DIST/$PACKAGE_NAME/bin/janusup"
tar -czf "$DIST/$PACKAGE_NAME.tar.gz" \
  -C "$DIST" "$PACKAGE_NAME"
(
  cd "$DIST"
  sha256sum "$PACKAGE_NAME.tar.gz" > "$PACKAGE_NAME.tar.gz.sha256"
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
  *.sha256) source="$DIST/$PACKAGE_NAME.tar.gz.sha256" ;;
  *) source="$DIST/$PACKAGE_NAME.tar.gz" ;;
esac
cp "\$source" "\$output"
EOF

cat > "$FAKE_BIN/gh" <<EOF
#!/bin/sh
if [ "\${1:-}" = "attestation" ] && [ "\${2:-}" = "--help" ]; then
  exit 1
fi
touch "$WORK/gh-verify-was-called"
exit 99
EOF
chmod +x "$FAKE_BIN/uname" "$FAKE_BIN/curl" "$FAKE_BIN/gh"

PATH="$FAKE_BIN:/usr/bin:/bin" \
HOME="$WORK/home" \
JANUS_VERSION=test \
JANUS_DIST_URL=https://example.invalid/"$PACKAGE_NAME.tar.gz" \
JANUSUP_HOME="$WORK/janus" \
  "$SOURCE_DIR/scripts/install.sh" >"$WORK/output" 2>"$WORK/error"

if [ ! -f "$WORK/janus/bin/janus" ]; then
  echo "install test: an old gh prevented installation" >&2
  exit 1
fi
if [ -e "$WORK/gh-verify-was-called" ]; then
  echo "install test: attestation verification used an unsupported gh" >&2
  exit 1
fi
if ! grep -q "version récente de GitHub CLI" "$WORK/error"; then
  echo "install test: the old-gh warning was not emitted" >&2
  exit 1
fi

if PATH="$FAKE_BIN:/usr/bin:/bin" \
   HOME="$WORK/home" \
   JANUS_VERSION=test \
   JANUS_DIST_URL=https://example.invalid/"$PACKAGE_NAME.tar.gz" \
   JANUSUP_HOME="$WORK/strict" \
   JANUS_REQUIRE_ATTESTATION=1 \
     "$SOURCE_DIR/scripts/install.sh" >"$WORK/strict-output" \
       2>"$WORK/strict-error"; then
  echo "install test: strict provenance accepted an unsupported gh" >&2
  exit 1
fi
if ! grep -q "commande attestation est nécessaire" "$WORK/strict-error"; then
  echo "install test: strict provenance produced the wrong error" >&2
  exit 1
fi
