# Commandes et outils Janus

## Commande `janus`

| Commande | Rôle |
| --- | --- |
| `janus new <dossier>` | créer un nouveau projet |
| `janus init [dossier]` | initialiser Janus dans un dossier existant |
| `janus check` | analyser le projet sans construire d'exécutable |
| `janus check --all` | analyser exhaustivement chaque module du projet |
| `janus run` | compiler et exécuter |
| `janus build` | construire en mode développement avec cache incrémental |
| `janus build --release` | construire avec optimisations |
| `janus build --no-cache` | forcer une construction sans lire ni écrire le cache |
| `janus build --deny-warnings` | refuser la construction si un module avertit |
| `janus clean` | supprimer `target/`, cache incrémental compris |
| `janus test [filtre]` | exécuter les tests et doctests |
| `janus test --doc` | exécuter uniquement les doctests |
| `janus test --format json|junit` | produire un rapport CI structuré |
| `janus fmt` | formater `src/` et `tests/` |
| `janus fmt --check` | vérifier le formatage |
| `janus doc` | générer la documentation d’API hors ligne |
| `janus doc --open` | générer puis ouvrir la documentation |
| `janus doc --search <requête>` | rechercher les API publiques hors ligne |
| `janus doc --search <requête> --format human|json` | choisir le format de recherche déterministe |
| `janus doc --search <requête> --module <nom>` | filtrer la recherche par module |
| `janus doc --search <requête> --kind <nature>` | filtrer la recherche par nature de symbole |
| `janus doc --search <requête> --package <nom>` | filtrer la recherche par paquet |
| `janus explain <code>` | expliquer un diagnostic stable et sa correction habituelle |

`janus explain JANA0025`, par exemple, détaille pourquoi une opération invalide
un propriétaire emprunté et propose de terminer l'emprunt avant l'opération.
La commande fonctionne hors ligne et son catalogue est livré avec le
compilateur.

Pour compiler un fichier isolé :

```bash
janus check fichier.janus
janus run fichier.janus
janus build fichier.janus -o programme
```

Lorsqu'il appartient à un paquet, un fichier explicite est résolu depuis la
racine `src/` et avec les dépendances du manifeste courant. `janus check --all`
parcourt tous les fichiers `.janus` sous `src/`, déduplique les modules partagés
et agrège leurs diagnostics. `--deny-warnings`, accepté par `check` et `build`,
renvoie le code `1` dès que cette analyse exhaustive contient un avertissement.
La passe est rejouée avant un build, y compris lorsqu'un artefact incrémental
est disponible, afin que le résultat ne dépende jamais d'un hit de cache.

Les options `--emit llvm-ir` et `--emit object` arrêtent la compilation après
la production de l'IR LLVM ou du fichier objet.

### Timings de compilation

`janus build --timings` écrit sur stderr un tableau humain qui attribue 100 %
du temps mesuré au chargement, parsing, analyse, génération LLVM,
optimisation/génération objet, édition de liens et surcoût résiduel. Pour une
sortie CI pure sur stdout, utilisez :

```bash
janus build --release --timings=json > timings.json
```

Le JSON versionné emploie les millisecondes, expose `total_ms` et un objet
`phases`. Les benchmarks canoniques et la politique d'alerte non bloquante sont
décrits dans [Performance du compilateur](compiler-performance.md).

`janus build`, `janus run` et `janus test` acceptent
`--panic-trace full|short|off`. Le mode `full`, utilisé par défaut en
développement, affiche l'origine source et une pile native symbolisée lorsque
la plateforme le permet. Le mode `short`, utilisé par défaut avec `--release`,
conserve uniquement l'origine source. Le mode `off` conserve seulement le
message de panique. Le format précis de ces traces reste expérimental avant
Janus 1.0.

### Documentation d’API

`janus doc` parcourt les sources du paquet sous `src/` et écrit une
documentation HTML statique dans `target/doc/`. `-o <dossier>` choisit une
autre destination, `--offline` explicite une génération sans réseau et
`--open` ouvre la page après sa création.

