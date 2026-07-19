# Janus

Janus est un langage de programmation compilé, fortement typé et orienté
performances. Son compilateur est écrit en C++ et produit des exécutables
natifs grâce à LLVM.

Janus combine plusieurs styles de programmation :

- des valeurs immuables avec `val` et des variables avec `var` ;
- des fonctions, closures et fonctions génériques de première classe ;
- des classes, traits, enums génériques et filtrage avec `match` ;
- des collections comme `Array`, `HashSet` et `HashMap` ;
- un accès bas niveau avec pointeurs, casts, interopérabilité C et gestion
  manuelle de la mémoire.

> Janus 0.1 est expérimental. La syntaxe et la bibliothèque standard peuvent
> encore évoluer avant la version 1.0.

## Installation

Les paquets officiels contiennent le compilateur, LLVM, Clang, LLD, le runtime
et la bibliothèque standard. Il n'est donc pas nécessaire d'installer LLVM
séparément.

Plateformes actuellement testées :

| Système | Architecture |
| --- | --- |
| Linux | x86_64 |
| macOS | Apple Silicon ARM64 |
| Windows | x86_64 |

### Linux

Installez d'abord les outils système nécessaires :

```bash
sudo apt update
sudo apt install build-essential curl
```

Puis installez Janus :

```bash
curl --proto '=https' --tlsv1.2 -fsSL \
  https://raw.githubusercontent.com/cyril103/janus/main/scripts/install.sh | sh
```

Si l'installateur le demande, ajoutez Janus au `PATH`, puis ouvrez un nouveau
terminal :

```bash
export PATH="$HOME/.janus/bin:$PATH"
```

### macOS

Installez les outils Apple, puis Janus :

```bash
xcode-select --install
curl --proto '=https' --tlsv1.2 -fsSL \
  https://raw.githubusercontent.com/cyril103/janus/main/scripts/install.sh | sh
```

### Windows

Ouvrez PowerShell et exécutez :

```powershell
irm https://raw.githubusercontent.com/cyril103/janus/main/scripts/install.ps1 | iex
```

Fermez et rouvrez ensuite le terminal pour actualiser le `PATH`.

Vérifiez l'installation :

```bash
janus --version
janusup list
```

Consultez le [guide d'installation détaillé](docs/getting-started.md) en cas de
problème ou pour mettre Janus à jour.

## Premier programme

Créez un projet et lancez-le :

```bash
janus new bonjour
cd bonjour
janus run
```

Le fichier `src/main.janus` contient le point d'entrée du programme :

```janus
def main() : int {
    println("Bonjour depuis Janus !")
    return 0
}
```

Un projet Janus possède cette structure :

```text
bonjour/
├── janus.toml
├── src/
│   └── main.janus
└── tests/
```

Commandes utiles :

```bash
janus check          # vérifier le programme sans produire d'exécutable
janus run            # compiler puis exécuter
janus build          # construire un exécutable dans target/
janus build --release
janus test           # compiler et exécuter les tests
janus fmt            # formater les fichiers Janus
janus fmt --check    # vérifier le formatage sans modifier les fichiers
```

## Aperçu du langage

Janus ne réalise pas de conversion implicite entre les types :

```janus
def add(left : int, right : int) : int {
    return left + right
}

def main() : int {
    val initial : int = 40      // valeur immuable
    var result : int = initial  // variable modifiable
    result = add(result, 2)
    println("Le calcul est terminé")
    return 0
}
```

Les principales fonctionnalités sont :

- types primitifs : `int`, `double`, `byte`, `char`, `bool`, `string`,
  `usize` et `Unit` ;
- fonctions et classes génériques ;
- closures avec captures et fonctions d'ordre supérieur ;
- contrôle de flux avec `if`, `while`, `for`, `break` et `continue` ;
- enums avec données associées et expressions `match` ;
- gestion d'erreurs avec `Option`, `Result` et l'opérateur `?` ;
- classes allouées avec `new`, libérées avec `delete`, destructeurs et `defer` ;
- visibilité `private`, traits et contraintes génériques ;
- modules, dépendances, collections et itérateurs paresseux ;
- pointeurs typés, allocation brute, casts explicites et `extern def`.

Le [guide du langage](docs/language-guide.md) présente chaque fonctionnalité
avec des exemples.

## Éditeurs

`janus-lsp` fournit les diagnostics pendant la saisie, le survol des symboles,
la navigation vers leur définition, leurs références, l'autocomplétion et le
formatage.

Une extension VS Code est disponible dans
[`editors/vscode`](editors/vscode/README.md). Elle détecte automatiquement
`janus-lsp` lorsqu'il est installé avec Janus.

## Documentation

- [Installation et premier projet](docs/getting-started.md)
- [Guide du langage](docs/language-guide.md)
- [Commandes, projets, dépendances et outils](docs/tooling.md)
- [Compiler Janus depuis les sources](docs/development.md)
- [Exemples complets](examples)
- [Historique des versions](CHANGELOG.md)

## Licence

Janus est distribué sous licence Apache 2.0 avec exception LLVM. Consultez
[LICENSE](LICENSE) et [NOTICE](NOTICE).
