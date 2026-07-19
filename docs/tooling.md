# Commandes et outils Janus

## Commande `janus`

| Commande | Rôle |
| --- | --- |
| `janus new <dossier>` | créer un nouveau projet |
| `janus init [dossier]` | initialiser Janus dans un dossier existant |
| `janus check` | analyser le projet sans construire d'exécutable |
| `janus run` | compiler et exécuter |
| `janus build` | construire en mode développement |
| `janus build --release` | construire avec optimisations |
| `janus test [filtre]` | exécuter les tests |
| `janus fmt` | formater `src/` et `tests/` |
| `janus fmt --check` | vérifier le formatage |

Pour compiler un fichier isolé :

```bash
janus check fichier.janus
janus run fichier.janus
janus build fichier.janus -o programme
```

Les options `--emit llvm-ir` et `--emit object` arrêtent la compilation après
la production de l'IR LLVM ou du fichier objet.

## Manifeste du projet

Un projet est décrit par `janus.toml` :

```toml
[package]
name = "application"
version = "0.1.0"
entry = "src/main.janus"

[dependencies]
outil = { path = "../outil" }
```

`janus.lock` conserve les versions et sources exactes. Il doit être ajouté au
contrôle de version. Le dossier `target/` contient les résultats de compilation
et doit rester ignoré.

## Dépendances

Ajouter ou retirer une dépendance :

```bash
janus add collections@^1.2.0
janus add outil --path ../outil
janus add protocole --git https://example.com/protocole.git \
  --rev 0123456789abcdef0123456789abcdef01234567
janus remove collections
```

Une dépendance Git exige un hash de commit complet pour garantir une
construction reproductible.

Options utiles :

```bash
janus build --locked   # refuser toute modification de janus.lock
janus build --offline  # utiliser uniquement le cache local
```

## Publication locale

Le registre actuel est un répertoire local, placé par défaut dans
`~/.janus/registry` :

```bash
janus publish
```

`JANUS_REGISTRY` permet d'utiliser un autre emplacement. Une version publiée
est immuable et ne peut pas être écrasée.

## Gestion des versions avec `janusup`

```bash
janusup list
janusup install stable
janusup install beta
janusup install nightly
janusup install 0.2.0
janusup default 0.2.0
janusup update
janusup uninstall 0.1.0
janusup home
```

Les téléchargements sont contrôlés par SHA-256. La présence de GitHub CLI
active également la vérification de provenance Sigstore. Pour la rendre
obligatoire dans l'installateur :

```bash
export JANUS_REQUIRE_ATTESTATION=1
```

## Formatage

`janus fmt` découvre récursivement les fichiers `.janus` de `src/` et
`tests/`. La configuration facultative `.janusfmt` accepte :

```toml
indent_width = 4
max_blank_lines = 1
```

## Serveur de langage

`janus-lsp` communique avec les éditeurs par le protocole LSP. Il prend
actuellement en charge :

- diagnostics lors de l'ouverture et de la modification d'un fichier ;
- survol d'un symbole ;
- définition et références ;
- autocomplétion ;
- formatage du document.

L'extension VS Code se trouve dans
[`editors/vscode`](../editors/vscode/README.md). Elle cherche le serveur dans
`janus.server.path`, `$JANUS_HOME/bin`, `~/.janus/bin`, puis le `PATH`.

## Variables d'environnement

| Variable | Utilisation |
| --- | --- |
| `JANUSUP_HOME` | dossier géré par `janusup` |
| `JANUS_CACHE` | cache des dépendances |
| `JANUS_REGISTRY` | registre local |
| `JANUS_CC` | pilote Clang utilisé pour l'édition de liens |
| `JANUS_REQUIRE_ATTESTATION` | exiger la vérification de provenance |