La syntaxe `///` et les liens `[[Symbole]]` sont décrits dans le
[contrat de documentation d’API](api-documentation.md). Les symboles
publics sont également écrits dans `api-index.json`. Une référence absente ou
ambiguë produit un avertissement ; elle n’empêche pas la génération.

### Doctests

Les fonctions `/// @test`, leur isolation, `std.testing`, le parallélisme et
les rapports CI sont décrits dans [Tests unitaires natifs](testing.md).

`janus test` compile aussi les blocs Markdown dont la première ligne porte
`// doctest: doctest` dans `README.md` et `docs/`. Un filtre sélectionne
indifféremment un test `.janus`, un chemin documentaire, une ligne ou un nom
de doctest. `--doc` exclut les tests `.janus` et `--doc-path <chemin>`
sélectionne une autre racine relative au paquet.

Les blocs volontairement partiels utilisent `// doctest: incomplete`. Les
exemples d’erreur utilisent `// doctest: compile_fail=CODE` et comparent le
code structuré, pas le texte du diagnostic. Le contrat complet et les exemples
figurent dans [Doctests Janus](doctests.md).

### Aide, erreurs et codes de sortie

`janus --help` affiche l'aide générale. Les commandes d'exécution disposent
aussi d'une aide ciblée : `janus check --help`, `janus build --help`,
`janus run --help`, `janus test --help` et `janus doc --help`. L'aide est écrite sur la sortie
standard, renvoie le code `0` et ne recherche ni projet ni chaîne d'outils.

`janus doc --stdlib --offline -o <répertoire>` génère la référence complète de
la bibliothèque standard installée sans rechercher de manifeste ni accéder au
réseau.

Pour `check`, `build`, `run` et `test`, une erreur d'invocation (option
inconnue, argument manquant ou combinaison incompatible) renvoie le code `2`,
affiche sur la sortie d'erreur un diagnostic qualifié par la commande puis
uniquement son usage. Une erreur opérationnelle
(compilation, manifeste, dépendance ou édition de liens) renvoie le code `1`
sans ajouter d'usage. `janus run` transmet le code de sortie du programme
exécuté. Les diagnostics de compilation utilisent la forme
`chemin:ligne:colonne: error: [code] message`, y compris pour `janus test`.

Pour transmettre des arguments au programme, séparez-les des options de Janus
avec `--` :

```bash
janus run -- 10 -2 4 8
janus run src/main.janus -- --verbose "texte avec espaces"
```

Chaque élément situé après `--` est transmis tel quel, y compris une chaîne
vide ou un argument commençant par `-`. Janus lance directement l’exécutable :
aucun shell n’interprète les variables, jokers ou substitutions de commande.

`janus check` et `janus build` acceptent
`--diagnostic-format human|json`. Le format humain est la valeur par défaut et
ajoute un extrait de source avec repère. Le format JSON est destiné aux outils,
reste écrit sur stderr et ne change ni stdout ni le code de sortie. Son schéma
versionné et le contrat des suggestions sont décrits dans la page
[Diagnostics structurés](diagnostics.md).

Avec `check --all`, la sortie JSON reste un document unique et contient le
même ensemble de codes, sévérités, emplacements et fichiers que la sortie
humaine.

Le LSP publie aussi les diagnostics des fichiers indexés qui ne sont pas
ouverts. À la fermeture d'un document du projet, il réanalyse sa version sur
disque au lieu d'effacer ses diagnostics ; les changements observés par le
file watcher republient ou retirent les diagnostics concernés.

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
janus search collections
janus add acme/collections@^1.2.0
janus add acme/collections@^1.2.0 --registry https://registry.example
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

## Registres et publication

Le registre distant par défaut est `https://registry.janus-lang.org`.
`JANUS_REGISTRY` le remplace pour toutes les commandes et `--registry <url>`
sélectionne explicitement un registre pour `search`, `add` ou `publish` :

```bash
export JANUS_REGISTRY_TOKEN=...
janus publish --registry https://registry.example
```

Le jeton est envoyé uniquement dans l'en-tête `Authorization`. Il n'est jamais
écrit dans les URLs, sorties, diagnostics ou lockfiles. Une version publiée est
immuable et ne peut pas être écrasée.

