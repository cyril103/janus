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

### Aide, erreurs et codes de sortie

`janus --help` affiche l'aide générale. Les commandes d'exécution disposent
aussi d'une aide ciblée : `janus check --help`, `janus build --help`,
`janus run --help` et `janus test --help`. L'aide est écrite sur la sortie
standard, renvoie le code `0` et ne recherche ni projet ni chaîne d'outils.

Pour `check`, `build`, `run` et `test`, une erreur d'invocation (option
inconnue, argument manquant ou combinaison incompatible) renvoie le code `2`,
affiche sur la sortie d'erreur un diagnostic qualifié par la commande puis
uniquement son usage. Une erreur opérationnelle
(compilation, manifeste, dépendance ou édition de liens) renvoie le code `1`
sans ajouter d'usage. `janus run` transmet le code de sortie du programme
exécuté. Les diagnostics de compilation utilisent la forme
`chemin:ligne:colonne: error: [code] message`, y compris pour `janus test`.

`janus check` et `janus build` acceptent
`--diagnostic-format human|json`. Le format humain est la valeur par défaut et
ajoute un extrait de source avec repère. Le format JSON est destiné aux outils,
reste écrit sur stderr et ne change ni stdout ni le code de sortie. Son schéma
versionné et le contrat des suggestions sont décrits dans la page
[Diagnostics structurés](diagnostics.md).

## Diagnostics optionnels

`janus check`, `janus build` et `janus run` acceptent
`--warn-high-growth-loops`. Sans cette option, les sorties et le comportement de
compilation restent inchangés.

Avec l'option, Janus analyse l'AST après l'analyse sémantique et émet des
warnings non bloquants pour les affectations dans une boucle où une variable
est multipliée par elle-même ou par un facteur avant d'être réaffectée :
`x = x * k`, `x = k * x`, et les formes additives directes comme
`x = 3 * x + 1`. Ces patterns peuvent provoquer un overflow entier ou un temps
d'exécution excessif ; ajoutez une borne explicite, choisissez un type numérique
sûr, ou imposez un time budget.

Limites actuelles : le diagnostic vise les affectations locales simples dans
les corps de `while` et `for`. Il ne prouve pas les bornes de boucle, ne suit pas
les alias/champs, et ne signale pas les multiplications hors boucle ni les
inductions additives comme `i = i + 1`.

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
janusup install 0.4.0
janusup default 0.4.0
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
- définition et références à l'échelle du workspace ;
- recherche de symboles dans le workspace ;
- autocomplétion ;
- formatage du document.

À l'initialisation, le serveur lit `janus.toml` et indexe les fichiers `.janus`
de `src/`, `tests/` et des dépendances résolues. Les dépendances par chemin sont
suivies directement ; les dépendances git ou registre déjà verrouillées et
présentes dans le cache sont chargées hors ligne. La navigation ne dépend donc
pas des fichiers préalablement ouverts. Les déclarations privées restent
visibles dans leur propre fichier, mais ne sont proposées ni résolues depuis un
autre module.

Le serveur demande au client de surveiller `**/*.janus` et `**/janus.toml`.
Les créations, modifications, suppressions, sauvegardes et changements de
dossiers de workspace actualisent l'index sans redémarrage. La méthode interne
`janus/workspaceIndexStats` expose le nombre de fichiers et symboles, les octets
source, une estimation de la mémoire de l'index et sa durée de démarrage.

Le projet de référence
[`tests/fixtures/lsp-workspace`](../tests/fixtures/lsp-workspace) protège un
budget de démarrage de 2 secondes et un budget mémoire estimé de 1 Mio. Ces
limites sont volontairement largement supérieures au coût actuel du petit
fixture afin de détecter une régression importante sur toutes les plateformes
de CI.

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
| `JANUS_RAYLIB_PATH` | chemin de la bibliothèque partagée raylib 6 |
| `JANUS_REQUIRE_ATTESTATION` | exiger la vérification de provenance |
