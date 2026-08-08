#!/bin/sh
set -eu

VERSION="${JANUS_VERSION:-0.9.0}"
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