Les dépendances distantes utilisent une identité `namespace/name`. Le client
suit le [protocole Janus Registry v1](registry-protocol-v1.md), vérifie
métadonnées, manifeste, taille et SHA-256 avant extraction, puis publie le cache
atomiquement. `--locked` conserve registre, version et checksums exacts ;
`--offline` n'utilise que des archives déjà vérifiées.

Le [registre de référence](reference-registry.md) fournit une implémentation
déployable du protocole v1 avec publication immuable, autorités par namespace,
yank, reçus de provenance signés, audit chaîné et sauvegarde/restauration
vérifiée.

Pour les tests et installations historiques, `JANUS_REGISTRY` peut encore
désigner un répertoire local. Ce transport local est expérimental et ne
possède pas les garanties réseau du protocole v1.

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

`nightly` résout, via la branche dédiée `nightly-channel`, un snapshot immuable
identifiable par son SHA source. Le
manifeste public n'est basculé qu'après les tests multiplateformes, checksums,
attestations et smoke downstream. Voir la
[politique de publication nightly](nightly-release.md) pour la fraîcheur, la
rétention et le rollback.

Les téléchargements sont contrôlés par SHA-256 et leur provenance est vérifiée
obligatoirement avec GitHub CLI avant extraction. L'archive est ensuite
inventoriée sans écrire sur disque : `janusup` et les installateurs refusent les
chemins absolus ou traversants, les noms ambigus entre plateformes, les
collisions insensibles à la casse, les liens et les types spéciaux. Une archive
doit avoir une seule racine attendue et reste limitée à 100 000 entrées, 1 Gio
par fichier et 4 Gio décompressés au total. Seul un miroir privé peut utiliser
l'opt-out explicite et bruyant :

```bash
export JANUS_ALLOW_UNVERIFIED_PRIVATE_MIRROR=1
```

