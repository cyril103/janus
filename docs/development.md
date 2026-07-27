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

### Cohérence de la documentation publique

`docs.public_surface` exécute
[`scripts/check_public_surface.py`](../scripts/check_public_surface.py). Ce
contrôle reste entièrement local et vérifie :

- que les modules et symboles publics extraits de `stdlib/std/` correspondent
  à `docs/public-surface-0.5.json` ;
- que l'empreinte normalisée des signatures publiques correspond à
  l'inventaire versionné et que chaque document associé existe ;
- que les appels, types génériques et membres cités en prose dans ces guides
  désignent une surface inventoriée ;
- que les commandes et options observées dans `janus --help` sont
  inventoriées.

Le diagnostic nomme le guide associé et la surface divergente. La fixture
`tests/fixtures/documentation/stale-public-surface.json` référence
volontairement `Array.removed` ; le test
`docs.public_surface_stale_fixture` prouve que cette dérive est rejetée avec
un message ciblé.

Pour auditer manuellement l'inventaire et afficher les empreintes calculées :

```bash
python3 scripts/check_public_surface.py \
  --root . \
  --janus build/janus \
  --print-digests
```

Une empreinte ne doit être mise à jour qu'après vérification de la signature
modifiée et de tous les guides associés. Le workflow Pages compile également
les extraits Janus autonomes, construit MkDocs en mode strict, puis crawle
l'artefact local servi sur `127.0.0.1` ; ces contrôles de l'artefact ne
dépendent pas du réseau.

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

Pour une release 1.x, fournissez le dernier compilateur publié afin d'activer
la comparaison N/N+1 pendant cette validation :

```bash
JANUS_PREVIOUS=/opt/janus-N/bin/janus \
  scripts/validate-release.sh build-release
```

Checklist de release :

1. vérifier que la version, le changelog et l'extension VS Code sont alignés ;
2. relire le [contrat de stabilité](stability-contract.md), notamment la liste
   des API expérimentales, et documenter toute promotion ou dépréciation ;
3. pour une version 1.x, exécuter la suite N/N+1 avec le dernier compilateur
   publié et le candidat sur chaque plateforme de niveau 1 ;
4. joindre un guide de migration à tout changement incompatible autorisé ;
5. vérifier manifestes et lockfiles précédents avec `--locked --offline` ;
6. terminer la validation de l'archive et de l'extension.

La commande N/N+1 exacte est documentée dans
[`tests/compatibility`](../tests/compatibility/README.md). CTest exécute aussi
`compatibility.current` avec le compilateur courant des deux côtés afin de
garder le harnais et les fixtures fonctionnels avant 1.0.

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
