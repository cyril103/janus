# Compiler Janus depuis les sources

Cette page s'adresse aux personnes qui souhaitent modifier le compilateur.
Pour écrire des programmes Janus, utilisez plutôt les paquets officiels.

## Prérequis

- un compilateur C++20 ;
- CMake 3.21 ou plus récent ;
- Ninja ;
- LLVM et ses fichiers de développement ;
- Clang et LLD.

La CI de référence utilise LLVM 18 sous Ubuntu.

Sur Ubuntu 24.04 :

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build \
  clang-18 lld-18 llvm-18-dev
```

## Configuration et compilation

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang-18 \
  -DCMAKE_CXX_COMPILER=clang++-18 \
  -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm

cmake --build build --parallel
```

Les principaux exécutables sont :

- `build/janus`, pilote utilisé par les projets ;
- `build/janusc`, frontend historique produisant de l'IR LLVM ;
- `build/janusup`, gestionnaire d'installation ;
- `build/janus-lsp`, serveur de langage.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

Pour un test précis :

```bash
ctest --test-dir build -R lsp.server --output-on-failure
```

## Archive redistribuable

```bash
cmake --build build --target dist
```

La cible produit une archive autonome et son fichier `.sha256`. Le script de
smoke test vérifie ensuite qu'un projet peut réellement être créé, compilé,
exécuté, formaté et testé avec le contenu de cette archive :

```bash
scripts/smoke-test-package.sh build/janus-*.tar.gz
```

Sous Windows, utilisez `scripts/smoke-test-package.ps1`.

## Validation avant une version

Sous Linux, la validation complète peut être relancée sur un répertoire déjà
configuré. Elle compile Janus, exécute les tests, construit et teste l'archive,
puis empaquette l'extension VS Code :

```bash
scripts/validate-release.sh build-release
```

## Organisation du dépôt

```text
include/          interfaces C++
src/frontend/     lexer, parser et chargement des modules
src/semantic/     analyse sémantique et système de types
src/backend/llvm/ génération LLVM et fichiers objets
src/driver/       projets, paquets, formatage et édition de liens
src/lsp/          serveur de langage
runtime/          runtime natif
stdlib/           bibliothèque standard Janus
tools/            exécutables en ligne de commande
tests/            tests C++ et programmes Janus
examples/         exemples du langage
```