La définition des sources officielles, la frontière de confiance et
l'exception réservée aux répertoires locaux sont détaillées dans le
[guide d'installation](getting-started.md#vérifier-linstallation).

### Transactions, récupération et durabilité

Les mutations de `janusup` sont sérialisées par un verrou global. Les fichiers
de journal et `default` sont écrits dans un fichier temporaire, synchronisés,
puis renommés atomiquement. Sur POSIX, `janusup` synchronise aussi les fichiers
publiés et les répertoires parents après les renommages critiques. Sur Windows,
les fichiers sont vidés explicitement et les renommages critiques demandent
une écriture immédiate via les API système. Ce modèle garantit la
récupération après l'arrêt brutal du processus. POSIX fournit en plus l'ordre de
persistance attendu après panne système lorsque le système de fichiers respecte
`fsync`; Windows ne fournit pas d'API portable permettant de vider séparément
les métadonnées d'un répertoire, donc la garantie après perte d'alimentation
reste celle offerte par `MOVEFILE_WRITE_THROUGH`, le système de fichiers et le
matériel.

Au démarrage d'une mutation, les transactions orphelines dont le nom appartient
strictement au namespace interne sont supprimées sous le verrou global, sauf si
un journal les référence. Les noms inconnus ne sont jamais touchés. Un journal
corrompu ou qui référence une transaction absente est déplacé dans
`.janusup-state/quarantine/`; aucune toolchain valide n'est supprimée sur cette
seule base. La récupération continue automatiquement si `default`, `bin` et la
toolchain installée donnent un état valide et déterministe. Sinon, `janusup`
signale le chemin de quarantaine et demande de réparer explicitement l'état avec
`janusup default <nom-toolchain-intacte>` ou en réinstallant la toolchain
concernée. Les fichiers en quarantaine sont conservés pour diagnostic et peuvent
être supprimés manuellement après vérification de l'état réparé.

## Formatage

`janus fmt` découvre récursivement les fichiers `.janus` de `src/` et
`tests/`. La configuration facultative `.janusfmt` accepte :

```toml
indent_width = 4
max_blank_lines = 1
max_line_length = 100
```

`max_line_length` accepte une valeur de 40 à 400. Le formatter conserve un
corps expression sur la ligne de sa déclaration lorsqu'il tient dans cette
limite ; sinon il le place dans une continuation indentée déterministe.

## Serveur de langage

`janus-lsp` communique avec les éditeurs par le protocole LSP. Il prend
actuellement en charge :

- diagnostics lors de l'ouverture et de la modification d'un fichier ;
- survol d'un symbole ;
- définition et références à l'échelle du workspace ;
- renommage sémantique à l'échelle du workspace, avec refus atomique des
  collisions et respect des symboles privés ;
- aide à la signature lors de `(` et `,` ;
- symboles de document, plages de sélection et pliage des corps expression
  multilignes ;
- jetons sémantiques pour les déclarations, identifiants, mots-clés et
  littéraux ;
- indications de types déduits ;
- navigation d'un trait vers les classes qui l'implémentent ;
- recherche de symboles dans le workspace ;
- autocomplétion ;
- formatage du document.

La complétion après `.` résout le type du receveur et ne propose que ses
méthodes et champs visibles. Elle couvre les types du workspace et ceux des
index d'API, notamment les collections de la bibliothèque standard comme
`Array`.

Pour les clients qui ne disposent pas encore d'un processus LSP persistant,
`janus-lsp --completion <workspace> <document> <snapshot> <line> <character>`
exécute une requête ponctuelle sur le contenu de `snapshot`. La valeur `-` pour
`workspace` évite l'indexation du projet et limite la réponse au document
courant et aux index d'API configurés.

À l'initialisation, le serveur lit `janus.toml` et indexe les fichiers `.janus`
de `src/`, `tests/` et des dépendances résolues. Les dépendances par chemin sont
suivies directement ; les dépendances git ou registre déjà verrouillées et
présentes dans le cache sont chargées hors ligne. La navigation ne dépend donc
pas des fichiers préalablement ouverts. Les déclarations privées restent
visibles dans leur propre fichier, mais ne sont proposées ni résolues depuis un
autre module.

Les indications de types déduits sont activées par défaut. Un client peut les
désactiver avec `janus.inlayHints.inferredTypes: false` dans les paramètres de
l'éditeur transmis par `workspace/didChangeConfiguration`. Lorsque l'analyse
sémantique du document aboutit, ces indices et le survol utilisent son type
canonique, y compris pour les appels et méthodes. Pendant l'édition d'un
document temporairement invalide, le serveur conserve seulement un repli
conservateur pour les littéraux dont le type par défaut est non ambigu. Une
annotation explicite ne reçoit pas d'indice et le document n'est pas modifié.

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

Les diagnostics LSP conservent leurs codes, notes, emplacements secondaires et
corrections structurées. Les quick fixes utilisent toujours un
`WorkspaceEdit`. Un import n'est proposé que lorsqu'un unique module public
exporte le symbole ; les imports ambigus ne produisent aucune action. L'ajout
de branches `match` reste une action explicite non préférée afin qu'elle ne
soit jamais appliquée automatiquement. La matrice de compatibilité, les
procédures d'installation/mise à jour et la préparation reproductible du VSIX
pour remise manuelle au mainteneur autorisé sont détaillées dans le README de
l'extension. La CI ne publie pas sur Marketplace : elle fournit le VSIX et son
SHA-256 comme artifact, tandis que la GitHub Release principale conserve
`janus-language.vsix` et son fichier `.sha256` parmi ses fichiers.

## Variables d'environnement

| Variable | Utilisation |
| --- | --- |
| `JANUSUP_HOME` | dossier géré par `janusup` |
| `JANUS_CACHE` | cache des dépendances |
| `JANUS_REGISTRY` | URL du registre par défaut ou registre local historique |
| `JANUS_REGISTRY_TOKEN` | jeton de publication distant, transmis seulement par `Authorization` |
| `JANUS_CC` | pilote Clang utilisé pour l'édition de liens |
| `JANUS_RAYLIB_PATH` | chemin de la bibliothèque partagée raylib 6 |
| `JANUS_ALLOW_UNVERIFIED_PRIVATE_MIRROR` | accepter explicitement un miroir privé sans attestation (jamais une source officielle) |
