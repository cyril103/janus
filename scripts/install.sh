#!/bin/sh
set -eu

validate_archive() {
  archive=$1 expected_root=$2
  archive_limit() {
    production=$1 value=$2
    case "$value" in ''|*[!0-9]*) echo "$production" ;; *)
      if [ "$value" -lt "$production" ]; then echo "$value"; else echo "$production"; fi ;;
    esac
  }
  max_entries=$(archive_limit 100000 "${JANUS_ARCHIVE_TEST_MAX_ENTRIES:-}")
  max_file=$(archive_limit 1073741824 "${JANUS_ARCHIVE_TEST_MAX_FILE_SIZE:-}")
  max_total=$(archive_limit 4294967296 "${JANUS_ARCHIVE_TEST_MAX_TOTAL_SIZE:-}")
  validation_work=$(mktemp -d)
  if ! LC_ALL=C tar -tf "$archive" >"$validation_work/names" ||
     ! LC_ALL=C tar --numeric-owner -tvf "$archive" >"$validation_work/verbose"; then
    rm -rf "$validation_work"
    return 1
  fi
  if LC_ALL=C awk -v root="$expected_root" -v max_entries="$max_entries" \
      -v max_file="$max_file" -v max_total="$max_total" '
function fail(message) { print "janusup: unsafe archive: " message > "/dev/stderr"; exit 1 }
function fold(value, result, i, c) {
  result=""; for (i=1;i<=length(value);i++) { c=substr(value,i,1); result=result tolower(c) }
  return result
}
NR==FNR {
  name=$0
  if (name == "" || name !~ /^[!-~]+$/ || name ~ /\\/)
    fail("ambiguous entry name")
  if (name ~ /^\// || name ~ /^[A-Za-z]:/) fail("absolute entry path")
  n=split(name, parts, "/"); normalized=""
  for (i=1;i<=n;i++) {
    if (parts[i] == "") { if (i != n) fail("empty path component"); continue }
    if (parts[i] == "." || parts[i] == "..") fail("traversing entry path")
    if (parts[i] ~ /[<>:"|?*]/ || parts[i] ~ /\.$/)
      fail("Windows-ambiguous entry path")
    portable=fold(parts[i]); split(portable, device_parts, "."); device=device_parts[1]
    if (device ~ /^(con|prn|aux|nul|com[1-9]|lpt[1-9])$/)
      fail("reserved Windows entry path")
    normalized=normalized (normalized ? "/" : "") parts[i]
  }
  if (parts[1] != root) fail("unexpected archive root")
  key=fold(normalized)
  if (seen[key]++) fail("colliding entry paths")
  if (++entries > max_entries) fail("too many entries")
  archive_paths[entries]=key
  next
}
{
  if (++verbose_entries > entries) fail("inconsistent archive listing")
  path=archive_paths[verbose_entries]
  type=substr($1,1,1)
  if (type != "-" && type != "d") fail("link or special entry")
  components=split(path, path_parts, "/"); ancestor=""
  for (i=1;i<components;i++) {
    ancestor=ancestor (ancestor ? "/" : "") path_parts[i]
    if (regular_paths[ancestor]) fail("file/directory path collision")
    required_directories[ancestor]=1
  }
  if (type == "d") next
  if (required_directories[path]) fail("file/directory path collision")
  regular_paths[path]=1
  if ($2 ~ /\//) size=$3; else size=$5
  if (size !~ /^[0-9]+$/) fail("unparseable entry size")
  if (size > max_file) fail("entry is too large")
  total += size
  if (total > max_total) fail("archive expands beyond total size limit")
}
END {
  if (entries == 0) fail("empty archive")
  if (verbose_entries != entries) fail("inconsistent archive listing")
}
' "$validation_work/names" "$validation_work/verbose"; then
    validation_status=0
  else
    validation_status=$?
  fi
  rm -rf "$validation_work"
  return "$validation_status"
}

if [ "${1:-}" = "--validate-archive" ]; then
  [ "$#" -eq 3 ] || exit 2
  validate_archive "$2" "$3"
  exit
fi

VERSION="${JANUS_VERSION:-0.18.0}"
BASE_URL="${JANUS_DIST_SERVER:-https://github.com/cyril103/janus/releases/download/v${VERSION}}"
HOME_DIR="${JANUSUP_HOME:-${HOME}/.janus}"

case "$(uname -s)" in
  Linux) OS=Linux ;;
  Darwin) OS=Darwin ;;
  *) echo "janusup: système non pris en charge: $(uname -s)" >&2; exit 1 ;;
