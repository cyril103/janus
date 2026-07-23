# Installation et premier projet

Ce guide explique comment installer Janus, créer un programme, le compiler,
l'exécuter et le tester.

## Installer Janus

Une distribution officielle contient tous les composants propres à Janus :

- `janus`, la commande principale ;
- `janusup`, le gestionnaire de versions ;
- `janus-lsp`, le serveur pour les éditeurs ;
- le runtime et la bibliothèque standard ;
- Clang, LLD et les bibliothèques LLVM nécessaires.

Le système doit néanmoins fournir son environnement natif : bibliothèques de
base sous Linux ou outils Apple sous macOS.

### Linux x86_64

Sur Ubuntu ou Debian :

```bash
sudo apt update
sudo apt install build-essential curl
curl --proto '=https' --tlsv1.2 -fsSL \
  https://raw.githubusercontent.com/cyril103/janus/main/scripts/install.sh | sh
```

Ajoutez durablement Janus au `PATH` si nécessaire :

```bash
echo 'export PATH="$HOME/.janus/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

Pour Zsh, utilisez `~/.zshrc` à la place de `~/.bashrc`.

### macOS ARM64

```bash
xcode-select --install
curl --proto '=https' --tlsv1.2 -fsSL \
  https://raw.githubusercontent.com/cyril103/janus/main/scripts/install.sh | sh
```

Ajoutez `~/.janus/bin` au `PATH` comme sous Linux.

### Windows x86_64

Dans PowerShell :

```powershell
irm https://raw.githubusercontent.com/cyril103/janus/main/scripts/install.ps1 | iex
```

L'installateur ajoute `%LOCALAPPDATA%\Janus\bin` au `PATH` utilisateur. Ouvrez
un nouveau terminal après l'installation.

### Vérifier l'installation

```bash
janus --version
janusup list
```

Sous Unix, Janus est installé dans `~/.janus`. Sous Windows, il est installé
dans `%LOCALAPPDATA%\Janus`.

Les archives sont toujours contrôlées avec leur somme SHA-256. Si GitHub CLI
est disponible, leur attestation de provenance est également vérifiée.

## Mettre Janus à jour

```bash
janusup update
```

`janusup` sait aussi gérer plusieurs versions et canaux :

```bash
janusup install stable
janusup install beta
janusup install nightly
janusup install 0.3.0
janusup default 0.3.0
janusup list
```

## Créer un projet

```bash
janus new bonjour
cd bonjour
```

La commande crée :

```text
bonjour/
├── .gitignore
├── janus.toml
├── src/
│   └── main.janus
└── tests/
```

`janus.toml` décrit le paquet et son point d'entrée. Le code commence dans
`src/main.janus` :

```janus
def main() : int {
    println("Hello from Janus!")
    return 0
}
```

## Exécuter et compiler

Depuis la racine du projet :

```bash
janus check
janus run
janus build
janus build --release
```

- `check` vérifie rapidement le code ;
- `run` compile puis lance le programme ;
- `build` crée un exécutable de développement sous `target/debug` ;
- `build --release` active les optimisations et écrit sous `target/release`.

## Ajouter un test

Créez `tests/basic.janus` :

```janus
def main() : int {
    val result : int = 40 + 2
    if result == 42 {
        return 0
    }
    return 1
}
```

Puis lancez :

```bash
janus test
```

Un test est réussi lorsque son `main` retourne `0`.

## Formater le code

```bash
janus fmt
janus fmt --check
```

Le fichier facultatif `.janusfmt` permet de personnaliser le formatage :

```toml
indent_width = 4
max_blank_lines = 1
```

## Désinstaller une version

```bash
janusup uninstall 0.1.0
```

`janusup home` affiche le dossier d'installation utilisé.
