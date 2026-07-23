#!/bin/sh
set -eu

RAYLIB_VERSION="${JANUS_RAYLIB_VERSION:-6.0}"
RAYLIB_REVISION="${JANUS_RAYLIB_REVISION:-dbc56a87da87d973a9c5baa4e7438a9d20121d28}"
RAYLIB_REPOSITORY="${JANUS_RAYLIB_REPOSITORY:-https://github.com/raysan5/raylib.git}"
PREFIX="${JANUS_RAYLIB_PREFIX:-/usr/local}"
INSTALL_DEPENDENCIES=1
DRY_RUN=0

usage() {
  cat <<EOF
Usage: janus-install-raylib [options]

Compile et installe la bibliothèque partagée raylib ${RAYLIB_VERSION}.

Options:
  --prefix <chemin>       préfixe d'installation (défaut: /usr/local)
  --skip-dependencies     ne pas installer les paquets système
  --dry-run               afficher la configuration sans rien modifier
  -h, --help              afficher cette aide
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --prefix)
      if [ "$#" -lt 2 ]; then
        echo "janus-install-raylib: --prefix attend un chemin" >&2
        exit 2
      fi
      PREFIX="$2"
      shift 2
      ;;
    --skip-dependencies)
      INSTALL_DEPENDENCIES=0
      shift
      ;;
    --dry-run)
      DRY_RUN=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "janus-install-raylib: option inconnue: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

case "$(uname -s)" in
  Linux) OS=Linux ;;
  Darwin) OS=Darwin ;;
  *)
    echo "janus-install-raylib: système non pris en charge: $(uname -s)" >&2
    exit 1
    ;;
esac

if [ "$DRY_RUN" = "1" ]; then
  echo "raylib version: $RAYLIB_VERSION"
  echo "raylib revision: $RAYLIB_REVISION"
  echo "raylib repository: $RAYLIB_REPOSITORY"
  echo "system: $OS"
  echo "prefix: $PREFIX"
  echo "install dependencies: $INSTALL_DEPENDENCIES"
  exit 0
fi

run_privileged() {
  if [ "$(id -u)" -eq 0 ]; then
    "$@"
  elif command -v sudo >/dev/null 2>&1; then
    sudo "$@"
  else
    echo "janus-install-raylib: sudo est nécessaire pour installer dans $PREFIX" >&2
    exit 1
  fi
}

if [ "$INSTALL_DEPENDENCIES" = "1" ]; then
  if [ "$OS" = "Linux" ] && command -v apt-get >/dev/null 2>&1; then
    run_privileged apt-get update
    run_privileged apt-get install -y \
      build-essential cmake git \
      libasound2-dev libgl1-mesa-dev libglu1-mesa-dev \
      libx11-dev libxcursor-dev libxi-dev libxinerama-dev libxrandr-dev
  elif [ "$OS" = "Darwin" ] && command -v brew >/dev/null 2>&1; then
    brew install cmake git glfw
  else
    echo "janus-install-raylib: gestionnaire de paquets non reconnu; utilisez --skip-dependencies après avoir installé cmake, git et les dépendances raylib" >&2
    exit 1
  fi
fi

for command_name in cmake git; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "janus-install-raylib: commande requise absente: $command_name" >&2
    exit 1
  fi
done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
SOURCE="$WORK/raylib"
BUILD="$WORK/build"

git init --quiet "$SOURCE"
git -C "$SOURCE" remote add origin "$RAYLIB_REPOSITORY"
git -C "$SOURCE" fetch --quiet --depth 1 origin "$RAYLIB_REVISION"
git -C "$SOURCE" checkout --quiet --detach FETCH_HEAD

cmake -S "$SOURCE" -B "$BUILD" \
  -DBUILD_SHARED_LIBS=ON \
  -DBUILD_EXAMPLES=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$BUILD" --parallel

if [ -w "$PREFIX" ] || { [ ! -e "$PREFIX" ] && [ -w "$(dirname "$PREFIX")" ]; }; then
  cmake --install "$BUILD"
else
  run_privileged cmake --install "$BUILD"
fi

if [ "$OS" = "Linux" && command -v ldconfig >/dev/null 2>&1; then
  run_privileged ldconfig
fi

echo "raylib $RAYLIB_VERSION est installé dans $PREFIX."
