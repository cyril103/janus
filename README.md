# Janus

Janus est un langage de programmation fortement typé dont le compilateur est
écrit en C++ et utilise LLVM comme backend.

Le projet est encore à un stade précoce. La chaîne de compilation sait
actuellement analyser un point d'entrée `main`, des déclarations `val` et une
instruction `return`, vérifier leurs types et produire l'IR LLVM correspondant.

## Exemple

```janus
def main() : int {
    val x : int = 5
    return 0
}
```

Tout programme Janus doit déclarer exactement un point d'entrée
`def main() : int`.

`val` crée une liaison immuable : après sa déclaration, `x` ne peut pas être
réaffecté.

Le type `int` est toujours un entier signé sur 32 bits. Il est abaissé vers le
type LLVM `i32`. LLVM ne distingue pas directement les entiers signés des
entiers non signés dans ses types ; cette information est donc conservée par
le système de types de Janus.

## Prérequis

- un compilateur compatible C++20, comme Clang ou GCC ;
- CMake 3.20 ou plus récent ;
- Ninja ;
- LLVM et ses fichiers de développement ;
- LLD, recommandé pour l'édition de liens.

Sur Ubuntu :

```bash
sudo apt install \
  build-essential \
  cmake \
  ninja-build \
  clang \
  llvm \
  llvm-21-dev \
  libzstd-dev \
  lld
```

Les outils suivants sont également recommandés pour le développement :

```bash
sudo apt install gdb clang-format clang-tidy clangd
```

## Compiler Janus

Depuis la racine du projet :

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++

cmake --build build
```

Le compilateur est alors disponible dans `build/janusc`.

## Générer de l'IR LLVM

Un programme d'exemple est fourni dans `examples/value.janus` :

```bash
./build/janusc examples/value.janus
```

`janusc` écrit actuellement l'IR LLVM sur la sortie standard. Pour le
conserver dans un fichier :

```bash
./build/janusc examples/value.janus > value.ll
```

L'IR obtenu peut ensuite être transformé en fichier objet puis en exécutable :

```bash
llc -filetype=obj value.ll -o value.o
clang value.o -fuse-ld=lld -o value
./value
```

Pour l'exemple précédent, le backend produit notamment :

```llvm
define i32 @main() {
entry:
  %x = alloca i32, align 4
  store i32 5, ptr %x, align 4
  ret i32 0
}
```

## Point d'entrée

Le point d'entrée d'un programme utilise le mot-clé `def` :

```text
def main() : int {
    <instructions>
    return <expression>
}
```

`main` ne reçoit actuellement aucun paramètre et doit retourner un `int`. Une
instruction `return` est obligatoire et doit être la dernière instruction
exécutée.

## Syntaxe actuellement supportée

La forme d'une déclaration est :

```text
val <identifiant> : <type> = <expression>
```

Fonctionnalités disponibles :

- mot-clé `def` pour déclarer une fonction ;
- point d'entrée obligatoire et unique `main() : int` ;
- mot-clé `return` pour retourner un entier ;
- mot-clé `val` pour déclarer une valeur immuable ;
- identifiants commençant par une lettre ou `_`, puis composés de lettres,
  chiffres et `_` ;
- type `int`, signé sur 32 bits ;
- littéraux entiers décimaux positifs ;
- point-virgule optionnel après une déclaration ;
- plusieurs déclarations dans le corps d'une fonction ;
- détection des identifiants déclarés plusieurs fois ;
- détection d'une fonction sans `return` et du code après `return` ;
- diagnostics avec ligne et colonne ;
- validation de l'IR généré par LLVM.

Exemples d'erreurs détectées :

```janus
def main() : int {
    val x int = 5
    val y : inconnu = 1
    val trop_grand : int = 2147483648
    val x : int = 1
    val x : int = 2
    return 0
}
```

Les expressions arithmétiques, les nombres négatifs, les références à des
valeurs, les paramètres de fonctions, les appels de fonctions et les autres
types ne sont pas encore implémentés.

## Tests

Après avoir compilé le projet :

```bash
ctest --test-dir build --output-on-failure
```

Les tests couvrent actuellement :

- les propriétés sémantiques du type `int` ;
- son abaissement vers LLVM `i32` ;
- la reconnaissance et la validation de `def main() : int` ;
- l'analyse de `val x : int = 5` ;
- l'immutabilité d'une déclaration `val` ;
- l'obligation de retourner une valeur depuis `main` ;
- la table des symboles et les déclarations dupliquées ;
- la génération de l'allocation et du stockage LLVM ;
- plusieurs erreurs de syntaxe et de type.

## Organisation du projet

```text
include/janus/
  ast/          Arbre syntaxique abstrait
  backend/llvm/ Abaissement des types et génération d'IR
  diagnostics/ Erreurs de compilation et positions dans le source
  frontend/     Lexer, tokens et parser
  semantic/     Analyse sémantique et table des symboles
  types/        Système de types

src/            Implémentation des bibliothèques
tools/janusc/   Exécutable du compilateur
tests/          Tests du langage et du système de types
examples/       Programmes Janus d'exemple
```