esac

case "$(uname -m)" in
  x86_64|amd64) ARCH=x86_64 ;;
  arm64|aarch64) ARCH=arm64 ;;
  *) echo "janusup: architecture non prise en charge: $(uname -m)" >&2; exit 1 ;;
esac

ARCHIVE="janus-${VERSION}-${OS}-${ARCH}.tar.gz"
URL="${JANUS_DIST_URL:-${BASE_URL%/}/${ARCHIVE}}"
is_official_url() {
  normalized=$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')
  case "$normalized" in
    https://*) remainder=${normalized#https://} ;;
    *) return 1 ;;
  esac
  authority=${remainder%%/*}
  [ "$authority" != "$remainder" ] || return 1
  authority=${authority##*@}
  case "$authority" in
    github.com|github.com:443) ;;
    *) return 1 ;;
  esac
  path=/${remainder#*/}
  path=${path%%\?*}
  path=${path%%\#*}
  case "$path" in
    /cyril103/janus/releases/download/*) return 0 ;;
    *) return 1 ;;
  esac
}
if is_official_url "$URL"; then OFFICIAL_SOURCE=1; else OFFICIAL_SOURCE=0; fi
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

if command -v curl >/dev/null 2>&1; then
  curl --fail --location --proto '=https' --tlsv1.2 "$URL" -o "$TMP/$ARCHIVE"
  curl --fail --location --proto '=https' --tlsv1.2 \
    "$URL.sha256" -o "$TMP/$ARCHIVE.sha256"
elif command -v wget >/dev/null 2>&1; then
  wget --https-only "$URL" -O "$TMP/$ARCHIVE"
  wget --https-only "$URL.sha256" -O "$TMP/$ARCHIVE.sha256"
else
  echo "janusup: curl ou wget est nécessaire" >&2
  exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
  (cd "$TMP" && sha256sum -c "$ARCHIVE.sha256")
elif command -v shasum >/dev/null 2>&1; then
  (cd "$TMP" && shasum -a 256 -c "$ARCHIVE.sha256")
else
  echo "janusup: aucun outil SHA-256 disponible" >&2
  exit 1
fi

if [ "${JANUS_ALLOW_UNVERIFIED_PRIVATE_MIRROR:-0}" = "1" ] &&
   [ "$OFFICIAL_SOURCE" = "0" ]; then
  echo "janusup: WARNING: using an unverified private mirror (JANUS_ALLOW_UNVERIFIED_PRIVATE_MIRROR=1)" >&2
elif command -v gh >/dev/null 2>&1 &&
     gh attestation --help >/dev/null 2>&1; then
  if ! gh attestation verify "$TMP/$ARCHIVE" --repo cyril103/janus; then
    echo "janusup: artifact provenance verification failed" >&2
    exit 1
  fi
else
  echo "janusup: une version récente de GitHub CLI avec la commande attestation est nécessaire pour vérifier la provenance" >&2
  exit 1
fi

mkdir "$TMP/package"
validate_archive "$TMP/$ARCHIVE" "janus-${VERSION}-${OS}-${ARCH}"
tar -xzf "$TMP/$ARCHIVE" -C "$TMP/package"
PACKAGE_ROOT="$TMP/package/janus-${VERSION}-${OS}-${ARCH}"
JANUSUP_HOME="$HOME_DIR" "$PACKAGE_ROOT/bin/janusup" \
  install "$PACKAGE_ROOT" "$VERSION"

if [ "${JANUS_INSTALL_RAYLIB:-0}" = "1" ]; then
  "$PACKAGE_ROOT/bin/janus-install-raylib"
fi

case ":${PATH}:" in
  *":${HOME_DIR}/bin:"*) ;;
  *)
    echo "Ajoutez cette ligne à votre profil shell :"
    echo "  export PATH=\"${HOME_DIR}/bin:\\\$PATH\""
    ;;
esac
echo "Janus ${VERSION} est installé."
