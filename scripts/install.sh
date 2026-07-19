#!/bin/sh
set -eu

VERSION="${JANUS_VERSION:-0.2.0}"
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
URL="${JANUS_DIST_URL:-${BASE_URL}/${ARCHIVE}}"
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

if command -v gh >/dev/null 2>&1 &&
   gh attestation --help >/dev/null 2>&1; then
  gh attestation verify "$TMP/$ARCHIVE" --repo cyril103/janus
elif [ "${JANUS_REQUIRE_ATTESTATION:-0}" = "1" ]; then
  echo "janusup: une version récente de GitHub CLI avec la commande attestation est nécessaire pour vérifier la provenance" >&2
  exit 1
else
  echo "janusup: avertissement: installez une version récente de GitHub CLI pour vérifier la provenance" >&2
fi

mkdir "$TMP/package"
tar -xzf "$TMP/$ARCHIVE" -C "$TMP/package"
PACKAGE_ROOT="$TMP/package/janus-${VERSION}-${OS}-${ARCH}"
JANUSUP_HOME="$HOME_DIR" "$PACKAGE_ROOT/bin/janusup" \
  install "$PACKAGE_ROOT" "$VERSION"

case ":${PATH}:" in
  *":${HOME_DIR}/bin:"*) ;;
  *)
    echo "Ajoutez cette ligne à votre profil shell :"
    echo "  export PATH=\"${HOME_DIR}/bin:\\\$PATH\""
    ;;
esac
echo "Janus ${VERSION} est installé."
